#pragma once

//
// Includes
//

// stdlib
#include <stdint.h>

// FreeRTOS
#include <FreeRTOS_Sockets.h>


// Uni.Net
#include "uni_net_http_common.h"



//
// Typedefs
//




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
    uni_net_http_file_t* file_handle;

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
     * A buffer to receive.
     */
    char buf_rx[ ipconfigTCP_MSS ];

    /**
     * A buffer to send.
     */
    char buf_tx[ ipconfigTCP_MSS ];

    /**
     * A buffer to send.
     */
    char buf_tx_hdr[ ipconfigTCP_MSS ];
} uni_net_http_server_state_t;


typedef struct {
    uni_net_http_file_t *files;

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

bool uni_net_http_server_from_isr(uni_net_http_server_context_t* ctx, BaseType_t* higherPriorityTaskWoken);
