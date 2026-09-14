/* SPDX-License-Identifier: Apache-2.0 */

#include "wasm_package.h"

#include <errno.h>
#include <string.h>

#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>

#define PREFIX_SIZE    52U
#define SIGNATURE_SIZE 64U
#define HASH_SIZE      32U
#define PUBLIC_KEY_SIZE 65U
#define FORMAT_VERSION 1U

static const uint8_t package_magic[8] = { 'A', 'E', 'S', 'L', 'W', 'A', 'S', 'M' };

int aesl_wasm_package_verify(const uint8_t *package, size_t package_size,
                             const uint8_t *public_key,
                             size_t public_key_size,
                             struct aesl_wasm_package_view *view)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t payload_hash[HASH_SIZE];
    uint8_t signing_hash[HASH_SIZE];
    size_t hash_length;
    uint32_t payload_size;
    psa_status_t status;
    int ret = -EKEYREJECTED;

    if (package == NULL || public_key == NULL || view == NULL) {
        return -EINVAL;
    }
    if (package_size < AESL_WASM_PACKAGE_HEADER_SIZE ||
        public_key_size != PUBLIC_KEY_SIZE || public_key[0] != 0x04) {
        return -EBADMSG;
    }
    if (memcmp(package, package_magic, sizeof(package_magic)) != 0 ||
        sys_get_le32(package + 8) != FORMAT_VERSION) {
        return -ENOTSUP;
    }

    if (sys_get_le32(package + 12) == 0U) {
        return -EBADMSG;
    }

    payload_size = sys_get_le32(package + 16);
    if ((size_t)payload_size != package_size - AESL_WASM_PACKAGE_HEADER_SIZE) {
        return -EBADMSG;
    }

    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        return -EIO;
    }
    status = psa_hash_compute(PSA_ALG_SHA_256,
                              package + AESL_WASM_PACKAGE_HEADER_SIZE,
                              payload_size, payload_hash,
                              sizeof(payload_hash), &hash_length);
    if (status != PSA_SUCCESS || hash_length != HASH_SIZE ||
        memcmp(payload_hash, package + 20, HASH_SIZE) != 0) {
        return -EBADMSG;
    }

    status = psa_hash_compute(PSA_ALG_SHA_256, package, PREFIX_SIZE,
                              signing_hash, sizeof(signing_hash), &hash_length);
    if (status != PSA_SUCCESS || hash_length != HASH_SIZE) {
        return -EIO;
    }

    psa_set_key_type(&attributes,
                     PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, public_key, public_key_size, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        return -EKEYREJECTED;
    }

    status = psa_verify_hash(key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                             signing_hash, sizeof(signing_hash),
                             package + PREFIX_SIZE, SIGNATURE_SIZE);
    if (status == PSA_SUCCESS) {
        view->version = sys_get_le32(package + 12);
        view->payload = package + AESL_WASM_PACKAGE_HEADER_SIZE;
        view->payload_size = payload_size;
        ret = 0;
    }
    (void)psa_destroy_key(key_id);
    return ret;
}
