# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aesl_wamr_rt1170)

target_sources(app PRIVATE
  src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/wasm/wasm_package.c
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/wasm)

set(WAMR_ROOT_DIR ${ZEPHYR_WASM_MICRO_RUNTIME_MODULE_DIR})
list(APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package(WASISDK REQUIRED)

include(ExternalProject)
ExternalProject_Add(wasm_payload
  SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/wasm
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm -B build
                    -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build
                    --prefix ${CMAKE_CURRENT_BINARY_DIR}/wasm
  BUILD_ALWAYS      TRUE
)

set(WASM_FILE ${CMAKE_CURRENT_BINARY_DIR}/wasm/aesl_payload.wasm)
set(DEV_PRIVATE_KEY ${CMAKE_CURRENT_BINARY_DIR}/dev-wasm-signing.pem)
set(DEV_PUBLIC_KEY ${CMAKE_CURRENT_BINARY_DIR}/dev-wasm-signing.pub)
set(WASM_PACKAGE ${CMAKE_CURRENT_BINARY_DIR}/wasm/aesl_payload.wpkg)
set(PACKAGE_TOOL ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/wasm-package.py)

add_custom_command(
  OUTPUT ${DEV_PRIVATE_KEY} ${DEV_PUBLIC_KEY}
  COMMAND ${Python3_EXECUTABLE} ${PACKAGE_TOOL} keygen
          --private ${DEV_PRIVATE_KEY} --public ${DEV_PUBLIC_KEY}
  DEPENDS ${PACKAGE_TOOL}
  COMMENT "Generating an ephemeral simulator-only WASM signing key"
  VERBATIM
)

add_custom_command(
  OUTPUT ${WASM_PACKAGE}
  COMMAND ${Python3_EXECUTABLE} ${PACKAGE_TOOL} package
          --wasm ${WASM_FILE} --output ${WASM_PACKAGE}
          --key ${DEV_PRIVATE_KEY} --version 7
  DEPENDS wasm_payload ${DEV_PRIVATE_KEY} ${PACKAGE_TOOL}
  COMMENT "Signing the simulator WASM payload"
  VERBATIM
)

set(WASM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/aesl_package.h)
set(PUBLIC_KEY_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/aesl_public_key.h)
add_custom_command(
  OUTPUT ${WASM_HEADER} ${PUBLIC_KEY_HEADER}
  COMMAND ${CMAKE_COMMAND}
          -DINPUT=${WASM_PACKAGE}
          -DOUTPUT=${WASM_HEADER}
          -DSYMBOL=aesl_wasm_package
          -P ${WAMR_ROOT_DIR}/product-mini/platforms/zephyr/cmake/bin_to_header.cmake
  COMMAND ${CMAKE_COMMAND}
          -DINPUT=${DEV_PUBLIC_KEY}
          -DOUTPUT=${PUBLIC_KEY_HEADER}
          -DSYMBOL=aesl_wasm_public_key
          -P ${WAMR_ROOT_DIR}/product-mini/platforms/zephyr/cmake/bin_to_header.cmake
  DEPENDS ${WASM_PACKAGE} ${DEV_PUBLIC_KEY}
  COMMENT "Embedding the signed WASM package and simulator public key"
  VERBATIM
)
add_custom_target(embed_wasm_payload DEPENDS ${WASM_HEADER} ${PUBLIC_KEY_HEADER})
add_dependencies(app embed_wasm_payload)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
