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

#include "uni_common_bytes.h"

//
// Private helpers
//

static inline void _lock(uni_net_udp_server_context_t *ctx) {
    if (ctx != nullptr && ctx->state.lock != nullptr) {
        (void) xSemaphoreTake(ctx->state.lock, portMAX_DELAY);
    }
}

static inline void _unlock(uni_net_udp_server_context_t *ctx) {
    if (ctx != nullptr && ctx->state.lock != nullptr) {
        (void) xSemaphoreGive(ctx->state.lock);
    }
}

static inline bool _socket_valid(Socket_t s) {
    return (s != nullptr) && (s != FREERTOS_INVALID_SOCKET);
}

static BaseType_t _apply_timeouts(Socket_t s, uint32_t rx_timeout_ms, uint32_t tx_timeout_ms) {
    TickType_t rx_ticks = pdMS_TO_TICKS(rx_timeout_ms);
    TickType_t tx_ticks = pdMS_TO_TICKS(tx_timeout_ms);
    BaseType_t r1 = FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &rx_ticks, sizeof(rx_ticks));
    BaseType_t r2 = FreeRTOS_setsockopt(s, 0, FREERTOS_SO_SNDTIMEO, &tx_ticks, sizeof(tx_ticks));
    return (r1 == 0 && r2 == 0) ? 0 : -1;
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

static void _uni_net_udp_server_task(void *arg) {
    uni_net_udp_server_context_t *ctx = (uni_net_udp_server_context_t *) arg;
    uint8_t rxbuf[UNI_NET_UDP_SERVER_RX_BUF_SIZE];

    // Wait for network up before binding/creating socket
    while ((FreeRTOS_IsNetworkUp() == pdFALSE) && !ctx->state.stop_requested) {
        vTaskDelay(pdMS_TO_TICKS(UNI_NET_UDP_SERVER_IFACE_TIME_MS));
    }

    if (!ctx->state.stop_requested) {
        // Create synchronization primitive
        ctx->state.lock = xSemaphoreCreateMutex();

        if (ctx->state.lock != nullptr) {
            // Create UDP socket
            ctx->state.socket = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP);

            if (_socket_valid(ctx->state.socket)) {
                // Apply timeouts and checksum option
                if (_apply_timeouts(ctx->state.socket, ctx->config.rx_timeout_ms, ctx->config.tx_timeout_ms) == 0) {
                    // Bind to local endpoint
                    struct freertos_sockaddr local = {0};
                    local.sin_family = FREERTOS_AF_INET;
                    local.sin_port = uni_common_bytes_swap16(ctx->config.bind_port);
                    local.sin_address.ulIP_IPv4 = ctx->config.bind_addr;

                    if (FreeRTOS_bind(ctx->state.socket, &local, sizeof(local)) == 0) {
                        ctx->state.initialized = true;
                    } else {
                        FreeRTOS_closesocket(ctx->state.socket);
                        ctx->state.socket = FREERTOS_INVALID_SOCKET;
                    }
                } else {
                    FreeRTOS_closesocket(ctx->state.socket);
                    ctx->state.socket = FREERTOS_INVALID_SOCKET;
                }
            }

            if (!ctx->state.initialized) {
                vSemaphoreDelete(ctx->state.lock);
                ctx->state.lock = nullptr;
            }
        }
    }

    // If configured without event-driven receive, idle until stop is requested
    if (!ctx->state.stop_requested && ctx->config.on_receive == nullptr) {
        while (!ctx->state.stop_requested) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // Event-driven receive loop
    while (!ctx->state.stop_requested && ctx->config.on_receive != NULL) {
        struct freertos_sockaddr from = {0};
        uint32_t from_len = sizeof(from);

        _lock(ctx);
        Socket_t s = ctx->state.socket;
        _unlock(ctx);

        if (!_socket_valid(s)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int32_t rv = FreeRTOS_recvfrom(s, rxbuf, sizeof(rxbuf), 0, &from, &from_len);
        rv = _map_timeout_to_wouldblock(rv);

        if (rv > 0 && ctx->config.on_receive != NULL) {
            uni_net_udp_endpoint_t ep;
            ep.addr = from.sin_address.ulIP_IPv4;
            ep.port = from.sin_port;
            ctx->config.on_receive(ctx->config.user, rxbuf, (size_t) rv, &ep);
        } else {
            // rv == 0 -> timeout; rv < 0 -> error; ignore in loop
        }
    }

    // Task exit
    ctx->state.task = nullptr;
    vTaskDelete(nullptr);
}

//
// Public API
//

bool uni_net_udp_server_start(uni_net_udp_server_context_t *ctx, const uni_net_udp_server_config_t *cfg) {
    bool result = false;

    if (ctx != nullptr) {
        memset(ctx, 0, sizeof(*ctx));

        // Defaults
        ctx->config.bind_addr = FREERTOS_INADDR_ANY;
        ctx->config.bind_port = 0U;
        ctx->config.rx_timeout_ms = UNI_NET_UDP_SERVER_DEFAULT_RX_TIMEOUT_MS;
        ctx->config.tx_timeout_ms = UNI_NET_UDP_SERVER_DEFAULT_TX_TIMEOUT_MS;
        ctx->config.on_receive = nullptr;
        ctx->config.user = nullptr;
        ctx->config.task_priority = UNI_NET_UDP_SERVER_TASK_PRIORITY;
        ctx->config.task_stack_words = UNI_NET_UDP_SERVER_TASK_STACK_WORDS;
        if (cfg != nullptr) {
            ctx->config.bind_addr = cfg->bind_addr;
            ctx->config.bind_port = cfg->bind_port;
            ctx->config.rx_timeout_ms = cfg->rx_timeout_ms;
            ctx->config.tx_timeout_ms = cfg->tx_timeout_ms;
            ctx->config.on_receive = cfg->on_receive;
            ctx->config.user = cfg->user;
            ctx->config.task_priority = cfg->task_priority;
            ctx->config.task_stack_words = cfg->task_stack_words;
        }
        ctx->state.initialized = false;
        ctx->state.stop_requested = false;

        // Always create a task; it will perform all initialization internally
        BaseType_t created = xTaskCreate(
            _uni_net_udp_server_task,
            "UNI_NET_UDP_SERVER",
            ctx->config.task_stack_words,
            ctx,
            ctx->config.task_priority,
            &ctx->state.task
        );
        if (created == pdTRUE) {
            result = true;
        }
        else{
            memset(ctx, 0, sizeof(*ctx));
        }
    }
    return result;
}

bool uni_net_udp_server_is_inited(const uni_net_udp_server_context_t *ctx) {
    return (ctx != nullptr) && (ctx->state.initialized);
}

bool uni_net_udp_server_stop(uni_net_udp_server_context_t *ctx) {
    if (ctx == nullptr) {
        return false;
    }

    // Request task termination if running
    ctx->state.stop_requested = true;

    // Speed up task exit by reducing receive timeout (if socket exists)
    _lock(ctx);
    if (_socket_valid(ctx->state.socket)) {
        TickType_t rx_ticks = pdMS_TO_TICKS(50U);
        (void) FreeRTOS_setsockopt(ctx->state.socket, 0, FREERTOS_SO_RCVTIMEO, &rx_ticks, sizeof(rx_ticks));
    }
    _unlock(ctx);

    // Wait for task exit if it exists
    while (ctx->state.task != nullptr) {
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
    if (ctx->state.lock != nullptr) {
        vSemaphoreDelete(ctx->state.lock);
    }

    memset(ctx, 0, sizeof(*ctx));
    return true;
}

int32_t uni_net_udp_server_recvfrom(uni_net_udp_server_context_t *ctx, uint8_t *buf, size_t buf_size,
                                    uni_net_udp_endpoint_t *out_from, uint32_t timeout_ms) {
    if (ctx == nullptr || buf == nullptr || buf_size == 0 || !uni_net_udp_server_is_inited(ctx)) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }
    // Cannot be used while event-driven receive task is active
    if ((ctx->config.on_receive != nullptr) && (ctx->state.task != nullptr)) {
        return -pdFREERTOS_ERRNO_EALREADY;
    }

    struct freertos_sockaddr from = {0};
    uint32_t from_len = sizeof(from);

    // Temporarily override RCVTIMEO for this call
    _lock(ctx);
    Socket_t s = ctx->state.socket;
    TickType_t orig_ticks = pdMS_TO_TICKS(ctx->config.rx_timeout_ms);
    TickType_t temp_ticks = pdMS_TO_TICKS(timeout_ms);
    (void) FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &temp_ticks, sizeof(temp_ticks));
    _unlock(ctx);

    int32_t rv = FreeRTOS_recvfrom(s, buf, (int32_t) buf_size, 0, &from, &from_len);
    rv = _map_timeout_to_wouldblock(rv);

    // Restore original timeout
    _lock(ctx);
    (void) FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &orig_ticks, sizeof(orig_ticks));
    _unlock(ctx);

    if (rv >= 0 && out_from != nullptr && from_len >= sizeof(from)) {
        out_from->addr = from.sin_address.ulIP_IPv4;
        out_from->port = from.sin_port;
    }

    return rv;
}

int32_t uni_net_udp_server_sendto(uni_net_udp_server_context_t *ctx, const uint8_t *buf, size_t len,
                                  const uni_net_udp_endpoint_t *to) {
    if (ctx == nullptr || buf == nullptr || len == 0 || to == nullptr || !uni_net_udp_server_is_inited(ctx)) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }

    struct freertos_sockaddr dst = {0};
    dst.sin_family = FREERTOS_AF_INET;
    dst.sin_port = to->port;
    dst.sin_address.ulIP_IPv4 = to->addr;

    // Avoid deadlock if called from server task: skip lock in that case
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    bool take_lock = (ctx->state.task == nullptr) || (self != ctx->state.task);
    if (take_lock) {
        _lock(ctx);
    }
    Socket_t s = ctx->state.socket;
    int32_t rv = FreeRTOS_sendto(s, buf, (int32_t) len, 0, &dst, sizeof(dst));
    if (take_lock) {
        _unlock(ctx);
    }

    rv = _map_timeout_to_wouldblock(rv);
    if (rv > 0 && (size_t) rv != len) {
        return -pdFREERTOS_ERRNO_EINVAL;
    }
    return rv;
}

bool uni_net_udp_server_set_timeouts(uni_net_udp_server_context_t *ctx, uint32_t rx_timeout_ms,
                                     uint32_t tx_timeout_ms) {
    if (ctx == nullptr || !uni_net_udp_server_is_inited(ctx)) {
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

bool uni_net_udp_server_get_timeouts(const uni_net_udp_server_context_t *ctx, uint32_t *rx_timeout_ms,
                                     uint32_t *tx_timeout_ms) {
    if (ctx == nullptr) {
        return false;
    }
    if (rx_timeout_ms != nullptr) {
        *rx_timeout_ms = ctx->config.rx_timeout_ms;
    }
    if (tx_timeout_ms != nullptr) {
        *tx_timeout_ms = ctx->config.tx_timeout_ms;
    }
    return true;
}
