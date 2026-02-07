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
#include <stddef.h>

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
// Local configuration
//

#ifndef UNI_NET_UDP_SERVER_RX_BATCH_DEPTH
#define UNI_NET_UDP_SERVER_RX_BATCH_DEPTH           (8U)
#endif

#ifndef UNI_NET_UDP_SERVER_RX_BURST_BUDGET
#define UNI_NET_UDP_SERVER_RX_BURST_BUDGET          (32U)
#endif

#ifndef UNI_NET_UDP_SERVER_RX_QUEUE_PACKETS
#define UNI_NET_UDP_SERVER_RX_QUEUE_PACKETS         (64U)
#endif

//
// Private types
//

typedef struct {
    const uint8_t*           payload;
    size_t                   length;
    uni_net_udp_endpoint_t   from;
    bool                     release_with_stack;
} _uni_net_udp_server_rx_item_t;

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

static inline void _stats_reset_for_ctx(uni_net_udp_server_context_t* ctx) {
    if (ctx == nullptr) {
        return;
    }
    ctx->state.rx_packets_total = 0U;
    ctx->state.rx_drop_total = 0U;
}

static inline void _stats_add_packets(uni_net_udp_server_context_t* ctx, uint64_t amount) {
    if (ctx == nullptr) {
        return;
    }
    ctx->state.rx_packets_total += amount;
}

static inline void _stats_add_drops(uni_net_udp_server_context_t* ctx, uint64_t amount) {
    if (ctx == nullptr) {
        return;
    }
    ctx->state.rx_drop_total += amount;
}

static inline uint64_t _stats_get_drop_count(const uni_net_udp_server_context_t* ctx) {
    if (ctx == nullptr) {
        return 0U;
    }
    return ctx->state.rx_drop_total;
}

static void _endpoint_from_sockaddr(const struct freertos_sockaddr* from, uni_net_udp_endpoint_t* ep) {
    if (from == nullptr || ep == nullptr) {
        return;
    }
    ep->addr = from->sin_address.ulIP_IPv4;
    ep->port = from->sin_port;
}

static void _apply_udp_rx_queue_tuning(Socket_t s) {
#if ( ipconfigUDP_MAX_RX_PACKETS > 0 )
    UBaseType_t rxq_packets = (UBaseType_t)UNI_NET_UDP_SERVER_RX_QUEUE_PACKETS;
    if (rxq_packets > (UBaseType_t)ipconfigUDP_MAX_RX_PACKETS) {
        rxq_packets = (UBaseType_t)ipconfigUDP_MAX_RX_PACKETS;
    }
    (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_UDP_MAX_RX_PACKETS, &rxq_packets, sizeof(rxq_packets));
#else
    (void)s;
#endif
}

static uint32_t _udp_rx_queue_packets_value(void) {
#if ( ipconfigUDP_MAX_RX_PACKETS > 0 )
    uint32_t packets = UNI_NET_UDP_SERVER_RX_QUEUE_PACKETS;
    if (packets > (uint32_t)ipconfigUDP_MAX_RX_PACKETS) {
        packets = (uint32_t)ipconfigUDP_MAX_RX_PACKETS;
    }
    return packets;
#else
    return 0U;
#endif
}

static size_t _recv_batch(uni_net_udp_server_context_t* ctx,
                          Socket_t s,
                          _uni_net_udp_server_rx_item_t* items,
                          size_t capacity,
                          bool wait_for_first) {
    if (ctx == nullptr || !_socket_valid(s) || items == nullptr || capacity == 0U) {
        return 0U;
    }

    size_t count = 0U;
    size_t iterations = 0U;

    while (iterations < capacity) {
        const BaseType_t flags = ((count == 0U) && wait_for_first) ? 0 : FREERTOS_MSG_DONTWAIT;
        struct freertos_sockaddr from = {0};
        uint32_t from_len = sizeof(from);
        void* payload = nullptr;
        int32_t rv = FreeRTOS_recvfrom(s, &payload, 0U, (flags | FREERTOS_ZERO_COPY), &from, &from_len);
        rv = _map_timeout_to_wouldblock(rv);

        if (rv <= 0) {
            if (payload != nullptr) {
                FreeRTOS_ReleaseUDPPayloadBuffer(payload);
            }

            if (rv < 0) {
                _stats_add_drops(ctx, 1U);
            }
            break;
        }

        items[count].payload = (const uint8_t*)payload;
        items[count].length = (size_t)rv;
        _endpoint_from_sockaddr(&from, &items[count].from);
        items[count].release_with_stack = true;
        count++;
        iterations++;
        continue;
    }

    return count;
}

static void _dispatch_batch(uni_net_udp_server_context_t* ctx, const _uni_net_udp_server_rx_item_t* items, size_t count) {
    if (ctx == nullptr || items == nullptr || count == 0U) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (ctx->config.on_receive != nullptr && items[i].payload != nullptr && items[i].length > 0U) {
            ctx->config.on_receive(ctx->config.user, items[i].payload, items[i].length, &items[i].from);
        } else {
            _stats_add_drops(ctx, 1U);
        }

        if (items[i].release_with_stack && items[i].payload != nullptr) {
            FreeRTOS_ReleaseUDPPayloadBuffer(items[i].payload);
        }
    }

    _stats_add_packets(ctx, count);
}

//
// Task-driven receive
//

static void _uni_net_udp_server_task(void *arg) {
    uni_net_udp_server_context_t *ctx = (uni_net_udp_server_context_t *) arg;

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
                    _apply_udp_rx_queue_tuning(ctx->state.socket);
                    ctx->state.rx_queue_packets = _udp_rx_queue_packets_value();

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

    /*
     * Event-driven receive loop.
     *
     * NOTE ABOUT Linux recvmmsg()/SO_RXQ_OVFL:
     * - This module currently uses FreeRTOS+TCP sockets (`FreeRTOS_socket` / `FreeRTOS_recvfrom`).
     * - Linux kernel ancillary control messages (cmsg, `SO_RXQ_OVFL`) and `recvmmsg()` are not available
     *   through the FreeRTOS+TCP socket API.
     * - To minimize copies and syscall overhead in this environment, we use:
     *     1) UDP zero-copy receive (`FREERTOS_ZERO_COPY`),
     *     2) burst-drain batching in non-blocking mode (`FREERTOS_MSG_DONTWAIT`).
     */
    while (!ctx->state.stop_requested && ctx->config.on_receive != nullptr) {
        _uni_net_udp_server_rx_item_t batch[UNI_NET_UDP_SERVER_RX_BATCH_DEPTH] = {0};
        size_t burst_budget = UNI_NET_UDP_SERVER_RX_BURST_BUDGET;
        bool wait_for_first = true;

        _lock(ctx);
        Socket_t s = ctx->state.socket;
        _unlock(ctx);

        if (!_socket_valid(s)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        while (!ctx->state.stop_requested && (burst_budget > 0U)) {
            const size_t cap = (burst_budget > UNI_NET_UDP_SERVER_RX_BATCH_DEPTH)
                                   ? UNI_NET_UDP_SERVER_RX_BATCH_DEPTH
                                   : burst_budget;

            const size_t received = _recv_batch(ctx,
                                                s,
                                                batch,
                                                cap,
                                                wait_for_first);

            if (received == 0U) {
                break;
            }

            wait_for_first = false;
            burst_budget -= received;
            _dispatch_batch(ctx, batch, received);
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
        _stats_reset_for_ctx(ctx);

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

    if (rv > 0) {
        _stats_add_packets(ctx, 1U);
    } else if (rv < 0) {
        _stats_add_drops(ctx, 1U);
    }

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

uint64_t uni_net_udp_server_get_rx_drop_count(const uni_net_udp_server_context_t *ctx) {
    if (ctx == nullptr) {
        return 0U;
    }
    return _stats_get_drop_count(ctx);
}

uint32_t uni_net_udp_server_get_rx_queue_packets(const uni_net_udp_server_context_t *ctx) {
    if (ctx == nullptr) {
        return 0U;
    }
    return ctx->state.rx_queue_packets;
}
