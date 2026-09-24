#ifndef GB_INTERRUPT_H
#define GB_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

#include "../memory/gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_INTERRUPT_VALID_MASK 0x1Fu
#define GB_INTERRUPT_SOURCE_COUNT 5u

typedef enum GB_InterruptSource {
    GB_INTERRUPT_SOURCE_VBLANK = 0,
    GB_INTERRUPT_SOURCE_STAT,
    GB_INTERRUPT_SOURCE_TIMER,
    GB_INTERRUPT_SOURCE_SERIAL,
    GB_INTERRUPT_SOURCE_JOYPAD,
    GB_INTERRUPT_SOURCE_COUNT_ENUM
} GB_InterruptSource;

/*
 * The interrupt controller owns IF and IE. IME is deliberately not here:
 * IME is an internal CPU state controlled by EI, DI, RETI, and interrupt entry.
 */
typedef struct GB_Interrupt {
    bool initialized;
    bool memory_connected;

    uint8_t interrupt_flags;  /* IF: request latches, lower five bits. */
    uint8_t interrupt_enable; /* IE: enable mask, lower five bits. */

    GB_Memory *memory;
} GB_Interrupt;

GB_Result gb_interrupt_init(GB_Interrupt *interrupts, GB_Error *error);
GB_Result gb_interrupt_reset(GB_Interrupt *interrupts, GB_Error *error);

/* Attach IF/IE and CPU interrupt callbacks to a memory instance. */
GB_Result gb_interrupt_connect_memory(GB_Interrupt *interrupts,
                                      GB_Memory *memory,
                                      GB_Error *error);
GB_Result gb_interrupt_disconnect_memory(GB_Interrupt *interrupts,
                                         GB_Error *error);

/* Hardware-visible register accessors used by GB_Memory. */
GB_Result gb_interrupt_read8(void *user, uint16_t address,
                             uint8_t *value, GB_Error *error);
GB_Result gb_interrupt_write8(void *user, uint16_t address,
                              uint8_t value, GB_Error *error);

/* Interrupt line / CPU bus services. */
GB_Result gb_interrupt_get_pending(void *user, uint8_t *pending_mask,
                                   GB_Error *error);
GB_Result gb_interrupt_acknowledge(void *user, uint8_t interrupt_mask,
                                   GB_Error *error);
GB_Result gb_interrupt_request(void *user, uint8_t interrupt_mask,
                               GB_Error *error);

/* Direct services are useful to hardware devices which already hold a controller pointer. */
GB_Result gb_interrupt_request_source(GB_Interrupt *interrupts,
                                      GB_InterruptSource source,
                                      GB_Error *error);
GB_Result gb_interrupt_clear_source(GB_Interrupt *interrupts,
                                    GB_InterruptSource source,
                                    GB_Error *error);

uint8_t gb_interrupt_pending_mask(const GB_Interrupt *interrupts);
uint8_t gb_interrupt_requested_mask(const GB_Interrupt *interrupts);
uint8_t gb_interrupt_enabled_mask(const GB_Interrupt *interrupts);

/* Returns the highest-priority pending source, or -1 if none is pending. */
int gb_interrupt_highest_priority_source(uint8_t pending_mask);
uint8_t gb_interrupt_source_mask(GB_InterruptSource source);
uint16_t gb_interrupt_source_vector(GB_InterruptSource source);
const char *gb_interrupt_source_name(GB_InterruptSource source);

GB_MemoryInterruptController gb_interrupt_memory_controller(GB_Interrupt *interrupts);

#ifdef __cplusplus
}
#endif

#endif /* GB_INTERRUPT_H */
