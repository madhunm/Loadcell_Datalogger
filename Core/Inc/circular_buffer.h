/**
 * @file    circular_buffer.h
 * @brief   Dual SPSC byte rings: 256 KB binary (ISR + main) and 1 KB CSV (main only).
 * @details Provides ringDesc_t descriptors so one implementation serves both buffer
 *          sizes without instantiating ringBuf_t twice (SRAM budget).  Producer ISR
 *          updates head with __DMB; consumer main updates tail with __DMB.
 *          Upstream: data_processing ISR (binary push), main (CSV/meta push, drain).
 *          Downstream: sdmmc_fatfs chunk writes.
 * @author  Madhu
 * @date    2026-04-13
 */

#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** @brief Binary ring capacity (power of two). */
#define RING_BIN_SIZE  (256U * 1024U)
/** @brief Binary ring index mask (size − 1). */
#define RING_BIN_MASK  (RING_BIN_SIZE - 1U)

/** @brief CSV ring capacity (power of two). */
#define CSV_BUF_SIZE   (1024U)
/** @brief CSV ring index mask. */
#define CSV_BUF_MASK   (CSV_BUF_SIZE - 1U)

/**
 * @brief Large binary ring backing store — exactly one instance (SRAM).
 */
typedef struct {
    uint8_t           buf[RING_BIN_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow;
} ringBuf_t;

/**
 * @brief Small CSV line ring — separate type so buf is 1 KB, not 256 KB.
 */
typedef struct {
    uint8_t           buf[CSV_BUF_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow;
} csvRingBuf_t;

/**
 * @brief Generic view over either ring for shared push/drain logic.
 */
typedef struct {
    uint8_t           *buf;
    uint32_t           mask;
    volatile uint32_t *head;
    volatile uint32_t *tail;
    volatile uint32_t *overflow;
} ringDesc_t;

extern ringBuf_t    g_binRing;
extern csvRingBuf_t g_csvRing;

extern volatile bool g_loggingActive;

extern volatile uint32_t g_adcPushCount;
extern volatile uint32_t g_forcePushCount;

/**
 * @brief  Build a descriptor for the binary ring.
 * @return Filled ringDesc_t pointing at g_binRing.
 */
static inline ringDesc_t ringDescBin(void)
{
    ringDesc_t d;
    d.buf       = g_binRing.buf;
    d.mask      = RING_BIN_MASK;
    d.head      = &g_binRing.head;
    d.tail      = &g_binRing.tail;
    d.overflow  = &g_binRing.overflow;
    return d;
}

/**
 * @brief  Build a descriptor for the CSV ring.
 * @return Filled ringDesc_t pointing at g_csvRing.
 */
static inline ringDesc_t ringDescCsv(void)
{
    ringDesc_t d;
    d.buf       = g_csvRing.buf;
    d.mask      = CSV_BUF_MASK;
    d.head      = &g_csvRing.head;
    d.tail      = &g_csvRing.tail;
    d.overflow  = &g_csvRing.overflow;
    return d;
}

void     ringInit(ringDesc_t d);
uint32_t ringPush(ringDesc_t d, const void *data, uint32_t len);
uint32_t ringUsed(ringDesc_t d);
uint32_t ringDrain(ringDesc_t d, uint8_t *dst, uint32_t maxLen);
uint32_t ringDrainContiguous(ringDesc_t d, const uint8_t **ptr);
void     ringAdvanceTail(ringDesc_t d, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* CIRCULAR_BUFFER_H */
