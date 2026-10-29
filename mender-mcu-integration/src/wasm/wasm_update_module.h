/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AESL_WASM_UPDATE_MODULE_H
#define AESL_WASM_UPDATE_MODULE_H

#include <mender/utils.h>

int wasm_update_module_init(void);
mender_err_t wasm_update_module_register(void);
uint32_t wasm_update_module_active_version(void);
#if defined(CONFIG_APP_WAMR_OTA_SELFTEST)
int wasm_update_module_selftest(void);
#endif

#endif /* AESL_WASM_UPDATE_MODULE_H */
