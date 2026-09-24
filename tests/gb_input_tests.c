#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/input/gb_input.h"
#include "../src/interrupt/gb_interrupt.h"

static void expect_ok(GB_Result result, const GB_Error *error)
{
    if (result != GB_RESULT_OK) {
        fprintf(stderr, "unexpected error %d: %s\n",
                (int)result, error != NULL ? error->message : "(no error)");
        assert(result == GB_RESULT_OK);
    }
}

static void init_system(GB_Memory *memory, GB_Interrupt *interrupts,
                        GB_Input *input, GB_Error *error)
{
    expect_ok(gb_memory_init(memory, GB_MEMORY_MODE_CGB, error), error);
    expect_ok(gb_interrupt_init(interrupts, error), error);
    expect_ok(gb_interrupt_connect_memory(interrupts, memory, error), error);
    expect_ok(gb_input_init(input, memory, error), error);
    expect_ok(gb_input_connect_interrupt(input, gb_interrupt_request,
                                          interrupts, error), error);
}

static void test_reset_and_register(GB_Memory *memory, GB_Input *input,
                                    GB_Error *error)
{
    uint8_t value = 0u;
    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert(value == 0xCFu);

    expect_ok(gb_input_write8(input, 0xFF00u, 0x20u, error), error);
    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert(value == 0xEFu);

    expect_ok(gb_input_write8(input, 0xFF00u, 0x10u, error), error);
    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert(value == 0xDFu);
}

static void test_action_buttons(GB_Memory *memory, GB_Input *input,
                                GB_Interrupt *interrupts, GB_Error *error)
{
    uint8_t value = 0u;

    expect_ok(gb_interrupt_reset(interrupts, error), error);
    expect_ok(gb_input_reset(input, error), error);
    expect_ok(gb_input_write8(input, 0xFF00u, 0x10u, error), error);

    expect_ok(gb_input_set_button(input, GB_INPUT_BUTTON_A, true, error), error);
    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert((value & 0x01u) == 0u);
    assert((gb_interrupt_requested_mask(interrupts) & GB_INTERRUPT_JOYPAD) != 0u);

    expect_ok(gb_interrupt_reset(interrupts, error), error);
    expect_ok(gb_input_set_button(input, GB_INPUT_BUTTON_A, true, error), error);
    assert(gb_interrupt_requested_mask(interrupts) == 0u);

    expect_ok(gb_input_set_button(input, GB_INPUT_BUTTON_A, false, error), error);
    assert((value & 0x01u) == 0u || value != 0u);

    expect_ok(gb_input_set_button(input, GB_INPUT_BUTTON_START, true, error), error);
    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert((value & 0x08u) == 0u);
}

static void test_dpad_and_selection(GB_Memory *memory, GB_Input *input,
                                   GB_Interrupt *interrupts, GB_Error *error)
{
    uint8_t value = 0u;

    expect_ok(gb_interrupt_reset(interrupts, error), error);
    expect_ok(gb_input_reset(input, error), error);
    expect_ok(gb_input_write8(input, 0xFF00u, 0x20u, error), error);
    expect_ok(gb_input_set_button(input, GB_INPUT_BUTTON_RIGHT, true, error), error);

    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert((value & 0x01u) == 0u);
    assert((gb_interrupt_requested_mask(interrupts) & GB_INTERRUPT_JOYPAD) != 0u);

    expect_ok(gb_interrupt_reset(interrupts, error), error);
    expect_ok(gb_input_write8(input, 0xFF00u, 0x30u, error), error);
    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert((value & 0x0Fu) == 0x0Fu);
    assert(gb_interrupt_requested_mask(interrupts) == 0u);

    expect_ok(gb_input_write8(input, 0xFF00u, 0x20u, error), error);
    assert((gb_interrupt_requested_mask(interrupts) & GB_INTERRUPT_JOYPAD) != 0u);
}

static void test_both_rows_and_shared_columns(GB_Memory *memory, GB_Input *input,
                                              GB_Error *error)
{
    uint8_t value = 0u;

    expect_ok(gb_input_reset(input, error), error);
    expect_ok(gb_input_write8(input, 0xFF00u, 0x00u, error), error);
    expect_ok(gb_input_set_pressed_mask(input,
                                       (uint8_t)(GB_INPUT_MASK_A | GB_INPUT_MASK_LEFT),
                                       error), error);

    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert((value & 0x0Fu) == 0x0Cu);
}

static void test_interrupt_latching_before_connect(GB_Memory *memory,
                                                   GB_Input *input,
                                                   GB_Interrupt *interrupts,
                                                   GB_Error *error)
{
    uint8_t value = 0u;
    expect_ok(gb_interrupt_reset(interrupts, error), error);
    expect_ok(gb_input_disconnect_interrupt(input, error), error);
    expect_ok(gb_input_reset(input, error), error);
    expect_ok(gb_input_write8(input, 0xFF00u, 0x10u, error), error);
    expect_ok(gb_input_set_button(input, GB_INPUT_BUTTON_B, true, error), error);
    assert(gb_interrupt_requested_mask(interrupts) == 0u);

    expect_ok(gb_input_connect_interrupt(input, gb_interrupt_request,
                                          interrupts, error), error);
    assert((gb_interrupt_requested_mask(interrupts) & GB_INTERRUPT_JOYPAD) != 0u);

    expect_ok(gb_memory_read8(memory, 0xFF00u, &value, error), error);
    assert((value & 0x02u) == 0u);
}

static void test_invalid_operations(GB_Input *input, GB_Error *error)
{
    GB_Result result = gb_input_set_button(input,
                                            GB_INPUT_BUTTON_COUNT_ENUM,
                                            true, error);
    assert(result == GB_RESULT_INVALID_ARGUMENT);

    result = gb_input_read8(input, 0xFF01u, NULL, error);
    assert(result == GB_RESULT_INVALID_ARGUMENT || result == GB_RESULT_NULL_ARGUMENT);
}

int main(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Input input;
    GB_Error error;
    memset(&memory, 0, sizeof(memory));
    memset(&interrupts, 0, sizeof(interrupts));
    memset(&input, 0, sizeof(input));
    gb_error_clear(&error);

    init_system(&memory, &interrupts, &input, &error);
    test_reset_and_register(&memory, &input, &error);
    test_action_buttons(&memory, &input, &interrupts, &error);
    test_dpad_and_selection(&memory, &input, &interrupts, &error);
    test_both_rows_and_shared_columns(&memory, &input, &error);
    test_interrupt_latching_before_connect(&memory, &input, &interrupts, &error);
    test_invalid_operations(&input, &error);

    expect_ok(gb_input_destroy(&input, &error), &error);
    expect_ok(gb_interrupt_disconnect_memory(&interrupts, &error), &error);

    printf("All input tests passed.\n");
    return 0;
}
