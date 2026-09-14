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

## Simulator proof

```bash
ZEPHYR_SDK_INSTALL_DIR=/home/ajlennon/zephyr-sdk-1.0.1 \
  ./scripts/build-wamr-rt1170.sh
./scripts/test-wamr-renode.sh
```

The test generates its platform from the build's `zephyr.dts`, derives VTOR,
SP, and PC from the exact ELF, and requires UART evidence for one allowed host
call, one denied call, and the final PASS marker.

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
the next integration layer. They must use artifact type `wasm-module`, declare
`requires_reboot=false`, retain the previous verified slot through commit, and
report `rootfs.wasm-module.version` through Artifact provides.
