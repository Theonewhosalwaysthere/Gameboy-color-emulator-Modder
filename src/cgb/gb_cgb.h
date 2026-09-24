#ifndef GB_CGB_H
#define GB_CGB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../cpu/gb_cpu.h"
#include "../dma/gb_dma.h"
#include "../memory/gb_memory.h"
#include "../ppu/gb_ppu.h"
#include "../timer/gb_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_CGB_ADDR_KEY1  0xFF4Du
#define GB_CGB_ADDR_HDMA1 0xFF51u
#define GB_CGB_ADDR_HDMA2 0xFF52u
#define GB_CGB_ADDR_HDMA3 0xFF53u
#define GB_CGB_ADDR_HDMA4 0xFF54u
#define GB_CGB_ADDR_HDMA5 0xFF55u
#define GB_CGB_ADDR_RP    0xFF56u

#define GB_CGB_HDMA_BLOCK_BYTES 16u
#define GB_CGB_HDMA_BLOCK_T_CYCLES 32u
#define GB_CGB_HDMA_MAX_BLOCKS 128u

/* KEY1's bit 7 selects the currently active CPU speed. */
typedef enum GB_CGBSpeed {
    GB_CGB_SPEED_NORMAL = 0,
    GB_CGB_SPEED_DOUBLE = 1
} GB_CGBSpeed;

typedef struct GB_CGB {
    bool initialized;
    bool mapped;

    GB_Memory *memory;
    GB_CPU *cpu;
    GB_Timer *timer;
    GB_DMA *dma;
    GB_PPU *ppu;

    size_t key1_device;
    size_t hdma_device;
    size_t rp_device;

    GB_CGBSpeed speed;
    bool speed_switch_prepared;

    uint8_t hdma1;
    uint8_t hdma2;
    uint8_t hdma3;
    uint8_t hdma4;
    uint8_t hdma5_status;

    bool hdma_active;
    bool hdma_hblank_mode;
    uint16_t hdma_source;
    uint16_t hdma_destination;
    uint8_t hdma_vram_bank;
    uint8_t hdma_blocks_remaining;

    uint32_t cpu_stall_t_cycles;
    uint32_t speed_switch_pause_t_cycles;
    bool hblank_hdma_block_done;
    GB_PPU_Mode previous_ppu_mode;
    bool previous_ppu_mode_valid;

    uint8_t rp_control;
    bool ir_input_active;
    bool ir_led_on;
} GB_CGB;

GB_Result gb_cgb_init(GB_CGB *cgb, GB_Memory *memory, GB_Error *error);
GB_Result gb_cgb_reset(GB_CGB *cgb, GB_Error *error);
GB_Result gb_cgb_destroy(GB_CGB *cgb, GB_Error *error);

GB_Result gb_cgb_attach_cpu(GB_CGB *cgb, GB_CPU *cpu, GB_Error *error);
GB_Result gb_cgb_attach_timer(GB_CGB *cgb, GB_Timer *timer, GB_Error *error);
GB_Result gb_cgb_attach_dma(GB_CGB *cgb, GB_DMA *dma, GB_Error *error);
GB_Result gb_cgb_attach_ppu(GB_CGB *cgb, GB_PPU *ppu, GB_Error *error);

GB_Result gb_cgb_read8(void *user, uint16_t address, uint8_t *value,
                       GB_Error *error);
GB_Result gb_cgb_write8(void *user, uint16_t address, uint8_t value,
                        GB_Error *error);
GB_Result gb_cgb_device_tick(void *user, uint32_t t_cycles,
                             GB_Error *error);

GB_Result gb_cgb_tick(GB_CGB *cgb, uint32_t t_cycles, GB_Error *error);

/* Process a STOP entered by the CPU. If KEY1 bit 0 was prepared, STOP performs
 * the CGB speed switch and immediately resumes the CPU instead of leaving it
 * stopped. Otherwise the normal STOP state is preserved. */
GB_Result gb_cgb_handle_cpu_stop(GB_CGB *cgb, bool *speed_switched,
                                 GB_Error *error);

GB_Result gb_cgb_set_speed(GB_CGB *cgb, GB_CGBSpeed speed, GB_Error *error);
GB_Result gb_cgb_prepare_speed_switch(GB_CGB *cgb, bool prepare, GB_Error *error);
GB_CGBSpeed gb_cgb_get_speed(const GB_CGB *cgb);
bool gb_cgb_speed_switch_prepared(const GB_CGB *cgb);

bool gb_cgb_hdma_active(const GB_CGB *cgb);
uint8_t gb_cgb_hdma_blocks_remaining(const GB_CGB *cgb);
uint32_t gb_cgb_cpu_stall_t_cycles(const GB_CGB *cgb);
bool gb_cgb_cpu_is_stalled(const GB_CGB *cgb);
bool gb_cgb_speed_switch_paused(const GB_CGB *cgb);

GB_Result gb_cgb_set_ir_input(GB_CGB *cgb, bool active, GB_Error *error);
bool gb_cgb_ir_led_on(const GB_CGB *cgb);

#ifdef __cplusplus
}
#endif

#endif /* GB_CGB_H */
