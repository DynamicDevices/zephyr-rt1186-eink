# SPDX-License-Identifier: Apache-2.0
# Minimal alternating register model used by dts2repl for RT1170 PLL waits.

if request.IsInit:
    flipflop_value = 0
elif request.IsWrite:
    flipflop_value = int(request.Value)
else:
    flipflop_value ^= 0xFFFFFFFF
    request.Value = flipflop_value
