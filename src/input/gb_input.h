#ifndef GB_INPUT_H
#define GB_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../memory/gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_INPUT_BUTTON_COUNT 8u
#define GB_INPUT_BUTTON_MASK_ALL 0xFFu

enum {
    GB_INPUT_ADDR_JOYP = 0xFF00u
};

typedef enum GB_InputButton {
    GB_INPUT_BUTTON_A = 0,
    GB_INPUT_BUTTON_B,
    GB_INPUT_BUTTON_SELECT,
    GB_INPUT_BUTTON_START,
    GB_INPUT_BUTTON_RIGHT,
    GB_INPUT_BUTTON_LEFT,
    GB_INPUT_BUTTON_UP,
    GB_INPUT_BUTTON_DOWN,
    GB_INPUT_BUTTON_COUNT_ENUM
} GB_InputButton;

/* Each bit represents whether the physical/logical button is currently held. */
enum {
    GB_INPUT_MASK_A      = 0x01u,
    GB_INPUT_MASK_B      = 0x02u,
    GB_INPUT_MASK_SELECT = 0x04u,
    GB_INPUT_MASK_START  = 0x08u,
    GB_INPUT_MASK_RIGHT  = 0x10u,
    GB_INPUT_MASK_LEFT   = 0x20u,
    GB_INPUT_MASK_UP     = 0x40u,
    GB_INPUT_MASK_DOWN   = 0x80u
};

/* JOYP bits 4 and 5 are the two matrix-select outputs. The lower nibble is
 * generated from the currently pressed buttons and is always active-low. */
#define GB_INPUT_JOYP_SELECT_MASK 0x30u
#define GB_INPUT_JOYP_FIXED_HIGH  0xC0u

typedef struct GB_Input {
    bool initialized;
    bool mapped;
    bool interrupt_connected;
    bool interrupt_latched_without_controller;

    GB_Memory *memory;

    /* Stored CPU selection outputs. Bits 4-5 are meaningful. Reset uses 0,
     * which makes an unpressed JOYP read as $CF. */
    uint8_t select_bits;

    uint8_t pressed_mask;
    uint8_t last_lines;

    GB_MemoryRequestInterrupt request_interrupt;
    void *interrupt_user;

    size_t io_device_index;
} GB_Input;

GB_Result gb_input_init(GB_Input *input, GB_Memory *memory, GB_Error *error);
GB_Result gb_input_reset(GB_Input *input, GB_Error *error);
GB_Result gb_input_destroy(GB_Input *input, GB_Error *error);

/* Connect the input device to the interrupt controller's request callback. */
GB_Result gb_input_connect_interrupt(GB_Input *input,
                                     GB_MemoryRequestInterrupt request_interrupt,
                                     void *interrupt_user,
                                     GB_Error *error);
GB_Result gb_input_disconnect_interrupt(GB_Input *input, GB_Error *error);

/* Set one button or the complete aggregated physical button state. */
GB_Result gb_input_set_button(GB_Input *input, GB_InputButton button,
                              bool pressed, GB_Error *error);
GB_Result gb_input_set_pressed_mask(GB_Input *input, uint8_t pressed_mask,
                                    GB_Error *error);

bool gb_input_is_pressed(const GB_Input *input, GB_InputButton button);
uint8_t gb_input_pressed_mask(const GB_Input *input);
uint8_t gb_input_get_select_bits(const GB_Input *input);
uint8_t gb_input_get_joyp(const GB_Input *input);

/* Hardware register callbacks used by GB_Memory. */
GB_Result gb_input_read8(void *user, uint16_t address,
                         uint8_t *value, GB_Error *error);
GB_Result gb_input_write8(void *user, uint16_t address,
                          uint8_t value, GB_Error *error);

#ifdef __cplusplus
}
#endif

#endif /* GB_INPUT_H */
