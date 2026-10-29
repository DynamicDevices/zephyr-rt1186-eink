# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
project(aesl_wasm_payload C)

add_executable(aesl_payload main.c)
set_target_properties(aesl_payload PROPERTIES SUFFIX .wasm)
target_compile_options(aesl_payload PRIVATE -Oz)
target_link_options(aesl_payload PRIVATE
  -nostdlib
  -z stack-size=256
  -Wl,--initial-memory=65536
  -Wl,--global-base=0
  -Wl,--export=app_main
  -Wl,--export=__data_end
  -Wl,--export=__heap_base
  -Wl,--strip-all
  -Wl,--no-entry
  -Wl,--allow-undefined
)

install(FILES $<TARGET_FILE:aesl_payload> DESTINATION .)
