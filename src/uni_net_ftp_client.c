//
// Includes
//

// stdlib
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FreeRTOS
#include <FreeRTOS_IP.h>

// Uni.Common
#include <uni_common.h>

// ox
#include "uni_net_ftp_client.h"



//
// Defines
//

#define UNI_NET_FTP_CLIENT_WAIT_MS (100U)
#if defined(UNI_NET_FTP_CLIENT_DEBUG)
    #define UNI_NET_FTP_CLIENT_DBG_0(x)     printf(x "\r\n")
    #define UNI_NET_FTP_CLIENT_DBG_1(x,y)   printf(x "\r\n", y )
    #define UNI_NET_FTP_CLIENT_DBG_2(x,y,z) printf(x "\r\n", y, z )
#else
    #define UNI_NET_FTP_CLIENT_DBG_0(x)
    #define UNI_NET_FTP_CLIENT_DBG_1(x,y)
    #define UNI_NET_FTP_CLIENT_DBG_2(x,y,z)
#endif


//
// Typedefs
//

typedef enum {
    UNI_NET_FTP_CODE_150_OPENING_DATA_CONN = 150,
    UNI_NET_FTP_CODE_200_OK = 200,
    UNI_NET_FTP_CODE_202_NO_MEANING = 202,
    UNI_NET_FTP_CODE_220_SERVICE_READY = 220,
    UNI_NET_FTP_CODE_226_TRANSFER_COMPLETE = 226,
    UNI_NET_FTP_CODE_227_ENTERING_PASSIVE_MODE = 227,
    UNI_NET_FTP_CODE_230_LOGIN_OK = 230,
    UNI_NET_FTP_CODE_257_PATHNAME = 257,
    UNI_NET_FTP_CODE_331_PASSWORD_REQUIRED = 331,
    UNI_NET_FTP_CODE_421_SERVICE_NOT_AVAILABLE = 421,
    UNI_NET_FTP_CODE_425_FAILED_TO_OPEN_CONN = 425,
    UNI_NET_FTP_CODE_426_ERROR_WRITING_NETWORK_STREAM = 426,
    UNI_NET_FTP_CODE_451_SOCKET_ERROR = 451,
    UNI_NET_FTP_CODE_500_SERVICE_ERROR = 500,
    UNI_NET_FTP_CODE_501_NEED_PARAMETER = 501,
    UNI_NET_FTP_CODE_530_NOT_LOGGED_IN = 530,
    UNI_NET_FTP_CODE_550_FILE_UNAVAILABLE = 550,
} uni_net_ftp_code_e;


//
// Globals
//

static const char *_uni_net_ftp_client_startup_cmds[] = {
    "OPTS UTF8 ON\r\n",
    "TYPE I\r\n",
    "PWD\r\n",
};

static size_t _uni_net_ftp_client_startup_cmds_cnt = sizeof(_uni_net_ftp_client_startup_cmds) / sizeof(_uni_net_ftp_client_startup_cmds[0]);


//
// Function/Helpers
//

static bool _uni_net_ftp_client_send_cmd(uni_net_ftp_client_context_t *ctx, const char *cmd) {
    bool result = false;
    if (cmd != NULL) {
        int32_t len = strlen(cmd);
        result = FreeRTOS_send(ctx->state.socket_cmd, cmd, len, 0) == len;
    }
    return result;
}

static bool _uni_net_ftp_client_switch_to_passive(uni_net_ftp_client_context_t *ctx) {
    return _uni_net_ftp_client_send_cmd(ctx, "PASV\r\n");
}

static bool _uni_net_ftp_client_list_files(uni_net_ftp_client_context_t *ctx) {
    return _uni_net_ftp_client_send_cmd(ctx, "LIST\r\n");
}

static bool _uni_net_ftp_client_retr_file(uni_net_ftp_client_context_t *ctx, const char *file) {
    char data[64] = {};
    sprintf(data, "RETR %s\r\n", file);
    return _uni_net_ftp_client_send_cmd(ctx, data);
}

static bool _uni_net_ftp_client_send_login(uni_net_ftp_client_context_t *ctx) {
    char data[64] = {};
    sprintf(data, "USER %s\r\n", ctx->config.auth_user);
    return _uni_net_ftp_client_send_cmd(ctx, data);
}

static bool _uni_net_ftp_client_send_password(uni_net_ftp_client_context_t *ctx) {
    char data[64] = {};
    sprintf(data, "PASS %s\r\n", ctx->config.auth_password);
    return _uni_net_ftp_client_send_cmd(ctx, data);
}

static void _uni_net_ftp_client_set_to_idle(uni_net_ftp_client_context_t *ctx) {
    if (ctx->state.socket_data != NULL) {
        UNI_NET_FTP_CLIENT_DBG_0("_uni_net_ftp_client_set_to_idle() -> close data socket");
        FreeRTOS_closesocket(ctx->state.socket_data);
        ctx->state.socket_data = NULL;
    }

    ctx->state.task.type = UNI_NET_FTP_CLIENT_TASK_TYPE_IDLE;
    ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_FINISHED;
    ctx->state.task.cookie_file = NULL;
    ctx->state.task.progress = 0;
    ctx->state.task.progress_total = 0;
    ctx->state.task.name[0] = '\0';
}


//
// Functions/Connection
//

static bool _uni_net_ftp_client_connect_socket(uni_net_ftp_client_context_t *ctx, Socket_t *socket, uint16_t port) {
    bool result = false;

    // create socket set
    if (ctx->state.socket_set == NULL) {
        ctx->state.socket_set = FreeRTOS_CreateSocketSet();
    }

    // create socket
    *socket = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);

    if (*socket) {
        // set RX socket window for data port
        if (port != ctx->config.server_port) {
            WinProperties_t xWinProperties = {0};
            xWinProperties.lRxBufSize = 20 * ipconfigTCP_MSS; /* Units of bytes. */
            xWinProperties.lRxWinSize = 10; /* Size in units of MSS */
            xWinProperties.lTxBufSize = 4 * ipconfigTCP_MSS; /* Units of bytes. */
            xWinProperties.lTxWinSize = 2; /* Size in units of MSS */
            FreeRTOS_setsockopt(*socket, 0, FREERTOS_SO_WIN_PROPERTIES, (void *) &xWinProperties,
                                sizeof(xWinProperties));
        }

        // create address structure
        struct freertos_sockaddr sockaddr = {
            .sin_family = FREERTOS_AF_INET,
            .sin_address = { .ulIP_IPv4 = ctx->config.server_addr },
            .sin_port = uni_common_bytes_swap16(port)
        };

        // set timeout RX
        if (ctx->config.timeout_rx > 0U) {
            TickType_t timeout_rx = pdMS_TO_TICKS(ctx->config.timeout_rx);
            FreeRTOS_setsockopt(*socket, 0, FREERTOS_SO_RCVTIMEO, &timeout_rx, sizeof(timeout_rx));
        }

        // set timeout TX
        if (ctx->config.timeout_tx > 0U) {
            TickType_t timeout_tx = pdMS_TO_TICKS(ctx->config.timeout_tx);
            FreeRTOS_setsockopt(*socket, 0, FREERTOS_SO_SNDTIMEO, &timeout_tx, sizeof(timeout_tx));
        }

        BaseType_t connect_result = FreeRTOS_connect(*socket, &sockaddr, sizeof(sockaddr));
        if (connect_result == 0) {
            result = true;
            FreeRTOS_FD_SET(*socket, ctx->state.socket_set, eSELECT_READ);
        } else {
            UNI_NET_FTP_CLIENT_DBG_1("_uni_net_ftp_client_connect_socket -> failed to connect socket, errno=%d",
                                (int)connect_result);
        }
    } else {
        UNI_NET_FTP_CLIENT_DBG_0("_uni_net_ftp_client_connect_socket -> failed to create socket");
    }

    return result;
}


static void _uni_net_ftp_client_disconnect(uni_net_ftp_client_context_t *ctx, bool call_callback) {
    if (ctx->state.socket_data != NULL) {
        FreeRTOS_closesocket(ctx->state.socket_data);
        ctx->state.socket_data = NULL;
    }

    if (ctx->state.socket_cmd != NULL) {
        FreeRTOS_closesocket(ctx->state.socket_cmd);
        ctx->state.socket_cmd = NULL;
    }

    if (ctx->state.socket_set != NULL) {
        FreeRTOS_DeleteSocketSet(ctx->state.socket_set);
        ctx->state.socket_set = NULL;
    }

    if(call_callback && ctx->state.callback) {
        ctx->state.callback(ctx->state.cookie, &ctx->state.task, UNI_NET_FTP_CLIENT_CALLBACK_DISCONNECT, NULL, 0);
    }

    ctx->state.task.type = UNI_NET_FTP_CLIENT_TASK_TYPE_TERMINATION;
}

//
// Functions/Handlers
//
static void _uni_net_ftp_client_handler_150(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_20x(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_220(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_226(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_227(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_230(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_257(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_331(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_error(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_530(uni_net_ftp_client_context_t *ctx, const char *str);
static void _uni_net_ftp_client_handler_550(uni_net_ftp_client_context_t *ctx, const char *str);

/**
 * 150: Opening Data Connection for File
 * @param ctx
 */
static void _uni_net_ftp_client_handler_150(uni_net_ftp_client_context_t *ctx, const char *str) {
    if (str != NULL) {
        //determine file size
        int data = 0;
        str = strchr(str, '(');
        if (str != NULL) {
            str++;
            sscanf(str, "%d", &data);
            ctx->state.task.progress_total = data;
        }
        else {
            ctx->state.task.progress_total = 1;
        }
    } else {
        _uni_net_ftp_client_set_to_idle(ctx);
    }
}


/**
 * 200: OK
 * @param ctx
 */
static void _uni_net_ftp_client_handler_20x(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void)str;
    ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_FINISHED;
}


/**
 * 220: service ready for new user
 * @param ctx
 */
static void _uni_net_ftp_client_handler_220(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void)str;
    // start startup sequence
    if(ctx->state.task.type == UNI_NET_FTP_CLIENT_TASK_TYPE_STARTUP) {
        ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_NOT_STARTED;
        ctx->state.task.progress = 0;
    }
}


/**
 * 226: transfer complete
 * @param ctx
 */
static void _uni_net_ftp_client_handler_226(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void) ctx;
    (void) str;
}


/**
 * 227: Entering Passive Mode
 * @param ctx
 */
static void _uni_net_ftp_client_handler_227(uni_net_ftp_client_context_t *ctx, const char *str) {
    bool result = false;

    if (str != NULL) {
        int data[6] = {0};
        str = strchr(str, '(');
        if (str != NULL) {
            str++;
            sscanf(str, "%d,%d,%d,%d,%d,%d", &data[0], &data[1], &data[2], &data[3], &data[4], &data[5]);
            uint16_t port = (data[4] << 8) + data[5];
            result = _uni_net_ftp_client_connect_socket(ctx, &ctx->state.socket_data, port);
        }
        ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_STARTED;
    }

    if (!result) {
        ctx->state.task.type = UNI_NET_FTP_CLIENT_TASK_TYPE_TERMINATION;
    }
}


/**
 * 230: Login Successful
 * @param ctx
 */
static void _uni_net_ftp_client_handler_230(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void)str;
    ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_FINISHED;
}


/**
 * 257: Pathname Created
 * @param ctx
 */
static void _uni_net_ftp_client_handler_257(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void)str;
    ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_FINISHED;
}


/**
 * 331: Password Required
 * @param ctx
 */
static void _uni_net_ftp_client_handler_331(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void)str;
    _uni_net_ftp_client_send_password(ctx);
}


static void _uni_net_ftp_client_handler_error(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void)str;
    _uni_net_ftp_client_disconnect(ctx, true);
}

/**
 * 530: not logged in
 * @param ctx
 */
static void _uni_net_ftp_client_handler_530(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void)str;
    _uni_net_ftp_client_send_login(ctx);
}


/**
 * 530: file unavailable
 * @param ctx
 */
static void _uni_net_ftp_client_handler_550(uni_net_ftp_client_context_t *ctx, const char *str) {
    (void)str;
    ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_FAILED;
}


//
// Functions/Thread
//

typedef void (*uni_net_ftp_client_cmd_handler_t)(uni_net_ftp_client_context_t *ctx, const char *str);

typedef struct {
    uni_net_ftp_code_e code;
    uni_net_ftp_client_cmd_handler_t handler;
} uni_net_ftp_client_cmd_map_t;

static const uni_net_ftp_client_cmd_map_t _uni_net_ftp_client_cmd_map[] = {
    { UNI_NET_FTP_CODE_150_OPENING_DATA_CONN, _uni_net_ftp_client_handler_150 },
    { UNI_NET_FTP_CODE_200_OK, _uni_net_ftp_client_handler_20x },
    { UNI_NET_FTP_CODE_202_NO_MEANING, _uni_net_ftp_client_handler_20x },
    { UNI_NET_FTP_CODE_220_SERVICE_READY, _uni_net_ftp_client_handler_220 },
    { UNI_NET_FTP_CODE_226_TRANSFER_COMPLETE, _uni_net_ftp_client_handler_226 },
    { UNI_NET_FTP_CODE_227_ENTERING_PASSIVE_MODE, _uni_net_ftp_client_handler_227},
    { UNI_NET_FTP_CODE_230_LOGIN_OK, _uni_net_ftp_client_handler_230 },
    { UNI_NET_FTP_CODE_257_PATHNAME, _uni_net_ftp_client_handler_257},
    { UNI_NET_FTP_CODE_331_PASSWORD_REQUIRED, _uni_net_ftp_client_handler_331},
    { UNI_NET_FTP_CODE_421_SERVICE_NOT_AVAILABLE, _uni_net_ftp_client_handler_error},
    { UNI_NET_FTP_CODE_425_FAILED_TO_OPEN_CONN, _uni_net_ftp_client_handler_error},
    { UNI_NET_FTP_CODE_426_ERROR_WRITING_NETWORK_STREAM, _uni_net_ftp_client_handler_error },
    { UNI_NET_FTP_CODE_451_SOCKET_ERROR, _uni_net_ftp_client_handler_error},
    { UNI_NET_FTP_CODE_500_SERVICE_ERROR, _uni_net_ftp_client_handler_error},
    { UNI_NET_FTP_CODE_501_NEED_PARAMETER, _uni_net_ftp_client_handler_error},
    { UNI_NET_FTP_CODE_530_NOT_LOGGED_IN, _uni_net_ftp_client_handler_530},
    { UNI_NET_FTP_CODE_550_FILE_UNAVAILABLE, _uni_net_ftp_client_handler_550},
};

static void _uni_net_ftp_client_work_cmd_single(uni_net_ftp_client_context_t *ctx, char *buf) {
    if (strlen(buf) < 4 || (buf[3] != ' ' && buf[3] != '-')) {
        printf("_uni_net_ftp_client_work_cmd() -> unknown data: %s\r\n", buf);
        _uni_net_ftp_client_disconnect(ctx, true);
        return;
    }

    buf[3] = '\0';
    int code = atoi(buf);
    const char *payload = &buf[4];
    UNI_NET_FTP_CLIENT_DBG_2("_uni_net_ftp_client_work_cmd() -> %d, %s", code, payload);
    
    for (size_t i = 0; i < sizeof(_uni_net_ftp_client_cmd_map) / sizeof(_uni_net_ftp_client_cmd_map[0]); ++i) {
        if (_uni_net_ftp_client_cmd_map[i].code == (uni_net_ftp_code_e)code) {
            _uni_net_ftp_client_cmd_map[i].handler(ctx, payload);
            return;
        }
    }

    printf("_uni_net_ftp_client_work_cmd() -> unknown cmd: %d\r\n", code);
    _uni_net_ftp_client_disconnect(ctx, true);
}

static void _uni_net_ftp_client_work_cmd(uni_net_ftp_client_context_t *ctx) {
    // retrieve amount of available data
    int byte_count = FreeRTOS_recvcount(ctx->state.socket_cmd);
    if (byte_count > 0) {
        // get received data
        // TODO: zerocopy
        char *buf = pvPortCalloc(byte_count + 1, 1);
        int byte_rcv = FreeRTOS_recv(ctx->state.socket_cmd, buf, byte_count, 0);
        if (byte_rcv > 0) {
            char *next_line;
            for (char *line = buf; line != NULL; line = next_line) {
                next_line = strstr(line, "\r\n");
                if (next_line) {
                    *next_line = '\0';
                    next_line += 2; 
                }
                if (*line) {
                    _uni_net_ftp_client_work_cmd_single(ctx, line);
                }
            }
        }


        // clear memory
        vPortFree(buf);
    }
}


static void _uni_net_ftp_client_work_data(uni_net_ftp_client_context_t *ctx) {
    switch (ctx->state.task.type) {
        case UNI_NET_FTP_CLIENT_TASK_TYPE_LIST:
        case UNI_NET_FTP_CLIENT_TASK_TYPE_RETR: {
            int32_t cnt = 0;
            do {
                uint8_t *buf = NULL;
                cnt = FreeRTOS_recv(ctx->state.socket_data, &buf, ipconfigTCP_MSS,
                                    FREERTOS_ZERO_COPY | FREERTOS_MSG_DONTWAIT);
                if (cnt > 0 && buf != NULL) {
                    if(ctx->state.task.state == UNI_NET_FTP_CLIENT_TASK_STATE_STARTED) {
                        ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_IN_PROGRESS;
                    }
                    if (ctx->state.callback) {
                        ctx->state.callback(ctx->state.cookie, &ctx->state.task, UNI_NET_FTP_CLIENT_CALLBACK_RECV, buf, cnt);
                    }
                    ctx->state.task.progress += cnt;
                    FreeRTOS_ReleaseTCPPayloadBuffer(ctx->state.socket_data, buf, cnt);
                }
            } while (cnt > 0 && ctx->state.task.progress < ctx->state.task.progress_total);
        }
        default: {
            break;
        }
    }
}

static void _uni_net_ftp_client_work_state_startup(uni_net_ftp_client_context_t *ctx) {
    if (ctx->state.task.state == UNI_NET_FTP_CLIENT_TASK_STATE_FINISHED) {
        ctx->state.task.progress++;
        ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_NOT_STARTED;
    }

    if (ctx->state.task.progress >= _uni_net_ftp_client_startup_cmds_cnt) {
        ctx->state.task.type = UNI_NET_FTP_CLIENT_TASK_TYPE_IDLE;
        ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_FINISHED;
    }

    if (ctx->state.task.state == UNI_NET_FTP_CLIENT_TASK_STATE_NOT_STARTED) {
        ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_IN_PROGRESS;
        _uni_net_ftp_client_send_cmd(ctx, _uni_net_ftp_client_startup_cmds[ctx->state.task.progress]);
    }
}

static void _uni_net_ftp_client_work_state_data(uni_net_ftp_client_context_t *ctx) {
    switch (ctx->state.task.state) {
        case UNI_NET_FTP_CLIENT_TASK_STATE_NOT_STARTED: {
            ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_SWITCH_TO_PASV;
            _uni_net_ftp_client_switch_to_passive(ctx);
            break;
        }
        case UNI_NET_FTP_CLIENT_TASK_STATE_STARTED: {
            if (ctx->state.task.type == UNI_NET_FTP_CLIENT_TASK_TYPE_RETR) {
                ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_REQUESTED;
                _uni_net_ftp_client_retr_file(ctx, ctx->state.task.name);
            } else if (ctx->state.task.type == UNI_NET_FTP_CLIENT_TASK_TYPE_LIST){
                ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_REQUESTED;
                _uni_net_ftp_client_list_files(ctx);
            }
            else {
                _uni_net_ftp_client_set_to_idle(ctx);
            }
            break;
        }
        case UNI_NET_FTP_CLIENT_TASK_STATE_IN_PROGRESS: {
            if (ctx->state.task.progress >= ctx->state.task.progress_total) {
                if(ctx->state.callback) {
                    ctx->state.callback(ctx->state.cookie, &ctx->state.task, UNI_NET_FTP_CLIENT_CALLBACK_RECV_FINISHED, NULL, 0);
                }
                _uni_net_ftp_client_set_to_idle(ctx);
            }
            break;
        }
        case UNI_NET_FTP_CLIENT_TASK_STATE_FAILED: {
            if(ctx->state.callback) {
                ctx->state.callback(ctx->state.cookie, &ctx->state.task, UNI_NET_FTP_CLIENT_CALLBACK_RECV_FAILED, NULL, 0);
            }
            _uni_net_ftp_client_set_to_idle(ctx);
            break;
        }
        default: {
            break;
        }
    }
}

static void _uni_net_ftp_client_work_state(uni_net_ftp_client_context_t *ctx) {
    switch (ctx->state.task.type) {
        case UNI_NET_FTP_CLIENT_TASK_TYPE_STARTUP: {
            _uni_net_ftp_client_work_state_startup(ctx);
            break;
        }
        case UNI_NET_FTP_CLIENT_TASK_TYPE_RETR:
        case UNI_NET_FTP_CLIENT_TASK_TYPE_LIST: {
            _uni_net_ftp_client_work_state_data(ctx);
            break;
        }
        default:
            break;
    };
}


static void _uni_net_ftp_client_thread(void *pv) {
    uni_net_ftp_client_context_t *ctx = pv;

    // cleanup
    _uni_net_ftp_client_disconnect(ctx, false);

    // prepare
    ctx->state.task.type = UNI_NET_FTP_CLIENT_TASK_TYPE_STARTUP;
    ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_IN_PROGRESS;
    ctx->state.task.progress = 0;

    while (ctx->state.task.type != UNI_NET_FTP_CLIENT_TASK_TYPE_TERMINATION) {
        // connect command socket
        if (ctx->state.socket_cmd == NULL) {
            if (!_uni_net_ftp_client_connect_socket(ctx, &ctx->state.socket_cmd, ctx->config.server_port)) {
                break;
            }
        }

        // Process incoming msgs
        BaseType_t select_result = FreeRTOS_select(ctx->state.socket_set, pdMS_TO_TICKS(UNI_NET_FTP_CLIENT_WAIT_MS));
        if (select_result != 0) {
            // process incoming command message
            if (FreeRTOS_FD_ISSET(ctx->state.socket_cmd, ctx->state.socket_set)) {
                _uni_net_ftp_client_work_cmd(ctx);
            }

            // process incoming data message
            if (ctx->state.socket_data != NULL) {
                if (FreeRTOS_FD_ISSET(ctx->state.socket_data, ctx->state.socket_set)) {
                    _uni_net_ftp_client_work_data(ctx);
                }
            }
        } else if (!FreeRTOS_IsNetworkUp() || FreeRTOS_issocketconnected(ctx->state.socket_cmd) != pdTRUE) {
            break;
        }

        // Process startup sequence
        _uni_net_ftp_client_work_state(ctx);
    }

    // disconnect
    _uni_net_ftp_client_disconnect(ctx, true);

    // terminate thread
    ctx->state.thread = NULL;
    vTaskDelete(NULL);
}


//
// Functions/Public
//

bool uni_net_ftp_client_connect(uni_net_ftp_client_context_t *ctx, uint32_t addr, uint16_t port) {
    UNI_NET_FTP_CLIENT_DBG_0("uni_net_ftp_client_connect");

    bool result = false;

    if (ctx != NULL) {
        if (addr != 0U) {
            ctx->config.server_addr = addr;
        }
        if (port != 0U) {
            ctx->config.server_port = port;
        }

        result = xTaskCreate(_uni_net_ftp_client_thread, "UNI_NET_FTP_CLIENT", configMINIMAL_STACK_SIZE * 4, ctx, 1,
                             &ctx->state.thread) == pdTRUE;
    }
    return result;
}


bool uni_net_ftp_client_disconnect(uni_net_ftp_client_context_t *ctx) {
    bool result = false;
    if (ctx != NULL) {
        ctx->state.task.type = UNI_NET_FTP_CLIENT_TASK_TYPE_TERMINATION;
    }
    return result;
}


bool uni_net_ftp_client_is_connected(const uni_net_ftp_client_context_t *ctx) {
    return ctx != NULL && (ctx->state.socket_cmd != NULL || ctx->state.thread != NULL);
}


bool uni_net_ftp_client_is_idle(const uni_net_ftp_client_context_t *ctx) {
    return uni_net_ftp_client_is_connected(ctx) && ctx->state.task.type == UNI_NET_FTP_CLIENT_TASK_TYPE_IDLE;
}

uint32_t uni_net_ftp_client_get_current_addr(const uni_net_ftp_client_context_t *ctx) {
    uint32_t result = UINT32_MAX;
    if(uni_net_ftp_client_is_connected(ctx)) {
        result = ctx->config.server_addr;
    }
    return result;
}

bool uni_net_ftp_client_set_callback(uni_net_ftp_client_context_t *ctx, uni_net_ftp_client_callback_t callback, void *cookie) {
    bool result = false;
    if (ctx != NULL) {
        ctx->state.callback = callback;
        ctx->state.cookie = cookie;
        result = true;
    }
    return result;
}


bool uni_net_ftp_client_download(uni_net_ftp_client_context_t *ctx, const char *filename, void *cookie, size_t size) {
    bool result = false;

    if (uni_net_ftp_client_is_idle(ctx)) {
        ctx->state.task.type = UNI_NET_FTP_CLIENT_TASK_TYPE_RETR;
        ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_NOT_STARTED;
        ctx->state.task.progress = 0;
        ctx->state.task.progress_total = size;
        ctx->state.task.cookie_file = cookie;
        strncpy(ctx->state.task.name, filename, sizeof(ctx->state.task.name) - 1U);
        result = true;
    }

    return result;
}

bool uni_net_ftp_client_list(uni_net_ftp_client_context_t *ctx) {
    bool result = false;

    if (uni_net_ftp_client_is_idle(ctx)) {
        ctx->state.task.type = UNI_NET_FTP_CLIENT_TASK_TYPE_LIST;
        ctx->state.task.state = UNI_NET_FTP_CLIENT_TASK_STATE_NOT_STARTED;
        ctx->state.task.progress = 0;
        ctx->state.task.progress_total = 0;
        ctx->state.task.name[0] = '\0';
        result = true;
    }

    return result;
}
