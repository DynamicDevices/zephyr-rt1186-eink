*** Settings ***
Suite Setup                   Setup
Suite Teardown                Teardown
Test Setup                    Reset Emulation
Test Teardown                 Test Teardown
Resource                      ${RENODEKEYWORDS}

*** Variables ***
${UART}                       sysbus.lpuart1

*** Test Cases ***
WAMR Capability Module Should Run On RT1170 ARM ELF
    Execute Command           mach create
    Execute Command           machine LoadPlatformDescription ${REPL}
    Execute Command           sysbus LoadELF ${ELF}
    Execute Command           cpu0 VectorTableOffset ${VTOR}
    Execute Command           cpu0 SP ${SP}
    Execute Command           cpu0 PC ${PC}
    Create Terminal Tester    ${UART}
    Start Emulation
    Wait For Line On Uart     Booting Zephyr OS                       timeout=30
    Wait For Line On Uart     WAMR RT1170: starting isolated module    timeout=30
    Wait For Line On Uart     WAMR HOST ALLOW: label=1 value=42       timeout=30
    Wait For Line On Uart     WAMR HOST DENY: label=99 value=42       timeout=30
    Wait For Line On Uart     PASS: WAMR capability host API          timeout=30
