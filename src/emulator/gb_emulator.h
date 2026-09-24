#ifndef GB_EMULATOR_H
#define GB_EMULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../audio/gb_audio.h"
#include "../cartridge/gb_cartridge.h"
#include "../cgb/gb_cgb.h"
#include "../cpu/gb_cpu.h"
#include "../debug/gb_debug.h"
#include "../dma/gb_dma.h"
#include "../input/gb_input.h"
#include "../interrupt/gb_interrupt.h"
#include "../memory/gb_memory.h"
#include "../ppu/gb_ppu.h"
#include "../save/gb_save.h"
#include "../timer/gb_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_EMULATOR_BASE_CLOCK_HZ 4194304ULL
#define GB_EMULATOR_FRAME_T_CYCLES 70224u
#define GB_EMULATOR_MAX_CATCHUP_FRAMES 3u

typedef enum GB_EmulatorModeRequest {
    GB_EMULATOR_MODE_AUTO = 0,
    GB_EMULATOR_MODE_DMG,
    GB_EMULATOR_MODE_CGB
} GB_EmulatorModeRequest;

typedef struct GB_EmulatorConfig {
    GB_EmulatorModeRequest mode;
    GB_CartridgeLoadOptions cartridge_options;
    GB_CPU_STARTUP cpu_startup_override;
    bool use_cpu_startup_override;
    bool enable_save_ram;
    GB_DebugConfig debug_config;
} GB_EmulatorConfig;

typedef struct GB_Emulator {
    bool initialized;
    bool running;

    GB_EmulatorModeRequest requested_mode;
    GB_MemoryMode memory_mode;
    GB_CartridgeLoadOptions cartridge_options;
    GB_CPU_STARTUP cpu_startup_override;
    bool use_cpu_startup_override;
    bool enable_save_ram;
    GB_DebugConfig debug_config;

    GB_Cartridge cartridge;
    GB_Memory memory;
    GB_Interrupt interrupt;
    GB_PPU ppu;
    GB_Timer timer;
    GB_Input input;
    GB_DMA dma;
    GB_CGB cgb;
    GB_Audio audio;
    GB_SaveRAM save_ram;
    GB_Debug debug;
    GB_CPU cpu;

    bool cgb_initialized;
    bool save_ram_initialized;
    bool debug_initialized;
    uint64_t frame_t_cycles;
    uint64_t total_t_cycles;
    uint64_t frame_count;
} GB_Emulator;

void gb_emulator_config_default(GB_EmulatorConfig *config);

GB_Result gb_emulator_init(GB_Emulator *emulator,
                           const GB_EmulatorConfig *config,
                           GB_Error *error);

GB_Result gb_emulator_load_rom(GB_Emulator *emulator,
                               const char *path,
                               GB_Error *error);

GB_Result gb_emulator_load_rom_buffer(GB_Emulator *emulator,
                                      const uint8_t *data,
                                      size_t size,
                                      GB_Error *error);

GB_Result gb_emulator_reset(GB_Emulator *emulator, GB_Error *error);
GB_Result gb_emulator_destroy(GB_Emulator *emulator, GB_Error *error);

/* Execute one unit of emulation. The returned count is CPU-clock T-cycles
 * consumed by the CPU or by a CGB CPU-stall interval. Hardware peripherals
 * continue to run on the base Game Boy clock internally. */
GB_Result gb_emulator_step(GB_Emulator *emulator,
                           uint32_t *t_cycles,
                           GB_Error *error);

/* Run until a new PPU frame is completed, or until STOP leaves the CPU with no
 * emulatable work. This does not perform host timing or rendering. */
GB_Result gb_emulator_run_frame(GB_Emulator *emulator,
                                uint32_t *t_cycles,
                                bool *frame_ready,
                                GB_Error *error);

/* Advance the hardware without executing CPU instructions. Used for explicit
 * CGB DMA stall intervals. The granularity is deliberately 4 T-cycles. */
GB_Result gb_emulator_tick_hardware(GB_Emulator *emulator,
                                    uint32_t t_cycles,
                                    GB_Error *error);

bool gb_emulator_is_stopped(const GB_Emulator *emulator);
bool gb_emulator_is_halted(const GB_Emulator *emulator);
bool gb_emulator_is_cgb(const GB_Emulator *emulator);

/* Current CPU clock. DMG and normal-speed CGB use 4.194304 MHz; CGB
 * double-speed mode uses 8.388608 MHz. */
uint64_t gb_emulator_cpu_clock_hz(const GB_Emulator *emulator);

GB_Result gb_emulator_save_ram(GB_Emulator *emulator, GB_Error *error);
GB_Debug *gb_emulator_debug(GB_Emulator *emulator);
const GB_Debug *gb_emulator_debug_const(const GB_Emulator *emulator);

#ifdef __cplusplus
}
#endif

#endif /* GB_EMULATOR_H */
