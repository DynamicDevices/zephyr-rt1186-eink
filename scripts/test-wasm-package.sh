#!/usr/bin/env bash
# Verify good, corrupted, and wrong-signer WASM envelope behavior.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PAYLOAD="${PAYLOAD:-${ROOT}/build-wamr-rt1170/wasm/aesl_payload.wasm}"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "${TEST_DIR}"' EXIT

if [[ ! -f "${PAYLOAD}" ]]; then
  PAYLOAD="${TEST_DIR}/minimal.wasm"
  python3 -c 'import pathlib, sys; pathlib.Path(sys.argv[1]).write_bytes(b"\0asm\x01\0\0\0")' \
    "${PAYLOAD}"
fi

TOOL="${ROOT}/scripts/wasm-package.py"
"${TOOL}" keygen --private "${TEST_DIR}/signing.pem" --public "${TEST_DIR}/signing.pub"
"${TOOL}" keygen --private "${TEST_DIR}/wrong.pem" --public "${TEST_DIR}/wrong.pub"
"${TOOL}" package --wasm "${PAYLOAD}" --output "${TEST_DIR}/payload.wpkg" \
  --key "${TEST_DIR}/signing.pem" --version 7
"${TOOL}" verify --package "${TEST_DIR}/payload.wpkg" --public "${TEST_DIR}/signing.pub"

if "${TOOL}" verify --package "${TEST_DIR}/payload.wpkg" --public "${TEST_DIR}/wrong.pub"; then
  echo "wrong signer was accepted" >&2
  exit 1
fi

cp "${TEST_DIR}/payload.wpkg" "${TEST_DIR}/corrupt.wpkg"
python3 -c 'import pathlib, sys; p = pathlib.Path(sys.argv[1]); p.write_bytes(p.read_bytes()[:-1])' \
  "${TEST_DIR}/corrupt.wpkg"
if "${TOOL}" verify --package "${TEST_DIR}/corrupt.wpkg" --public "${TEST_DIR}/signing.pub"; then
  echo "corrupted payload was accepted" >&2
  exit 1
fi

if "${TOOL}" package --wasm "${PAYLOAD}" --output "${TEST_DIR}/v0.wpkg" \
    --key "${TEST_DIR}/signing.pem" --version 0; then
  echo "zero version was accepted" >&2
  exit 1
fi

echo "PASS: signed WASM package accepts its trusted signer and rejects tampering, wrong signer, and version zero"
