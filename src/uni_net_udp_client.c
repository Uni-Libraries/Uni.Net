/*
 * SPDX-License-Identifier: MIT
 * UDP client implementation for Uni.NET using FreeRTOS+TCP
 */

// Includes

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <FreeRTOS_IP.h>
#include <FreeRTOS_Sockets.h>

#include "uni_net_udp_client.h"

// Private helpers
static inline void _lock(uni_net_udp_client_context_t* ctx) {
    if (ctx != NULL && ctx->state.lock != NULL) {
        (void)xSemaphoreTake(ctx->state.lock, portMAX_DELAY);
    }
}
static inline void _unlock(uni_net_udp_client_context_t* ctx) {
    if (ctx != NULL && ctx->state.lock != NULL) {
        (void)xSemaphoreGive(ctx->state.lock);
    }
}

static BaseType_t _apply_timeouts(Socket_t s, uint32_t rx_timeout_ms, uint32_t tx_timeout_ms) {
    TickType_t rx_ticks = pdMS_TO_TICKS(rx_timeout_ms);
    TickType_t tx_ticks = pdMS_TO_TICKS(tx_timeout_ms);
    BaseType_t r1 = FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &rx_ticks, sizeof(rx_ticks));
    BaseType_t r2 = FreeRTOS_setsockopt(s, 0, FREERTOS_SO_SNDTIMEO, &tx_ticks, sizeof(tx_ticks));
    return (r1 == 0 && r2 == 0) ? 0 : -1;
}

static BaseType_t _apply_broadcast(Socket_t s, bool enable) {
    (void)s;
    (void)enable;
    /* FreeRTOS+TCP has no SO_BROADCAST option; sending to 255.255.255.255 is sufficient. */
    return 0;
}

static BaseType_t _apply_checksum_disable(Socket_t s, bool disable) {
    /* FreeRTOS+TCP option to enable(1)/disable(0) UDP checksum on this socket (outgoing). */
    BaseType_t on = disable ? 0 : 1;
    return FreeRTOS_setsockopt(s, 0, FREERTOS_SO_UDPCKSUM_OUT, &on, sizeof(on));
}

static inline int32_t _map_timeout_to_wouldblock(int32_t rv) {
    if (rv == -pdFREERTOS_ERRNO_EWOULDBLOCK || rv == -pdFREERTOS_ERRNO_ETIMEDOUT) {
        return UNI_NET_UDP_RET_TIMEOUT;
    }
    return rv;
}

static inline bool _socket_valid(Socket_t s) {
    return (s != NULL) && (s != FREERTOS_INVALID_SOCKET);
}

// Public API
bool uni_net_udp_client_init(uni_net_udp_client_context_t* ctx, const uni_net_udp_client_config_t* cfg) {
    if (ctx == NULL) {
        return false;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->config.rx_timeout_ms    = (cfg != NULL) ? cfg->rx_timeout_ms    : UNI_NET_UDP_DEFAULT_RX_TIMEOUT_MS;
    ctx->config.tx_timeout_ms    = (cfg != NULL) ? cfg->tx_timeout_ms    : UNI_NET_UDP_DEFAULT_TX_TIMEOUT_MS;
    ctx->config.broadcast_enable = (cfg != NULL) ? cfg->broadcast_enable : false;
    ctx->config.checksum_disable = (cfg != NULL) ? cfg->checksum_disable : false;

    ctx->state.lock = xSemaphoreCreateMutex();
    if (ctx->state.lock == NULL) {
        return false;
    }

    ctx->state.socket = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP);
    if (!_socket_valid(ctx->state.socket)) {
        vSemaphoreDelete(ctx->state.lock);
        memset(ctx, 0, sizeof(*ctx));
        return false;
    }

    if (_apply_timeouts(ctx->state.socket, ctx->config.rx_timeout_ms, ctx->config.tx_timeout_ms) != 0) {
        FreeRTOS_closesocket(ctx->state.socket);
        vSemaphoreDelete(ctx->state.lock);
        memset(ctx, 0, sizeof(*ctx));
        return false;
    }

    (void)_apply_broadcast(ctx->state.socket, ctx->config.broadcast_enable);
    (void)_apply_checksum_disable(ctx->state.socket, ctx->config.checksum_disable);

    ctx->state.initialized = true;
    return true;
}

bool uni_net_udp_client_is_inited(const uni_net_udp_client_context_t* ctx) {
    return (ctx != NULL) && (ctx->state.initialized);
}

bool uni_net_udp_client_deinit(uni_net_udp_client_context_t* ctx) {
    if (ctx == NULL) {
        return false;
    }

    _lock(ctx);
    if (_socket_valid(ctx->state.socket)) {
        FreeRTOS_closesocket(ctx->state.socket);
        ctx->state.socket = FREERTOS_INVALID_SOCKET;
    }
    ctx->state.connected = false;
    _unlock(ctx);

    if (ctx->state.lock != NULL) {
        vSemaphoreDelete(ctx->state.lock);
    }

    memset(ctx, 0, sizeof(*ctx));
    return true;
}

bool uni_net_udp_client_connect(uni_net_udp_client_context_t* ctx, const uni_net_udp_endpoint_t* remote) {
    if (ctx == NULL || remote == NULL || !uni_net_udp_client_is_inited(ctx)) {
        return false;
    }

    struct freertos_sockaddr addr = { 0 };
    addr.sin_family = FREERTOS_AF_INET;
    addr.sin_port = remote->port;
#if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
    addr.sin_addr = remote->addr;
#else
    addr.sin_address.ulIP_IPv4 = remote->addr;
#endif

    _lock(ctx);
    BaseType_t rc = FreeRTOS_connect(ctx->state.socket, &addr, sizeof(addr));
    if (rc == 0) {
        ctx->state.connected = true;
        ctx->state.default_remote = addr;
    }
    _unlock(ctx);

    return (rc == 0);
}

bool uni_net_udp_client_is_connected(const uni_net_udp_client_context_t* ctx) {
    return (ctx != NULL) && (ctx->state.connected);
}

int32_t uni_net_udp_client_send(uni_net_udp_client_context_t* ctx, const uint8_t* buf, size_t len) {
    if (ctx == NULL || buf == NULL || len == 0 || !uni_net_udp_client_is_inited(ctx)) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }
    if (!ctx->state.connected) {
        return -pdFREERTOS_ERRNO_ENOTCONN;
    }

    _lock(ctx);
    int32_t rv = FreeRTOS_send(ctx->state.socket, buf, (int32_t)len, 0);
    _unlock(ctx);

    rv = _map_timeout_to_wouldblock(rv);
    if (rv > 0 && (size_t)rv != len) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }
    return rv;
}

int32_t uni_net_udp_client_sendto(uni_net_udp_client_context_t* ctx, const uint8_t* buf, size_t len, const uni_net_udp_endpoint_t* remote) {
    if (ctx == NULL || buf == NULL || len == 0 || remote == NULL || !uni_net_udp_client_is_inited(ctx)) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }

    struct freertos_sockaddr to = { 0 };
    to.sin_family = FREERTOS_AF_INET;
    to.sin_port = remote->port;
#if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
    to.sin_addr = remote->addr;
#else
    to.sin_address.ulIP_IPv4 = remote->addr;
#endif

    _lock(ctx);
    int32_t rv = FreeRTOS_sendto(ctx->state.socket, buf, (int32_t)len, 0, &to, sizeof(to));
    _unlock(ctx);

    rv = _map_timeout_to_wouldblock(rv);
    if (rv > 0 && (size_t)rv != len) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }
    return rv;
}

int32_t uni_net_udp_client_recvfrom(uni_net_udp_client_context_t* ctx, uint8_t* buf, size_t buf_size, uni_net_udp_endpoint_t* out_src) {
    if (ctx == NULL || buf == NULL || buf_size == 0 || !uni_net_udp_client_is_inited(ctx)) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }

    struct freertos_sockaddr from = {0};
    uint32_t from_len = sizeof(from);

    _lock(ctx);
    int32_t rv = FreeRTOS_recvfrom(ctx->state.socket, buf, (int32_t)buf_size, 0, &from, &from_len);
    _unlock(ctx);

    rv = _map_timeout_to_wouldblock(rv);
    if (rv >= 0 && out_src != NULL && from_len >= sizeof(from)) {
#if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
        out_src->addr = from.sin_addr;
#else
        out_src->addr = from.sin_address.ulIP_IPv4;
#endif
        out_src->port = from.sin_port;
    }
    return rv;
}

bool uni_net_udp_client_set_timeouts(uni_net_udp_client_context_t* ctx, uint32_t rx_timeout_ms, uint32_t tx_timeout_ms) {
    if (ctx == NULL || !uni_net_udp_client_is_inited(ctx)) {
        return false;
    }
    _lock(ctx);
    BaseType_t rc = _apply_timeouts(ctx->state.socket, rx_timeout_ms, tx_timeout_ms);
    if (rc == 0) {
        ctx->config.rx_timeout_ms = rx_timeout_ms;
        ctx->config.tx_timeout_ms = tx_timeout_ms;
    }
    _unlock(ctx);
    return (rc == 0);
}

bool uni_net_udp_client_get_timeouts(const uni_net_udp_client_context_t* ctx, uint32_t* rx_timeout_ms, uint32_t* tx_timeout_ms) {
    if (ctx == NULL) {
        return false;
    }
    if (rx_timeout_ms != NULL) {
        *rx_timeout_ms = ctx->config.rx_timeout_ms;
    }
    if (tx_timeout_ms != NULL) {
        *tx_timeout_ms = ctx->config.tx_timeout_ms;
    }
    return true;
}

bool uni_net_udp_client_set_broadcast(uni_net_udp_client_context_t* ctx, bool enable) {
    if (ctx == NULL || !uni_net_udp_client_is_inited(ctx)) {
        return false;
    }
    _lock(ctx);
    BaseType_t rc = _apply_broadcast(ctx->state.socket, enable);
    if (rc == 0) {
        ctx->config.broadcast_enable = enable;
    }
    _unlock(ctx);
    return (rc == 0);
}

bool uni_net_udp_client_get_broadcast(const uni_net_udp_client_context_t* ctx, bool* enable) {
    if (ctx == NULL || enable == NULL) {
        return false;
    }
    *enable = ctx->config.broadcast_enable;
    return true;
}

bool uni_net_udp_client_set_checksum_disable(uni_net_udp_client_context_t* ctx, bool disable) {
    if (ctx == NULL || !uni_net_udp_client_is_inited(ctx)) {
        return false;
    }
    _lock(ctx);
    BaseType_t rc = _apply_checksum_disable(ctx->state.socket, disable);
    if (rc == 0) {
        ctx->config.checksum_disable = disable;
    }
    _unlock(ctx);
    return (rc == 0);
}
