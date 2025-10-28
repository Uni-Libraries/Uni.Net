#pragma once

//
// Includes
//

// stdlib
#include <stdint.h>
#include <stdbool.h>

// FreeRTOS
#include <FreeRTOS_IP.h>
#include <FreeRTOS_Sockets.h>



// Uni.Net
#include "uni_net_http_common.h"

#include "uni_common_array.h"



//
// Typedefs
//


#define UNI_NET_HTTP_SERVER_RX_BUF        (2U * ipconfigTCP_MSS)
#define UNI_NET_HTTP_SERVER_TX_BUF        (6U * ipconfigTCP_MSS)


typedef struct {
    /**
     * Connect client socket
     */
    Socket_t socket;

    /**
     * Command
     */
    uni_net_http_command_type_e command_type;

    /**
     * Pointer to current file description
     */
    const uni_net_http_file_t* file;
    const uni_net_http_handler_t* handler;

    /**
     * Current file offset
     */
    uint32_t file_offset;

    /**
     * Content Length to sent
     */
    uint32_t content_length;

    /**
     * Start of the reply was sent
     */
    bool header_sent;

    /**
     * Content type to sent
     */
    const char* content_type;

    /**
     * Accumulated bytes in buf_rx for request parsing
     */
    uint32_t rx_len;

    /**
     * Request headers parsing state (true when "\r\n\r\n" found)
     */
    bool headers_done;

    /**
     * A buffer to receive.
     */
    char buf_rx[ UNI_NET_HTTP_SERVER_RX_BUF ];

    /**
     * A buffer to send.
     */
    char buf_tx[ UNI_NET_HTTP_SERVER_TX_BUF ];
} uni_net_http_server_client_state_t;


typedef struct
{
    /**
     * Server was initialized
     */
    bool initialized;

    /**
     * Server task handle
     */
    TaskHandle_t handle;

    /**
     * Set of server and client sockets
     */
    SocketSet_t socket_set;

    /**
     * Server listening socket
     */
    Socket_t socket;

    /**
     * HTTP clients
     */
    uni_net_http_server_client_state_t ** clients;

    /**
     * A buffer to send.
     */
    char buf_tx_hdr[ ipconfigTCP_MSS ];
} uni_net_http_server_state_t;


typedef struct {
    /**
     * An array of responce handlers
     */
    uni_common_array_t handlers;

    /**
     * An array of registered files
     */
    uni_common_array_t files;

    /**
     * Number of max clients
     */
    size_t max_clients;

} uni_net_http_server_config_t;


typedef struct {
    /**
     * Server configuration
     */
    uni_net_http_server_config_t config;

    /**
     * Server state
     */
    uni_net_http_server_state_t state;
} uni_net_http_server_context_t;



//
// Functions
//

bool uni_net_http_server_init(uni_net_http_server_context_t* ctx);

bool uni_net_http_server_is_inited(const uni_net_http_server_context_t* ctx );

bool uni_net_http_server_signal(uni_net_http_server_context_t* ctx);

bool uni_net_http_server_signal_from_isr(uni_net_http_server_context_t* ctx, BaseType_t* higherPriorityTaskWoken);

bool uni_net_http_server_register_file(uni_net_http_server_context_t* ctx, const uni_net_http_file_t* file);
bool uni_net_http_server_register_file_ex(uni_net_http_server_context_t* ctx, const char* path, const uint8_t* data, uint32_t size);

bool uni_net_http_server_register_handler(uni_net_http_server_context_t* ctx, const uni_net_http_handler_t* handler);
bool uni_net_http_server_register_handler_ex(uni_net_http_server_context_t* ctx, uni_net_http_command_type_e command, const char* path, uni_net_http_handler_fn function, void* userdata);
