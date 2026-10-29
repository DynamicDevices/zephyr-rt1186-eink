#!/usr/bin/env bash
# Build the exact RT1170 ARM ELF used by the WAMR Renode proof.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

BOARD="${RT1170_BOARD:-mimxrt1170_evk/mimxrt1176/cm7}"
BUILD_DIR="${BUILD_DIR:-build-wamr-rt1170}"

if [[ -f zephyr/zephyr-env.sh ]]; then
  # shellcheck source=/dev/null
  source zephyr/zephyr-env.sh
fi

west build -p always -d "${BUILD_DIR}" -b "${BOARD}" samples/wamr-rt1170 -- \
  -DWAMR_BUILD_TARGET=THUMBV7 "$@"

echo "OK: ${BUILD_DIR}/zephyr/zephyr.elf"
