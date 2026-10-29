/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AESL_WASM_PACKAGE_H
#define AESL_WASM_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#define AESL_WASM_PACKAGE_HEADER_SIZE 116U

struct aesl_wasm_package_view {
    const uint8_t *payload;
    size_t payload_size;
    uint32_t version;
};

int aesl_wasm_package_verify(const uint8_t *package, size_t package_size,
                             const uint8_t *public_key,
                             size_t public_key_size,
                             struct aesl_wasm_package_view *view);

#endif /* AESL_WASM_PACKAGE_H */
