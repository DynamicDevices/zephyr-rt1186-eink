# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aesl_wamr_rt1170)

target_sources(app PRIVATE src/main.c)

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

set(WASM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/aesl_payload.h)
add_custom_command(
  OUTPUT ${WASM_HEADER}
  COMMAND ${CMAKE_COMMAND}
          -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/wasm/aesl_payload.wasm
          -DOUTPUT=${WASM_HEADER}
          -DSYMBOL=aesl_wasm_payload
          -P ${WAMR_ROOT_DIR}/product-mini/platforms/zephyr/cmake/bin_to_header.cmake
  DEPENDS wasm_payload
  COMMENT "Embedding the capability-host test WebAssembly module"
  VERBATIM
)
add_custom_target(embed_wasm_payload DEPENDS ${WASM_HEADER})
add_dependencies(app embed_wasm_payload)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
