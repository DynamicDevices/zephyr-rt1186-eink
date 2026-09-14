#!/usr/bin/env bash
# Run the exact RT1170 ARM ELF under Renode; this is not native_sim proof.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="${BUILD_DIR:-build-wamr-rt1170}"
ELF="${ROOT}/${BUILD_DIR}/zephyr/zephyr.elf"
DTS="${ROOT}/${BUILD_DIR}/zephyr/zephyr.dts"
REPL="${ROOT}/${BUILD_DIR}/rt1170.repl"
ROBOT="${ROOT}/tests/renode/wamr_rt1170.robot"

[[ -f "${ELF}" ]] || { echo "missing ${ELF}; run scripts/build-wamr-rt1170.sh" >&2; exit 1; }
[[ -f "${DTS}" ]] || { echo "missing ${DTS}; run scripts/build-wamr-rt1170.sh" >&2; exit 1; }

DTS2REPL="${DTS2REPL:-$(command -v dts2repl || true)}"
[[ -n "${DTS2REPL}" ]] || { echo "dts2repl not found" >&2; exit 1; }
"${DTS2REPL}" --loglevel warning -o "${REPL}" "${DTS}"

VTOR_LINE="$(python3 scripts/find-vtor.py "${ELF}" | awk '/RAM VTOR=/{print; exit}')"
[[ -n "${VTOR_LINE}" ]] || { echo "no RAM vector table found in ${ELF}" >&2; exit 1; }
eval "$(awk '{
  for (i=1;i<=NF;i++) {
    split($i,a,"=");
    if (a[1]=="VTOR") printf "VTOR=%s\n", a[2];
    if (a[1]=="SP") printf "SP=%s\n", a[2];
    if (a[1]=="PC") printf "PC=%s\n", a[2];
  }
}' <<<"${VTOR_LINE}")"

RENODE_TEST="${RENODE_TEST:-$(command -v renode-test || true)}"
if [[ -z "${RENODE_TEST}" && -x "${HOME}/.local/opt/renode-portable/renode-test" ]]; then
  RENODE_TEST="${HOME}/.local/opt/renode-portable/renode-test"
fi
[[ -n "${RENODE_TEST}" ]] || { echo "renode-test not found" >&2; exit 1; }

echo "ELF=${ELF}"
echo "VTOR=${VTOR} SP=${SP} PC=${PC}"
exec "${RENODE_TEST}" \
  --variable "ROOT:${ROOT}" \
  --variable "REPL:@${REPL}" \
  --variable "ELF:@${ELF}" \
  --variable "VTOR:${VTOR}" \
  --variable "SP:${SP}" \
  --variable "PC:${PC}" \
  "${ROBOT}"
