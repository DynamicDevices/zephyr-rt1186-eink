#!/usr/bin/env bash
# Build and run the product Mender/WAMR lifecycle test on native_sim.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

ARM_BUILD_DIR="${ARM_BUILD_DIR:-build-wamr-rt1170}"
NATIVE_BUILD_DIR="${BUILD_DIR:-build-native_sim-wamr}"
PAYLOAD="${ROOT}/${ARM_BUILD_DIR}/wasm/aesl_payload.wasm"
PRIVATE_KEY="${ROOT}/${ARM_BUILD_DIR}/dev-wasm-signing.pem"
PUBLIC_KEY="${ROOT}/${ARM_BUILD_DIR}/dev-wasm-signing.pub"
PACKAGE_V1="${ROOT}/${ARM_BUILD_DIR}/wasm/aesl_payload.wpkg"
PACKAGE_V2="${ROOT}/${ARM_BUILD_DIR}/wasm/aesl_payload_v8.wpkg"

if [[ ! -f "${PAYLOAD}" || ! -f "${PRIVATE_KEY}" ||
      ! -f "${PUBLIC_KEY}" || ! -f "${PACKAGE_V1}" ]]; then
  BUILD_DIR="${ARM_BUILD_DIR}" ./scripts/build-wamr-rt1170.sh
fi

./scripts/wasm-package.py package --wasm "${PAYLOAD}" \
  --output "${PACKAGE_V2}" --key "${PRIVATE_KEY}" --version 8

BUILD_DIR="${NATIVE_BUILD_DIR}" \
WASM_SIGNING_PUBLIC_KEY_FILE="${PUBLIC_KEY}" \
WASM_SELFTEST_PACKAGE_V1_FILE="${PACKAGE_V1}" \
WASM_SELFTEST_PACKAGE_V2_FILE="${PACKAGE_V2}" \
  ./scripts/build-native-sim-wamr.sh --pristine

LOG="${ROOT}/${NATIVE_BUILD_DIR}/wasm-selftest.log"
if ! timeout 15s "${ROOT}/${NATIVE_BUILD_DIR}/zephyr/zephyr.exe" \
    --flash_in_ram --stop_at=5 >"${LOG}" 2>&1; then
  tail -n 80 "${LOG}" >&2
  exit 1
fi

PASS='PASS: Mender wasm-module A/B v7->v8 downgrade-denied rollback-v7 corrupt-active-recovered-v7 reboot=false'
if ! grep -Fq "${PASS}" "${LOG}"; then
  tail -n 80 "${LOG}" >&2
  exit 1
fi
echo "${PASS}"
