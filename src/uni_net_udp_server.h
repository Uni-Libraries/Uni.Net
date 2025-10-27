#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

//
// Includes
//

// stdlib
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// FreeRTOS+TCP
#include <FreeRTOS_IP.h>
#include <FreeRTOS_Sockets.h>
#include <semphr.h>

// Uni.NET UDP client (for endpoint type and helpers)
#include "uni_net_udp_client.h"

//
// Defaults and configuration
//

#ifndef UNI_NET_UDP_SERVER_DEFAULT_RX_TIMEOUT_MS
#define UNI_NET_UDP_SERVER_DEFAULT_RX_TIMEOUT_MS   (1000U)
#endif

#ifndef UNI_NET_UDP_SERVER_DEFAULT_TX_TIMEOUT_MS
#define UNI_NET_UDP_SERVER_DEFAULT_TX_TIMEOUT_MS   (1000U)
#endif

#ifndef UNI_NET_UDP_SERVER_TASK_STACK_WORDS
#define UNI_NET_UDP_SERVER_TASK_STACK_WORDS        (configMINIMAL_STACK_SIZE * 2U)
#endif

#ifndef UNI_NET_UDP_SERVER_TASK_PRIORITY
#define UNI_NET_UDP_SERVER_TASK_PRIORITY           (2U)
#endif

#ifndef UNI_NET_UDP_SERVER_IFACE_TIME_MS
#define UNI_NET_UDP_SERVER_IFACE_TIME_MS           (250U)
#endif

#ifndef UNI_NET_UDP_SERVER_RX_BUF_SIZE
#define UNI_NET_UDP_SERVER_RX_BUF_SIZE             (1536U)
#endif

/**
 * Return value indicating timeout/would-block for UDP operations.
 * Functions here return:
 *  - > 0 : number of bytes processed
 *  -   0 : timeout / would-block
 *  - < 0 : negative FreeRTOS+TCP-style error code (e.g., -pdFREERTOS_ERRNO_EINVAL)
 */
#ifndef UNI_NET_UDP_RET_TIMEOUT
#define UNI_NET_UDP_RET_TIMEOUT (0)
#endif


//
// Typedefs
//

/**
 * Receive callback signature for event-driven server mode.
 * Called from the server's internal task upon receiving a datagram.
 *
 * Parameters:
 *  - user: user-provided context pointer from configuration.
 *  - payload: received data buffer (valid for the duration of the callback).
 *  - length: number of bytes in payload.
 *  - from: source endpoint of the datagram.
 */
typedef void (*uni_net_udp_server_recv_cb)(void* user, const uint8_t* payload, size_t length, const uni_net_udp_endpoint_t* from);


//
// Configuration, State, Context
//

typedef struct {
    // Bind configuration
    uint16_t bind_port_hbo;     /* Local UDP port to bind (host byte order). */
    uint32_t bind_addr_nbo;     /* Local IPv4 address (network byte order), use 0 or FREERTOS_INADDR_ANY for any. */

    // Socket options
    uint32_t rx_timeout_ms;     /* Receive timeout in milliseconds. */
    uint32_t tx_timeout_ms;     /* Send timeout in milliseconds. */
    bool     allow_broadcast;   /* If true, application intents to use broadcast; FreeRTOS+TCP doesn't need SO_BROADCAST. */
    bool     checksum_disable;  /* Best-effort: disable UDP checksum if supported; otherwise a no-op. */

    // Event-driven mode
    bool                       use_task;         /* If true, start a dedicated task to receive datagrams. */
    uni_net_udp_server_recv_cb on_receive;       /* Callback to invoke on incoming datagrams in task mode. */
    void*                      user;             /* User context pointer passed to callback. */
    UBaseType_t                task_priority;    /* Task priority for the server task. */
    uint32_t                   task_stack_words; /* Task stack size in words. */

} uni_net_udp_server_config_t;

typedef struct {
    bool                 initialized;
    Socket_t             socket;
    SemaphoreHandle_t    lock;
    TaskHandle_t         task;
    volatile bool        stop_requested;
} uni_net_udp_server_state_t;

typedef struct {
    uni_net_udp_server_config_t config;
    uni_net_udp_server_state_t  state;
} uni_net_udp_server_context_t;


//
// Public API
//

/**
 * Start a UDP server: create and bind a UDP socket, apply options, and optionally spawn a task.
 *
 * Behavior:
 *  - Waits for network up using FreeRTOS_IsNetworkUp() before binding.
 *  - Creates a datagram socket (AF_INET/SOCK_DGRAM/IPPROTO_UDP).
 *  - Applies RCVTIMEO/SNDTIMEO using pdMS_TO_TICKS.
 *  - Binding to the requested local port/address (IPv4).
 *  - If config.use_task is true and on_receive is non-NULL, creates a FreeRTOS task that blocks in
 *    FreeRTOS_recvfrom and invokes the callback for each datagram received.
 *
 * Parameters:
 *  - ctx: Server context to initialize (must not be NULL).
 *  - cfg: Optional configuration. If NULL, sensible defaults are used:
 *         any address, port 0 (ephemeral), default timeouts, no broadcast, checksum enabled,
 *         no task mode.
 *
 * Returns:
 *  - true on success; false on invalid arguments or socket/task creation failure.
 */
bool uni_net_udp_server_start(uni_net_udp_server_context_t* ctx, const uni_net_udp_server_config_t* cfg);

/**
 * Check if server was started and initialized.
 */
bool uni_net_udp_server_is_inited(const uni_net_udp_server_context_t* ctx);

/**
 * Stop the UDP server: request task termination if running, unblock any pending recvfrom,
 * wait for the task to exit, then close the socket and release synchronization primitives.
 *
 * Returns:
 *  - true on success; false on invalid argument or not initialized.
 */
bool uni_net_udp_server_stop(uni_net_udp_server_context_t* ctx);

/**
 * Synchronous receive API: receive one UDP datagram with a specified timeout override.
 * This API must not be used concurrently with the task-driven mode (i.e., when a server task is running).
 *
 * Parameters:
 *  - ctx: Server context (must be started and not running a receive task).
 *  - buf: Destination buffer for payload (must not be NULL).
 *  - buf_size: Size of destination buffer in bytes (> 0).
 *  - out_from: Optional; if non-NULL, filled with source endpoint info.
 *  - timeout_ms: Timeout in milliseconds for this call; use 0 for non-blocking,
 *                or e.g. UNI_NET_UDP_SERVER_DEFAULT_RX_TIMEOUT_MS for blocking.
 *
 * Returns:
 *  - >= 0 : number of bytes copied into buf (0 indicates timeout/would-block)
 *  -  < 0 : negative FreeRTOS+TCP-style error code
 *
 * Note:
 *  - If the incoming datagram exceeds buf_size, excess bytes are discarded by the stack.
 */
int32_t uni_net_udp_server_recvfrom(uni_net_udp_server_context_t* ctx, uint8_t* buf, size_t buf_size, uni_net_udp_endpoint_t* out_from, uint32_t timeout_ms);

/**
 * Send one UDP datagram to a specific endpoint. Can be called from the server's callback or other tasks.
 * Guarantees single-datagram semantics; partial sends are treated as error.
 *
 * Returns:
 *  - > 0 : bytes sent (must equal len on success)
 *  -   0 : timeout / would-block (UNI_NET_UDP_RET_TIMEOUT)
 *  - < 0 : negative FreeRTOS+TCP-style error
 */
int32_t uni_net_udp_server_sendto(uni_net_udp_server_context_t* ctx, const uint8_t* buf, size_t len, const uni_net_udp_endpoint_t* to);

/**
 * Configure send/receive timeouts (in milliseconds) for the server socket at runtime.
 * Thread-safe w.r.t. sendto; the server's internal task continues using the updated timeouts.
 */
bool uni_net_udp_server_set_timeouts(uni_net_udp_server_context_t* ctx, uint32_t rx_timeout_ms, uint32_t tx_timeout_ms);

/**
 * Query current configured timeouts (values stored in the context config).
 */
bool uni_net_udp_server_get_timeouts(const uni_net_udp_server_context_t* ctx, uint32_t* rx_timeout_ms, uint32_t* tx_timeout_ms);

/**
 * Enable/disable broadcast intent. FreeRTOS+TCP allows sending to 255.255.255.255 without SO_BROADCAST.
 * This function records the preference and returns true; it is a no-op on the socket.
 */
bool uni_net_udp_server_set_broadcast(uni_net_udp_server_context_t* ctx, bool enable);

/**
 * Get broadcast intent flag from the server configuration.
 */
bool uni_net_udp_server_get_broadcast(const uni_net_udp_server_context_t* ctx, bool* enable);

/**
 * Best-effort control to disable UDP checksum if supported by the stack build. If not supported,
 * this call is a no-op and returns true.
 */
bool uni_net_udp_server_set_checksum_disable(uni_net_udp_server_context_t* ctx, bool disable);


//
// Usage example (copy into an application task)
//
// Note: Make sure FreeRTOS+TCP is initialized and the network is up.
//

/*
#include <uni_net.h>

static void my_udp_server_on_receive(void* user, const uint8_t* payload, size_t length, const uni_net_udp_endpoint_t* from) {
    (void)user;
    FreeRTOS_printf(("UDP server got %u bytes from %x:%u\n",
                     (unsigned)length, (unsigned)from->addr, (unsigned)FreeRTOS_ntohs(from->port)));
}

void app_udp_server_task(void* arg) {
    uni_net_udp_server_context_t server = {0};

    uni_net_udp_server_config_t cfg = {
        .bind_port_hbo   = 12345,
        .bind_addr_nbo   = 0, // any
        .rx_timeout_ms   = UNI_NET_UDP_SERVER_DEFAULT_RX_TIMEOUT_MS,
        .tx_timeout_ms   = UNI_NET_UDP_SERVER_DEFAULT_TX_TIMEOUT_MS,
        .allow_broadcast = false,
        .checksum_disable= false,
        .use_task        = true,
        .on_receive      = my_udp_server_on_receive,
        .user            = NULL,
        .task_priority   = UNI_NET_UDP_SERVER_TASK_PRIORITY,
        .task_stack_words= UNI_NET_UDP_SERVER_TASK_STACK_WORDS
    };

    if (!uni_net_udp_server_start(&server, &cfg)) {
        FreeRTOS_printf(("UDP server start failed\n"));
        vTaskDelete(NULL);
    }

    // ... run other application code ...

    (void)uni_net_udp_server_stop(&server);
    vTaskDelete(NULL);
}
*/

#if defined(__cplusplus)
}
#endif
