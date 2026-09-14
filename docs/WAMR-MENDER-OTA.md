# WAMR module OTA on NXP i.MX RT1170

This lane proves a WebAssembly payload on the same Zephyr ARM ELF that is
intended for the MIMXRT1170-EVK. Renode is the current proof class; EVK and
custom-board execution remain explicit hardware validation boundaries.

## Trust and capability boundaries

The Mender Artifact transport manifest provides corruption detection, but WASM
code authorization uses a separate `AESLWASM` envelope and product trust root.
The envelope contains a monotonically increasing module version, payload size,
SHA-256 digest, and a raw ECDSA P-256 signature over the signed header. The
private signing key stays outside the repository. The device contains only the
65-byte uncompressed public key; a production build must source it from the
release key ceremony rather than generate it during a build.

WAMR is built without WASI, sockets, filesystem access, pthreads, JIT, or AOT.
The proof registers one `env.aesl_label_set(i32, i32) -> i32` host function.
The host validates the label identifier and value range before accepting it.
The module receives no native pointers or general access to the display,
storage, networking, Mender client, or Zephyr kernel APIs.

This design treats a valid module signature as authorization from a trusted
publisher. The CRC on activation state detects torn/corrupt records but is not
a secure monotonic counter: an attacker with arbitrary flash-write access could
restore an older signed package and matching state. Production anti-rollback
against that attacker requires an EdgeLock/OTP-backed version floor. WAMR's
fixed heap bounds memory allocation, but this interpreter profile has no fuel or
execution-time pre-emption; signing review must reject unbounded module loops,
and a future worker-thread watchdog is required before accepting less-trusted
publishers.

## Simulator proof

```bash
ZEPHYR_SDK_INSTALL_DIR=/home/ajlennon/zephyr-sdk-1.0.1 \
  ./scripts/build-wamr-rt1170.sh
./scripts/test-wamr-renode.sh
```

The test generates its platform from the build's `zephyr.dts`, derives VTOR,
SP, and PC from the exact ELF, and requires UART evidence for one allowed host
call, one denied call, and the final PASS marker. Its board overlay disables
the unmodelled CAAM entropy peripheral and selects Zephyr's deterministic test
RNG. That setting is simulator-only; the product image must use hardware
entropy on the EVK/custom board.

The exact ARM ELF currently uses 126,152 bytes of flash and 143,680 bytes of
RAM. Renode reports 1,019 ms of simulated target time for the complete negative
signature checks, trusted-package verification, WAMR load/instantiate/run, and
teardown sequence. Treat that as a deterministic regression measurement, not
an EVK performance benchmark.

The product-level lifecycle test uses Zephyr `native_sim`, LittleFS in RAM, the
real Mender Update Module callbacks, and the same signed packages:

```bash
./scripts/test-native-sim-wamr.sh
```

It checks chunked download, v7 activation, v8 activation, v7 downgrade
rejection, explicit rollback to v7, corruption of active v8, boot recovery to
v7, and `requires_reboot=false`. Package-tool tests separately reject a
truncated/tampered payload and a package from an untrusted signer.

## Signed payload tool

For local development only, generate an ephemeral key outside tracked paths:

```bash
mkdir -p build-wamr-rt1170/dev-keys
./scripts/wasm-package.py keygen \
  --private build-wamr-rt1170/dev-keys/wasm-signing.pem \
  --public build-wamr-rt1170/dev-keys/wasm-signing.pub
./scripts/wasm-package.py package \
  --wasm build-wamr-rt1170/wasm/aesl_payload.wasm \
  --output build-wamr-rt1170/aesl-label-v7.wpkg \
  --key build-wamr-rt1170/dev-keys/wasm-signing.pem --version 7
./scripts/wasm-package.py verify \
  --package build-wamr-rt1170/aesl-label-v7.wpkg \
  --public build-wamr-rt1170/dev-keys/wasm-signing.pub
```

`./scripts/test-wasm-package.sh` checks the trusted-signer success path and
both wrong-signer and payload-tamper failures.

Create the Mender Artifact without uploading it:

```bash
WASM_SIGNING_KEY_FILE=/secure/wasm-signing.pem WASM_VERSION=7 \
  ./scripts/create-wasm-artifact.sh
mender-artifact read build-wamr-rt1170/wasm-module-v7.mender
```

The Artifact type is `wasm-module`; its software provide is
`rootfs.wasm-module.version=7`. An optional outer Mender Artifact signature can
be selected with `MENDER_ARTIFACT_SIGNING_KEY_FILE`, but the device-side WASM
trust decision always uses the inner envelope and its distinct public key.

The Mender update-module callbacks and atomic A/B module slot activation are
implemented under `mender-mcu-integration/src/wasm`. The module:

- accepts only artifact type `wasm-module` and enforces a configured size cap;
- streams to `pending.wpkg`, verifies the inner signature and monotonic version,
  then executes the candidate before activation;
- atomically renames the verified candidate into the inactive LittleFS slot and
  switches a CRC-protected generation record via temp-file, sync, and rename;
- retains the previous slot for Mender rollback and recovers it at boot if the
  active package no longer verifies or runs;
- declares `requires_reboot=false` and `supports_rollback=true`.

The Mender Artifact provide `rootfs.wasm-module.version=<version>` is reported
by the normal Mender commit flow. The on-device active version is also available
as `wasm_update_module_active_version()` for inventory/UI integration.

## RT1170 product build

`mender-artifact` 4.2 or later is required by the pinned Mender MCU revision.
The command below compiles the full MCUboot + Mender + e-ink + WAMR image. The
public key may be passed through the environment because sysbuild does not
forward arbitrary top-level CMake cache variables to child images.

```bash
WASM_SIGNING_PUBLIC_KEY_FILE=/secure/wasm-signing.pub \
RT1170_EXTRA_CONF_FILE='boards/mimxrt1170_eink_shell.conf;boards/mimxrt1170_evk_mimxrt1176_cm7_eink_el133.conf;wasm-rt1170.conf' \
BUILD_DIR=build-rt1170-evk-eink-wamr \
  ./scripts/build-rt1170-evk-eink.sh
```

The measured product application is 544,996 bytes flash out of 7,327,408
bytes (7.44%) and 4,291,440 bytes RAM out of 64 MiB (6.39%). The existing
full-framebuffer configuration dominates RAM; WAMR's configured global pool is
128 KiB. MCUboot is 69,456 bytes of its 128 KiB region. These are link-time
figures from the RT1170-EVK profile, not live peak-heap measurements.

## Deployment flow and remaining boundary

Create a signed module Artifact, inspect it, then upload/deploy it using the
normal Hosted Mender release workflow:

```bash
WASM_SIGNING_KEY_FILE=/secure/wasm-signing.pem WASM_VERSION=8 \
MENDER_ARTIFACT_SIGNING_KEY_FILE=/secure/mender-artifact.pem \
  ./scripts/create-wasm-artifact.sh
mender-artifact read build-wamr-rt1170/wasm-module-v8.mender
```

No tenant credential or production private key belongs in the repository. A
live Hosted Mender upload/deployment was deliberately not performed by this
simulator lane.

The remaining proof boundary is physical hardware: flash the product image on
the MIMXRT1170-EVK, confirm CAAM-backed entropy and P-256 verification, deploy a
real signed `wasm-module` Artifact from the tenant, measure activation latency
and peak heap, power-cycle during each state-file/slot transition, then repeat
on the custom board and its final flash layout. The Renode overlay's test RNG
must never be carried into either hardware build.
