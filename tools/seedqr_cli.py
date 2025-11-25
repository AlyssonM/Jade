import argparse
import os
import sys
from typing import List
import qrcode
from qrcode.constants import ERROR_CORRECT_L, ERROR_CORRECT_M, ERROR_CORRECT_Q, ERROR_CORRECT_H
from PIL import Image


def _load_english_wordlist() -> List[str]:
    here = os.path.dirname(__file__)
    repo_root = os.path.dirname(here)
    wl_path = os.path.join(
        repo_root, "components", "libwally-core", "upstream", "src", "data", "wordlists", "english.txt"
    )
    with open(wl_path, "r", encoding="utf-8") as f:
        words = [w.strip() for w in f.read().splitlines() if w.strip()]
    if len(words) != 2048:
        raise RuntimeError("english.txt inesperado: tamanho da wordlist != 2048")
    return words


def _mnemonic_to_indices(mnemonic: str, wordlist: List[str]) -> List[int]:
    words = [w.strip().lower() for w in mnemonic.strip().split() if w.strip()]
    if len(words) not in (12, 24):
        raise ValueError("mnemonic deve ter 12 ou 24 palavras")
    index_map = {w: i for i, w in enumerate(wordlist)}
    idxs = []
    for w in words:
        if w not in index_map:
            raise ValueError(f"palavra desconhecida na wordlist: {w}")
        idxs.append(index_map[w])
    return idxs


def _indices_to_entropy_bytes(indices: List[int], verify_checksum: bool = True) -> bytes:
    n = len(indices)
    ent_bits = (n // 3) * 32  # 128 para 12 palavras, 256 para 24 palavras
    cs_bits_len = n * 11 - ent_bits
    # Constrói o bitstream (big-endian)
    bitstream = 0
    bitlen = 0
    for idx in indices:
        if not (0 <= idx < 2048):
            raise ValueError("índice fora de faixa (0..2047)")
        bitstream = (bitstream << 11) | idx
        bitlen += 11
    # Extrai ENT (descarta checksum)
    shift = bitlen - ent_bits
    entropy_int = bitstream >> shift
    entropy_bytes = entropy_int.to_bytes(ent_bits // 8, byteorder="big")
    if verify_checksum:
        import hashlib
        cs_src = bitstream & ((1 << cs_bits_len) - 1)
        cs_calc = int.from_bytes(hashlib.sha256(entropy_bytes).digest(), "big") >> (256 - cs_bits_len)
        if cs_src != cs_calc:
            raise ValueError("checksum BIP39 inválido para o mnemonic informado")
    return entropy_bytes


def _indices_to_seedqr_standard(indices: List[int]) -> str:
    # Concatena 4 dígitos por palavra, zero-left padded, sem espaços
    return "".join(f"{idx:04d}" for idx in indices)


def _make_qr_image(data, box_size: int, border: int, ec: str) -> Image.Image:
    ec_map = {"L": ERROR_CORRECT_L, "M": ERROR_CORRECT_M, "Q": ERROR_CORRECT_Q, "H": ERROR_CORRECT_H}
    qr = qrcode.QRCode(version=None, error_correction=ec_map.get(ec, ERROR_CORRECT_M), box_size=box_size, border=border)
    qr.add_data(data)
    qr.make(fit=True)
    return qr.make_image().convert("RGB")


def main():
    ap = argparse.ArgumentParser(description="Gerar Compact/Standard SeedQR a partir de um mnemonic BIP39 (EN)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    cm = sub.add_parser("compact", help="QR binário com ENT (128/256 bits)")
    cm.add_argument("mnemonic", help="mnemonic de 12/24 palavras (EN)")
    cm.add_argument("--out", dest="out", default=os.path.join(os.path.dirname(os.path.dirname(__file__)), "out_frames", "seedqr_compact.png"), help="caminho para PNG")
    cm.add_argument("--show", dest="show", action="store_true", help="abrir imagem gerada")
    cm.add_argument("--box_size", type=int, default=26)
    cm.add_argument("--border", type=int, default=4)
    cm.add_argument("--ec", choices=["L", "M", "Q", "H"], default="H")
    cm.add_argument("--print_hex", action="store_true", help="imprime ENT em hex no stdout")
    cm.add_argument("--skip_checksum", action="store_true", help="não valida checksum BIP39")

    st = sub.add_parser("standard", help="QR texto com 4 dígitos por palavra (SeedQR padrão)")
    st.add_argument("mnemonic", help="mnemonic de 12/24 palavras (EN)")
    st.add_argument("--out", dest="out", default=os.path.join(os.path.dirname(os.path.dirname(__file__)), "out_frames", "seedqr_standard.png"), help="caminho para PNG")
    st.add_argument("--show", dest="show", action="store_true", help="abrir imagem gerada")
    st.add_argument("--box_size", type=int, default=26)
    st.add_argument("--border", type=int, default=4)
    st.add_argument("--ec", choices=["L", "M", "Q", "H"], default="H")
    st.add_argument("--print_str", action="store_true", help="imprime string SeedQR no stdout")

    args = ap.parse_args()

    wordlist = _load_english_wordlist()
    indices = _mnemonic_to_indices(args.mnemonic, wordlist)

    if args.cmd == "compact":
        entropy_bytes = _indices_to_entropy_bytes(indices, verify_checksum=not args.skip_checksum)
        img = _make_qr_image(entropy_bytes, args.box_size, args.border, args.ec)
        if args.out:
            os.makedirs(os.path.dirname(args.out), exist_ok=True)
            img.save(args.out)
        if args.show:
            try:
                tmp = args.out
                if not tmp:
                    tmp = os.path.join(os.path.dirname(__file__), f"seedqr_compact.png")
                    img.save(tmp)
                if sys.platform.startswith("win"):
                    os.startfile(tmp)
                else:
                    import webbrowser
                    webbrowser.open(f"file://{tmp}")
            except Exception:
                pass
        if args.print_hex:
            print(entropy_bytes.hex())
        if not args.out and not args.show and not args.print_hex:
            # default: salva ao lado do script
            out_path = os.path.join(os.path.dirname(__file__), "seedqr_compact.png")
            img.save(out_path)
            print(out_path)
    elif args.cmd == "standard":
        seedqr_str = _indices_to_seedqr_standard(indices)
        img = _make_qr_image(seedqr_str, args.box_size, args.border, args.ec)
        if args.out:
            os.makedirs(os.path.dirname(args.out), exist_ok=True)
            img.save(args.out)
        if args.show:
            try:
                tmp = args.out
                if not tmp:
                    tmp = os.path.join(os.path.dirname(__file__), f"seedqr_standard.png")
                    img.save(tmp)
                if sys.platform.startswith("win"):
                    os.startfile(tmp)
                else:
                    import webbrowser
                    webbrowser.open(f"file://{tmp}")
            except Exception:
                pass
        if args.print_str:
            print(seedqr_str)
        if not args.out and not args.show and not args.print_str:
            out_path = os.path.join(os.path.dirname(__file__), "seedqr_standard.png")
            img.save(out_path)
            print(out_path)


if __name__ == "__main__":
    sys.exit(main())