#include "gb_dma.h"

#include <stdio.h>
#include <string.h>

static void dma_error(GB_Error *error, GB_Result code,
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

static GB_Result require_dma(GB_DMA *dma, GB_Error *error)
{
    if (dma == NULL) {
        dma_error(error, GB_RESULT_NULL_ARGUMENT, GB_DMA_ADDR,
                  "DMA pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!dma->initialized || dma->memory == NULL) {
        dma_error(error, GB_RESULT_BAD_STATE, GB_DMA_ADDR,
                  "DMA subsystem is not initialized with memory");
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static bool dma_cpu_access_allowed(void *user, uint16_t address, bool write)
{
    (void)write;

    GB_DMA *dma = (GB_DMA *)user;
    if (dma == NULL || !dma->initialized || !dma->active) {
        return true;
    }

    if (address >= 0xFF80u && address <= 0xFFFEu) {
        return true;
    }

    if (dma->memory->mode == GB_MEMORY_MODE_DMG) {
        return false;
    }

    uint16_t source = (uint16_t)((uint16_t)dma->source_high << 8u);
    bool source_is_cartridge = source <= 0x7FFFu ||
                               (source >= 0xA000u && source <= 0xBFFFu);
    bool source_is_wram = (source >= 0xC000u && source <= 0xDFFFu);

    if (source_is_cartridge) {
        return address >= 0xC000u && address <= 0xFDFFu;
    }

    if (source_is_wram) {
        return address <= 0x7FFFu ||
               (address >= 0xA000u && address <= 0xBFFFu);
    }

    return false;
}

static uint32_t dma_cycles_per_byte(const GB_DMA *dma)
{
    return dma->cgb_double_speed ? 2u : 4u;
}

static uint16_t dma_start_delay(const GB_DMA *dma)
{
    return (uint16_t)(dma->cgb_double_speed ? 2u : 4u);
}

static GB_Result copy_one_byte(GB_DMA *dma, GB_Error *error)
{
    if (dma->transfer_index >= GB_DMA_TRANSFER_BYTES) {
        dma->active = false;
        return GB_RESULT_OK;
    }

    uint16_t source = (uint16_t)(((uint16_t)dma->source_high << 8u) |
                                 dma->transfer_index);
    uint8_t value = 0xFFu;
    GB_Result result = GB_RESULT_OK;
    if (dma->source_high <= 0xDFu) {
        result = gb_memory_dma_read8(dma->memory, source, &value, error);
    }
    if (result != GB_RESULT_OK) {
        return result;
    }

    result = gb_memory_dma_write_oam(dma->memory, dma->transfer_index, value, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    dma->transfer_index = (uint16_t)(dma->transfer_index + 1u);
    if (dma->transfer_index >= GB_DMA_TRANSFER_BYTES) {
        dma->active = false;
    }
    return GB_RESULT_OK;
}

GB_Result gb_dma_init(GB_DMA *dma, GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);

    if (dma == NULL || memory == NULL) {
        dma_error(error, GB_RESULT_NULL_ARGUMENT, GB_DMA_ADDR,
                  "DMA and memory pointers are required");
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(dma, 0, sizeof(*dma));
    dma->memory = memory;
    dma->initialized = true;

    GB_Result result = gb_memory_map_io_device(memory,
                                               GB_DMA_ADDR,
                                               GB_DMA_ADDR,
                                               dma,
                                               gb_dma_read8,
                                               gb_dma_write8,
                                               gb_dma_device_tick,
                                               &dma->io_device_index,
                                               error);
    if (result != GB_RESULT_OK) {
        memset(dma, 0, sizeof(*dma));
        return result;
    }

    GB_MemoryDMAController controller = gb_dma_memory_controller(dma);
    result = gb_memory_set_dma_controller(memory, &controller, error);
    if (result != GB_RESULT_OK) {
        (void)gb_memory_unmap_io_device(memory, dma->io_device_index, NULL);
        memset(dma, 0, sizeof(*dma));
        return result;
    }

    dma->mapped = true;
    return gb_dma_reset(dma, error);
}

GB_Result gb_dma_reset(GB_DMA *dma, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_dma(dma, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    dma->active = false;
    dma->cgb_double_speed = false;
    dma->source_high = 0u;
    dma->transfer_index = 0u;
    dma->startup_delay = 0u;
    dma->cycle_accumulator = 0u;
    return GB_RESULT_OK;
}

GB_Result gb_dma_destroy(GB_DMA *dma, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_dma(dma, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (dma->mapped) {
        result = gb_memory_unmap_io_device(dma->memory,
                                           dma->io_device_index,
                                           error);
        if (result != GB_RESULT_OK) {
            return result;
        }

        result = gb_memory_clear_dma_controller(dma->memory, error);
        if (result != GB_RESULT_OK) {
            return result;
        }
    }

    memset(dma, 0, sizeof(*dma));
    return GB_RESULT_OK;
}

GB_Result gb_dma_tick(GB_DMA *dma, uint32_t t_cycles, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_dma(dma, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (!dma->active || t_cycles == 0u) {
        return GB_RESULT_OK;
    }

    uint32_t remaining = t_cycles;

    if (dma->startup_delay != 0u) {
        uint16_t consumed = (remaining >= dma->startup_delay)
                                ? dma->startup_delay
                                : (uint16_t)remaining;
        dma->startup_delay = (uint16_t)(dma->startup_delay - consumed);
        remaining -= consumed;
        if (remaining == 0u) {
            return GB_RESULT_OK;
        }
    }

    uint32_t cycles_per_byte = dma_cycles_per_byte(dma);
    uint32_t accumulated = (uint32_t)dma->cycle_accumulator + remaining;
    uint32_t byte_count = accumulated / cycles_per_byte;
    dma->cycle_accumulator = (uint8_t)(accumulated % cycles_per_byte);

    while (byte_count > 0u && dma->active) {
        result = copy_one_byte(dma, error);
        if (result != GB_RESULT_OK) {
            return result;
        }
        --byte_count;
    }

    return GB_RESULT_OK;
}

GB_Result gb_dma_set_cgb_double_speed(GB_DMA *dma, bool enabled, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_dma(dma, error);
    if (result != GB_RESULT_OK) return result;

    dma->cgb_double_speed = enabled;
    return GB_RESULT_OK;
}

GB_Result gb_dma_device_tick(void *user, uint32_t t_cycles, GB_Error *error)
{
    return gb_dma_tick((GB_DMA *)user, t_cycles, error);
}

GB_Result gb_dma_read8(void *user, uint16_t address,
                       uint8_t *value, GB_Error *error)
{
    gb_error_clear(error);

    GB_DMA *dma = (GB_DMA *)user;
    GB_Result result = require_dma(dma, error);
    if (result != GB_RESULT_OK) {
        return result;
    }
    if (address != GB_DMA_ADDR || value == NULL) {
        dma_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                  "Invalid DMA register access");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    *value = dma->source_high;
    return GB_RESULT_OK;
}

GB_Result gb_dma_write8(void *user, uint16_t address,
                        uint8_t value, GB_Error *error)
{
    gb_error_clear(error);

    GB_DMA *dma = (GB_DMA *)user;
    GB_Result result = require_dma(dma, error);
    if (result != GB_RESULT_OK) {
        return result;
    }
    if (address != GB_DMA_ADDR) {
        dma_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                  "Invalid DMA register access");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    dma->source_high = value;
    dma->transfer_index = 0u;
    dma->startup_delay = dma_start_delay(dma);
    dma->cycle_accumulator = 0u;
    dma->active = true;
    return GB_RESULT_OK;
}

GB_MemoryDMAController gb_dma_memory_controller(GB_DMA *dma)
{
    GB_MemoryDMAController controller;
    controller.user = dma;
    controller.cpu_access_allowed = dma_cpu_access_allowed;
    return controller;
}

bool gb_dma_is_active(const GB_DMA *dma)
{
    return dma != NULL && dma->initialized && dma->active;
}

uint16_t gb_dma_bytes_transferred(const GB_DMA *dma)
{
    return dma != NULL && dma->initialized ? dma->transfer_index : 0u;
}

uint16_t gb_dma_source_address(const GB_DMA *dma)
{
    if (dma == NULL || !dma->initialized) {
        return (uint16_t)0u;
    }
    return (uint16_t)((uint16_t)dma->source_high << 8u);
}
