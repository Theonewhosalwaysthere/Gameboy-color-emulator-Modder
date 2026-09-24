#include "gb_input.h"

#include <stdio.h>
#include <string.h>

static void input_error(GB_Error *error, GB_Result code,
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

static GB_Result require_input(const GB_Input *input, GB_Error *error)
{
    if (input == NULL) {
        input_error(error, GB_RESULT_NULL_ARGUMENT, GB_INPUT_ADDR_JOYP,
                    "Input pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (!input->initialized) {
        input_error(error, GB_RESULT_BAD_STATE, GB_INPUT_ADDR_JOYP,
                    "Input subsystem is not initialized");
        return GB_RESULT_BAD_STATE;
    }

    return GB_RESULT_OK;
}

static bool valid_button(GB_InputButton button)
{
    return button >= GB_INPUT_BUTTON_A &&
           button < GB_INPUT_BUTTON_COUNT_ENUM;
}

static uint8_t button_mask(GB_InputButton button)
{
    if (!valid_button(button)) {
        return 0u;
    }
    return (uint8_t)(1u << (unsigned)button);
}

static uint8_t input_lines(const GB_Input *input)
{
    uint8_t lines = 0x0Fu;
    uint8_t selected = input->select_bits;

    /* Bits 4 and 5 select the two physical rows. A low select line enables
     * that row. If both are selected, each shared column is pulled low when
     * either corresponding button is pressed. */
    if ((selected & 0x20u) == 0u) {
        if ((input->pressed_mask & GB_INPUT_MASK_A) != 0u) {
            lines &= (uint8_t)~0x01u;
        }
        if ((input->pressed_mask & GB_INPUT_MASK_B) != 0u) {
            lines &= (uint8_t)~0x02u;
        }
        if ((input->pressed_mask & GB_INPUT_MASK_SELECT) != 0u) {
            lines &= (uint8_t)~0x04u;
        }
        if ((input->pressed_mask & GB_INPUT_MASK_START) != 0u) {
            lines &= (uint8_t)~0x08u;
        }
    }

    if ((selected & 0x10u) == 0u) {
        if ((input->pressed_mask & GB_INPUT_MASK_RIGHT) != 0u) {
            lines &= (uint8_t)~0x01u;
        }
        if ((input->pressed_mask & GB_INPUT_MASK_LEFT) != 0u) {
            lines &= (uint8_t)~0x02u;
        }
        if ((input->pressed_mask & GB_INPUT_MASK_UP) != 0u) {
            lines &= (uint8_t)~0x04u;
        }
        if ((input->pressed_mask & GB_INPUT_MASK_DOWN) != 0u) {
            lines &= (uint8_t)~0x08u;
        }
    }

    return lines;
}

static GB_Result request_joypad_interrupt(GB_Input *input,
                                          GB_Error *error)
{
    if (input->request_interrupt == NULL) {
        input->interrupt_latched_without_controller = true;
        return GB_RESULT_OK;
    }

    GB_Error child_error;
    gb_error_clear(&child_error);
    GB_Result result = input->request_interrupt(input->interrupt_user,
                                                GB_INTERRUPT_JOYPAD,
                                                &child_error);
    if (result != GB_RESULT_OK) {
        if (error != NULL) {
            *error = child_error;
            if (error->message[0] == '\0') {
                (void)snprintf(error->message, sizeof(error->message),
                               "Failed to request the joypad interrupt");
            }
            error->code = GB_RESULT_INTERRUPT_ERROR;
            error->pc = GB_INPUT_ADDR_JOYP;
        }
        return GB_RESULT_INTERRUPT_ERROR;
    }

    input->interrupt_latched_without_controller = false;
    return GB_RESULT_OK;
}

static GB_Result refresh_lines_and_interrupt(GB_Input *input,
                                              GB_Error *error)
{
    uint8_t new_lines = input_lines(input);
    uint8_t falling_edges = (uint8_t)(input->last_lines &
                                      (uint8_t)~new_lines);
    input->last_lines = new_lines;

    if (falling_edges == 0u) {
        return GB_RESULT_OK;
    }

    return request_joypad_interrupt(input, error);
}

static GB_Result flush_latched_interrupt(GB_Input *input, GB_Error *error)
{
    if (!input->interrupt_latched_without_controller ||
        input->request_interrupt == NULL) {
        return GB_RESULT_OK;
    }

    return request_joypad_interrupt(input, error);
}

GB_Result gb_input_init(GB_Input *input, GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);

    if (input == NULL || memory == NULL) {
        input_error(error, GB_RESULT_NULL_ARGUMENT, GB_INPUT_ADDR_JOYP,
                    "Input and memory pointers are required");
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(input, 0, sizeof(*input));
    input->memory = memory;
    input->initialized = true;

    GB_Result result = gb_input_reset(input, error);
    if (result != GB_RESULT_OK) {
        memset(input, 0, sizeof(*input));
        return result;
    }

    result = gb_memory_map_io_device(memory,
                                     GB_INPUT_ADDR_JOYP,
                                     GB_INPUT_ADDR_JOYP,
                                     input,
                                     gb_input_read8,
                                     gb_input_write8,
                                     NULL,
                                     &input->io_device_index,
                                     error);
    if (result != GB_RESULT_OK) {
        memset(input, 0, sizeof(*input));
        return result;
    }

    input->mapped = true;
    return GB_RESULT_OK;
}

GB_Result gb_input_reset(GB_Input *input, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_input(input, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    input->select_bits = 0u;
    input->pressed_mask = 0u;
    input->interrupt_latched_without_controller = false;
    input->last_lines = input_lines(input);
    return GB_RESULT_OK;
}

GB_Result gb_input_destroy(GB_Input *input, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_input(input, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (input->mapped) {
        if (input->memory == NULL) {
            input_error(error, GB_RESULT_BAD_STATE, GB_INPUT_ADDR_JOYP,
                        "Input mapping exists without a memory object");
            return GB_RESULT_BAD_STATE;
        }

        result = gb_memory_unmap_io_device(input->memory,
                                           input->io_device_index,
                                           error);
        if (result != GB_RESULT_OK) {
            return result;
        }
    }

    memset(input, 0, sizeof(*input));
    return GB_RESULT_OK;
}

GB_Result gb_input_connect_interrupt(GB_Input *input,
                                     GB_MemoryRequestInterrupt request_interrupt,
                                     void *interrupt_user,
                                     GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_input(input, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (request_interrupt == NULL) {
        input_error(error, GB_RESULT_NULL_ARGUMENT, GB_INPUT_ADDR_JOYP,
                    "Joypad interrupt request callback is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    input->request_interrupt = request_interrupt;
    input->interrupt_user = interrupt_user;
    input->interrupt_connected = true;

    return flush_latched_interrupt(input, error);
}

GB_Result gb_input_disconnect_interrupt(GB_Input *input, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_input(input, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    input->request_interrupt = NULL;
    input->interrupt_user = NULL;
    input->interrupt_connected = false;
    return GB_RESULT_OK;
}

GB_Result gb_input_set_pressed_mask(GB_Input *input, uint8_t pressed_mask,
                                    GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_input(input, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    input->pressed_mask = pressed_mask;
    return refresh_lines_and_interrupt(input, error);
}

GB_Result gb_input_set_button(GB_Input *input, GB_InputButton button,
                              bool pressed, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_input(input, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    uint8_t mask = button_mask(button);
    if (mask == 0u) {
        input_error(error, GB_RESULT_INVALID_ARGUMENT, GB_INPUT_ADDR_JOYP,
                    "Invalid Game Boy input button");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    if (pressed) {
        input->pressed_mask |= mask;
    } else {
        input->pressed_mask &= (uint8_t)~mask;
    }

    return refresh_lines_and_interrupt(input, error);
}

bool gb_input_is_pressed(const GB_Input *input, GB_InputButton button)
{
    uint8_t mask = button_mask(button);
    if (input == NULL || !input->initialized || mask == 0u) {
        return false;
    }
    return (input->pressed_mask & mask) != 0u;
}

uint8_t gb_input_pressed_mask(const GB_Input *input)
{
    if (input == NULL || !input->initialized) {
        return 0u;
    }
    return input->pressed_mask;
}

uint8_t gb_input_get_select_bits(const GB_Input *input)
{
    if (input == NULL || !input->initialized) {
        return 0u;
    }
    return (uint8_t)(input->select_bits & GB_INPUT_JOYP_SELECT_MASK);
}

uint8_t gb_input_get_joyp(const GB_Input *input)
{
    if (input == NULL || !input->initialized) {
        return 0xFFu;
    }

    return (uint8_t)(GB_INPUT_JOYP_FIXED_HIGH |
                     (input->select_bits & GB_INPUT_JOYP_SELECT_MASK) |
                     input_lines(input));
}

GB_Result gb_input_read8(void *user, uint16_t address,
                         uint8_t *value, GB_Error *error)
{
    gb_error_clear(error);

    GB_Input *input = (GB_Input *)user;
    GB_Result result = require_input(input, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (value == NULL) {
        input_error(error, GB_RESULT_NULL_ARGUMENT, address,
                    "Input register read output pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (address != GB_INPUT_ADDR_JOYP) {
        input_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                    "Invalid input register address");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    *value = gb_input_get_joyp(input);
    return GB_RESULT_OK;
}

GB_Result gb_input_write8(void *user, uint16_t address,
                          uint8_t value, GB_Error *error)
{
    gb_error_clear(error);

    GB_Input *input = (GB_Input *)user;
    GB_Result result = require_input(input, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (address != GB_INPUT_ADDR_JOYP) {
        input_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                    "Invalid input register address");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    input->select_bits = (uint8_t)(value & GB_INPUT_JOYP_SELECT_MASK);
    return refresh_lines_and_interrupt(input, error);
}
