#include "gb_cgb.h"

#include <stdio.h>
#include <string.h>

static void cgb_error(GB_Error *error, GB_Result code,
                      uint16_t address, const char *message)
{
    if (error == NULL) {
        return;
    }
    gb_error_clear(error);
    error->code = code;
    error->pc = address;
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static GB_Result require_cgb(const GB_CGB *cgb, GB_Error *error)
{
    if (cgb == NULL) {
        cgb_error(error, GB_RESULT_NULL_ARGUMENT, 0u, "CGB pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!cgb->initialized || cgb->memory == NULL) {
        cgb_error(error, GB_RESULT_BAD_STATE, 0u,
                  "CGB hardware is not initialized");
        return GB_RESULT_BAD_STATE;
    }
    if (cgb->memory->mode != GB_MEMORY_MODE_CGB) {
        cgb_error(error, GB_RESULT_UNSUPPORTED, 0u,
                  "CGB hardware requires CGB memory mode");
        return GB_RESULT_UNSUPPORTED;
    }
    return GB_RESULT_OK;
}

static bool source_range_valid(uint16_t address)
{
    return address <= 0x7FF0u ||
           (address >= 0xA000u && address <= 0xDFF0u);
}

static bool source_byte_range_valid(uint16_t address)
{
    return address <= 0x7FFFu ||
           (address >= 0xA000u && address <= 0xDFFFu);
}

static uint16_t hdma_source_from_registers(const GB_CGB *cgb)
{
    return (uint16_t)(((uint16_t)cgb->hdma1 << 8u) |
                      (uint16_t)(cgb->hdma2 & 0xF0u));
}

static uint16_t hdma_destination_from_registers(const GB_CGB *cgb)
{
    return (uint16_t)(0x8000u |
                      ((uint32_t)(cgb->hdma3 & 0x1Fu) << 8u) |
                      (uint16_t)(cgb->hdma4 & 0xF0u));
}

static void update_hdma_registers(GB_CGB *cgb)
{
    cgb->hdma1 = (uint8_t)(cgb->hdma_source >> 8u);
    cgb->hdma2 = (uint8_t)(cgb->hdma_source & 0xF0u);
    cgb->hdma3 = (uint8_t)((cgb->hdma_destination >> 8u) & 0x1Fu);
    cgb->hdma4 = (uint8_t)(cgb->hdma_destination & 0xF0u);
}

static uint8_t hdma_read_status(const GB_CGB *cgb)
{
    return cgb->hdma5_status;
}

static void finish_hdma(GB_CGB *cgb)
{
    cgb->hdma_active = false;
    cgb->hdma_hblank_mode = false;
    cgb->hdma_blocks_remaining = 0u;
    cgb->hdma5_status = 0xFFu;
}

static GB_Result copy_hdma_block(GB_CGB *cgb, GB_Error *error)
{
    if (cgb->hdma_blocks_remaining == 0u) {
        finish_hdma(cgb);
        return GB_RESULT_OK;
    }

    if (!source_range_valid(cgb->hdma_source) ||
        !source_byte_range_valid((uint16_t)(cgb->hdma_source + 15u))) {
        cgb_error(error, GB_RESULT_INVALID_ARGUMENT, cgb->hdma_source,
                  "HDMA source address leaves the supported CGB source ranges");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    if (cgb->hdma_destination > 0x9FF0u) {
        finish_hdma(cgb);
        return GB_RESULT_OK;
    }

    for (uint16_t i = 0u; i < GB_CGB_HDMA_BLOCK_BYTES; ++i) {
        uint8_t value = 0xFFu;
        GB_Result result = gb_memory_dma_read8(
            cgb->memory,
            (uint16_t)(cgb->hdma_source + i),
            &value,
            error);
        if (result != GB_RESULT_OK) {
            return result;
        }

        result = gb_memory_dma_write_vram8(
            cgb->memory,
            cgb->hdma_vram_bank,
            (uint16_t)((cgb->hdma_destination - 0x8000u) + i),
            value,
            error);
        if (result != GB_RESULT_OK) {
            return result;
        }
    }

    cgb->hdma_source = (uint16_t)(cgb->hdma_source + GB_CGB_HDMA_BLOCK_BYTES);
    cgb->hdma_destination = (uint16_t)(cgb->hdma_destination + GB_CGB_HDMA_BLOCK_BYTES);
    cgb->hdma_blocks_remaining = (uint8_t)(cgb->hdma_blocks_remaining - 1u);
    update_hdma_registers(cgb);

    if (cgb->hdma_blocks_remaining == 0u || cgb->hdma_destination > 0x9FF0u) {
        finish_hdma(cgb);
    } else {
        cgb->hdma5_status = (uint8_t)((cgb->hdma_blocks_remaining - 1u) & 0x7Fu);
    }

    cgb->cpu_stall_t_cycles += GB_CGB_HDMA_BLOCK_T_CYCLES;
    return GB_RESULT_OK;
}

static GB_Result start_hdma(GB_CGB *cgb, bool hblank, uint8_t length_mode,
                            GB_Error *error)
{
    uint8_t block_count = (uint8_t)((length_mode & 0x7Fu) + 1u);
    uint16_t source = hdma_source_from_registers(cgb);
    uint16_t destination = hdma_destination_from_registers(cgb);

    if (!source_range_valid(source)) {
        cgb_error(error, GB_RESULT_INVALID_ARGUMENT, source,
                  "HDMA source address is not aligned to a supported CGB source range");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    if (destination < 0x8000u || destination > 0x9FF0u ||
        (destination & 0x000Fu) != 0u) {
        cgb_error(error, GB_RESULT_INVALID_ARGUMENT, destination,
                  "HDMA destination is outside CGB VRAM");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    cgb->hdma_source = source;
    cgb->hdma_destination = destination;
    cgb->hdma_vram_bank = cgb->memory->vram_bank;
    cgb->hdma_blocks_remaining = block_count;
    cgb->hdma_active = true;
    cgb->hdma_hblank_mode = hblank;
    cgb->hdma5_status = (uint8_t)(length_mode & 0x7Fu);

    if (!hblank) {
        while (cgb->hdma_active) {
            GB_Result result = copy_hdma_block(cgb, error);
            if (result != GB_RESULT_OK) {
                return result;
            }
        }
    }

    return GB_RESULT_OK;
}

static GB_Result cgb_tick_hblank(GB_CGB *cgb, GB_Error *error)
{
    if (!cgb->hdma_active || !cgb->hdma_hblank_mode || cgb->ppu == NULL) {
        return GB_RESULT_OK;
    }

    GB_PPU_Mode mode = gb_ppu_get_mode(cgb->ppu);
    uint8_t ly = gb_ppu_get_ly(cgb->ppu);

    if (mode != GB_PPU_MODE_HBLANK || ly >= GB_PPU_VISIBLE_SCANLINES) {
        cgb->hblank_hdma_block_done = false;
    }

    /* A HBlank transfer may be blocked while the CPU is halted, but it must
     * resume later in the same HBlank once the CPU wakes. Therefore we track
     * whether this HBlank's 16-byte block has actually been transferred
     * instead of treating the HBlank transition itself as the only chance. */
    if (mode == GB_PPU_MODE_HBLANK && ly < GB_PPU_VISIBLE_SCANLINES &&
        !cgb->hblank_hdma_block_done) {
        if (cgb->cpu == NULL || !gb_cpu_is_halted(cgb->cpu)) {
            GB_Result result = copy_hdma_block(cgb, error);
            if (result != GB_RESULT_OK) return result;
            cgb->hblank_hdma_block_done = true;
        }
    }

    cgb->previous_ppu_mode = mode;
    cgb->previous_ppu_mode_valid = true;
    return GB_RESULT_OK;
}

static GB_Result key1_read(const GB_CGB *cgb, uint8_t *value, GB_Error *error)
{
    if (value == NULL) {
        cgb_error(error, GB_RESULT_NULL_ARGUMENT, GB_CGB_ADDR_KEY1,
                  "KEY1 read output is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    *value = (uint8_t)(0x7Eu |
                       (cgb->speed == GB_CGB_SPEED_DOUBLE ? 0x80u : 0u) |
                       (cgb->speed_switch_prepared ? 0x01u : 0u));
    return GB_RESULT_OK;
}

GB_Result gb_cgb_read8(void *user, uint16_t address, uint8_t *value,
                       GB_Error *error)
{
    gb_error_clear(error);
    GB_CGB *cgb = (GB_CGB *)user;
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;
    if (value == NULL) {
        cgb_error(error, GB_RESULT_NULL_ARGUMENT, address,
                  "CGB register read output is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    switch (address) {
    case GB_CGB_ADDR_KEY1:
        return key1_read(cgb, value, error);
    case GB_CGB_ADDR_HDMA1:
    case GB_CGB_ADDR_HDMA2:
    case GB_CGB_ADDR_HDMA3:
    case GB_CGB_ADDR_HDMA4:
        *value = 0xFFu;
        return GB_RESULT_OK;
    case GB_CGB_ADDR_HDMA5:
        *value = hdma_read_status(cgb);
        return GB_RESULT_OK;
    case GB_CGB_ADDR_RP: {
        uint8_t read_data = cgb->ir_input_active ? 0x00u : 0x02u;
        *value = (uint8_t)(0xC0u | read_data | (cgb->ir_led_on ? 0x01u : 0u));
        return GB_RESULT_OK;
    }
    default:
        cgb_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                  "Invalid CGB register address");
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_Result gb_cgb_write8(void *user, uint16_t address, uint8_t value,
                        GB_Error *error)
{
    gb_error_clear(error);
    GB_CGB *cgb = (GB_CGB *)user;
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;

    switch (address) {
    case GB_CGB_ADDR_KEY1:
        cgb->speed_switch_prepared = (value & 0x01u) != 0u;
        return GB_RESULT_OK;

    case GB_CGB_ADDR_HDMA1:
        cgb->hdma1 = value;
        return GB_RESULT_OK;
    case GB_CGB_ADDR_HDMA2:
        cgb->hdma2 = (uint8_t)(value & 0xF0u);
        return GB_RESULT_OK;
    case GB_CGB_ADDR_HDMA3:
        cgb->hdma3 = (uint8_t)(value & 0x1Fu);
        return GB_RESULT_OK;
    case GB_CGB_ADDR_HDMA4:
        cgb->hdma4 = (uint8_t)(value & 0xF0u);
        return GB_RESULT_OK;
    case GB_CGB_ADDR_HDMA5:
        if (cgb->hdma_active && cgb->hdma_hblank_mode && (value & 0x80u) == 0u) {
            cgb->hdma_active = false;
            cgb->hdma_hblank_mode = false;
            cgb->hdma5_status = (uint8_t)(0x80u |
                                          ((cgb->hdma_blocks_remaining - 1u) & 0x7Fu));
            return GB_RESULT_OK;
        }
        if (cgb->hdma_active) {
            return GB_RESULT_OK;
        }
        return start_hdma(cgb, (value & 0x80u) != 0u,
                          (uint8_t)(value & 0x7Fu), error);

    case GB_CGB_ADDR_RP:
        cgb->rp_control = value;
        cgb->ir_led_on = (value & 0x01u) != 0u;
        return GB_RESULT_OK;

    default:
        cgb_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                  "Invalid CGB register address");
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_Result gb_cgb_init(GB_CGB *cgb, GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);
    if (cgb == NULL || memory == NULL) {
        cgb_error(error, GB_RESULT_NULL_ARGUMENT, 0u,
                  "CGB and memory pointers are required");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (memory->mode != GB_MEMORY_MODE_CGB) {
        cgb_error(error, GB_RESULT_UNSUPPORTED, 0u,
                  "CGB hardware cannot be initialized in DMG memory mode");
        return GB_RESULT_UNSUPPORTED;
    }

    memset(cgb, 0, sizeof(*cgb));
    cgb->memory = memory;
    cgb->initialized = true;
    cgb->speed = GB_CGB_SPEED_NORMAL;
    cgb->hdma5_status = 0xFFu;
    cgb->rp_control = 0x00u;

    GB_Result result = gb_memory_map_io_device(memory,
                                               GB_CGB_ADDR_KEY1,
                                               GB_CGB_ADDR_KEY1,
                                               cgb,
                                               gb_cgb_read8,
                                               gb_cgb_write8,
                                               NULL,
                                               &cgb->key1_device,
                                               error);
    if (result != GB_RESULT_OK) goto fail;

    result = gb_memory_map_io_device(memory,
                                     GB_CGB_ADDR_HDMA1,
                                     GB_CGB_ADDR_HDMA5,
                                     cgb,
                                     gb_cgb_read8,
                                     gb_cgb_write8,
                                     gb_cgb_device_tick,
                                     &cgb->hdma_device,
                                     error);
    if (result != GB_RESULT_OK) goto fail_key1;

    result = gb_memory_map_io_device(memory,
                                     GB_CGB_ADDR_RP,
                                     GB_CGB_ADDR_RP,
                                     cgb,
                                     gb_cgb_read8,
                                     gb_cgb_write8,
                                     NULL,
                                     &cgb->rp_device,
                                     error);
    if (result != GB_RESULT_OK) goto fail_hdma;

    cgb->mapped = true;
    return gb_cgb_reset(cgb, error);

fail_hdma:
    (void)gb_memory_unmap_io_device(memory, cgb->hdma_device, NULL);
fail_key1:
    (void)gb_memory_unmap_io_device(memory, cgb->key1_device, NULL);
fail:
    memset(cgb, 0, sizeof(*cgb));
    return result;
}

GB_Result gb_cgb_reset(GB_CGB *cgb, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;

    cgb->speed = GB_CGB_SPEED_NORMAL;
    cgb->speed_switch_prepared = false;
    cgb->hdma1 = 0u;
    cgb->hdma2 = 0u;
    cgb->hdma3 = 0u;
    cgb->hdma4 = 0u;
    cgb->hdma5_status = 0xFFu;
    cgb->hdma_active = false;
    cgb->hdma_hblank_mode = false;
    cgb->hdma_source = 0u;
    cgb->hdma_destination = 0x8000u;
    cgb->hdma_vram_bank = 0u;
    cgb->hdma_blocks_remaining = 0u;
    cgb->cpu_stall_t_cycles = 0u;
    cgb->speed_switch_pause_t_cycles = 0u;
    cgb->hblank_hdma_block_done = false;
    cgb->previous_ppu_mode = GB_PPU_MODE_HBLANK;
    cgb->previous_ppu_mode_valid = false;
    cgb->rp_control = 0u;
    cgb->ir_input_active = false;
    cgb->ir_led_on = false;

    if (cgb->timer != NULL) {
        result = gb_timer_set_double_speed(cgb->timer, false, error);
        if (result != GB_RESULT_OK) return result;
    }
    if (cgb->dma != NULL) {
        result = gb_dma_set_cgb_double_speed(cgb->dma, false, error);
        if (result != GB_RESULT_OK) return result;
    }
    return GB_RESULT_OK;
}

GB_Result gb_cgb_destroy(GB_CGB *cgb, GB_Error *error)
{
    gb_error_clear(error);
    if (cgb == NULL) {
        cgb_error(error, GB_RESULT_NULL_ARGUMENT, 0u, "CGB pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (cgb->mapped && cgb->memory != NULL) {
        GB_Result result = gb_memory_unmap_io_device(cgb->memory, cgb->rp_device, error);
        if (result != GB_RESULT_OK) return result;
        result = gb_memory_unmap_io_device(cgb->memory, cgb->hdma_device, error);
        if (result != GB_RESULT_OK) return result;
        result = gb_memory_unmap_io_device(cgb->memory, cgb->key1_device, error);
        if (result != GB_RESULT_OK) return result;
    }
    memset(cgb, 0, sizeof(*cgb));
    return GB_RESULT_OK;
}

GB_Result gb_cgb_attach_cpu(GB_CGB *cgb, GB_CPU *cpu, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;
    if (cpu == NULL) {
        cgb_error(error, GB_RESULT_NULL_ARGUMENT, 0u, "CPU pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    cgb->cpu = cpu;
    return GB_RESULT_OK;
}

GB_Result gb_cgb_attach_timer(GB_CGB *cgb, GB_Timer *timer, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;
    cgb->timer = timer;
    return timer != NULL ? gb_timer_set_double_speed(timer,
                                                     cgb->speed == GB_CGB_SPEED_DOUBLE,
                                                     error)
                         : GB_RESULT_OK;
}

GB_Result gb_cgb_attach_dma(GB_CGB *cgb, GB_DMA *dma, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;
    cgb->dma = dma;
    return dma != NULL ? gb_dma_set_cgb_double_speed(dma,
                                                     cgb->speed == GB_CGB_SPEED_DOUBLE,
                                                     error)
                       : GB_RESULT_OK;
}

GB_Result gb_cgb_attach_ppu(GB_CGB *cgb, GB_PPU *ppu, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;
    cgb->ppu = ppu;
    cgb->previous_ppu_mode_valid = false;
    return GB_RESULT_OK;
}

GB_Result gb_cgb_set_speed(GB_CGB *cgb, GB_CGBSpeed speed, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;
    if (speed != GB_CGB_SPEED_NORMAL && speed != GB_CGB_SPEED_DOUBLE) {
        cgb_error(error, GB_RESULT_INVALID_ARGUMENT, GB_CGB_ADDR_KEY1,
                  "Invalid CGB CPU speed");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    cgb->speed = speed;
    if (cgb->timer != NULL) {
        result = gb_timer_set_double_speed(cgb->timer,
                                           speed == GB_CGB_SPEED_DOUBLE,
                                           error);
        if (result != GB_RESULT_OK) return result;
    }
    if (cgb->dma != NULL) {
        result = gb_dma_set_cgb_double_speed(cgb->dma,
                                             speed == GB_CGB_SPEED_DOUBLE,
                                             error);
        if (result != GB_RESULT_OK) return result;
    }
    return GB_RESULT_OK;
}

GB_Result gb_cgb_prepare_speed_switch(GB_CGB *cgb, bool prepare, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;
    cgb->speed_switch_prepared = prepare;
    return GB_RESULT_OK;
}

GB_Result gb_cgb_handle_cpu_stop(GB_CGB *cgb, bool *speed_switched,
                                 GB_Error *error)
{
    gb_error_clear(error);
    if (speed_switched != NULL) *speed_switched = false;
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;

    if (cgb->cpu == NULL || !gb_cpu_is_stopped(cgb->cpu)) {
        return GB_RESULT_OK;
    }
    if (!cgb->speed_switch_prepared) {
        return GB_RESULT_OK;
    }

    GB_CGBSpeed next = cgb->speed == GB_CGB_SPEED_NORMAL
                           ? GB_CGB_SPEED_DOUBLE
                           : GB_CGB_SPEED_NORMAL;
    result = gb_cgb_set_speed(cgb, next, error);
    if (result != GB_RESULT_OK) return result;

    cgb->speed_switch_prepared = false;
    cgb->speed_switch_pause_t_cycles = 8200u;
    if (speed_switched != NULL) *speed_switched = true;
    return GB_RESULT_OK;
}

GB_Result gb_cgb_tick(GB_CGB *cgb, uint32_t t_cycles, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;

    if (cgb->cpu_stall_t_cycles != 0u && t_cycles != 0u) {
        uint32_t consumed = t_cycles < cgb->cpu_stall_t_cycles
                                ? t_cycles
                                : cgb->cpu_stall_t_cycles;
        cgb->cpu_stall_t_cycles -= consumed;
    }

    return cgb_tick_hblank(cgb, error);
}

GB_Result gb_cgb_device_tick(void *user, uint32_t t_cycles, GB_Error *error)
{
    return gb_cgb_tick((GB_CGB *)user, t_cycles, error);
}

GB_CGBSpeed gb_cgb_get_speed(const GB_CGB *cgb)
{
    return (cgb != NULL && cgb->initialized) ? cgb->speed : GB_CGB_SPEED_NORMAL;
}

bool gb_cgb_speed_switch_prepared(const GB_CGB *cgb)
{
    return cgb != NULL && cgb->initialized && cgb->speed_switch_prepared;
}

bool gb_cgb_hdma_active(const GB_CGB *cgb)
{
    return cgb != NULL && cgb->initialized && cgb->hdma_active;
}

uint8_t gb_cgb_hdma_blocks_remaining(const GB_CGB *cgb)
{
    return (cgb != NULL && cgb->initialized) ? cgb->hdma_blocks_remaining : 0u;
}

uint32_t gb_cgb_cpu_stall_t_cycles(const GB_CGB *cgb)
{
    return (cgb != NULL && cgb->initialized) ? cgb->cpu_stall_t_cycles : 0u;
}

bool gb_cgb_cpu_is_stalled(const GB_CGB *cgb)
{
    return cgb != NULL && cgb->initialized && cgb->cpu_stall_t_cycles != 0u;
}

bool gb_cgb_speed_switch_paused(const GB_CGB *cgb)
{
    return cgb != NULL && cgb->initialized && cgb->speed_switch_pause_t_cycles != 0u;
}

GB_Result gb_cgb_set_ir_input(GB_CGB *cgb, bool active, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_cgb(cgb, error);
    if (result != GB_RESULT_OK) return result;
    cgb->ir_input_active = active;
    return GB_RESULT_OK;
}

bool gb_cgb_ir_led_on(const GB_CGB *cgb)
{
    return cgb != NULL && cgb->initialized && cgb->ir_led_on;
}
