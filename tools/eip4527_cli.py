import argparse
import os
import sys
import binascii
import tempfile
import webbrowser
from uuid import uuid4
import cbor2
from ur.ur import UR
from ur.ur_encoder import UREncoder
import qrcode
from qrcode.constants import ERROR_CORRECT_L, ERROR_CORRECT_M, ERROR_CORRECT_Q, ERROR_CORRECT_H
from PIL import Image

def _b(hex_or_str):
    if hex_or_str.startswith("0x"):
        return binascii.unhexlify(hex_or_str[2:])
    try:
        return binascii.unhexlify(hex_or_str)
    except Exception:
        return hex_or_str.encode()

def build_eth_sign_request(sign_data_bytes, request_id_bytes, chain_id):
    m = {6: cbor2.CBORTag(37, request_id_bytes), 1: sign_data_bytes}
    if chain_id is not None:
        m[5] = int(chain_id)
    return cbor2.dumps(m)

def build_eth_signature(sig65_bytes, request_id_bytes):
    m = {1: cbor2.CBORTag(37, request_id_bytes), 2: sig65_bytes}
    return cbor2.dumps(m)

def encode_ur_parts(ur_type, cbor_bytes, max_fragment_len):
    u = UR(ur_type, cbor_bytes)
    enc = UREncoder(u, max_fragment_len)
    parts = []
    while True:
        part = enc.next_part()
        parts.append(part)
        if enc.is_complete():
            break
    return parts

def make_qr_image(text, box_size, border, ec):
    ec_map = {"L": ERROR_CORRECT_L, "M": ERROR_CORRECT_M, "Q": ERROR_CORRECT_Q, "H": ERROR_CORRECT_H}
    qr = qrcode.QRCode(version=None, error_correction=ec_map.get(ec, ERROR_CORRECT_M), box_size=box_size, border=border)
    qr.add_data(text)
    qr.make(fit=True)
    return qr.make_image().convert("RGB")

def save_qr_frames(parts, out_dir, box_size, border, ec):
    os.makedirs(out_dir, exist_ok=True)
    for i, p in enumerate(parts, 1):
        img = make_qr_image(p, box_size, border, ec)
        img.save(os.path.join(out_dir, f"frame_{i:03d}.png"))

def save_qr_gif(parts, out_path, fps, box_size, border, ec):
    imgs = [make_qr_image(p, box_size, border, ec) for p in parts]
    max_w = max(im.width for im in imgs)
    max_h = max(im.height for im in imgs)
    norm = []
    for im in imgs:
        if im.width != max_w or im.height != max_h:
            canvas = Image.new("RGB", (max_w, max_h), "white")
            x = (max_w - im.width) // 2
            y = (max_h - im.height) // 2
            canvas.paste(im, (x, y))
            im = canvas
        norm.append(im.convert("P"))
    duration_ms = max(1, int(1000 / max(1, fps)))
    norm[0].save(out_path, save_all=True, append_images=norm[1:], duration=duration_ms, loop=0, optimize=True)

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    sreq = sub.add_parser("sign-request")
    sreq.add_argument("sign_data", help="hex or utf8 string for signData")
    sreq.add_argument("--request_id", default=None)
    sreq.add_argument("--chain_id", type=int, default=1)
    sreq.add_argument("--frag", type=int, default=200)
    sreq.add_argument("--out", default=None)
    sreq.add_argument("--gif", default=None)
    sreq.add_argument("--fps", type=int, default=6)
    sreq.add_argument("--show", action="store_true")
    sreq.add_argument("--box_size", type=int, default=10)
    sreq.add_argument("--border", type=int, default=4)
    sreq.add_argument("--ec", choices=["L", "M", "Q", "H"], default="M")

    ssig = sub.add_parser("signature")
    ssig.add_argument("sig65", help="signature R||S||V (65-byte hex)")
    ssig.add_argument("--request_id", required=True)
    ssig.add_argument("--frag", type=int, default=200)
    ssig.add_argument("--out", default=None)
    ssig.add_argument("--gif", default=None)
    ssig.add_argument("--fps", type=int, default=6)
    ssig.add_argument("--show", action="store_true")
    ssig.add_argument("--box_size", type=int, default=10)
    ssig.add_argument("--border", type=int, default=4)
    ssig.add_argument("--ec", choices=["L", "M", "Q", "H"], default="M")

    args = ap.parse_args()

    if args.cmd == "sign-request":
        sd = _b(args.sign_data)
        rid = _b(args.request_id) if args.request_id else uuid4().bytes
        cbor_bytes = build_eth_sign_request(sd, rid, args.chain_id)
        parts = encode_ur_parts("eth-sign-request", cbor_bytes, args.frag)
        if args.gif:
            save_qr_gif(parts, args.gif, args.fps, args.box_size, args.border, args.ec)
        if args.out:
            save_qr_frames(parts, args.out, args.box_size, args.border, args.ec)
        if args.show:
            tmp = args.gif or os.path.join(tempfile.gettempdir(), f"ur_req_{uuid4().hex}.gif")
            if not args.gif:
                save_qr_gif(parts, tmp, args.fps, args.box_size, args.border, args.ec)
            try:
                if sys.platform.startswith("win"):
                    os.startfile(tmp)
                else:
                    webbrowser.open(f"file://{tmp}")
            except Exception:
                pass
        if not args.out and not args.gif:
            for p in parts:
                print(p)
    elif args.cmd == "signature":
        sig = _b(args.sig65)
        rid = _b(args.request_id)
        cbor_bytes = build_eth_signature(sig, rid)
        parts = encode_ur_parts("eth-signature", cbor_bytes, args.frag)
        if args.gif:
            save_qr_gif(parts, args.gif, args.fps, args.box_size, args.border, args.ec)
        if args.out:
            save_qr_frames(parts, args.out, args.box_size, args.border, args.ec)
        if args.show:
            tmp = args.gif or os.path.join(tempfile.gettempdir(), f"ur_sig_{uuid4().hex}.gif")
            if not args.gif:
                save_qr_gif(parts, tmp, args.fps, args.box_size, args.border, args.ec)
            try:
                if sys.platform.startswith("win"):
                    os.startfile(tmp)
                else:
                    webbrowser.open(f"file://{tmp}")
            except Exception:
                pass
        if not args.out and not args.gif:
            for p in parts:
                print(p)

if __name__ == "__main__":
    sys.exit(main())