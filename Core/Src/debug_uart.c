/**
 * @file debug_uart.c
 * @brief Debug output — printf retarget to USART1 + USB CDC with non-blocking TX.
 * @details Implements three output paths:
 *          1. uartWrite() — blocking HAL_UART_Transmit to USART1 (921600 baud).
 *          2. cdcWrite()  — enqueue into a 2 KB ring buffer; cdcPoll() drains it.
 *          3. _write()     — newlib retarget: timestamps each line, sends to USART1
 *                            immediately (blocking, 30 ms timeout).
 *
 *          The CDC TX state machine (cdcPoll) drives ux_device_class_cdc_acm_write_run()
 *          which is a non-blocking, multi-call API:
 *            1st call  → copies data to USB transfer buffer, returns UX_STATE_WAIT
 *            Nth call  → checks USB HW completion, returns UX_STATE_WAIT again
 *            Final     → transfer complete, returns UX_STATE_NEXT
 *          cdcPoll() must be called every main-loop iteration to make progress.
 *
 * @author Madhu
 * @date   2026-04-12
 * @see    USBX CDC ACM standalone (UX_STANDALONE) API documentation
 */

#include "debug_uart.h"
#include "usart.h"
#include "ux_device_cdc_acm.h"
#include "ux_api.h"
#include <stdint.h>
#include <stdio.h>

#define CDC_TX_BUF_SIZE  4096
#define CDC_TX_CHUNK_SZ  64     /**< USB FS max packet size. */

static uint8_t  cdcTxBuf[CDC_TX_BUF_SIZE];
static volatile uint32_t cdcTxHead = 0;   /**< Written by printf context. */
static volatile uint32_t cdcTxTail = 0;   /**< Read by cdcPoll().        */

static uint8_t  cdcTxChunk[CDC_TX_CHUNK_SZ];
static ULONG    cdcTxActual;
static ULONG    cdcTxPending;

typedef enum { CDC_IDLE, CDC_SENDING } cdcState_t;
static cdcState_t cdcState = CDC_IDLE;

/**
 * @brief  Enqueue bytes into the CDC TX ring buffer.
 * @param[in] data  Source buffer.
 * @param[in] len   Number of bytes to enqueue.
 * @note   Silently drops bytes if the ring buffer is full (no back-pressure).
 */
static void cdcEnqueue(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        uint32_t next = (cdcTxHead + 1) % CDC_TX_BUF_SIZE;
        if (next == cdcTxTail)
            break;
        cdcTxBuf[cdcTxHead] = data[i];
        cdcTxHead = next;
    }
}

void cdcPoll(void)
{
    UX_SLAVE_CLASS_CDC_ACM *cdc = cdc_acm_get_instance();
    if (cdc == UX_NULL)
        return;

    UINT status;

    switch (cdcState)
    {
    case CDC_IDLE:
        if (cdcTxHead == cdcTxTail)
            return;
        {
            uint32_t n = 0;
            while (n < CDC_TX_CHUNK_SZ && cdcTxTail != cdcTxHead)
            {
                cdcTxChunk[n++] = cdcTxBuf[cdcTxTail];
                cdcTxTail = (cdcTxTail + 1) % CDC_TX_BUF_SIZE;
            }
            cdcTxPending = n;
            status = ux_device_class_cdc_acm_write_run(cdc, cdcTxChunk,
                                                       (ULONG)n, &cdcTxActual);
            if (status == UX_STATE_WAIT)
                cdcState = CDC_SENDING;
        }
        break;

    case CDC_SENDING:
        status = ux_device_class_cdc_acm_write_run(cdc, cdcTxChunk,
                                                   cdcTxPending, &cdcTxActual);
        if (status < UX_STATE_WAIT)
            cdcState = CDC_IDLE;
        break;
    }
}

void uartWrite(const uint8_t *data, uint32_t len)
{
    HAL_UART_Transmit(&huart1, data, (uint16_t)len, 30);
}

void cdcWrite(const uint8_t *data, uint32_t len)
{
    cdcEnqueue(data, len);
}

/**
 * @brief  Newlib _write() retarget — called by printf / puts.
 * @details Prepends a millisecond timestamp at each line start, then sends the
 *          formatted output to USART1 (blocking).  CDC output is handled
 *          separately by the VT220 UI layer (debug_ui.c) via cdcWrite().
 * @param[in] file  File descriptor (ignored — stdout/stderr both go to UART).
 * @param[in] ptr   Data buffer from newlib.
 * @param[in] len   Number of bytes.
 * @return Number of bytes consumed (always @p len).
 * @note   Must NOT be called from ISR context (blocking UART with 30 ms timeout).
 */
int _write(int file, char *ptr, int len)
{
    (void)file;
    static int bol = 1;
    char buf[320];
    int bp = 0;

    for (int i = 0; i < len; i++)
    {
        if (bol && ptr[i] != '\r' && ptr[i] != '\n')
        {
            bp += snprintf(buf + bp, sizeof(buf) - bp,
                           "%7lu ", (unsigned long)HAL_GetTick());
            bol = 0;
        }
        if (bp < (int)sizeof(buf) - 1)
            buf[bp++] = ptr[i];
        if (ptr[i] == '\n')
            bol = 1;
        if (bp >= (int)sizeof(buf) - 20)
        {
            HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)bp, 30);
            bp = 0;
        }
    }

    if (bp > 0)
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)bp, 30);

    return len;
}
