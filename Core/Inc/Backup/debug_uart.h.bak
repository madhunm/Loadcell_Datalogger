/**
 * @file debug_uart.h
 * @brief Debug output — printf retarget to USART1 + USB CDC, with CDC TX state machine.
 * @details Implements newlib _write() to send printf output simultaneously to
 *          USART1 (blocking, immediate) and USBX CDC ACM (buffered, polled).
 *          A 2 KB ring buffer decouples printf callers from the non-blocking
 *          CDC write_run() state machine.
 *
 *          Upstream: printf() via _write() from any non-ISR context.
 *          Downstream: USART1 at 921600 baud; USB CDC to host terminal.
 *
 * @author Madhu
 * @date   2026-04-12
 */

#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Pump the CDC TX state machine — must be called every main-loop iteration.
 * @details Dequeues up to 64 bytes from the CDC ring buffer and drives the
 *          non-blocking ux_device_class_cdc_acm_write_run() until the USB
 *          transfer completes or the host disconnects.
 * @pre    MX_USBX_Init() and ux_dcd_stm32_initialize() completed.
 */
void cdcPoll(void);

/**
 * @brief  Blocking write to USART1 (debug UART, 921600 baud).
 * @param[in] data  Pointer to the byte buffer.
 * @param[in] len   Number of bytes to transmit.
 */
void uartWrite(const uint8_t *data, uint32_t len);

/**
 * @brief  Enqueue data into the CDC TX ring buffer for later transmission.
 * @param[in] data  Pointer to the byte buffer.
 * @param[in] len   Number of bytes to enqueue.
 * @note   Non-blocking; silently drops bytes if the 2 KB ring buffer is full.
 */
void cdcWrite(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_UART_H */
