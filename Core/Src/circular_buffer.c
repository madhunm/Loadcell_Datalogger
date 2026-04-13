/**
 * @file    circular_buffer.c
 * @brief   Lock-free SPSC byte rings with descriptor-based API.
 * @details kfifo-style head/tail counters; indices use mask; full when
 *          (head - tail) == (mask + 1).  Overflow increments *overflow and
 *          stops the current push.  See ARM DMB for visibility between ISR and main.
 * @author  Madhu
 * @date    2026-04-13
 */

#include "circular_buffer.h"
#include "stm32h5xx_hal.h"
#include <string.h>

ringBuf_t    g_binRing;
csvRingBuf_t g_csvRing;

volatile bool g_loggingActive = false;

volatile uint32_t g_adcPushCount  = 0U;
volatile uint32_t g_forcePushCount = 0U;

/**
 * @brief  Reset ring indices and overflow counter.
 * @param[in] d  Ring descriptor.
 */
void ringInit(ringDesc_t d)
{
    *d.head     = 0U;
    *d.tail     = 0U;
    *d.overflow = 0U;
    __DMB();
}

/**
 * @brief  Return bytes currently in the ring.
 * @param[in] d  Ring descriptor.
 * @return Occupied byte count (0 .. capacity).
 */
uint32_t ringUsed(ringDesc_t d)
{
    uint32_t h = *d.head;
    uint32_t t = *d.tail;
    __DMB();
    return h - t;
}

/**
 * @brief  Push bytes to the tail side (producer).
 * @param[in]  d     Ring descriptor.
 * @param[in]  data  Source bytes.
 * @param[in]  len   Number of bytes to push.
 * @return Number of bytes actually written (may be 0 if full).
 */
uint32_t ringPush(ringDesc_t d, const void *data, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t       cap = d.mask + 1U;
    uint32_t       n   = 0U;

    while (n < len)
    {
        uint32_t used = *d.head - *d.tail;
        if (used >= cap)
        {
            (*d.overflow)++;
            break;
        }
        d.buf[*d.head & d.mask] = src[n];
        (*d.head)++;
        __DMB();
        n++;
    }
    return n;
}

/**
 * @brief  Copy up to maxLen bytes from the ring into dst (consumer).
 * @param[in]  d      Ring descriptor.
 * @param[out] dst    Destination buffer.
 * @param[in]  maxLen Max bytes to copy.
 * @return Bytes copied.
 */
uint32_t ringDrain(ringDesc_t d, uint8_t *dst, uint32_t maxLen)
{
    uint32_t n = 0U;
    while (n < maxLen)
    {
        uint32_t used = *d.head - *d.tail;
        if (used == 0U)
            break;
        dst[n] = d.buf[*d.tail & d.mask];
        (*d.tail)++;
        __DMB();
        n++;
    }
    return n;
}

/**
 * @brief  Get a pointer to a contiguous run from the read side (no copy).
 * @param[in]  d    Ring descriptor.
 * @param[out] ptr  Receives start address or NULL if empty.
 * @return Number of contiguous bytes available from *ptr.
 */
uint32_t ringDrainContiguous(ringDesc_t d, const uint8_t **ptr)
{
    uint32_t used = *d.head - *d.tail;
    if (used == 0U)
    {
        *ptr = NULL;
        return 0U;
    }
    uint32_t cap   = d.mask + 1U;
    uint32_t idx   = *d.tail & d.mask;
    uint32_t toEnd = cap - idx;
    uint32_t n     = (used < toEnd) ? used : toEnd;
    *ptr = d.buf + idx;
    return n;
}

/**
 * @brief  Advance the read index after consuming bytes from ringDrainContiguous.
 * @param[in] d  Ring descriptor.
 * @param[in] n  Bytes consumed (must not exceed prior contiguous length).
 */
void ringAdvanceTail(ringDesc_t d, uint32_t n)
{
    *d.tail += n;
    __DMB();
}
