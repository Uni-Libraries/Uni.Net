#!/usr/bin/env pwsh

#
# Functions
#

function Get-FreeRTOS-TCP(){
    Invoke-WebRequest -Uri "https://codeload.github.com/FreeRTOS/FreeRTOS-Plus-TCP/zip/refs/heads/main"      -OutFile "./~temp/freertos_tcp.zip"
    Expand-Archive -Path "./~temp/freertos_tcp.zip" -DestinationPath "./~temp/"

    Remove-Item -Path "./freertos_tcp/src"                        -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/src/"                       -ItemType Directory
    Copy-Item   -Path "./~temp/FreeRTOS-Plus-TCP-main/source/*.c" -Destination "./freertos_tcp/src/" -Recurse
  
    Remove-Item -Path "./freertos_tcp/include"                            -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/include/"                           -ItemType Directory
    Copy-Item   -Path "./~temp/FreeRTOS-Plus-TCP-main/source/include/*.h" -Destination "./freertos_tcp/include" -Recurse

    Remove-Item -Path "./freertos_tcp/include_gcc"                            -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/include_gcc/"                           -ItemType Directory
    Copy-Item   -Path "./~temp/FreeRTOS-Plus-TCP-main/source/portable/Compiler/GCC/*.h" -Destination "./freertos_tcp/include_gcc" -Recurse

    Remove-Item -Path "./freertos_tcp/src_buffer"                            -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/src_buffer/"                           -ItemType Directory
    Copy-Item   -Path "./~temp/FreeRTOS-Plus-TCP-main/source/portable/BufferManagement/*.c" -Destination "./freertos_tcp/src_buffer" -Recurse

    Remove-Item -Path "./freertos_tcp/src_driver/linux"                            -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/src_driver/linux/"                           -ItemType Directory -ErrorAction SilentlyContinue
    Copy-Item   -Path "./~temp/FreeRTOS-Plus-TCP-main/source/portable/NetworkInterface/linux/*.c" -Destination "./freertos_tcp/src_driver/linux" -Recurse

    New-Item    -Path "./freertos_tcp/src_driver/stm32h7_hal/"                           -ItemType Directory -ErrorAction SilentlyContinue
    Remove-Item -Path "./freertos_tcp/src_driver/stm32h7_hal/NetworkInterface.c"         -ErrorAction SilentlyContinue
    Copy-Item   -Path "./~temp/FreeRTOS-Plus-TCP-main/source/portable/NetworkInterface/STM32/NetworkInterface.c" -Destination "./freertos_tcp/src_driver/stm32h7_hal/NetworkInterface.c" -Recurse

    Remove-Item -Path "./freertos_tcp/include_driver/"                            -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/include_driver/"                           -ItemType Directory
    Copy-Item   -Path "./~temp/FreeRTOS-Plus-TCP-main/source/portable/NetworkInterface/include/phyHandling.h" -Destination "./freertos_tcp/include_driver/phyHandling.h" -Recurse

    Remove-Item -Path "./freertos_tcp/src_driver/common"                            -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/src_driver/common/"                           -ItemType Directory
    Copy-Item   -Path "./~temp/FreeRTOS-Plus-TCP-main/source/portable/NetworkInterface/Common/phyHandling.c" -Destination "./freertos_tcp/src_driver/common/phyHandling.c" -Recurse
    
    Remove-Item -Path "./freertos_tcp/include_iperf/"  -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/include_iperf/"  -ItemType Directory
    Remove-Item -Path "./freertos_tcp/src_iperf/"      -Recurse -ErrorAction SilentlyContinue
    New-Item    -Path "./freertos_tcp/src_iperf/"      -ItemType Directory
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/htibosch/freertos_plus_projects/master/plus/Common/Utilities/iperf_task.h"       -OutFile "./freertos_tcp/include_iperf/iperf_task.h"
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/htibosch/freertos_plus_projects/master/plus/Common/Utilities/iperf_task_v3_0g.c" -OutFile "./freertos_tcp/src_iperf/iperf_task.c"
}



#
# Pipeline
#

Push-Location $PSScriptRoot

Remove-Item -Path "./~temp/" -Force -Recurse -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "./~temp/" -ErrorAction SilentlyContinue

Get-FreeRTOS-TCP

Pop-Location
