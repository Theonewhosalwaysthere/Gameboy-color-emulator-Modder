#include "gb_interrupt.h"

#include <stdio.h>
#include <string.h>

static void interrupt_error(GB_Error *error, GB_Result code,
                            const char *message, uint16_t address)
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

static GB_Result require_interrupts(const GB_Interrupt *interrupts,
                                    GB_Error *error)
{
    if (interrupts == NULL) {
        interrupt_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Interrupt controller pointer is NULL", 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (!interrupts->initialized) {
        interrupt_error(error, GB_RESULT_BAD_STATE,
                        "Interrupt controller is not initialized", 0);
        return GB_RESULT_BAD_STATE;
    }

    return GB_RESULT_OK;
}

static bool is_single_bit(uint8_t mask)
{
    return mask != 0u && (mask & (uint8_t)(mask - 1u)) == 0u;
}

GB_Result gb_interrupt_init(GB_Interrupt *interrupts, GB_Error *error)
{
    gb_error_clear(error);

    if (interrupts == NULL) {
        interrupt_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Interrupt controller pointer is NULL", 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(interrupts, 0, sizeof(*interrupts));
    interrupts->initialized = true;
    return GB_RESULT_OK;
}

GB_Result gb_interrupt_reset(GB_Interrupt *interrupts, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    interrupts->interrupt_flags = 0u;
    interrupts->interrupt_enable = 0u;
    return GB_RESULT_OK;
}

GB_Result gb_interrupt_connect_memory(GB_Interrupt *interrupts,
                                      GB_Memory *memory,
                                      GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (memory == NULL) {
        interrupt_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Memory pointer is NULL", GB_ADDR_IF);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (interrupts->memory_connected && interrupts->memory != memory) {
        interrupt_error(error, GB_RESULT_BAD_STATE,
                        "Interrupt controller is already connected to a different memory instance",
                        GB_ADDR_IF);
        return GB_RESULT_BAD_STATE;
    }

    GB_MemoryInterruptController controller = gb_interrupt_memory_controller(interrupts);
    result = gb_memory_set_interrupt_controller(memory, &controller, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    interrupts->memory = memory;
    interrupts->memory_connected = true;
    return GB_RESULT_OK;
}

GB_Result gb_interrupt_disconnect_memory(GB_Interrupt *interrupts,
                                         GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (!interrupts->memory_connected || interrupts->memory == NULL) {
        interrupt_error(error, GB_RESULT_BAD_STATE,
                        "Interrupt controller is not connected to memory", 0);
        return GB_RESULT_BAD_STATE;
    }

    result = gb_memory_clear_interrupt_controller(interrupts->memory, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    interrupts->memory = NULL;
    interrupts->memory_connected = false;
    return GB_RESULT_OK;
}

GB_Result gb_interrupt_read8(void *user, uint16_t address,
                             uint8_t *value, GB_Error *error)
{
    gb_error_clear(error);

    GB_Interrupt *interrupts = (GB_Interrupt *)user;
    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (value == NULL) {
        interrupt_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Interrupt register read output pointer is NULL", address);
        return GB_RESULT_NULL_ARGUMENT;
    }

    switch (address) {
    case GB_ADDR_IF:
        /* IF bits 7-5 are not storage and read back high. */
        *value = (uint8_t)(interrupts->interrupt_flags | 0xE0u);
        return GB_RESULT_OK;

    case GB_ADDR_IE:
        /* IE only exposes five interrupt-enable bits; unused bits read high. */
        *value = (uint8_t)(interrupts->interrupt_enable | 0xE0u);
        return GB_RESULT_OK;

    default:
        interrupt_error(error, GB_RESULT_INVALID_ARGUMENT,
                        "Invalid interrupt-controller register address", address);
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_Result gb_interrupt_write8(void *user, uint16_t address,
                              uint8_t value, GB_Error *error)
{
    gb_error_clear(error);

    GB_Interrupt *interrupts = (GB_Interrupt *)user;
    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    switch (address) {
    case GB_ADDR_IF:
        /* Upper three IF bits are unused. */
        interrupts->interrupt_flags = (uint8_t)(value & GB_INTERRUPT_VALID_MASK);
        return GB_RESULT_OK;

    case GB_ADDR_IE:
        /* Upper three IE bits are unused. */
        interrupts->interrupt_enable = (uint8_t)(value & GB_INTERRUPT_VALID_MASK);
        return GB_RESULT_OK;

    default:
        interrupt_error(error, GB_RESULT_INVALID_ARGUMENT,
                        "Invalid interrupt-controller register address", address);
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_Result gb_interrupt_get_pending(void *user, uint8_t *pending_mask,
                                   GB_Error *error)
{
    gb_error_clear(error);

    GB_Interrupt *interrupts = (GB_Interrupt *)user;
    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (pending_mask == NULL) {
        interrupt_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Pending interrupt output pointer is NULL", GB_ADDR_IF);
        return GB_RESULT_NULL_ARGUMENT;
    }

    *pending_mask = gb_interrupt_pending_mask(interrupts);
    return GB_RESULT_OK;
}

GB_Result gb_interrupt_acknowledge(void *user, uint8_t interrupt_mask,
                                   GB_Error *error)
{
    gb_error_clear(error);

    GB_Interrupt *interrupts = (GB_Interrupt *)user;
    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (!is_single_bit(interrupt_mask) ||
        (interrupt_mask & (uint8_t)~GB_INTERRUPT_VALID_MASK) != 0u) {
        interrupt_error(error, GB_RESULT_INVALID_ARGUMENT,
                        "Interrupt acknowledge requires exactly one valid interrupt bit",
                        GB_ADDR_IF);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    interrupts->interrupt_flags &= (uint8_t)~interrupt_mask;
    return GB_RESULT_OK;
}

GB_Result gb_interrupt_request(void *user, uint8_t interrupt_mask,
                               GB_Error *error)
{
    gb_error_clear(error);

    GB_Interrupt *interrupts = (GB_Interrupt *)user;
    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if ((interrupt_mask & (uint8_t)~GB_INTERRUPT_VALID_MASK) != 0u) {
        interrupt_error(error, GB_RESULT_INVALID_ARGUMENT,
                        "Interrupt request contains invalid bits", GB_ADDR_IF);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    interrupts->interrupt_flags |= interrupt_mask;
    return GB_RESULT_OK;
}

GB_Result gb_interrupt_request_source(GB_Interrupt *interrupts,
                                      GB_InterruptSource source,
                                      GB_Error *error)
{
    gb_error_clear(error);

    uint8_t mask = gb_interrupt_source_mask(source);
    if (mask == 0u) {
        interrupt_error(error, GB_RESULT_INVALID_ARGUMENT,
                        "Invalid interrupt source", GB_ADDR_IF);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    return gb_interrupt_request(interrupts, mask, error);
}

GB_Result gb_interrupt_clear_source(GB_Interrupt *interrupts,
                                    GB_InterruptSource source,
                                    GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_interrupts(interrupts, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    uint8_t mask = gb_interrupt_source_mask(source);
    if (mask == 0u) {
        interrupt_error(error, GB_RESULT_INVALID_ARGUMENT,
                        "Invalid interrupt source", GB_ADDR_IF);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    interrupts->interrupt_flags &= (uint8_t)~mask;
    return GB_RESULT_OK;
}

uint8_t gb_interrupt_pending_mask(const GB_Interrupt *interrupts)
{
    if (interrupts == NULL || !interrupts->initialized) {
        return 0u;
    }

    return (uint8_t)((interrupts->interrupt_flags & interrupts->interrupt_enable) &
                     GB_INTERRUPT_VALID_MASK);
}

uint8_t gb_interrupt_requested_mask(const GB_Interrupt *interrupts)
{
    if (interrupts == NULL || !interrupts->initialized) {
        return 0u;
    }

    return (uint8_t)(interrupts->interrupt_flags & GB_INTERRUPT_VALID_MASK);
}

uint8_t gb_interrupt_enabled_mask(const GB_Interrupt *interrupts)
{
    if (interrupts == NULL || !interrupts->initialized) {
        return 0u;
    }

    return (uint8_t)(interrupts->interrupt_enable & GB_INTERRUPT_VALID_MASK);
}

int gb_interrupt_highest_priority_source(uint8_t pending_mask)
{
    pending_mask &= GB_INTERRUPT_VALID_MASK;

    for (int source = 0; source < (int)GB_INTERRUPT_SOURCE_COUNT; ++source) {
        uint8_t mask = (uint8_t)(1u << (unsigned)source);
        if ((pending_mask & mask) != 0u) {
            return source;
        }
    }

    return -1;
}

uint8_t gb_interrupt_source_mask(GB_InterruptSource source)
{
    if (source < GB_INTERRUPT_SOURCE_VBLANK ||
        source >= GB_INTERRUPT_SOURCE_COUNT_ENUM) {
        return 0u;
    }

    return (uint8_t)(1u << (unsigned)source);
}

uint16_t gb_interrupt_source_vector(GB_InterruptSource source)
{
    switch (source) {
    case GB_INTERRUPT_SOURCE_VBLANK: return 0x0040u;
    case GB_INTERRUPT_SOURCE_STAT:   return 0x0048u;
    case GB_INTERRUPT_SOURCE_TIMER:  return 0x0050u;
    case GB_INTERRUPT_SOURCE_SERIAL: return 0x0058u;
    case GB_INTERRUPT_SOURCE_JOYPAD: return 0x0060u;
    default:                         return 0u;
    }
}

const char *gb_interrupt_source_name(GB_InterruptSource source)
{
    switch (source) {
    case GB_INTERRUPT_SOURCE_VBLANK: return "VBlank";
    case GB_INTERRUPT_SOURCE_STAT:   return "STAT";
    case GB_INTERRUPT_SOURCE_TIMER:  return "Timer";
    case GB_INTERRUPT_SOURCE_SERIAL: return "Serial";
    case GB_INTERRUPT_SOURCE_JOYPAD: return "Joypad";
    default:                         return "Invalid";
    }
}

GB_MemoryInterruptController gb_interrupt_memory_controller(GB_Interrupt *interrupts)
{
    GB_MemoryInterruptController controller;
    memset(&controller, 0, sizeof(controller));

    controller.user = interrupts;
    controller.read8 = gb_interrupt_read8;
    controller.write8 = gb_interrupt_write8;
    controller.get_pending_interrupts = gb_interrupt_get_pending;
    controller.acknowledge_interrupt = gb_interrupt_acknowledge;
    controller.request_interrupt = gb_interrupt_request;
    return controller;
}
