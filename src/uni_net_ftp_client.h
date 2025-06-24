#pragma once

//
// Includes
//

// stdlib
#include <stdint.h>

// FreeRTOS TCP
#include <FreeRTOS_Sockets.h>


//
// Typedefs
//

#define UNI_NET_FTP_CLIENT_DEFAULT_PORT (21U)

typedef enum {
    UNI_NET_FTP_CLIENT_CALLBACK_DISCONNECT = 0,
    UNI_NET_FTP_CLIENT_CALLBACK_RECV = 1,
    UNI_NET_FTP_CLIENT_CALLBACK_RECV_FINISHED = 2,
    UNI_NET_FTP_CLIENT_CALLBACK_RECV_FAILED = 3,
} uni_net_ftp_client_callback_type_e;


/**
 * FTP Client Task Type
 */
typedef enum {
    /**
     * Client Startup
     */
    UNI_NET_FTP_CLIENT_TASK_TYPE_STARTUP = 0,

    /**
     * Client Idle
     */
    UNI_NET_FTP_CLIENT_TASK_TYPE_IDLE = 1,

    /**
     * Client data retrieval
     */
    UNI_NET_FTP_CLIENT_TASK_TYPE_RETR = 2,

    /**
     * Retrieval of file list
     */
    UNI_NET_FTP_CLIENT_TASK_TYPE_LIST = 3,

    /**
     * Client termination
     */
    UNI_NET_FTP_CLIENT_TASK_TYPE_TERMINATION = 4,
} uni_net_ftp_client_task_type_e;


/**
 * FTP Client Task State
 */
typedef enum {
    UNI_NET_FTP_CLIENT_TASK_STATE_NOT_STARTED = 0, // command only queued from user
    UNI_NET_FTP_CLIENT_TASK_STATE_SWITCH_TO_PASV = 2, // PASV request sent to the server
    UNI_NET_FTP_CLIENT_TASK_STATE_STARTED = 3, // PASV mode accepted
    UNI_NET_FTP_CLIENT_TASK_STATE_REQUESTED = 4, // server requested for a data
    UNI_NET_FTP_CLIENT_TASK_STATE_IN_PROGRESS = 4, // at least one data message received
    UNI_NET_FTP_CLIENT_TASK_STATE_FINISHED = 5, // all data received
    UNI_NET_FTP_CLIENT_TASK_STATE_FAILED = 6, // error occurred
} uni_net_ftp_client_task_state_e;


/**
 * FTP Client Task
 */
typedef struct {
    /**
     * Task Type
     */
    uni_net_ftp_client_task_type_e type;

    /**
     * Task State
     */
    uni_net_ftp_client_task_state_e state;

    /**
     * User data connected to file
     */
    void *cookie_file;

    /**
     * Progress Current
     */
    uint32_t progress;

    /**
     * Progress Total
     */
    uint32_t progress_total;

    /**
     * Char of pathname
     */
    char name[32];
} uni_net_ftp_client_task_t;


/**
 * FTP client callback
 */
typedef void (*uni_net_ftp_client_callback_t)(void *cookie, const uni_net_ftp_client_task_t *task,
                                         uni_net_ftp_client_callback_type_e type, void *data, size_t data_size);


/**
 * FTP Client Configuration
 */
typedef struct {
    /**
     * Server address
     */
    uint32_t server_addr;

    /**
     * Server port
     */
    uint16_t server_port;

    /**
     * Timeout RX
     */
    uint32_t timeout_rx;

    /**
     * Timeout TX
     */
    uint32_t timeout_tx;

    /**
     * Username
     */
    const char *auth_user;

    /**
     * Password
     */
    const char *auth_password;
} uni_net_ftp_client_config_t;


/**
 * FTP Client State
 */
typedef struct {
    /**
     * Thread Handle
     */
    TaskHandle_t thread;

    /**
     * Socket set
     */
    SocketSet_t socket_set;

    /**
     * Command socket
     */
    Socket_t socket_cmd;

    /**
     * Data socket
     */
    Socket_t socket_data;

    /**
     * Client task
     */
    uni_net_ftp_client_task_t task;

    /**
     * Client callback
     */
    uni_net_ftp_client_callback_t callback;

    /**
     * Client callback user data
     */
    void *cookie;
} uni_net_ftp_client_state_t;


/**
 * FTP Client Context
 */
typedef struct {
    /**
     * Configuration
     */
    uni_net_ftp_client_config_t config;

    /**
     * Current state
     */
    uni_net_ftp_client_state_t state;
} uni_net_ftp_client_context_t;


//
// Functions
//

/**
 * Connect to the given FTP server
 * @param ctx FTP client context
 * @param addr FTP server IPv4 address
 * @param port FTP server port
 * @return true on sucessfull connection
 */
bool uni_net_ftp_client_connect(uni_net_ftp_client_context_t *ctx, uint32_t addr, uint16_t port);


/**
 * Disconnect from FTP server
 * @param ctx FTP client context pointer
 * @return true on success
 */
bool uni_net_ftp_client_disconnect(uni_net_ftp_client_context_t *ctx);


/**
 * Check that client is currently connected to the server
 * @param ctx FTP client context pointer
 * @return true in case client is connected
 */
bool uni_net_ftp_client_is_connected(const uni_net_ftp_client_context_t *ctx);


/**
 * Check that client is ready to receive new commanfs
 * @param ctx FTP client context pointer
 * @return true in case client currently doing nothing
 */
bool uni_net_ftp_client_is_idle(const uni_net_ftp_client_context_t *ctx);


uint32_t uni_net_ftp_client_get_current_addr(const uni_net_ftp_client_context_t *ctx);

/**
 * Get current FTP task
 * @param ctx FTP client context pointer
 * @return pointer to the current task
 */
const uni_net_ftp_client_task_t *uni_net_ftp_client_get_task(const uni_net_ftp_client_context_t *ctx);

/**
 * Download file from FTP server
 * @param ctx FTP client context pointer
 * @param filename file name
 * @param cookie user data connected to file
 * @return true in case of successful start
 */
bool uni_net_ftp_client_download(uni_net_ftp_client_context_t *ctx, const char *filename, void *cookie, size_t size);


/**
 *
 * @param ctx
 * @return
 */
bool uni_net_ftp_client_list(uni_net_ftp_client_context_t *ctx);


/**
 * Set FTP client callback
 * @param ctx FTO client context pointer
 * @param callback pointer to the callback function
 * @param cookie user data to be passed to callback
 * @return true on success
 */
bool uni_net_ftp_client_set_callback(uni_net_ftp_client_context_t *ctx, uni_net_ftp_client_callback_t callback, void *cookie);
