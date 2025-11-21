Import("env")

import os
import subprocess


def _generate_embeds(source, target, env_):
    project_dir = env_["PROJECT_DIR"]
    build_dir = env_.subst("$BUILD_DIR")
    # Resolve framework and cmake paths directly from the PlatformIO home
    # directory to avoid depending on internal env keys that may not yet be
    # populated when this script runs.
    home_dir = os.path.expanduser("~")
    framework_dir = os.path.join(home_dir, ".platformio", "packages", "framework-espidf")
    cmake = os.path.join(home_dir, ".platformio", "packages", "tool-cmake", "bin", "cmake.exe")
    pio_python = os.path.join(home_dir, ".platformio", "python3", "python.exe")

    # data_file_embed_asm.cmake location may vary slightly between IDF versions
    candidates = [
        os.path.join(framework_dir, "tools", "cmake", "scripts", "data_file_embed_asm.cmake"),
        os.path.join(framework_dir, "tools", "cmake", "data_file_embed_asm.cmake"),
    ]
    embed_script = next((p for p in candidates if os.path.exists(p)), None)
    if not embed_script:
        print("pio_gen_embeds: data_file_embed_asm.cmake not found; skipping embed generation")
        return

    os.makedirs(build_dir, exist_ok=True)

    # ------- Generate embedded .S files (public key + logos) -------

    files = [
        ("pinserver_public_key.pub", "pinserver_public_key.pub.S"),
        (os.path.join("logo", "splash.bin.gz"), "splash.bin.gz.S"),
        (os.path.join("logo", "ce.bin.gz"), "ce.bin.gz.S"),
        (os.path.join("logo", "fcc.bin.gz"), "fcc.bin.gz.S"),
        (os.path.join("logo", "statusbar_small.bin.gz"), "statusbar_small.bin.gz.S"),
        (os.path.join("logo", "statusbar_large.bin.gz"), "statusbar_large.bin.gz.S"),
        (os.path.join("logo", "weee.bin.gz"), "weee.bin.gz.S"),
    ]

    for src_rel, out_name in files:
        src_path = os.path.join(project_dir, src_rel)
        if not os.path.exists(src_path):
            # Some logo files are board‑dependent; skip silently if missing
            continue

        out_path = os.path.join(build_dir, out_name)
        try:
            print(f"pio_gen_embeds: generating {out_name} from {src_rel}")
            subprocess.check_call(
                [
                    cmake,
                    f"-DDATA_FILE={src_path}",
                    f"-DSOURCE_FILE={out_path}",
                    "-DFILE_TYPE=BINARY",
                    "-P",
                    embed_script,
                ]
            )
        except Exception as exc:  # pragma: no cover - best effort helper
            print(f"pio_gen_embeds: failed to generate {out_name}: {exc}")

    # ------- Generate asset_data.inc and asset_data_testnet.inc -------

    try:
        assets_build_dir = os.path.join(build_dir, "esp-idf", "assets")
        os.makedirs(assets_build_dir, exist_ok=True)

        gen_assets = os.path.join(project_dir, "components", "assets", "gen_assets.py")
        asset_json = os.path.join(project_dir, "components", "assets", "asset_data.json")
        asset_json_testnet = os.path.join(project_dir, "components", "assets", "asset_data_testnet.json")

        if os.path.exists(gen_assets) and os.path.exists(pio_python):
            print("pio_gen_embeds: generating asset_data.inc")
            subprocess.check_call([pio_python, gen_assets, asset_json,
                                   os.path.join(assets_build_dir, "asset_data.inc")])

            print("pio_gen_embeds: generating asset_data_testnet.inc")
            subprocess.check_call([pio_python, gen_assets, asset_json_testnet,
                                   os.path.join(assets_build_dir, "asset_data_testnet.inc")])
    except Exception as exc:  # pragma: no cover - best effort helper
        print(f"pio_gen_embeds: failed to generate asset inc files: {exc}")


# Run immediately when the extra_script is loaded so that the .S files
# exist before PlatformIO/SCons starts compiling sources.
_generate_embeds(None, None, env)
