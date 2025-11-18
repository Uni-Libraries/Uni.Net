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


//
// Defaults and configuration
//

/**
 * Default UDP receive timeout in milliseconds used if not specified in config.
 */
#ifndef UNI_NET_UDP_DEFAULT_RX_TIMEOUT_MS
#define UNI_NET_UDP_DEFAULT_RX_TIMEOUT_MS   (1000U)
#endif

/**
 * Default UDP send timeout in milliseconds used if not specified in config.
 */
#ifndef UNI_NET_UDP_DEFAULT_TX_TIMEOUT_MS
#define UNI_NET_UDP_DEFAULT_TX_TIMEOUT_MS   (1000U)
#endif

/**
 * Return value that indicates the operation timed out or would block, matching FreeRTOS+TCP semantics.
 * Functions in this module will either return:
 *  - >0 : number of bytes processed
 *  -  0 : timeout / would-block (no data sent/received within timeout)
 *  - <0 : negative FreeRTOS+TCP-style error code
 *
 * Note: This constant is provided for convenience checks in application code. Functions here do not
 * map negative values to custom enums but pass through FreeRTOS+TCP negatives as-is.
 */
#ifndef UNI_NET_UDP_RET_TIMEOUT
#define UNI_NET_UDP_RET_TIMEOUT (0)
#endif


//
// Endpoint helpers
//

/**
 * IPv4 endpoint container. Members are stored in network byte order.
 */
typedef struct {
    uint32_t addr;  /* IPv4 address in network byte order (as returned by FreeRTOS_inet_addr or _quick) */
    uint16_t port;  /* UDP port in network byte order (use FreeRTOS_htons on host order) */
} uni_net_udp_endpoint_t;

/**
 * Convert 4 octets to IPv4 address (network byte order).
 */
static inline uint32_t uni_net_udp_ipv4_from_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return FreeRTOS_inet_addr_quick(a, b, c, d);
}

/**
 * Parse dotted IPv4 string to address (network byte order). If parsing fails and DNS is
 * enabled in the project, attempts FreeRTOS_gethostbyname. Returns 0 on failure.
 */
static inline uint32_t uni_net_udp_ipv4_from_string(const char* dotted_or_host) {
    uint32_t ip = 0U;
    if (dotted_or_host != NULL) {
        ip = FreeRTOS_inet_addr(dotted_or_host);
#if (ipconfigUSE_DNS == 1)
        if (ip == 0U) {
            ip = FreeRTOS_gethostbyname(dotted_or_host);
        }
#endif
    }
    return ip;
}

/**
 * Initialize an endpoint from IPv4 address (network order) and port (host order).
 */
static inline void uni_net_udp_endpoint_init(uni_net_udp_endpoint_t* ep, uint32_t addr_nbo, uint16_t port_hbo) {
    if (ep != NULL) {
        ep->addr = addr_nbo;
        ep->port = FreeRTOS_htons(port_hbo);
    }
}

/**
 * Initialize an endpoint from four octets and a port (host order).
 */
static inline void uni_net_udp_endpoint_init_quick(uni_net_udp_endpoint_t* ep, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port_hbo) {
    if (ep != NULL) {
        ep->addr = FreeRTOS_inet_addr_quick(a, b, c, d);
        ep->port = FreeRTOS_htons(port_hbo);
    }
}


//
// Client context
//

typedef struct {
    uint32_t rx_timeout_ms;     /* Receive timeout in milliseconds */
    uint32_t tx_timeout_ms;     /* Send timeout in milliseconds */
} uni_net_udp_client_config_t;

typedef struct {
    bool            initialized;
    Socket_t        socket;
    bool            connected;      /* default remote configured */
    bool            stack_connected; /* FreeRTOS_connect succeeded */
    struct freertos_sockaddr default_remote; /* valid when connected == true */
    SemaphoreHandle_t lock; /* serialize config and I/O */
} uni_net_udp_client_state_t;

typedef struct {
    uni_net_udp_client_config_t config;
    uni_net_udp_client_state_t  state;
} uni_net_udp_client_context_t;


//
// Public API
//

/**
 * Initialize a UDP client context and create a UDP socket.
 *
 * The function creates a datagram socket (AF_INET/SOCK_DGRAM/IPPROTO_UDP), applies send/recv timeouts
 * expressed in TickType_t using pdMS_TO_TICKS, and optionally enables broadcast. If the system provides
 * an API to detect network readiness (FreeRTOS_IsNetworkUp), the socket is created regardless; however,
 * DNS resolution attempts will naturally fail until the network is up.
 *
 * Parameters:
 *  - ctx: Context to initialize (must not be NULL).
 *  - cfg: Optional configuration. If NULL, defaults are used.
 *
 * Returns:
 *  - true on success; false on invalid argument or socket creation failure.
 *
 * Usage:
 *  See the example snippet at the end of this header.
 */
bool uni_net_udp_client_init(uni_net_udp_client_context_t* ctx, const uni_net_udp_client_config_t* cfg);

/**
 * Check if client was initialized.
 */
bool uni_net_udp_client_is_inited(const uni_net_udp_client_context_t* ctx);

/**
 * Deinitialize client and close socket. Safe to call multiple times.
 */
bool uni_net_udp_client_deinit(uni_net_udp_client_context_t* ctx);

/**
 * Connect the UDP socket to a default remote endpoint. This sets the default destination for FreeRTOS_send,
 * while still allowing uni_net_udp_client_sendto for unconnected sends.
 *
 * Returns:
 *  - true when a default remote endpoint is stored (FreeRTOS_connect is attempted; if not supported it will still succeed here).
 *    Returns false only on invalid arguments.
 */
bool uni_net_udp_client_connect(uni_net_udp_client_context_t* ctx, const uni_net_udp_endpoint_t* remote);

/**
 * Whether the client socket has a default remote endpoint set.
 */
bool uni_net_udp_client_is_connected(const uni_net_udp_client_context_t* ctx);

/**
 * Send a single UDP datagram using the connected remote endpoint (set via connect).
 * Guarantees single-datagram semantics. On timeout/would-block returns 0.
 *
 * Returns:
 *  - > 0 : bytes sent (must equal len for success)
 *  -   0  : timeout / would-block (UNI_NET_UDP_RET_TIMEOUT)
 *  - < 0 : negative FreeRTOS+TCP-style error
 */
int32_t uni_net_udp_client_send(uni_net_udp_client_context_t* ctx, const uint8_t* buf, size_t len);

/**
 * Send a single UDP datagram to the specified endpoint. Can be used regardless of connection state.
 * On timeout/would-block returns 0.
 *
 * Returns:
 *  - > 0 : bytes sent (must equal len for success)
 *  -   0  : timeout / would-block (UNI_NET_UDP_RET_TIMEOUT)
 *  - < 0 : negative FreeRTOS+TCP-style error
 */
int32_t uni_net_udp_client_sendto(uni_net_udp_client_context_t* ctx, const uint8_t* buf, size_t len, const uni_net_udp_endpoint_t* remote);

/**
 * Receive a single UDP datagram. If out_src is provided, it will be filled with the source endpoint.
 * On timeout/would-block returns 0. If buf_size is smaller than the datagram, the excess is discarded by
 * the stack as per FreeRTOS+TCP behavior; the function returns the number of bytes actually copied into buf.
 *
 * Returns:
 *  - >= 0 : number of bytes copied into buf (0 indicates timeout)
 *  -  < 0 : negative FreeRTOS+TCP-style error
 */
int32_t uni_net_udp_client_recvfrom(uni_net_udp_client_context_t* ctx, uint8_t* buf, size_t buf_size, uni_net_udp_endpoint_t* out_src);

/**
 * Configure send/receive timeouts in milliseconds. Values are applied using FreeRTOS_setsockopt with
 * pdMS_TO_TICKS conversion. Thread-safe.
 */
bool uni_net_udp_client_set_timeouts(uni_net_udp_client_context_t* ctx, uint32_t rx_timeout_ms, uint32_t tx_timeout_ms);

/**
 * Query current configured timeouts (as stored in the context config). Thread-safe.
 */
bool uni_net_udp_client_get_timeouts(const uni_net_udp_client_context_t* ctx, uint32_t* rx_timeout_ms, uint32_t* tx_timeout_ms);

/**
 * Enable or disable broadcast intent. FreeRTOS+TCP allows broadcast to 255.255.255.255 without a socket option; this records preference.
 */
bool uni_net_udp_client_set_broadcast(uni_net_udp_client_context_t* ctx, bool enable);

/**
 * Get current broadcast setting from the context config.
 */
bool uni_net_udp_client_get_broadcast(const uni_net_udp_client_context_t* ctx, bool* enable);

/**
 * Best-effort control to disable UDP checksum if supported by the stack build. If the option macro is
 * not available, the call is a no-op and returns true to keep application flow simple.
 */
bool uni_net_udp_client_set_checksum_disable(uni_net_udp_client_context_t* ctx, bool disable);


//
// Usage example (copy into an application task)
//
// Note: Make sure FreeRTOS+TCP is initialized and the network is up.
//

/*
#include <uni_net.h>

void app_udp_client_task(void* arg) {
    uni_net_udp_client_context_t client = {0};

    uni_net_udp_client_config_t cfg = {
        .rx_timeout_ms = UNI_NET_UDP_DEFAULT_RX_TIMEOUT_MS,
        .tx_timeout_ms = UNI_NET_UDP_DEFAULT_TX_TIMEOUT_MS,
        .broadcast_enable = false,
        .checksum_disable = false
    };

    if (!uni_net_udp_client_init(&client, &cfg)) {
        FreeRTOS_printf(("UDP client init failed\n"));
        vTaskDelete(NULL);
    }

    // Option A: connect then send with default remote
    uni_net_udp_endpoint_t remote = {0};
    uni_net_udp_endpoint_init_quick(&remote, 192,168,1,100, 12345);
    if (uni_net_udp_client_connect(&client, &remote)) {
        const char msg[] = "hello";
        int32_t rc = uni_net_udp_client_send(&client, (const uint8_t*)msg, sizeof(msg));
        if (rc > 0) {
            FreeRTOS_printf(("Sent %d bytes\n", (int)rc));
        }
    }

    // Receive any datagram
    uint8_t rxbuf[512];
    uni_net_udp_endpoint_t from = {0};
    int32_t got = uni_net_udp_client_recvfrom(&client, rxbuf, sizeof(rxbuf), &from);
    if (got > 0) {
        FreeRTOS_printf(("Got %d bytes from %x:%u\n", (int)got, (unsigned)from.addr, (unsigned)FreeRTOS_ntohs(from.port)));
    } else if (got == UNI_NET_UDP_RET_TIMEOUT) {
        FreeRTOS_printf(("Receive timeout\n"));
    }

    (void)uni_net_udp_client_deinit(&client);
    vTaskDelete(NULL);
}
*/

#if defined(__cplusplus)
}
#endif
