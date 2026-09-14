#!/usr/bin/env python3
"""Fast CI guard for the maintained WAMR/Mender security contract."""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(text: str, values: list[str], label: str) -> None:
    missing = [value for value in values if value not in text]
    if missing:
        raise SystemExit(f"{label}: missing contract(s): {', '.join(missing)}")


manifest = read("mender-mcu-integration/west.yml")
match = re.search(
    r"- name: wasm-micro-runtime\n"
    r"\s+url: https://github.com/bytecodealliance/wasm-micro-runtime\n"
    r"(?:\s+#.*\n)*\s+revision: ([0-9a-f]{40})",
    manifest,
)
if match is None:
    raise SystemExit("west.yml: WAMR must use the official URL and a full SHA")

for profile in (
    "samples/wamr-rt1170/prj.conf",
    "mender-mcu-integration/wasm-native-sim.conf",
    "mender-mcu-integration/wasm-rt1170.conf",
):
    require(
        read(profile),
        [
            "CONFIG_WAMR_INTERP=y",
            "CONFIG_WAMR_FAST_INTERP=n",
            "CONFIG_WAMR_AOT=n",
            "CONFIG_WAMR_LIBC_BUILTIN=n",
            "CONFIG_WAMR_LIBC_WASI=n",
            "CONFIG_WAMR_GLOBAL_HEAP_POOL=y",
        ],
        profile,
    )

package = read("lib/wasm/wasm_package.c")
require(
    package,
    [
        "PSA_ALG_ECDSA(PSA_ALG_SHA_256)",
        "PSA_KEY_USAGE_VERIFY_HASH",
        "sys_get_le32(package + 12) == 0U",
        "memcmp(payload_hash, package + 20, HASH_SIZE)",
    ],
    "signed package verifier",
)

module = read("mender-mcu-integration/src/wasm/wasm_update_module.c")
require(
    module,
    [
        'module->artifact_type = "wasm-module"',
        "module->requires_reboot = false",
        "module->supports_rollback = true",
        "package.version <= state.active_version",
        "data->length > data->size - data->offset",
        "fs_sync(&download_file)",
        "fs_rename(pending, target)",
        "activate_slot(state.previous_slot, state.previous_version)",
        '{ "aesl_label_set", host_label_set, "(ii)i", NULL }',
    ],
    "Mender WASM Update Module",
)

for forbidden in ("wasi_", "socket(", "fs_open("):
    host_function = module[module.index("static int32_t host_label_set") : module.index("static NativeSymbol")]
    if forbidden in host_function:
        raise SystemExit(f"host capability unexpectedly contains {forbidden!r}")

print(f"PASS: WAMR/Mender contract pinned at {match.group(1)}")
