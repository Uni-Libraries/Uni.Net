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
elseif(UNI_HAL_TARGET_MCU STREQUAL "PC")
    target_sources(freertos_tcp PRIVATE "src_driver/linux/NetworkInterface.c")
    set_source_files_properties("src_driver/linux/NetworkInterface.c" PROPERTIES COMPILE_FLAGS -Wno-unused-parameter)
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
    target_link_libraries(freertos_tcp PUBLIC pcap)
elseif(UNI_HAL_TARGET_MCU STREQUAL "STM32H743")
    target_link_libraries(freertos_tcp PUBLIC st_stm32h7)
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


file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/freertos_tcp_config/FreeRTOSIPConfig.h"
        "#endif                                                                    \n"
        "                                                                          \n"
)
