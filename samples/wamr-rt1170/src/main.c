/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "wasm_export.h"
#include "aesl_payload.h"

#define WASM_STACK_SIZE 8192U
#define WASM_HEAP_SIZE  8192U
#define HOST_LABEL_ID   1
#define HOST_VALUE_MAX  9999

static char wamr_heap[CONFIG_WAMR_GLOBAL_HEAP_SIZE];

/*
 * Deliberately narrow product capability: a module may request one bounded
 * label value. It receives no filesystem, network, display-buffer, or raw
 * memory access. The production adapter can map this validated command onto
 * the e-ink scheduler rather than exporting that subsystem to WebAssembly.
 */
static int32_t host_label_set(wasm_exec_env_t exec_env, int32_t label_id,
                              int32_t value)
{
    ARG_UNUSED(exec_env);

    if (label_id != HOST_LABEL_ID || value < 0 || value > HOST_VALUE_MAX) {
        printk("WAMR HOST DENY: label=%d value=%d\n", label_id, value);
        return -1;
    }

    printk("WAMR HOST ALLOW: label=%d value=%d\n", label_id, value);
    return 0;
}

static NativeSymbol host_api[] = {
    { "aesl_label_set", host_label_set, "(ii)i", NULL },
};

static int run_payload(void)
{
    RuntimeInitArgs init_args;
    wasm_module_t module = NULL;
    wasm_module_inst_t instance = NULL;
    wasm_exec_env_t exec_env = NULL;
    wasm_function_inst_t entry;
    uint32_t argv[1] = { 0 };
    char error[128] = { 0 };
    int rc = -1;

    memset(&init_args, 0, sizeof(init_args));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = wamr_heap;
    init_args.mem_alloc_option.pool.heap_size = sizeof(wamr_heap);
    init_args.native_module_name = "env";
    init_args.native_symbols = host_api;
    init_args.n_native_symbols = ARRAY_SIZE(host_api);

    if (!wasm_runtime_full_init(&init_args)) {
        printk("WAMR FAIL: runtime init\n");
        return -1;
    }

    module = wasm_runtime_load((uint8_t *)aesl_wasm_payload,
                               sizeof(aesl_wasm_payload), error,
                               sizeof(error));
    if (module == NULL) {
        printk("WAMR FAIL: load: %s\n", error);
        goto out;
    }

    instance = wasm_runtime_instantiate(module, WASM_STACK_SIZE,
                                        WASM_HEAP_SIZE, error, sizeof(error));
    if (instance == NULL) {
        printk("WAMR FAIL: instantiate: %s\n", error);
        goto out;
    }

    entry = wasm_runtime_lookup_function(instance, "app_main");
    if (entry == NULL) {
        printk("WAMR FAIL: app_main missing\n");
        goto out;
    }

    exec_env = wasm_runtime_create_exec_env(instance, WASM_STACK_SIZE);
    if (exec_env == NULL) {
        printk("WAMR FAIL: exec env\n");
        goto out;
    }

    if (!wasm_runtime_call_wasm(exec_env, entry, 0, argv)) {
        printk("WAMR FAIL: exception: %s\n",
               wasm_runtime_get_exception(instance));
        goto out;
    }

    rc = (int32_t)argv[0];

out:
    if (exec_env != NULL) {
        wasm_runtime_destroy_exec_env(exec_env);
    }
    if (instance != NULL) {
        wasm_runtime_deinstantiate(instance);
    }
    if (module != NULL) {
        wasm_runtime_unload(module);
    }
    wasm_runtime_destroy();
    return rc;
}

int main(void)
{
    int64_t started = k_uptime_get();
    int rc;

    printk("WAMR RT1170: starting isolated module\n");
    rc = run_payload();
    if (rc == 0) {
        printk("PASS: WAMR capability host API\n");
    } else {
        printk("FAIL: WAMR module result=%d\n", rc);
    }
    printk("WAMR elapsed_ms=%lld\n", k_uptime_delta(&started));
    return rc;
}
