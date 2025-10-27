/*
 * SPDX-License-Identifier: MIT
 * UDP server implementation for Uni.NET using FreeRTOS+TCP
 */

//
// Includes
//

// stdlib
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// FreeRTOS
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <FreeRTOS_IP.h>
#include <FreeRTOS_Sockets.h>

// Uni.NET
#include "uni_net_udp_server.h"

//
// Private helpers
//

static inline void _lock(uni_net_udp_server_context_t* ctx) {
    if (ctx != NULL && ctx->state.lock != NULL) {
        (void)xSemaphoreTake(ctx->state.lock, portMAX_DELAY);
    }
}
static inline void _unlock(uni_net_udp_server_context_t* ctx) {
    if (ctx != NULL && ctx->state.lock != NULL) {
        (void)xSemaphoreGive(ctx->state.lock);
    }
}

static inline bool _socket_valid(Socket_t s) {
    return (s != NULL) && (s != FREERTOS_INVALID_SOCKET);
}

static BaseType_t _apply_timeouts(Socket_t s, uint32_t rx_timeout_ms, uint32_t tx_timeout_ms) {
    TickType_t rx_ticks = pdMS_TO_TICKS(rx_timeout_ms);
    TickType_t tx_ticks = pdMS_TO_TICKS(tx_timeout_ms);
    BaseType_t r1 = FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &rx_ticks, sizeof(rx_ticks));
    BaseType_t r2 = FreeRTOS_setsockopt(s, 0, FREERTOS_SO_SNDTIMEO, &tx_ticks, sizeof(tx_ticks));
    return (r1 == 0 && r2 == 0) ? 0 : -1;
}

static BaseType_t _apply_checksum_disable(Socket_t s, bool disable) {
    /* FreeRTOS+TCP option to enable(1)/disable(0) outgoing UDP checksum on this socket. */
    BaseType_t on = disable ? 0 : 1;
    return FreeRTOS_setsockopt(s, 0, FREERTOS_SO_UDPCKSUM_OUT, &on, sizeof(on));
}

static inline int32_t _map_timeout_to_wouldblock(int32_t rv) {
    if (rv == -pdFREERTOS_ERRNO_EWOULDBLOCK || rv == -pdFREERTOS_ERRNO_ETIMEDOUT) {
        return UNI_NET_UDP_RET_TIMEOUT; // 0
    }
    return rv;
}

//
// Task-driven receive
//

static void _uni_net_udp_server_task(void* arg) {
    uni_net_udp_server_context_t* ctx = (uni_net_udp_server_context_t*)arg;
    uint8_t rxbuf[UNI_NET_UDP_SERVER_RX_BUF_SIZE];

    for (;;) {
        if (ctx->state.stop_requested) {
            break;
        }

        struct freertos_sockaddr from = { 0 };
        uint32_t from_len = sizeof(from);

        int32_t rv;
        _lock(ctx);
        Socket_t s = ctx->state.socket;
        _unlock(ctx);

        if (!_socket_valid(s)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        rv = FreeRTOS_recvfrom(s, rxbuf, sizeof(rxbuf), 0, &from, &from_len);
        rv = _map_timeout_to_wouldblock(rv);

        if (rv > 0 && ctx->config.on_receive != NULL) {
            uni_net_udp_endpoint_t ep;
#if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
            ep.addr = from.sin_addr;
#else
            ep.addr = from.sin_address.ulIP_IPv4;
#endif
            ep.port = from.sin_port;
            ctx->config.on_receive(ctx->config.user, rxbuf, (size_t)rv, &ep);
        } else {
            // rv == 0 -> timeout; rv < 0 -> error; ignore in loop
        }
    }

    // Task exit
    ctx->state.task = NULL;
    vTaskDelete(NULL);
}

//
// Public API
//

bool uni_net_udp_server_start(uni_net_udp_server_context_t* ctx, const uni_net_udp_server_config_t* cfg) {
    if (ctx == NULL) {
        return false;
    }

    memset(ctx, 0, sizeof(*ctx));

    // Defaults
    ctx->config.rx_timeout_ms     = (cfg != NULL) ? cfg->rx_timeout_ms     : UNI_NET_UDP_SERVER_DEFAULT_RX_TIMEOUT_MS;
    ctx->config.tx_timeout_ms     = (cfg != NULL) ? cfg->tx_timeout_ms     : UNI_NET_UDP_SERVER_DEFAULT_TX_TIMEOUT_MS;
    ctx->config.allow_broadcast   = (cfg != NULL) ? cfg->allow_broadcast   : false;
    ctx->config.checksum_disable  = (cfg != NULL) ? cfg->checksum_disable  : false;
    ctx->config.bind_port_hbo     = (cfg != NULL) ? cfg->bind_port_hbo     : 0U;
    ctx->config.bind_addr_nbo     = (cfg != NULL) ? cfg->bind_addr_nbo     : (uint32_t)FREERTOS_INADDR_ANY;

    ctx->config.use_task          = (cfg != NULL) ? cfg->use_task          : false;
    ctx->config.on_receive        = (cfg != NULL) ? cfg->on_receive        : NULL;
    ctx->config.user              = (cfg != NULL) ? cfg->user              : NULL;
    ctx->config.task_priority     = (cfg != NULL) ? cfg->task_priority     : UNI_NET_UDP_SERVER_TASK_PRIORITY;
    ctx->config.task_stack_words  = (cfg != NULL) ? cfg->task_stack_words  : UNI_NET_UDP_SERVER_TASK_STACK_WORDS;

    // Wait for network up before binding
    while (FreeRTOS_IsNetworkUp() == pdFALSE) {
        vTaskDelay(pdMS_TO_TICKS(UNI_NET_UDP_SERVER_IFACE_TIME_MS));
    }

    ctx->state.lock = xSemaphoreCreateMutex();
    if (ctx->state.lock == NULL) {
        memset(ctx, 0, sizeof(*ctx));
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

    (void)_apply_checksum_disable(ctx->state.socket, ctx->config.checksum_disable);
    // Broadcast enable is a no-op in FreeRTOS+TCP; intention is stored in config.

    struct freertos_sockaddr local = { 0 };
    local.sin_family = FREERTOS_AF_INET;
    local.sin_port   = FreeRTOS_htons(ctx->config.bind_port_hbo);
#if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
    local.sin_addr   = ctx->config.bind_addr_nbo;
#else
    local.sin_address.ulIP_IPv4 = ctx->config.bind_addr_nbo;
#endif

    if (FreeRTOS_bind(ctx->state.socket, &local, sizeof(local)) != 0) {
        FreeRTOS_closesocket(ctx->state.socket);
        vSemaphoreDelete(ctx->state.lock);
        memset(ctx, 0, sizeof(*ctx));
        return false;
    }

    ctx->state.initialized = true;
    ctx->state.stop_requested = false;

    if (ctx->config.use_task && ctx->config.on_receive != NULL) {
        BaseType_t created = xTaskCreate(
            _uni_net_udp_server_task,
            "UNI_NET_UDP_SERVER",
            ctx->config.task_stack_words,
            ctx,
            ctx->config.task_priority,
            &ctx->state.task
        );
        if (created != pdTRUE) {
            FreeRTOS_closesocket(ctx->state.socket);
            vSemaphoreDelete(ctx->state.lock);
            memset(ctx, 0, sizeof(*ctx));
            return false;
        }
    }

    return true;
}

bool uni_net_udp_server_is_inited(const uni_net_udp_server_context_t* ctx) {
    return (ctx != NULL) && (ctx->state.initialized);
}

bool uni_net_udp_server_stop(uni_net_udp_server_context_t* ctx) {
    if (ctx == NULL || !uni_net_udp_server_is_inited(ctx)) {
        return false;
    }

    // Request task termination if running
    ctx->state.stop_requested = true;

    // Speed up task exit by reducing receive timeout
    _lock(ctx);
    if (_socket_valid(ctx->state.socket)) {
        TickType_t rx_ticks = pdMS_TO_TICKS(50U);
        (void)FreeRTOS_setsockopt(ctx->state.socket, 0, FREERTOS_SO_RCVTIMEO, &rx_ticks, sizeof(rx_ticks));
    }
    _unlock(ctx);

    // Wait for task exit if it exists
    while (ctx->state.task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Close socket
    _lock(ctx);
    if (_socket_valid(ctx->state.socket)) {
        FreeRTOS_closesocket(ctx->state.socket);
        ctx->state.socket = FREERTOS_INVALID_SOCKET;
    }
    _unlock(ctx);

    // Delete lock
    if (ctx->state.lock != NULL) {
        vSemaphoreDelete(ctx->state.lock);
    }

    memset(ctx, 0, sizeof(*ctx));
    return true;
}

int32_t uni_net_udp_server_recvfrom(uni_net_udp_server_context_t* ctx, uint8_t* buf, size_t buf_size, uni_net_udp_endpoint_t* out_from, uint32_t timeout_ms) {
    if (ctx == NULL || buf == NULL || buf_size == 0 || !uni_net_udp_server_is_inited(ctx)) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }
    // Cannot be used while task-driven mode is active
    if (ctx->state.task != NULL) {
        return -pdFREERTOS_ERRNO_EALREADY;
    }

    struct freertos_sockaddr from = { 0 };
    uint32_t from_len = sizeof(from);

    // Temporarily override RCVTIMEO for this call
    _lock(ctx);
    Socket_t s = ctx->state.socket;
    TickType_t orig_ticks = pdMS_TO_TICKS(ctx->config.rx_timeout_ms);
    TickType_t temp_ticks = pdMS_TO_TICKS(timeout_ms);
    (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &temp_ticks, sizeof(temp_ticks));
    _unlock(ctx);

    int32_t rv = FreeRTOS_recvfrom(s, buf, (int32_t)buf_size, 0, &from, &from_len);
    rv = _map_timeout_to_wouldblock(rv);

    // Restore original timeout
    _lock(ctx);
    (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &orig_ticks, sizeof(orig_ticks));
    _unlock(ctx);

    if (rv >= 0 && out_from != NULL && from_len >= sizeof(from)) {
#if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
        out_from->addr = from.sin_addr;
#else
        out_from->addr = from.sin_address.ulIP_IPv4;
#endif
        out_from->port = from.sin_port;
    }

    return rv;
}

int32_t uni_net_udp_server_sendto(uni_net_udp_server_context_t* ctx, const uint8_t* buf, size_t len, const uni_net_udp_endpoint_t* to) {
    if (ctx == NULL || buf == NULL || len == 0 || to == NULL || !uni_net_udp_server_is_inited(ctx)) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }

    struct freertos_sockaddr dst = { 0 };
    dst.sin_family = FREERTOS_AF_INET;
    dst.sin_port   = to->port;
#if ( ipconfigIPv4_BACKWARD_COMPATIBLE == 1 )
    dst.sin_addr   = to->addr;
#else
    dst.sin_address.ulIP_IPv4 = to->addr;
#endif

    // Avoid deadlock if called from server task: skip lock in that case
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    bool take_lock = (ctx->state.task == NULL) || (self != ctx->state.task);
    if (take_lock) {
        _lock(ctx);
    }
    Socket_t s = ctx->state.socket;
    int32_t rv = FreeRTOS_sendto(s, buf, (int32_t)len, 0, &dst, sizeof(dst));
    if (take_lock) {
        _unlock(ctx);
    }

    rv = _map_timeout_to_wouldblock(rv);
    if (rv > 0 && (size_t)rv != len) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }
    return rv;
}

bool uni_net_udp_server_set_timeouts(uni_net_udp_server_context_t* ctx, uint32_t rx_timeout_ms, uint32_t tx_timeout_ms) {
    if (ctx == NULL || !uni_net_udp_server_is_inited(ctx)) {
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

bool uni_net_udp_server_get_timeouts(const uni_net_udp_server_context_t* ctx, uint32_t* rx_timeout_ms, uint32_t* tx_timeout_ms) {
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

bool uni_net_udp_server_set_broadcast(uni_net_udp_server_context_t* ctx, bool enable) {
    if (ctx == NULL || !uni_net_udp_server_is_inited(ctx)) {
        return false;
    }
    // No SO_BROADCAST in FreeRTOS+TCP; just record intention
    ctx->config.allow_broadcast = enable;
    return true;
}

bool uni_net_udp_server_get_broadcast(const uni_net_udp_server_context_t* ctx, bool* enable) {
    if (ctx == NULL || enable == NULL) {
        return false;
    }
    *enable = ctx->config.allow_broadcast;
    return true;
}

bool uni_net_udp_server_set_checksum_disable(uni_net_udp_server_context_t* ctx, bool disable) {
    if (ctx == NULL || !uni_net_udp_server_is_inited(ctx)) {
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
