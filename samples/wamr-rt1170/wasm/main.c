/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

__attribute__((import_module("env"), import_name("aesl_label_set")))
extern int32_t aesl_label_set(int32_t label_id, int32_t value);

int32_t app_main(void)
{
    if (aesl_label_set(1, 42) != 0) {
        return 1;
    }

    /* This request must be rejected by the host-side capability boundary. */
    if (aesl_label_set(99, 42) == 0) {
        return 2;
    }

    return 0;
}
