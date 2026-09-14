#!/usr/bin/env bash
# Create a reboot-free Mender artifact containing an independently signed WASM package.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WASM_FILE="${WASM_FILE:-${ROOT}/build-wamr-rt1170/wasm/aesl_payload.wasm}"
WASM_SIGNING_KEY_FILE="${WASM_SIGNING_KEY_FILE:-}"
WASM_VERSION="${WASM_VERSION:-}"
DEVICE_TYPE="${MENDER_DEVICE_TYPE:-mimxrt1170_evk}"
ARTIFACT_NAME="${MENDER_ARTIFACT_NAME:-wasm-module-v${WASM_VERSION}}"
OUTPUT="${MENDER_ARTIFACT_OUTPUT:-${ROOT}/build-wamr-rt1170/${ARTIFACT_NAME}.mender}"
PACKAGE="${OUTPUT%.mender}.wpkg"

[[ -f "${WASM_FILE}" ]] || { echo "missing WASM_FILE=${WASM_FILE}" >&2; exit 1; }
[[ -n "${WASM_SIGNING_KEY_FILE}" && -f "${WASM_SIGNING_KEY_FILE}" ]] || {
  echo "set WASM_SIGNING_KEY_FILE to an external ECDSA P-256 private key" >&2
  exit 1
}
[[ "${WASM_VERSION}" =~ ^[1-9][0-9]*$ ]] || {
  echo "set WASM_VERSION to a positive integer" >&2
  exit 1
}
command -v mender-artifact >/dev/null || { echo "mender-artifact not found" >&2; exit 1; }

mkdir -p "$(dirname "${OUTPUT}")"
"${ROOT}/scripts/wasm-package.py" package --wasm "${WASM_FILE}" \
  --output "${PACKAGE}" --key "${WASM_SIGNING_KEY_FILE}" --version "${WASM_VERSION}"

MENDER_ARGS=(
  write module-image
  --output-path "${OUTPUT}"
  --artifact-name "${ARTIFACT_NAME}"
  --type wasm-module
  --file "${PACKAGE}"
  --compression none
  --device-type "${DEVICE_TYPE}"
  --software-filesystem rootfs
  --software-name wasm-module
  --software-version "${WASM_VERSION}"
)

# Optional outer Artifact signature. Device-side WASM authorization never
# depends on this; it always verifies the signed AESLWASM envelope.
if [[ -n "${MENDER_ARTIFACT_SIGNING_KEY_FILE:-}" ]]; then
  [[ -f "${MENDER_ARTIFACT_SIGNING_KEY_FILE}" ]] || {
    echo "missing MENDER_ARTIFACT_SIGNING_KEY_FILE" >&2
    exit 1
  }
  MENDER_ARGS+=(--key "${MENDER_ARTIFACT_SIGNING_KEY_FILE}")
fi

mender-artifact "${MENDER_ARGS[@]}"
echo "OK: ${OUTPUT} (type=wasm-module, version=${WASM_VERSION}, reboot=false on device)"
