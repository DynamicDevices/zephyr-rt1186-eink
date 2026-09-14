#!/usr/bin/env bash
# Build the product Mender + e-ink + WAMR Update Module for native_sim.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PUBLIC_KEY_FILE="${WASM_SIGNING_PUBLIC_KEY_FILE:-}"
PACKAGE_V1_FILE="${WASM_SELFTEST_PACKAGE_V1_FILE:-}"
PACKAGE_V2_FILE="${WASM_SELFTEST_PACKAGE_V2_FILE:-}"
[[ -n "${PUBLIC_KEY_FILE}" && -f "${PUBLIC_KEY_FILE}" ]] || {
  echo "set WASM_SIGNING_PUBLIC_KEY_FILE to a 65-byte P-256 public key" >&2
  exit 1
}
[[ -n "${PACKAGE_V1_FILE}" && -f "${PACKAGE_V1_FILE}" ]] || {
  echo "set WASM_SELFTEST_PACKAGE_V1_FILE to a signed .wpkg" >&2
  exit 1
}
[[ -n "${PACKAGE_V2_FILE}" && -f "${PACKAGE_V2_FILE}" ]] || {
  echo "set WASM_SELFTEST_PACKAGE_V2_FILE to a newer signed .wpkg" >&2
  exit 1
}

export BUILD_DIR="${BUILD_DIR:-build-native_sim-wamr}"
export NATIVE_SIM_EXTRA_CONF="eink-native-sim.conf;wasm-native-sim.conf"
export NATIVE_SIM_EXTRA_CMAKE_ARGS="-DDTC_OVERLAY_FILE=${ROOT}/mender-mcu-integration/boards/native_sim_eink.overlay -DWASM_SIGNING_PUBLIC_KEY_FILE=${PUBLIC_KEY_FILE} -DWASM_SELFTEST_PACKAGE_V1_FILE=${PACKAGE_V1_FILE} -DWASM_SELFTEST_PACKAGE_V2_FILE=${PACKAGE_V2_FILE}"
exec "${ROOT}/scripts/build-native-sim-eink.sh" "$@"
