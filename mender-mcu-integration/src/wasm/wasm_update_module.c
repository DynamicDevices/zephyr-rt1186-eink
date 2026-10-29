/* SPDX-License-Identifier: Apache-2.0 */

#include "wasm_update_module.h"
#include "wasm_package.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mender/alloc.h>
#include <mender/log.h>
#include <mender/update-module.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "wasm_export.h"

#define SLOT_NONE       UINT32_MAX
#define STATE_MAGIC     0x31534D57U /* WMS1 */
#define WASM_STACK_SIZE 8192U
#define WASM_HEAP_SIZE  8192U
#define LABEL_ID        1
#define LABEL_VALUE_MAX 9999

static const uint8_t signing_public_key[] = {
#include "wasm_signing_public_key.inc"
};

#if defined(CONFIG_APP_WAMR_OTA_SELFTEST)
static const uint8_t selftest_package_v1[] = {
#include "wasm_selftest_V1.inc"
};
static const uint8_t selftest_package_v2[] = {
#include "wasm_selftest_V2.inc"
};
#endif

struct slot_state {
    uint32_t magic;
    uint32_t generation;
    uint32_t active_slot;
    uint32_t previous_slot;
    uint32_t active_version;
    uint32_t previous_version;
    uint32_t crc;
};

static struct slot_state state = {
    .magic = STATE_MAGIC,
    .active_slot = SLOT_NONE,
    .previous_slot = SLOT_NONE,
};
static struct fs_file_t download_file;
static bool download_open;
static bool download_complete;
static bool deployment_activated;
static size_t expected_download_size;
static char wamr_heap[CONFIG_WAMR_GLOBAL_HEAP_SIZE];

static void make_path(char *out, size_t out_size, const char *name)
{
    (void)snprintf(out, out_size, "%s/%s", CONFIG_APP_WAMR_OTA_ROOT, name);
}

static void slot_path(char *out, size_t out_size, uint32_t slot)
{
    (void)snprintf(out, out_size, "%s/slot%u.wpkg",
                   CONFIG_APP_WAMR_OTA_ROOT, slot);
}

static void unlink_if_present(const char *path)
{
    struct fs_dirent entry;

    if (fs_stat(path, &entry) == 0) {
        (void)fs_unlink(path);
    }
}

static int ensure_directory(void)
{
    struct fs_dirent entry;
    int ret = fs_stat(CONFIG_APP_WAMR_OTA_ROOT, &entry);

    if (ret == 0) {
        return entry.type == FS_DIR_ENTRY_DIR ? 0 : -ENOTDIR;
    }
    return ret == -ENOENT ? fs_mkdir(CONFIG_APP_WAMR_OTA_ROOT) : ret;
}

static uint32_t state_crc(const struct slot_state *value)
{
    return crc32_ieee((const uint8_t *)value,
                      offsetof(struct slot_state, crc));
}

static int save_state(const struct slot_state *new_state)
{
    struct slot_state stored = *new_state;
    struct fs_file_t file;
    char path[192];
    char temp[192];
    ssize_t written;
    int ret;

    stored.magic = STATE_MAGIC;
    stored.generation = state.generation + 1;
    stored.crc = state_crc(&stored);
    make_path(path, sizeof(path), "active.state");
    make_path(temp, sizeof(temp), "active.state.tmp");
    unlink_if_present(temp);

    fs_file_t_init(&file);
    ret = fs_open(&file, temp, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (ret < 0) {
        return ret;
    }
    written = fs_write(&file, &stored, sizeof(stored));
    if (written == sizeof(stored)) {
        ret = fs_sync(&file);
    } else {
        ret = written < 0 ? (int)written : -EIO;
    }
    (void)fs_close(&file);
    if (ret < 0) {
        unlink_if_present(temp);
        return ret;
    }
    ret = fs_rename(temp, path);
    if (ret < 0) {
        unlink_if_present(temp);
        return ret;
    }
    state = stored;
    return 0;
}

static int load_state(void)
{
    struct fs_file_t file;
    struct slot_state stored;
    char path[192];
    ssize_t count;
    int ret;

    make_path(path, sizeof(path), "active.state");
    fs_file_t_init(&file);
    ret = fs_open(&file, path, FS_O_READ);
    if (ret == -ENOENT) {
        return 0;
    }
    if (ret < 0) {
        return ret;
    }
    count = fs_read(&file, &stored, sizeof(stored));
    (void)fs_close(&file);
    if (count != sizeof(stored) || stored.magic != STATE_MAGIC ||
        stored.crc != state_crc(&stored) ||
        (stored.active_slot != SLOT_NONE && stored.active_slot > 1) ||
        (stored.previous_slot != SLOT_NONE && stored.previous_slot > 1)) {
        return -EBADMSG;
    }
    state = stored;
    return 0;
}

static int read_verified_package(const char *path, uint8_t **buffer,
                                 size_t *buffer_size,
                                 struct aesl_wasm_package_view *view)
{
    struct fs_dirent entry;
    struct fs_file_t file;
    uint8_t *data;
    ssize_t count;
    int ret;

    ret = fs_stat(path, &entry);
    if (ret < 0) {
        return ret;
    }
    if (entry.type != FS_DIR_ENTRY_FILE ||
        entry.size > CONFIG_APP_WAMR_OTA_MAX_PAYLOAD_SIZE +
                         AESL_WASM_PACKAGE_HEADER_SIZE) {
        return -EFBIG;
    }
    data = malloc(entry.size);
    if (data == NULL) {
        return -ENOMEM;
    }
    fs_file_t_init(&file);
    ret = fs_open(&file, path, FS_O_READ);
    if (ret < 0) {
        free(data);
        return ret;
    }
    count = fs_read(&file, data, entry.size);
    (void)fs_close(&file);
    if (count != entry.size) {
        free(data);
        return count < 0 ? (int)count : -EIO;
    }
    ret = aesl_wasm_package_verify(data, entry.size, signing_public_key,
                                   sizeof(signing_public_key), view);
    if (ret < 0) {
        free(data);
        return ret;
    }
    *buffer = data;
    *buffer_size = entry.size;
    return 0;
}

static int32_t host_label_set(wasm_exec_env_t exec_env, int32_t label_id,
                              int32_t value)
{
    ARG_UNUSED(exec_env);
    if (label_id != LABEL_ID || value < 0 || value > LABEL_VALUE_MAX) {
        mender_log_warning("deny label capability id=%d value=%d", label_id,
                           value);
        return -1;
    }
    mender_log_info("allow label capability id=%d value=%d", label_id, value);
    return 0;
}

static NativeSymbol host_api[] = {
    { "aesl_label_set", host_label_set, "(ii)i", NULL },
};

static int execute_package(const struct aesl_wasm_package_view *package)
{
    RuntimeInitArgs args;
    wasm_module_t module = NULL;
    wasm_module_inst_t instance = NULL;
    wasm_exec_env_t exec_env = NULL;
    wasm_function_inst_t entry;
    uint32_t argv[1] = { 0 };
    char error[128] = { 0 };
    int ret = -ENOEXEC;

    memset(&args, 0, sizeof(args));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = wamr_heap;
    args.mem_alloc_option.pool.heap_size = sizeof(wamr_heap);
    args.native_module_name = "env";
    args.native_symbols = host_api;
    args.n_native_symbols = ARRAY_SIZE(host_api);
    if (!wasm_runtime_full_init(&args)) {
        return -ENOMEM;
    }
    module = wasm_runtime_load((uint8_t *)package->payload,
                               package->payload_size, error, sizeof(error));
    if (module == NULL) {
        mender_log_error("WASM load: %s", error);
        goto out;
    }
    instance = wasm_runtime_instantiate(module, WASM_STACK_SIZE,
                                        WASM_HEAP_SIZE, error, sizeof(error));
    if (instance == NULL) {
        mender_log_error("WASM instantiate: %s", error);
        goto out;
    }
    entry = wasm_runtime_lookup_function(instance, "app_main");
    exec_env = wasm_runtime_create_exec_env(instance, WASM_STACK_SIZE);
    if (entry == NULL || exec_env == NULL) {
        goto out;
    }
    if (wasm_runtime_call_wasm(exec_env, entry, 0, argv) &&
        wasm_runtime_get_exception(instance) == NULL && argv[0] == 0) {
        ret = 0;
    }
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
    return ret;
}

static int activate_slot(uint32_t slot, uint32_t expected_version)
{
    struct aesl_wasm_package_view package;
    uint8_t *buffer = NULL;
    size_t buffer_size;
    char path[192];
    int ret;

    slot_path(path, sizeof(path), slot);
    ret = read_verified_package(path, &buffer, &buffer_size, &package);
    if (ret == 0 && package.version != expected_version) {
        ret = -EBADMSG;
    }
    if (ret == 0) {
        ret = execute_package(&package);
    }
    free(buffer);
    return ret;
}

int wasm_update_module_init(void)
{
    int ret = ensure_directory();

    if (ret < 0) {
        return ret;
    }
    ret = load_state();
    if (ret < 0) {
        mender_log_error("invalid WASM activation state: %d", ret);
        return ret;
    }
    if (state.active_slot != SLOT_NONE) {
        ret = activate_slot(state.active_slot, state.active_version);
        if (ret < 0 && state.previous_slot != SLOT_NONE) {
            struct slot_state recovered = state;

            mender_log_error("active WASM failed; recovering previous slot");
            ret = activate_slot(state.previous_slot, state.previous_version);
            if (ret == 0) {
                recovered.active_slot = state.previous_slot;
                recovered.active_version = state.previous_version;
                recovered.previous_slot = SLOT_NONE;
                recovered.previous_version = 0;
                ret = save_state(&recovered);
            }
        }
    }
    return ret;
}

uint32_t wasm_update_module_active_version(void)
{
    return state.active_version;
}

static void close_download(void)
{
    if (download_open) {
        (void)fs_close(&download_file);
        download_open = false;
    }
}

static mender_err_t download_callback(mender_update_state_t update_state,
                                      mender_update_state_data_t callback_data)
{
    struct mender_update_download_state_data_s *data =
        callback_data.download_state_data;
    char path[192];
    ssize_t written;
    int ret;

    ARG_UNUSED(update_state);
    if (data == NULL || data->filename == NULL ||
        data->size > CONFIG_APP_WAMR_OTA_MAX_PAYLOAD_SIZE +
                         AESL_WASM_PACKAGE_HEADER_SIZE) {
        return MENDER_FAIL;
    }
    make_path(path, sizeof(path), "pending.wpkg");
    if (data->offset == 0) {
        close_download();
        unlink_if_present(path);
        fs_file_t_init(&download_file);
        ret = fs_open(&download_file, path,
                      FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
        if (ret < 0) {
            return MENDER_FAIL;
        }
        download_open = true;
        download_complete = false;
        expected_download_size = data->size;
    }
    if (!download_open || data->size != expected_download_size ||
        data->offset > data->size || data->length > data->size - data->offset ||
        (data->length > 0U && data->data == NULL) ||
        fs_seek(&download_file, data->offset, FS_SEEK_SET) < 0) {
        return MENDER_FAIL;
    }
    written = fs_write(&download_file, data->data, data->length);
    if (written != data->length) {
        return MENDER_FAIL;
    }
    if (data->done || data->offset + data->length == data->size) {
        ret = fs_sync(&download_file);
        close_download();
        if (ret < 0) {
            return MENDER_FAIL;
        }
        download_complete = true;
    }
    return MENDER_OK;
}

static mender_err_t install_callback(mender_update_state_t update_state,
                                     mender_update_state_data_t callback_data)
{
    struct aesl_wasm_package_view package;
    struct slot_state activated = state;
    uint8_t *buffer = NULL;
    size_t buffer_size;
    uint32_t inactive;
    char pending[192];
    char target[192];
    int ret;

    ARG_UNUSED(update_state);
    ARG_UNUSED(callback_data);
    if (!download_complete) {
        return MENDER_FAIL;
    }
    make_path(pending, sizeof(pending), "pending.wpkg");
    ret = read_verified_package(pending, &buffer, &buffer_size, &package);
    if (ret < 0 || (state.active_slot != SLOT_NONE &&
                    package.version <= state.active_version)) {
        free(buffer);
        return MENDER_FAIL;
    }
    ret = execute_package(&package);
    if (ret < 0) {
        free(buffer);
        return MENDER_FAIL;
    }

    inactive = state.active_slot == 0 ? 1 : 0;
    slot_path(target, sizeof(target), inactive);
    unlink_if_present(target);
    ret = fs_rename(pending, target);
    if (ret < 0) {
        free(buffer);
        return MENDER_FAIL;
    }
    activated.previous_slot = state.active_slot;
    activated.previous_version = state.active_version;
    activated.active_slot = inactive;
    activated.active_version = package.version;
    ret = save_state(&activated);
    free(buffer);
    if (ret < 0) {
        return MENDER_FAIL;
    }
    deployment_activated = true;
    download_complete = false;
    mender_log_info("activated WASM version %u without firmware reboot",
                    activated.active_version);
    return MENDER_OK;
}

static mender_err_t rollback_callback(mender_update_state_t update_state,
                                      mender_update_state_data_t callback_data)
{
    struct slot_state rolled_back = state;
    int ret;

    ARG_UNUSED(update_state);
    ARG_UNUSED(callback_data);
    if (state.previous_slot == SLOT_NONE) {
        return MENDER_FAIL;
    }
    ret = activate_slot(state.previous_slot, state.previous_version);
    if (ret < 0) {
        return MENDER_FAIL;
    }
    rolled_back.active_slot = state.previous_slot;
    rolled_back.active_version = state.previous_version;
    rolled_back.previous_slot = state.active_slot;
    rolled_back.previous_version = state.active_version;
    if (save_state(&rolled_back) < 0) {
        return MENDER_FAIL;
    }
    deployment_activated = false;
    mender_log_info("rolled back WASM to version %u",
                    rolled_back.active_version);
    return MENDER_OK;
}

static mender_err_t commit_callback(mender_update_state_t update_state,
                                    mender_update_state_data_t callback_data)
{
    ARG_UNUSED(update_state);
    ARG_UNUSED(callback_data);
    deployment_activated = false;
    return MENDER_OK;
}

static mender_err_t failure_callback(mender_update_state_t update_state,
                                     mender_update_state_data_t callback_data)
{
    char pending[192];

    ARG_UNUSED(update_state);
    ARG_UNUSED(callback_data);
    close_download();
    make_path(pending, sizeof(pending), "pending.wpkg");
    unlink_if_present(pending);
    download_complete = false;
    if (deployment_activated) {
        return rollback_callback(MENDER_UPDATE_STATE_ROLLBACK,
                                 (mender_update_state_data_t)NULL);
    }
    return MENDER_OK;
}

mender_err_t wasm_update_module_register(void)
{
    mender_update_module_t *module = mender_calloc(1, sizeof(*module));
    mender_err_t ret;

    if (module == NULL) {
        return MENDER_FAIL;
    }
    module->callbacks[MENDER_UPDATE_STATE_DOWNLOAD] = download_callback;
    module->callbacks[MENDER_UPDATE_STATE_INSTALL] = install_callback;
    module->callbacks[MENDER_UPDATE_STATE_COMMIT] = commit_callback;
    module->callbacks[MENDER_UPDATE_STATE_CLEANUP] = failure_callback;
    module->callbacks[MENDER_UPDATE_STATE_FAILURE] = failure_callback;
    module->callbacks[MENDER_UPDATE_STATE_ROLLBACK] = rollback_callback;
    module->artifact_type = "wasm-module";
    module->requires_reboot = false;
    module->supports_rollback = true;
    ret = mender_update_module_register(module);
    if (ret != MENDER_OK) {
        mender_free(module);
    }
    return ret;
}

#if defined(CONFIG_APP_WAMR_OTA_SELFTEST)
static int selftest_download(const uint8_t *package, size_t package_size)
{
    struct mender_update_download_state_data_s download = {
        .id = "wasm-selftest",
        .artifact_name = "wasm-selftest",
        .type = "wasm-module",
        .filename = "payload.wpkg",
        .size = package_size,
    };
    mender_update_state_data_t callback_data;
    const size_t first_chunk = MIN(package_size, 73U);

    download.data = package;
    download.length = first_chunk;
    download.offset = 0;
    download.done = false;
    callback_data.download_state_data = &download;
    if (download_callback(MENDER_UPDATE_STATE_DOWNLOAD, callback_data) !=
        MENDER_OK) {
        return -EIO;
    }

    download.data = package + first_chunk;
    download.offset = first_chunk;
    download.length = package_size - first_chunk;
    download.done = true;
    if (download_callback(MENDER_UPDATE_STATE_DOWNLOAD, callback_data) !=
        MENDER_OK) {
        return -EIO;
    }
    return 0;
}

static int selftest_install(const uint8_t *package, size_t package_size)
{
    if (selftest_download(package, package_size) < 0 ||
        install_callback(MENDER_UPDATE_STATE_INSTALL,
                         (mender_update_state_data_t)NULL) != MENDER_OK ||
        commit_callback(MENDER_UPDATE_STATE_COMMIT,
                        (mender_update_state_data_t)NULL) != MENDER_OK) {
        return -EIO;
    }
    return 0;
}

static int selftest_corrupt_active_slot(void)
{
    struct fs_file_t file;
    char path[192];
    uint8_t corrupt = 0;
    ssize_t written;
    int ret;

    slot_path(path, sizeof(path), state.active_slot);
    fs_file_t_init(&file);
    ret = fs_open(&file, path, FS_O_WRITE);
    if (ret < 0) {
        return ret;
    }
    written = fs_write(&file, &corrupt, sizeof(corrupt));
    if (written == sizeof(corrupt)) {
        ret = fs_sync(&file);
    } else {
        ret = written < 0 ? (int)written : -EIO;
    }
    (void)fs_close(&file);
    return ret;
}

int wasm_update_module_selftest(void)
{
    uint32_t version_v1;
    uint32_t version_v2;
    struct aesl_wasm_package_view view;

    if (aesl_wasm_package_verify(selftest_package_v1,
                                 sizeof(selftest_package_v1),
                                 signing_public_key,
                                 sizeof(signing_public_key), &view) < 0) {
        return -EKEYREJECTED;
    }
    version_v1 = view.version;
    if (aesl_wasm_package_verify(selftest_package_v2,
                                 sizeof(selftest_package_v2),
                                 signing_public_key,
                                 sizeof(signing_public_key), &view) < 0) {
        return -EKEYREJECTED;
    }
    version_v2 = view.version;
    if (version_v2 <= version_v1 || selftest_install(selftest_package_v1,
                                                      sizeof(selftest_package_v1)) < 0 ||
        state.active_version != version_v1 ||
        selftest_install(selftest_package_v2,
                         sizeof(selftest_package_v2)) < 0 ||
        state.active_version != version_v2) {
        return -EIO;
    }
    if (selftest_download(selftest_package_v1,
                          sizeof(selftest_package_v1)) < 0 ||
        install_callback(MENDER_UPDATE_STATE_INSTALL,
                         (mender_update_state_data_t)NULL) != MENDER_FAIL ||
        failure_callback(MENDER_UPDATE_STATE_FAILURE,
                         (mender_update_state_data_t)NULL) != MENDER_OK ||
        state.active_version != version_v2 ||
        rollback_callback(MENDER_UPDATE_STATE_ROLLBACK,
                          (mender_update_state_data_t)NULL) != MENDER_OK ||
        state.active_version != version_v1 ||
        selftest_install(selftest_package_v2,
                         sizeof(selftest_package_v2)) < 0 ||
        selftest_corrupt_active_slot() < 0 ||
        wasm_update_module_init() < 0 ||
        state.active_version != version_v1 || state.previous_slot != SLOT_NONE) {
        return -EIO;
    }
    printk("PASS: Mender wasm-module A/B v%u->v%u downgrade-denied "
           "rollback-v%u corrupt-active-recovered-v%u reboot=false\n",
           version_v1, version_v2, version_v1, state.active_version);
    return 0;
}
#endif
