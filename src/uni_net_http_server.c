//
// Includes
//

// stdlib
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// FreeRTOS TCP
#include <FreeRTOS_IP.h>
#include <FreeRTOS_Sockets.h>

// Uni.Common
#include <uni_common.h>

// Uni.Hal
#include <uni_hal.h>

// Uni.Net
#include "uni_net_http_server.h"



//
// Defines
//

#define UNI_NET_HTTP_SERVER_IFACE_TIME    (250U)
#define UNI_NET_HTTP_SERVER_BLOCKING_TIME (50U)
#define UNI_NET_HTTP_SERVER_PORT          (80U)
#define UNI_NET_HTTP_SERVER_BACKLOG       (10U)
#define UNI_NET_HTTP_SERVER_RX_WIN        (2U)
#define UNI_NET_HTTP_SERVER_RX_BUF        (2U * ipconfigTCP_MSS)
#define UNI_NET_HTTP_SERVER_TX_WIN        (2U)
#define UNI_NET_HTTP_SERVER_TX_BUF        (2U * ipconfigTCP_MSS)



//
// Typedefs
//



typedef struct
{
    int32_t cmd_len;
    const char * cmd_name;
    const uni_net_http_command_type_e cmd_type;
} uni_net_http_command_t;



//
// Globals
//

#define UNI_NET_HTTP_CMD_COUNT (10U)

static const size_t g_UNI_NET_http_cmd_count = UNI_NET_HTTP_CMD_COUNT;

const uni_net_http_command_t g_UNI_NET_http_cmd[UNI_NET_HTTP_CMD_COUNT] =
{
    { 3, "GET",     UNI_NET_HTTP_COMMAND_GET     },
    { 4, "HEAD",    UNI_NET_HTTP_COMMAND_HEAD    },
    { 4, "POST",    UNI_NET_HTTP_COMMAND_POST    },
    { 3, "PUT",     UNI_NET_HTTP_COMMAND_PUT     },
    { 6, "DELETE",  UNI_NET_HTTP_COMMAND_DELETE  },
    { 5, "TRACE",   UNI_NET_HTTP_COMMAND_TRACE   },
    { 7, "OPTIONS", UNI_NET_HTTP_COMMAND_OPTIONS },
    { 7, "CONNECT", UNI_NET_HTTP_COMMAND_CONNECT },
    { 5, "PATCH",   UNI_NET_HTTP_COMMAND_PATCH   },
    { 4, "UNKN",    UNI_NET_HTTP_COMMAND_UNKNOWN },
};





//
// Private/CMD

static int32_t _uni_net_http_server_cmd_get_start(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, const char* url, const char* data, size_t data_len);
static int32_t _uni_net_http_server_cmd_get_next(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client);
static int32_t _uni_net_http_server_cmd_post_start(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, const char* url, const char* data, size_t data_len);
static int32_t _uni_net_http_server_cmd_post_next(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client);

typedef int32_t (*uni_net_http_server_cmd_start_handler_t)(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, const char* url, const char* data, size_t data_len);
typedef int32_t (*uni_net_http_server_cmd_next_handler_t)(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client);

typedef struct {
    uni_net_http_command_type_e cmd_type;
    uni_net_http_server_cmd_start_handler_t start_handler;
    uni_net_http_server_cmd_next_handler_t next_handler;
} uni_net_http_server_cmd_map_t;

static const uni_net_http_server_cmd_map_t g_http_cmd_map[] = {
    { UNI_NET_HTTP_COMMAND_GET,  _uni_net_http_server_cmd_get_start,  _uni_net_http_server_cmd_get_next  },
    { UNI_NET_HTTP_COMMAND_POST, _uni_net_http_server_cmd_post_start, _uni_net_http_server_cmd_post_next },
};



const char * _uni_net_http_server_status_name(uni_net_http_status_e aCode ) {
    switch (aCode) {
        case UNI_NET_HTTP_STATUS_OK: /*  = 200, */
            return "OK";
        case UNI_NET_HTTP_STATUS_NOCONTENT: /* 204 */
            return "No content";
        case UNI_NET_HTTP_STATUS_BADREQUEST: /*  = 400, */
            return "Bad request";
        case UNI_NET_HTTP_STATUS_UNAUTHORIZED: /*  = 401, */
            return "Authorization Required";
        case UNI_NET_HTTP_STATUS_NOTFOUND: /*  = 404, */
            return "Not Found";
        case UNI_NET_HTTP_STATUS_GONE: /*  = 410, */
            return "Done";
        case UNI_NET_HTTP_STATUS_PRECONDFAILED: /*  = 412, */
            return "Precondition Failed";
        case UNI_NET_HTTP_STATUS_INTERNALSERVERR: /*  = 500, */
            return "Internal Server Error";
        default:
            break;
    }
    return "Unknown";
}

static const char * _uni_net_http_server_content_type( const char * filename ) {
    const char *result = "text/html";

    if(filename != nullptr) {
        const char *slash = nullptr;
        const char *dot = nullptr;
        for (const char *ptr = filename; *ptr; ptr++) {
            if (*ptr == '.') {
                dot = ptr;
            }
            if (*ptr == '/') {
                slash = ptr;
            }
        }

        if (dot > slash) {
            dot++;
            result = uni_net_http_get_mime_type(dot);
        }
    }

    return result;
}

static int32_t _uni_net_http_server_send_header(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, uni_net_http_status_e status) {
    size_t idx = 0;
    ctx->state.buf_tx_hdr[0] = '\0';

    if(status != UNI_NET_HTTP_STATUS_OK) {
        client->content_type = nullptr;
        client->content_length = 0;
    }

    // HTTP code
    idx += uni_hal_io_stdio_snprintf(&ctx->state.buf_tx_hdr[idx], sizeof(ctx->state.buf_tx_hdr[idx])-idx-1, "HTTP/1.1 %d %s\r\n", (int) status, _uni_net_http_server_status_name(status));

    // Content Type
    idx += uni_hal_io_stdio_snprintf(&ctx->state.buf_tx_hdr[idx], sizeof(ctx->state.buf_tx_hdr[idx])-idx-1, "Content-Type: %s\r\n",
                   client->content_type != nullptr ? client->content_type : "text/html");

    // Connection
    idx += uni_hal_io_stdio_snprintf(&ctx->state.buf_tx_hdr[idx], sizeof(ctx->state.buf_tx_hdr[idx])-idx-1, "Connection: keep-alive\r\n");

    // Content length
#if defined(__linux__)
    idx += uni_hal_io_stdio_snprintf(&ctx->state.buf_tx_hdr[idx], sizeof(ctx->state.buf_tx_hdr[idx])-idx-1, "Content-Length: %u\r\n\r\n", client->content_length);
#else
    idx += uni_hal_io_stdio_snprintf(&ctx->state.buf_tx_hdr[idx], sizeof(ctx->state.buf_tx_hdr[idx])-idx-1, "Content-Length: %lu\r\n\r\n", (unsigned long)client->content_length);
#endif

    int32_t result = FreeRTOS_send(client->socket, ctx->state.buf_tx_hdr, idx, 0);
    client->header_sent = true;

    return result;
}

static void _uni_net_http_server_client_clear(uni_net_http_server_client_state_t* client) {
    client->command_type = UNI_NET_HTTP_COMMAND_UNKNOWN;
    client->file = NULL;
    client->handler = NULL;
    client->file_offset = 0U;
    client->header_sent = false;
}



//
// Private/CMD/Get
//

static int32_t _uni_net_http_server_cmd_get_sendfile(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, const char* url) {
    int32_t result = 0;

    if (!client->header_sent) {
        client->content_type = _uni_net_http_server_content_type(url);
        client->content_length = client->file->size;
        result = _uni_net_http_server_send_header(ctx, client, UNI_NET_HTTP_STATUS_OK);
    }

    size_t uxCount = 0;
    if (result >= 0) {
        do {
            size_t uxSpace = FreeRTOS_tx_space(client->socket);
            uxCount = uni_common_math_min3(uxSpace, client->file->size - client->file_offset,
                                       sizeof(ctx->state.buf_tx));

            if (uxCount > 0u) {
                memcpy(ctx->state.buf_tx, &client->file->data[client->file_offset], uxCount);
                client->file_offset += uxCount;
                result = FreeRTOS_send(client->socket, ctx->state.buf_tx, uxCount, 0);
                if (result < 0) {
                    break;
                }
            }
        } while (uxCount > 0u);
    }

    if (client->file_offset >= client->file->size) {
        // Writing is ready, no need for further 'eSELECT_WRITE' events.
        FreeRTOS_FD_CLR(client->socket, ctx->state.socket_set, eSELECT_WRITE);
        _uni_net_http_server_client_clear(client);
    } else {
        // Wake up the TCP task as soon as this socket may be written to
        FreeRTOS_FD_SET(client->socket, ctx->state.socket_set, eSELECT_WRITE);
    }

    return result;
}

static int32_t _uni_net_http_server_cmd_get_sendresponse(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, const char* url) {
    int32_t result = 0;

    // format string
    if (client->handler->command == UNI_NET_HTTP_COMMAND_GET && client->handler->function != NULL) {
        result = client->handler->function(client->handler->userdata, (uint8_t*)ctx->state.buf_tx, sizeof(ctx->state.buf_tx), (const uint8_t*)ctx->state.buf_rx, sizeof(ctx->state.buf_rx));

        // calculate space
        client->content_length = FreeRTOS_tx_space(client->socket);
        client->content_length = uni_common_math_min(client->content_length, (uint32_t)result);

        // send response
        client->content_type = _uni_net_http_server_content_type(url);

        // Requested file action OK
        if (_uni_net_http_server_send_header(ctx, client, UNI_NET_HTTP_STATUS_OK) >= 0) {
            if (result >= 0) {
                result = FreeRTOS_send(client->socket, ctx->state.buf_tx, client->content_length, 0);
            }
        }
    } else {
        result = _uni_net_http_server_send_header(ctx, client, UNI_NET_HTTP_STATUS_INTERNALSERVERR);
    }

    // Writing is ready, no need for further 'eSELECT_WRITE' events.
    FreeRTOS_FD_CLR(client->socket, ctx->state.socket_set, eSELECT_WRITE);

    _uni_net_http_server_client_clear(client);

    return result;
}


static int32_t _uni_net_http_server_cmd_get_start(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, const char* url, const char* data, size_t data_len) {
    int32_t result;

    (void)data;
    (void)data_len;

    client->command_type = UNI_NET_HTTP_COMMAND_GET;

    if (uni_common_array_valid(&ctx->config.handlers)) {
        for (size_t i = 0; i < uni_common_array_size(&ctx->config.handlers); ++i) {
            const uni_net_http_handler_t *handler = (const uni_net_http_handler_t *)uni_common_array_get(&ctx->config.handlers, i);
            if (handler->command == UNI_NET_HTTP_COMMAND_GET && strcmp(url, handler->path) == 0) {
                client->handler = handler;
                break;
            }
        }
    }

    if (client->handler != NULL) {
        result = _uni_net_http_server_cmd_get_sendresponse(ctx, client, url);
    } else {
        if (uni_common_array_valid(&ctx->config.files)) {
            for (size_t i = 0; i < uni_common_array_size(&ctx->config.files); ++i) {
                const uni_net_http_file_t *file = (const uni_net_http_file_t *)uni_common_array_get(&ctx->config.files, i);
                if (strcmp(url, file->path) == 0) {
                    client->file = file;
                    break;
                }
            }
        }

        if (client->file != NULL) {
            result = _uni_net_http_server_cmd_get_sendfile(ctx, client, url);
        } else {
            result = _uni_net_http_server_send_header(ctx, client, UNI_NET_HTTP_STATUS_NOTFOUND);
            _uni_net_http_server_client_clear(client);
        }
    }

    return result;
}


static int32_t _uni_net_http_server_cmd_get_next(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client) {
    int32_t result = 0U;
    if (client->file != NULL) {
        result = _uni_net_http_server_cmd_get_sendfile(ctx, client, NULL);
    }
    return result;
}



//
// Private/CMD/POST
//

static int32_t _uni_net_http_server_cmd_post_next(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client) {
    int32_t result = 0U;

    if (client->handler != NULL) {
        size_t remaining = client->content_length - client->file_offset;
        if (remaining > 0U) {
            result = FreeRTOS_recv(client->socket, ctx->state.buf_rx, sizeof(ctx->state.buf_rx), 0);
            if (result > 0) {
                client->handler->function(client->handler->userdata, NULL, 0U, (const uint8_t*)ctx->state.buf_rx, result);
                client->file_offset += result;
            } else {
                FreeRTOS_printf(("Receive error during POST body: %d\n", result));
            }
        }
        else{
            result = client->handler->function(client->handler->userdata, (uint8_t*)ctx->state.buf_tx, sizeof(ctx->state.buf_tx), NULL, 0U);

            client->content_length = FreeRTOS_tx_space(client->socket);
            client->content_length = uni_common_math_min(client->content_length, (uint32_t)result);
            client->content_type = "text/plain";

            if (_uni_net_http_server_send_header(ctx, client, UNI_NET_HTTP_STATUS_OK) >= 0) {
                if (result >= 0) {
                    result = FreeRTOS_send(client->socket, ctx->state.buf_tx, client->content_length, 0);
                }
            }
            _uni_net_http_server_client_clear(client);
            FreeRTOS_FD_CLR(client->socket, ctx->state.socket_set, eSELECT_READ | eSELECT_WRITE);
        }
    }

    return result;
}


static int32_t _uni_net_http_server_cmd_post_start(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, const char* url, const char* data, size_t data_len) {
    int32_t result = 0U;

    client->command_type = UNI_NET_HTTP_COMMAND_POST;

    if (uni_common_array_valid(&ctx->config.handlers)) {
        for (size_t i = 0; i < uni_common_array_size(&ctx->config.handlers); ++i) {
            const uni_net_http_handler_t *handler = (const uni_net_http_handler_t *)uni_common_array_get(&ctx->config.handlers, i);
            if (handler->command == UNI_NET_HTTP_COMMAND_POST && strcmp(url, handler->path) == 0) {
                client->handler = handler;
                break;
            }
        }
    }

    if (client->handler != NULL) {
        char *length_ptr = strstr(data, "Content-Length: ");
        char *body_start = strstr(data, "\r\n\r\n");
        if (length_ptr != NULL && body_start != NULL) {
            length_ptr += 16;
            body_start += 4;

            client->content_length = strtol(length_ptr, NULL, 10);
            FreeRTOS_printf(("_uni_net_http_server_cmd_post_start: CONLEN= %u\n", client->content_length));

                data_len -= (body_start - data);
                client->handler->function(client->handler->userdata, NULL, 0U, (const uint8_t*)body_start, data_len);
                client->file_offset += data_len;
                if (client->file_offset >= client->content_length) {
                    result = _uni_net_http_server_cmd_post_next(ctx, client);
                }
                else{
                    FreeRTOS_FD_SET(client->socket, ctx->state.socket_set, eSELECT_READ);
                }
        }
        else{
            result = _uni_net_http_server_send_header(ctx, client, UNI_NET_HTTP_STATUS_BADREQUEST);
            _uni_net_http_server_client_clear(client);
        }
    }
    else {
        result = _uni_net_http_server_send_header(ctx, client, UNI_NET_HTTP_STATUS_NOTFOUND);
        _uni_net_http_server_client_clear(client);
    }

    return result;
}

//
// Private/CMD
//

static int32_t _uni_net_http_server_cmd_process_start(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client, const uni_net_http_command_t* cmd, const char* url, const char* data, size_t data_len) {
    for (size_t i = 0; i < sizeof(g_http_cmd_map) / sizeof(g_http_cmd_map[0]); ++i) {
        if (g_http_cmd_map[i].cmd_type == cmd->cmd_type) {
            return g_http_cmd_map[i].start_handler(ctx, client, url, data, data_len);
        }
    }
    return 0;
}

static int32_t _uni_net_http_server_cmd_process_next(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client) {
    for (size_t i = 0; i < sizeof(g_http_cmd_map) / sizeof(g_http_cmd_map[0]); ++i) {
        if (g_http_cmd_map[i].cmd_type == client->command_type) {
            return g_http_cmd_map[i].next_handler(ctx, client);
        }
    }
    return 0;
}

//
// Private/Client
//

static void _uni_net_http_server_client_new(uni_net_http_server_context_t* ctx, Socket_t socket) {
    if (socket != nullptr) {
        uni_net_http_server_client_state_t *client = pvPortCalloc(sizeof(*client), 1);
        client->socket = socket;
        FreeRTOS_FD_SET(client->socket, ctx->state.socket_set, eSELECT_READ | eSELECT_EXCEPT);

        for (size_t idx = 0; idx < ctx->config.max_clients; idx++) {
            if (ctx->state.clients[idx] == nullptr) {
                ctx->state.clients[idx] = client;
                break;
            }
        }
    }
}


static int32_t _uni_net_http_server_client_work(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client) {
    int32_t result = 0;
    if (!FreeRTOS_issocketconnected(client->socket)) {
        result = -1;
    }
    else if (client->command_type == UNI_NET_HTTP_COMMAND_UNKNOWN) {
        int32_t recv_cnt = FreeRTOS_recv(client->socket, ( void * ) ctx->state.buf_rx, sizeof( ctx->state.buf_rx ), 0 );
        if( recv_cnt > 0 ) {
            result = recv_cnt;
            char *pcBuffer = ctx->state.buf_rx;
            if (result < (BaseType_t) sizeof(ctx->state.buf_rx)) {
                pcBuffer[result] = '\0';
            }

            while (result && (pcBuffer[result - 1] == 13 || pcBuffer[result - 1] == 10)) {
                pcBuffer[--result] = '\0';
            }

            const char * pcEndOfCmd = pcBuffer + result;

            // Pointing to "/index.html HTTP/1.1"
            char* url = pcBuffer;
            char* data = nullptr;

            // Last entry is "ECMD_UNK"
            size_t cmd_idx = 0;
            for (; cmd_idx < g_UNI_NET_http_cmd_count - 1; cmd_idx++) {
                int32_t cmd_len = g_UNI_NET_http_cmd[cmd_idx].cmd_len;
                const char *cmd_name = g_UNI_NET_http_cmd[cmd_idx].cmd_name;

                if ((result >= cmd_len) && (memcmp(cmd_name, pcBuffer, cmd_len) == 0)) {
                    url += cmd_len + 1;
                    for (data = url; data < pcEndOfCmd; data++) {
                        char ch = *data;

                        if ((ch == '\0') || (strchr("\n\r \t", ch) != NULL)) {
                            *data = '\0';
                            break;
                        }
                    }
                    break;
                }
            }

            if (cmd_idx < (g_UNI_NET_http_cmd_count - 1)) {
                result = _uni_net_http_server_cmd_process_start(ctx, client, &g_UNI_NET_http_cmd[cmd_idx], url, data+1, recv_cnt - (data + 1 - ctx->state.buf_rx));
            }
        }
    }
    else {
        result = _uni_net_http_server_cmd_process_next(ctx, client);
    }

    return result;
}


static void _uni_net_http_server_client_delete(uni_net_http_server_context_t* ctx, uni_net_http_server_client_state_t* client) {
    if (client->socket != nullptr) {
        FreeRTOS_FD_CLR(client->socket, ctx->state.socket_set, eSELECT_ALL);
        FreeRTOS_closesocket(client->socket);
    }
    _uni_net_http_server_client_clear(client);
    vPortFree(client);
}



//
// Private/Work
//

bool _uni_net_http_server_work(uni_net_http_server_context_t* ctx) {
    bool result = true;

    uint32_t flags = FreeRTOS_select(ctx->state.socket_set, pdMS_TO_TICKS(UNI_NET_HTTP_SERVER_BLOCKING_TIME));
    if (flags != 0U) {
        struct freertos_sockaddr address;
        uint32_t address_length = sizeof(address);
        Socket_t socket_client = FreeRTOS_accept(ctx->state.socket, &address, &address_length);
        if ((socket_client != nullptr) && (socket_client != FREERTOS_INVALID_SOCKET)) {
            _uni_net_http_server_client_new(ctx, socket_client);
        }
    }

    for (size_t idx = 0; idx < ctx->config.max_clients; idx++) {
        uni_net_http_server_client_state_t *client = ctx->state.clients[idx];
        if (client != nullptr && _uni_net_http_server_client_work(ctx, client) < 0) {
            _uni_net_http_server_client_delete(ctx, client);
            ctx->state.clients[idx] = nullptr;
        }
    }

    return result;
}



//
// Private/Init
//

bool _uni_net_http_server_init(uni_net_http_server_context_t* ctx) {
    bool result = false;
    if (ctx != nullptr) {
        memset(&ctx->state, 0, sizeof(ctx->state));

        // This function is called from a worker thread, and it's possible that the network is not yet ready. 
        // Ensure that network is up.
        while (FreeRTOS_IsNetworkUp() == pdFALSE) {
            vTaskDelay(pdMS_TO_TICKS(UNI_NET_HTTP_SERVER_IFACE_TIME));
       }

        ctx->state.clients = pvPortCalloc(ctx->config.max_clients, sizeof(uni_net_http_server_client_state_t *));
        ctx->state.socket_set = FreeRTOS_CreateSocketSet();
        ctx->state.socket = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);

        struct freertos_sockaddr xAddress = {
            .sin_port = FreeRTOS_htons(UNI_NET_HTTP_SERVER_PORT),
            .sin_family = FREERTOS_AF_INET,
        };
        FreeRTOS_bind(ctx->state.socket, &xAddress, sizeof(xAddress));
        FreeRTOS_listen(ctx->state.socket, ctx->config.max_clients);

        BaseType_t xNoTimeout = 0;
        FreeRTOS_setsockopt(ctx->state.socket, 0, FREERTOS_SO_RCVTIMEO, (void *) &xNoTimeout, sizeof(xNoTimeout));
        FreeRTOS_setsockopt(ctx->state.socket, 0, FREERTOS_SO_SNDTIMEO, (void *) &xNoTimeout, sizeof(xNoTimeout));

        WinProperties_t xWinProps = {
            .lTxBufSize = UNI_NET_HTTP_SERVER_TX_BUF,
            .lTxWinSize = UNI_NET_HTTP_SERVER_TX_WIN,
            .lRxBufSize = UNI_NET_HTTP_SERVER_RX_BUF,
            .lRxWinSize = UNI_NET_HTTP_SERVER_RX_WIN,
        };
        FreeRTOS_setsockopt(ctx->state.socket, 0, FREERTOS_SO_WIN_PROPERTIES, (void *) &xWinProps, sizeof(xWinProps));

        FreeRTOS_FD_SET(ctx->state.socket, ctx->state.socket_set, eSELECT_READ | eSELECT_EXCEPT);
        result = true;
    }

    return result;
}



//
// Private/Thread
//

_Noreturn void _uni_net_http_thread(void* args) { //-V1082
    uni_net_http_server_context_t *ctx = (uni_net_http_server_context_t *) args;

    _uni_net_http_server_init(ctx);
    while (true) { //-V1044 //-V776
        _uni_net_http_server_work(ctx);
    }
}



//
// Functions
//

bool uni_net_http_server_init(uni_net_http_server_context_t* ctx ) {
    bool result = false;

    if (ctx != nullptr && !uni_net_http_server_is_inited(ctx)) {
        result = xTaskCreate(_uni_net_http_thread, "UNI_NET_HTTP_SERVER", configMINIMAL_STACK_SIZE * 4, ctx, 1,
                             &ctx->state.handle) == pdTRUE;
        ctx->state.initialized = result;
    }

    return result;
}


bool uni_net_http_server_is_inited(const uni_net_http_server_context_t* ctx ){
    return ctx != nullptr && ctx->state.initialized != false;
}


bool uni_net_http_server_signal(uni_net_http_server_context_t* ctx) {
    bool result = false;
    if (ctx != nullptr) {
        FreeRTOS_SignalSocket(ctx->state.socket);
        result = true;
    }
    return result;
}

bool uni_net_http_server_register_file(uni_net_http_server_context_t* ctx, const uni_net_http_file_t* file) {
    bool result = false;
    if (ctx != NULL && file != NULL) {
        result = uni_common_array_push_back(&ctx->config.files, file);
    }
    return result;
}

bool uni_net_http_server_register_handler(uni_net_http_server_context_t* ctx, const uni_net_http_handler_t* handler) {
    bool result = false;
    if (ctx != NULL && handler != NULL) {
        result = uni_common_array_push_back(&ctx->config.handlers, handler);
    }
    return result;
}


bool uni_net_http_server_register_handler_ex(uni_net_http_server_context_t* ctx, uni_net_http_command_type_e command, const char* path, uni_net_http_handler_fn function, void* userdata) {
    bool result = false;
    if (ctx != NULL && path != NULL && function != NULL) {
        uni_net_http_handler_t handler = {
            .path = path,
            .command = command,
            .function = function,
            .userdata = userdata,
        };
        result = uni_net_http_server_register_handler(ctx, &handler);
    }
    return result;
}

bool uni_net_http_server_register_file_ex(uni_net_http_server_context_t* ctx, const char* path, const uint8_t* data, uint32_t size) {
    bool result = false;
    if (ctx != NULL && path != NULL && data != NULL) {
        uni_net_http_file_t file = {
            .path = path,
            .data = data,
            .size = size,
        };
        result = uni_net_http_server_register_file(ctx, &file);
    }
    return result;
}

bool uni_net_http_server_signal_from_isr(uni_net_http_server_context_t* ctx, BaseType_t *  pxHigherPriorityTaskWoken) {
    bool result = false;
    if (ctx != NULL) {
        FreeRTOS_SignalSocketFromISR(ctx->state.socket, pxHigherPriorityTaskWoken);
        result = true;
    }
    return result;
}
