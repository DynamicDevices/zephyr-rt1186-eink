#!/usr/bin/env python3
"""Create and verify independently signed Active-ESL WASM payload envelopes."""

# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import struct
import sys
from pathlib import Path
from typing import Tuple

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

MAGIC = b"AESLWASM"
FORMAT_VERSION = 1
PREFIX = struct.Struct("<8sIII32s")
SIGNATURE_SIZE = 64
HEADER_SIZE = PREFIX.size + SIGNATURE_SIZE


def generate_key(private_path: Path, public_path: Path) -> None:
    if private_path.exists() or public_path.exists():
        raise FileExistsError("refusing to overwrite a signing key")
    key = ec.generate_private_key(ec.SECP256R1())
    private_path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    private_path.chmod(0o600)
    public_path.write_bytes(
        key.public_key().public_bytes(
            serialization.Encoding.X962,
            serialization.PublicFormat.UncompressedPoint,
        )
    )


def load_private(path: Path) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise ValueError("signing key must be ECDSA P-256")
    return key


def load_public(path: Path) -> ec.EllipticCurvePublicKey:
    raw = path.read_bytes()
    if len(raw) != 65 or raw[0] != 4:
        raise ValueError("public key must be a 65-byte uncompressed P-256 point")
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), raw)


def create_package(wasm_path: Path, output_path: Path, key_path: Path, version: int) -> None:
    payload = wasm_path.read_bytes()
    payload_hash = hashlib.sha256(payload).digest()
    prefix = PREFIX.pack(MAGIC, FORMAT_VERSION, version, len(payload), payload_hash)
    signing_hash = hashlib.sha256(prefix).digest()
    der_signature = load_private(key_path).sign(
        signing_hash, ec.ECDSA(utils.Prehashed(hashes.SHA256()))
    )
    r, s = utils.decode_dss_signature(der_signature)
    signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    output_path.write_bytes(prefix + signature + payload)


def verify_package(package_path: Path, public_path: Path) -> Tuple[int, int, str]:
    package = package_path.read_bytes()
    if len(package) < HEADER_SIZE:
        raise ValueError("package is shorter than its fixed header")
    prefix = package[: PREFIX.size]
    magic, fmt, version, payload_size, expected_hash = PREFIX.unpack(prefix)
    if magic != MAGIC or fmt != FORMAT_VERSION:
        raise ValueError("unsupported WASM package header")
    payload = package[HEADER_SIZE:]
    if payload_size != len(payload):
        raise ValueError("payload size does not match the header")
    actual_hash = hashlib.sha256(payload).digest()
    if actual_hash != expected_hash:
        raise ValueError("payload SHA-256 mismatch")
    signature = package[PREFIX.size:HEADER_SIZE]
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    der_signature = utils.encode_dss_signature(r, s)
    signing_hash = hashlib.sha256(prefix).digest()
    try:
        load_public(public_path).verify(
            der_signature,
            signing_hash,
            ec.ECDSA(utils.Prehashed(hashes.SHA256())),
        )
    except InvalidSignature as exc:
        raise ValueError("WASM package signature is invalid") from exc
    return version, payload_size, actual_hash.hex()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    keygen = sub.add_parser("keygen", help="generate a development P-256 key pair")
    keygen.add_argument("--private", required=True, type=Path)
    keygen.add_argument("--public", required=True, type=Path)

    package = sub.add_parser("package", help="sign a WASM payload envelope")
    package.add_argument("--wasm", required=True, type=Path)
    package.add_argument("--output", required=True, type=Path)
    package.add_argument("--key", required=True, type=Path)
    package.add_argument("--version", required=True, type=int)

    verify = sub.add_parser("verify", help="verify and inspect a payload envelope")
    verify.add_argument("--package", required=True, type=Path)
    verify.add_argument("--public", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "keygen":
            generate_key(args.private, args.public)
            print(f"generated private={args.private} public={args.public}")
        elif args.command == "package":
            if args.version < 1 or args.version > 0xFFFFFFFF:
                raise ValueError("version must be in the range 1..4294967295")
            create_package(args.wasm, args.output, args.key, args.version)
            print(f"created package={args.output} version={args.version}")
        else:
            version, size, digest = verify_package(args.package, args.public)
            print(f"PASS: version={version} payload_size={size} sha256={digest}")
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
