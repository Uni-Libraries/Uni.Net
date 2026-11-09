#
# CMake
#

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/")

if(NOT DEFINED UNI_NET_FREERTOS_TCP_CONFIG_EXTRA)
    set(UNI_NET_FREERTOS_TCP_CONFIG_EXTRA "" CACHE FILEPATH "Header included at the end of FreeRTOSIPConfig.h, to add or to #undef and redefine settings")
endif()

#
# Library
#

add_library(freertos_tcp STATIC)



#
# Sources
#
target_sources(freertos_tcp PRIVATE
        "src/FreeRTOS_ARP.c"
        "src/FreeRTOS_BitConfig.c"
        "src/FreeRTOS_DHCP.c"
        "src/FreeRTOS_DHCPv6.c"
        "src/FreeRTOS_DNS.c"
        "src/FreeRTOS_DNS_Cache.c"
        "src/FreeRTOS_DNS_Callback.c"
        "src/FreeRTOS_DNS_Networking.c"
        "src/FreeRTOS_DNS_Parser.c"
        "src/FreeRTOS_ICMP.c"
        "src/FreeRTOS_IP.c"
        "src/FreeRTOS_IP_Timers.c"
        "src/FreeRTOS_IP_Utils.c"
        "src/FreeRTOS_IPv4.c"
        "src/FreeRTOS_IPv4_Sockets.c"
        "src/FreeRTOS_IPv4_Utils.c"
        "src/FreeRTOS_IPv6.c"
        "src/FreeRTOS_IPv6_Sockets.c"
        "src/FreeRTOS_IPv6_Utils.c"
        "src/FreeRTOS_ND.c"
        "src/FreeRTOS_RA.c"
        "src/FreeRTOS_Routing.c"
        "src/FreeRTOS_Sockets.c"
        "src/FreeRTOS_Stream_Buffer.c"
        "src/FreeRTOS_TCP_IP.c"
        "src/FreeRTOS_TCP_IP_IPv4.c"
        "src/FreeRTOS_TCP_IP_IPv6.c"
        "src/FreeRTOS_TCP_Reception.c"
        "src/FreeRTOS_TCP_State_Handling.c"
        "src/FreeRTOS_TCP_State_Handling_IPv4.c"
        "src/FreeRTOS_TCP_State_Handling_IPv6.c"
        "src/FreeRTOS_TCP_Transmission.c"
        "src/FreeRTOS_TCP_Transmission_IPv4.c"
        "src/FreeRTOS_TCP_Transmission_IPv6.c"
        "src/FreeRTOS_TCP_Utils.c"
        "src/FreeRTOS_TCP_Utils_IPv4.c"
        "src/FreeRTOS_TCP_Utils_IPv6.c"
        "src/FreeRTOS_TCP_WIN.c"
        "src/FreeRTOS_Tiny_TCP.c"
        "src/FreeRTOS_UDP_IP.c"
        "src/FreeRTOS_UDP_IPv4.c"
        "src/FreeRTOS_UDP_IPv6.c"

        "src_buffer/BufferAllocation_1.c"
        "src_driver/common/phyHandling.c"
        #"src_iperf/iperf_task.c"
)

if(UNI_HAL_TARGET_MCU STREQUAL "STM32H743")
    target_sources(freertos_tcp PRIVATE "src_driver/stm32h7_hal/NetworkInterface.c")
    set_source_files_properties("src_driver/stm32h7_hal/NetworkInterface.c" PROPERTIES COMPILE_FLAGS "-Wno-expansion-to-defined -Wno-unused-function -Wno-unused-parameter")
elseif(UNI_HAL_TARGET_MCU STREQUAL "ZYNQ7000")
    # Xilinx GEM (emacps) of the PS; the driver allocates the network buffers in uncached memory
    set(_zynq_src
        "src_driver/Zynq/NetworkInterface.c"
        "src_driver/Zynq/uncached_memory.c"
        "src_driver/Zynq/x_emacpsif_dma.c"
        "src_driver/Zynq/x_emacpsif_hw.c"
        "src_driver/Zynq/x_emacpsif_physpeed.c"
    )
    target_sources(freertos_tcp PRIVATE ${_zynq_src})
    target_include_directories(freertos_tcp PRIVATE "src_driver" "src_driver/Zynq")
    # upstream code: unused variables, and a #warning whose condition is inverted
    # (it fires when ipconfigNIC_LINKSPEED100 is *not* set)
    set_source_files_properties(${_zynq_src} PROPERTIES COMPILE_FLAGS "-Wno-unused-variable -Wno-unused-parameter -Wno-cpp")
elseif(UNI_HAL_TARGET_MCU STREQUAL "PC")
    if(LINUX)
        find_package(PCAP REQUIRED)
        target_sources(freertos_tcp PRIVATE "src_driver/linux/NetworkInterface.c")
        set_source_files_properties("src_driver/linux/NetworkInterface.c" PROPERTIES COMPILE_FLAGS -Wno-unused-parameter)
    else()
        message(FATAL_ERROR "Unsupported platform")
    endif()
endif()



#
# Includes
#

target_include_directories(freertos_tcp PUBLIC "include")
target_include_directories(freertos_tcp PUBLIC "include_gcc")
target_include_directories(freertos_tcp PRIVATE "include_driver")
target_include_directories(freertos_tcp PUBLIC "include_iperf")
target_include_directories(freertos_tcp PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/freertos_tcp_config/")



#
# Libraries
#

target_link_libraries(freertos_tcp PUBLIC freertos_kernel)
if(UNI_HAL_TARGET_MCU STREQUAL "PC")
    target_link_libraries(freertos_tcp PUBLIC pcap::pcap)
elseif(UNI_HAL_TARGET_MCU STREQUAL "STM32H743")
    target_link_libraries(freertos_tcp PUBLIC st_stm32h7)
elseif(UNI_HAL_TARGET_MCU STREQUAL "ZYNQ7000")
    target_link_libraries(freertos_tcp PUBLIC xilinx_zynq7000)
endif()


#
# Properties
#

#language standard)
if(MSVC)
    target_compile_features(freertos_tcp PRIVATE c_std_17)
else()
    target_compile_features(freertos_tcp PRIVATE c_std_23)
endif()

target_compile_options(freertos_tcp PRIVATE -Wno-array-bounds)
target_compile_options(freertos_tcp PRIVATE -Wno-strict-aliasing)
target_compile_options(freertos_tcp PRIVATE -Wno-unused-but-set-variable)



#
# Config
#

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/freertos_tcp_config/FreeRTOSIPConfig.h"
        "#ifndef FREERTOS_IP_CONFIG_H                                                   \n"
        "#define FREERTOS_IP_CONFIG_H                                                   \n"
        "                                                                               \n"
        "// Architecture                                                                \n"
        "#define ipconfigBYTE_ORDER                          (pdFREERTOS_LITTLE_ENDIAN) \n"
        "                                                                               \n"
        "// Debugging                                                                   \n"
        "#define ipconfigHAS_PRINTF                          (ipconfigDISABLE)          \n"
        "#define ipconfigHAS_DEBUG_PRINTF                    (ipconfigDISABLE)          \n"
        "                                                                               \n"
        "// DHCP                                                                        \n"
        "#define ipconfigUSE_DHCP                            (ipconfigENABLE)           \n"
        "#define ipconfigUSE_DHCP_HOOK                       (ipconfigENABLE)           \n"
        "#define ipconfigDHCP_REGISTER_HOSTNAME              (ipconfigENABLE)           \n"
        "#define ipconfigMAXIMUM_DISCOVER_TX_PERIOD          (5000          )           \n"
        "                                                                               \n"
        "// DNS                                                                         \n"
        "#define ipconfigUSE_DNS                             (ipconfigDISABLE)          \n"
        "                                                                               \n"
        "// Driver                                                                      \n"
        "#define ipconfigZERO_COPY_RX_DRIVER                 (ipconfigENABLE)           \n"
        "#define ipconfigZERO_COPY_TX_DRIVER                 (ipconfigENABLE)           \n"
        "#define ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES (ipconfigENABLE)           \n"
        "#define ipconfigETHERNET_DRIVER_FILTERS_PACKETS     (ipconfigDISABLE)          \n"
        "#define ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM      (ipconfigENABLE)           \n"
        "#define ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM      (ipconfigENABLE)           \n"
        "#define ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS      (32)                       \n"
        "#define ipconfigPHY_MAX_PORTS                       (1)                        \n"
        "                                                                               \n"
        "// ICMP                                                                        \n"
        "#define ipconfigREPLY_TO_INCOMING_PINGS             (ipconfigENABLE)           \n"
        "#define ipconfigSUPPORT_OUTGOING_PINGS              (ipconfigENABLE)           \n"
        "                                                                               \n"
        "// IPerf                                                                       \n"
        "#define ipconfigIPERF_VERSION                       (3)                        \n"
        "                                                                               \n"
        "// PHY                                                                         \n"
        "#define ipconfigPHY_LS_HIGH_CHECK_TIME_MS           (1000)                     \n"
        "                                                                               \n"
        "// Routing                                                                     \n"
        "#define ipconfigMULTI_INTERFACE                     (ipconfigDISABLE)          \n"
        "                                                                               \n"
        "// TCP                                                                         \n"
        "#define ipconfigTCP_WIN_SEG_COUNT                   (64)                       \n"
        "#define ipconfigTCP_HANG_PROTECTION				 (ipconfigENABLE)           \n"
        "#define ipconfigTCP_HANG_PROTECTION_TIME	         (30)                       \n"
        "#define ipconfigTCP_KEEP_ALIVE			             (ipconfigENABLE)           \n"
        "#define ipconfigTCP_KEEP_ALIVE_INTERVAL		     (20)                       \n"
        "#define ipconfigTCP_RX_BUFFER_LENGTH                (8 * ipconfigTCP_MSS )     \n"
        "                                                                               \n"
        "#define ipconfigSUPPORT_SELECT_FUNCTION    (ipconfigENABLE)                    \n"
        "#define ipconfigSUPPORT_NETWORK_DOWN_EVENT (ipconfigENABLE)                    \n"
        "#define ipconfigSUPPORT_SIGNALS            (ipconfigENABLE)                    \n"
        "                                                                               \n"
        "#define ipconfigUSE_CALLBACKS            (ipconfigENABLE)                      \n"
        "#define ipconfigUSE_TCP_WIN              (ipconfigENABLE)                      \n"
        "#define ipconfigUSE_LINKED_RX_MESSAGES   (ipconfigENABLE)                      \n"
        "#define ipconfigUSE_NETWORK_EVENT_HOOK   (ipconfigENABLE)                      \n"
        "#define ipconfigUSE_IPv6                 (ipconfigDISABLE)                     \n"
        "                                                                               \n"
)

if(UNI_HAL_TARGET_MCU STREQUAL PC)
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/freertos_tcp_config/FreeRTOSIPConfig.h"
        "// Target Specific: Linux                                                 \n"
        "#define configNET_MASK0                  (255)                            \n"
        "#define configNET_MASK1                  (255)                            \n"
        "#define configNET_MASK2                  (0)                              \n"
        "#define configNET_MASK3                  (0)                              \n"
        "#define configMAC_ISR_SIMULATOR_PRIORITY (3)                              \n"
        "#define configNETWORK_INTERFACE_TO_USE   (1)                              \n"
        "#define configWINDOWS_MAC_INTERRUPT_SIMULATOR_DELAY (10)                  \n"
    )
endif()


if(UNI_HAL_TARGET_MCU STREQUAL "ZYNQ7000")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/freertos_tcp_config/FreeRTOSIPConfig.h"
        "// Target Specific: Zynq-7000 (src_driver/Zynq, see its README)           \n"
        "// the driver headers use XPAR_* without including it themselves          \n"
        "#include <xparameters.h>                                                  \n"
        "#define ipconfigPACKET_FILLER_SIZE       (2)                              \n"
        "#define ipconfigNIC_LINKSPEED_AUTODETECT (1)                              \n"
        "#define ipconfigNIC_N_TX_DESC            (128)                            \n"
        "#define ipconfigNIC_N_RX_DESC            (64)                             \n"
        "// more buffers than the RX descriptors hold                              \n"
        "#undef  ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS                            \n"
        "#define ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS (256)                      \n"
        "// GEM reference clock dividers for the link speeds: the SDT BSP does not \n"
        "// provide them, without them 100/10 Mbit/s links do not transmit.        \n"
        "// For IO PLL = 1000 MHz: 125 MHz (8, 1), 25 MHz (8, 5), 2.5 MHz (8, 50).  \n"
        "#define XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV0 (8)                   \n"
        "#define XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV1 (1)                   \n"
        "#define XPAR_PS7_ETHERNET_0_ENET_SLCR_100MBPS_DIV0  (8)                   \n"
        "#define XPAR_PS7_ETHERNET_0_ENET_SLCR_100MBPS_DIV1  (5)                   \n"
        "#define XPAR_PS7_ETHERNET_0_ENET_SLCR_10MBPS_DIV0   (8)                   \n"
        "#define XPAR_PS7_ETHERNET_0_ENET_SLCR_10MBPS_DIV1   (50)                  \n"
    )
endif()

if(UNI_NET_FREERTOS_TCP_CONFIG_EXTRA)
    get_filename_component(UNI_NET_FREERTOS_TCP_CONFIG_EXTRA_ABS "${UNI_NET_FREERTOS_TCP_CONFIG_EXTRA}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/freertos_tcp_config/FreeRTOSIPConfig.h"
        "// project specific settings                                              \n"
        "#include \"${UNI_NET_FREERTOS_TCP_CONFIG_EXTRA_ABS}\"                    \n"
    )
endif()

file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/freertos_tcp_config/FreeRTOSIPConfig.h"
        "#endif                                                                    \n"
        "                                                                          \n"
)
