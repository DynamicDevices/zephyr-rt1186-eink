cmake_minimum_required(VERSION 3.20.0)

# ESP32-S3-DevkitC is the default board
set(BOARD
    "esp32s3_devkitc/esp32s3/procpu"
    CACHE
    STRING
    "Board for the Zephyr application"
)

# Keep LittleFS discoverable in linked/symlinked west workspaces as well as in
# clean checkouts where west discovers it from this application's manifest.
set(LITTLEFS_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../modules/fs/littlefs")
if(EXISTS "${LITTLEFS_MODULE_DIR}/zephyr/module.yml")
    list(APPEND ZEPHYR_EXTRA_MODULES "${LITTLEFS_MODULE_DIR}")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(mender-mcu-integration)

target_sources(app PRIVATE
    src/main.c
    src/utils/netup.c
    src/utils/certs.c
    src/utils/soc_uid.c
)

target_include_directories(app PUBLIC
    src
)

if(CONFIG_MENDER_APP_NOOP_UPDATE_MODULE MATCHES "y")
    target_sources(app PRIVATE src/modules/noop-update-module.c)
endif()

if(CONFIG_APP_WAMR_OTA)
    # native_sim builds WAMR as strict C17; expose strtok_r from the host libc.
    if(TARGET wamr AND CONFIG_ARCH_POSIX)
        target_compile_definitions(wamr PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    # Environment fallback is required for sysbuild: ordinary top-level CMake
    # cache variables are not automatically forwarded to child images.
    if((NOT DEFINED WASM_SIGNING_PUBLIC_KEY_FILE OR
        WASM_SIGNING_PUBLIC_KEY_FILE STREQUAL "") AND
       DEFINED ENV{WASM_SIGNING_PUBLIC_KEY_FILE})
        set(WASM_SIGNING_PUBLIC_KEY_FILE
            "$ENV{WASM_SIGNING_PUBLIC_KEY_FILE}")
    endif()
    if(NOT DEFINED WASM_SIGNING_PUBLIC_KEY_FILE OR
       NOT EXISTS "${WASM_SIGNING_PUBLIC_KEY_FILE}")
        message(FATAL_ERROR
            "APP_WAMR_OTA requires -DWASM_SIGNING_PUBLIC_KEY_FILE=<65-byte P-256 public key>")
    endif()
    file(SIZE "${WASM_SIGNING_PUBLIC_KEY_FILE}" WASM_PUBLIC_KEY_SIZE)
    if(NOT WASM_PUBLIC_KEY_SIZE EQUAL 65)
        message(FATAL_ERROR "WASM signing public key must be exactly 65 bytes")
    endif()

    set(WASM_PUBLIC_KEY_INC
        "${ZEPHYR_BINARY_DIR}/include/generated/wasm_signing_public_key.inc")
    add_custom_target(wasm_signing_public_key DEPENDS "${WASM_SIGNING_PUBLIC_KEY_FILE}")
    add_dependencies(app wasm_signing_public_key)
    generate_inc_file_for_target(wasm_signing_public_key
        "${WASM_SIGNING_PUBLIC_KEY_FILE}" "${WASM_PUBLIC_KEY_INC}")

    if(CONFIG_APP_WAMR_OTA_SELFTEST)
        foreach(version V1 V2)
            set(package_var "WASM_SELFTEST_PACKAGE_${version}_FILE")
            if(NOT DEFINED ${package_var} OR NOT EXISTS "${${package_var}}")
                message(FATAL_ERROR
                    "APP_WAMR_OTA_SELFTEST requires -D${package_var}=<signed .wpkg>")
            endif()
            set(package_inc
                "${ZEPHYR_BINARY_DIR}/include/generated/wasm_selftest_${version}.inc")
            generate_inc_file_for_target(wasm_signing_public_key
                "${${package_var}}" "${package_inc}")
        endforeach()
    endif()

    target_sources(app PRIVATE
        src/wasm/wasm_update_module.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/wasm/wasm_package.c
    )
    target_include_directories(app PRIVATE
        src/wasm
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/wasm
    )
endif()

if(CONFIG_APP_WIFI_SIM)
    target_sources(app PRIVATE src/net/wifi_sim.c)
endif()

if(CONFIG_APP_EINK)
    target_sources(app PRIVATE
        src/eink/eink_frame.c
        src/eink/eink_display.c
        src/eink/eink_panel.c
        src/eink/eink_scheduler_core.c
        src/eink/eink_scheduler.c
        src/eink/eink_store.c
        src/eink/eink_power.c
    )
    target_include_directories(app PRIVATE src/eink)
    if(CONFIG_APP_EINK_LOCATION)
        target_sources(app PRIVATE src/eink/eink_location.c)
    endif()
    if(CONFIG_APP_EINK_GNSS)
        target_sources(app PRIVATE src/eink/eink_gnss.c)
    endif()
    if(CONFIG_APP_EINK_LZ4_LIB)
        target_sources(app PRIVATE
            src/eink/eink_lz4.c
            third_party/lz4/lz4.c
            third_party/lz4/lz4hc.c
            third_party/lz4/lz4frame.c
            third_party/lz4/xxhash.c
        )
        target_include_directories(app PRIVATE third_party/lz4)
        # Keep XXHash private to the LZ4 TU set.
        target_compile_definitions(app PRIVATE XXH_NAMESPACE=EINK_)
    endif()
    if(CONFIG_APP_EINK_SHELL)
        target_sources(app PRIVATE src/eink/eink_shell.c)
    endif()
    if(CONFIG_APP_EINK_T2000)
        target_sources(app PRIVATE
            src/eink/eink_t2000_usb.c
            src/eink/eink_t2000_proto.c
        )
        # usbh_device.h / usbh_class.h live under the host stack (not public API).
        target_include_directories(app PRIVATE
            ${ZEPHYR_BASE}/subsys/usb/host
        )
    endif()
    if(CONFIG_APP_EINK_HTTP)
        target_sources(app PRIVATE src/eink/eink_http.c src/eink/eink_log_ring.c)
    endif()
    if(CONFIG_APP_EINK_SELFTEST)
        target_sources(app PRIVATE src/eink/eink_selftest.c)
    endif()
    if(CONFIG_APP_EINK_BATTERY_DUTY_CYCLE)
        target_sources(app PRIVATE src/eink/eink_wake.c)
    endif()
    if(CONFIG_APP_EINK_OTA_FLASH_STAGING)
        target_sources(app PRIVATE src/eink/eink_ota_stage.c)
    endif()
endif()

option(BUILD_INTEGRATION_TESTS "Enable integration tests" OFF)

if(BUILD_INTEGRATION_TESTS)
    target_compile_definitions(app PRIVATE BUILD_INTEGRATION_TESTS)
    target_include_directories(app PUBLIC tests/integration/src)
    target_sources(app PRIVATE tests/integration/src/modules/test-update-module.c)
    target_sources(app PRIVATE tests/integration/src/callbacks.c)
endif()

# Install certificates if a hosted Mender server is selected
if (CONFIG_MENDER_SERVER_HOST_US OR CONFIG_MENDER_SERVER_HOST_EU)
    # Links to certificates used for hosted Mender US
    if (CONFIG_MENDER_SERVER_HOST_US)
        set(PRIMARY_CERTIFICATE_LINK "https://docs.mender.io/releases/certs/AmazonRootCA1.der")
        set(PRIMARY_CERTIFICATE_SHA256 "8ecde6884f3d87b1125ba31ac3fcb13d7016de7f57cc904fe1cb97c6ae98196e")

        set(SECONDARY_CERTIFICATE_LINK "https://docs.mender.io/releases/certs/GoogleTrustServicesR4.der")
        set(SECONDARY_CERTIFICATE_SHA256 "349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d")
    # Links to certificates used for hosted Mender EU
    elseif (CONFIG_MENDER_SERVER_HOST_EU)
        set(PRIMARY_CERTIFICATE_LINK "https://docs.mender.io/releases/certs/isrgrootx1.der")
        set(PRIMARY_CERTIFICATE_SHA256 "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6")

        set(SECONDARY_CERTIFICATE_LINK "https://docs.mender.io/releases/certs/GoogleTrustServicesR4.der")
        set(SECONDARY_CERTIFICATE_SHA256 "349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d")
    endif()

    set(PRIMARY_CERTIFICATE "${CMAKE_CURRENT_BINARY_DIR}/PrimaryCertificate.cer")
    file(DOWNLOAD
        ${PRIMARY_CERTIFICATE_LINK} ${PRIMARY_CERTIFICATE}
        EXPECTED_HASH SHA256=${PRIMARY_CERTIFICATE_SHA256}
    )

    set(SECONDARY_CERTIFICATE "${CMAKE_CURRENT_BINARY_DIR}/SecondaryCertificate.cer")
    file(DOWNLOAD
        ${SECONDARY_CERTIFICATE_LINK} ${SECONDARY_CERTIFICATE}
        EXPECTED_HASH SHA256=${SECONDARY_CERTIFICATE_SHA256}
    )

elseif(CONFIG_MENDER_SERVER_HOST_ON_PREM)
    if (NOT CONFIG_MENDER_APP_SERVER_HOST_ON_PREM_CERT STREQUAL "")
        set(PRIMARY_CERTIFICATE "${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_MENDER_APP_SERVER_HOST_ON_PREM_CERT}")
    else()
        message(WARNING "CONFIG_MENDER_SERVER_HOST_ON_PREM without CONFIG_MENDER_APP_SERVER_HOST_ON_PREM_CERT results in a broken configuration")
    endif()
else()
    # This is an impossible configuration, MENDER_SERVER_HOST is a choice
    message(WARNING "Either CONFIG_MENDER_SERVER_HOST_US, CONFIG_MENDER_SERVER_HOST_EU or CONFIG_MENDER_SERVER_HOST_ON_PREM needs to be selected")
endif()

if (PRIMARY_CERTIFICATE)
    add_custom_target(certificate_one DEPENDS ${PRIMARY_CERTIFICATE})
    add_dependencies(app certificate_one)

    generate_inc_file_for_target(certificate_one
        ${PRIMARY_CERTIFICATE}
        "${ZEPHYR_BINARY_DIR}/include/generated/PrimaryCertificate.cer.inc"
    )
endif()

if (SECONDARY_CERTIFICATE)
    add_custom_target(certificate_two DEPENDS ${SECONDARY_CERTIFICATE})
    add_dependencies(app certificate_two)

    generate_inc_file_for_target(certificate_two
        ${SECONDARY_CERTIFICATE}
        "${ZEPHYR_BINARY_DIR}/include/generated/SecondaryCertificate.cer.inc"
    )
endif()

file(COPY_FILE "config-tls-mender.h" "${ZEPHYR_BINARY_DIR}/include/generated/config-tls-mender.h")
