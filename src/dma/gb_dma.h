#ifndef GB_DMA_H
#define GB_DMA_H

#include <stdbool.h>
#include <stdint.h>

#include "../memory/gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_DMA_ADDR 0xFF46u
#define GB_DMA_TRANSFER_BYTES 0x00A0u
#define GB_DMA_DOTS_PER_BYTE 4u
#define GB_DMA_START_DELAY_T_CYCLES 4u

typedef struct GB_DMA {
    bool initialized;
    bool mapped;
    bool active;
    bool cgb_double_speed;

    GB_Memory *memory;
    size_t io_device_index;

    uint8_t source_high;
    uint16_t transfer_index;
    uint16_t startup_delay;
    uint8_t cycle_accumulator;
} GB_DMA;

GB_Result gb_dma_init(GB_DMA *dma, GB_Memory *memory, GB_Error *error);
GB_Result gb_dma_reset(GB_DMA *dma, GB_Error *error);
GB_Result gb_dma_destroy(GB_DMA *dma, GB_Error *error);
GB_Result gb_dma_set_cgb_double_speed(GB_DMA *dma, bool enabled, GB_Error *error);

GB_Result gb_dma_tick(GB_DMA *dma, uint32_t t_cycles, GB_Error *error);
GB_Result gb_dma_device_tick(void *user, uint32_t t_cycles, GB_Error *error);

GB_Result gb_dma_read8(void *user, uint16_t address,
                       uint8_t *value, GB_Error *error);
GB_Result gb_dma_write8(void *user, uint16_t address,
                        uint8_t value, GB_Error *error);

GB_MemoryDMAController gb_dma_memory_controller(GB_DMA *dma);

bool gb_dma_is_active(const GB_DMA *dma);
uint16_t gb_dma_bytes_transferred(const GB_DMA *dma);
uint16_t gb_dma_source_address(const GB_DMA *dma);

#ifdef __cplusplus
}
#endif

#endif /* GB_DMA_H */
