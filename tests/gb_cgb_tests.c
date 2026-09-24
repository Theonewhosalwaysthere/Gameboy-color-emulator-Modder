#include "../src/cgb/gb_cgb.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", (msg)); \
        return 1; \
    } \
} while (0)

#define CHECK_RESULT(expr, msg) do { \
    GB_Result _r = (expr); \
    if (_r != GB_RESULT_OK) { \
        fprintf(stderr, "FAIL: %s (result=%d, error=%s)\n", \
                (msg), (int)_r, error.message); \
        return 1; \
    } \
} while (0)

static void clear_error(GB_Error *error)
{
    gb_error_clear(error);
}

static int test_key1_and_speed_switch(void)
{
    GB_Error error;
    clear_error(&error);

    GB_Memory memory;
    GB_CGB cgb;
    GB_CPU cpu;

    CHECK(gb_memory_init(&memory, GB_MEMORY_MODE_CGB, &error) == GB_RESULT_OK,
          "CGB memory initializes");
    CHECK(gb_cgb_init(&cgb, &memory, &error) == GB_RESULT_OK,
          "CGB hardware initializes");
    CHECK(gb_cgb_attach_cpu(&cgb, &cpu, &error) == GB_RESULT_OK,
          "CPU attaches to CGB speed controller");

    uint8_t value = 0u;
    CHECK_RESULT(gb_memory_read8(&memory, GB_CGB_ADDR_KEY1, &value, &error),
                 "read initial KEY1");
    CHECK(value == 0x7Eu, "KEY1 starts in normal speed with prepare clear");

    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_KEY1, 0x01u, &error),
                 "prepare CGB speed switch");
    CHECK(gb_cgb_speed_switch_prepared(&cgb), "KEY1 prepare bit is stored");

    cpu.stopped = true;
    bool switched = false;
    CHECK_RESULT(gb_cgb_handle_cpu_stop(&cgb, &switched, &error),
                 "process STOP speed switch");
    CHECK(switched, "STOP toggles speed when KEY1 is prepared");
    CHECK(gb_cgb_get_speed(&cgb) == GB_CGB_SPEED_DOUBLE,
          "CGB enters double-speed mode");
    CHECK(!gb_cpu_is_stopped(&cpu), "CPU resumes after speed switch");
    CHECK(!gb_cgb_speed_switch_prepared(&cgb), "KEY1 prepare bit clears after switch");

    CHECK_RESULT(gb_memory_read8(&memory, GB_CGB_ADDR_KEY1, &value, &error),
                 "read double-speed KEY1");
    CHECK(value == 0xFEu, "KEY1 reports double-speed state");

    CHECK_RESULT(gb_cgb_destroy(&cgb, &error), "destroy CGB hardware");
    return 0;
}

static int test_ir_register(void)
{
    GB_Error error;
    clear_error(&error);

    GB_Memory memory;
    GB_CGB cgb;
    CHECK(gb_memory_init(&memory, GB_MEMORY_MODE_CGB, &error) == GB_RESULT_OK,
          "CGB memory initializes");
    CHECK(gb_cgb_init(&cgb, &memory, &error) == GB_RESULT_OK,
          "CGB hardware initializes");

    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_RP, 0x01u, &error),
                 "turn IR LED on");
    CHECK(gb_cgb_ir_led_on(&cgb), "IR LED state is stored");

    uint8_t value = 0u;
    CHECK_RESULT(gb_memory_read8(&memory, GB_CGB_ADDR_RP, &value, &error),
                 "read IR register");
    CHECK((value & 0xC1u) == 0xC1u, "IR status exposes enable bits and LED output");

    CHECK_RESULT(gb_cgb_set_ir_input(&cgb, true, &error), "set IR input active");
    CHECK_RESULT(gb_memory_read8(&memory, GB_CGB_ADDR_RP, &value, &error),
                 "read active IR input");
    CHECK((value & 0x02u) == 0u, "IR input reports receiving state");

    CHECK_RESULT(gb_cgb_destroy(&cgb, &error), "destroy CGB hardware");
    return 0;
}

static int test_general_purpose_hdma(void)
{
    GB_Error error;
    clear_error(&error);

    GB_Memory memory;
    GB_CGB cgb;
    CHECK(gb_memory_init(&memory, GB_MEMORY_MODE_CGB, &error) == GB_RESULT_OK,
          "CGB memory initializes");
    CHECK(gb_cgb_init(&cgb, &memory, &error) == GB_RESULT_OK,
          "CGB hardware initializes");

    for (uint16_t i = 0u; i < 0x20u; ++i) {
        CHECK_RESULT(gb_memory_dma_read8(&memory, (uint16_t)(0xC000u + i),
                                         &(uint8_t){0u}, &error),
                     "validate CGB WRAM DMA source access");
        memory.wram[0][i] = (uint8_t)(0x80u + i);
    }

    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA1, 0xC0u, &error),
                 "set HDMA source high");
    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA2, 0x00u, &error),
                 "set HDMA source low");
    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA3, 0x00u, &error),
                 "set HDMA destination high");
    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA4, 0x00u, &error),
                 "set HDMA destination low");
    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA5, 0x00u, &error),
                 "start one-block general-purpose HDMA");

    for (uint16_t i = 0u; i < 0x10u; ++i) {
        CHECK(memory.vram[0][i] == (uint8_t)(0x80u + i),
              "general-purpose HDMA copied one 16-byte block");
    }
    CHECK(!gb_cgb_hdma_active(&cgb), "general-purpose HDMA completes immediately");
    CHECK(gb_cgb_cpu_stall_t_cycles(&cgb) == GB_CGB_HDMA_BLOCK_T_CYCLES,
          "general-purpose HDMA imposes one block of CPU stall time");

    CHECK_RESULT(gb_cgb_tick(&cgb, GB_CGB_HDMA_BLOCK_T_CYCLES, &error),
                 "consume general-purpose HDMA stall");
    CHECK(!gb_cgb_cpu_is_stalled(&cgb), "general-purpose HDMA stall clears");

    CHECK_RESULT(gb_cgb_destroy(&cgb, &error), "destroy CGB hardware");
    return 0;
}

static int test_hblank_hdma(void)
{
    GB_Error error;
    clear_error(&error);

    GB_Memory memory;
    GB_CGB cgb;
    GB_PPU ppu;

    CHECK(gb_memory_init(&memory, GB_MEMORY_MODE_CGB, &error) == GB_RESULT_OK,
          "CGB memory initializes");
    CHECK(gb_ppu_init(&ppu, &memory, &error) == GB_RESULT_OK,
          "PPU initializes");
    CHECK(gb_cgb_init(&cgb, &memory, &error) == GB_RESULT_OK,
          "CGB hardware initializes");
    CHECK(gb_cgb_attach_ppu(&cgb, &ppu, &error) == GB_RESULT_OK,
          "PPU attaches to CGB HDMA controller");

    for (uint16_t i = 0u; i < 0x10u; ++i) {
        memory.wram[0][i] = (uint8_t)(0x20u + i);
    }

    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA1, 0xC0u, &error),
                 "set HBlank DMA source high");
    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA2, 0x00u, &error),
                 "set HBlank DMA source low");
    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA3, 0x00u, &error),
                 "set HBlank DMA destination high");
    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA4, 0x20u, &error),
                 "set HBlank DMA destination low");
    CHECK_RESULT(gb_memory_write8(&memory, GB_CGB_ADDR_HDMA5, 0x80u, &error),
                 "start one-block HBlank DMA");
    CHECK(gb_cgb_hdma_active(&cgb), "HBlank DMA remains active until HBlank");

    /* Enable LCD and advance through OAM + transfer to reach HBlank. */
    CHECK_RESULT(gb_memory_write8(&memory, 0xFF40u, 0x91u, &error),
                 "enable LCD");
    CHECK_RESULT(gb_ppu_tick(&ppu, 80u + 172u, &error),
                 "advance PPU into HBlank");
    CHECK(gb_ppu_get_mode(&ppu) == GB_PPU_MODE_HBLANK,
          "PPU reaches HBlank");

    CHECK_RESULT(gb_cgb_tick(&cgb, 0u, &error),
                 "process HBlank DMA at HBlank entry");
    for (uint16_t i = 0u; i < 0x10u; ++i) {
        CHECK(memory.vram[0][0x20u + i] == (uint8_t)(0x20u + i),
              "HBlank DMA copied one block into VRAM");
    }
    CHECK(!gb_cgb_hdma_active(&cgb), "single HBlank block completes transfer");
    CHECK(gb_cgb_cpu_stall_t_cycles(&cgb) == GB_CGB_HDMA_BLOCK_T_CYCLES,
          "HBlank DMA imposes one block of CPU stall time");

    CHECK_RESULT(gb_ppu_destroy(&ppu, &error), "destroy PPU");
    CHECK_RESULT(gb_cgb_destroy(&cgb, &error), "destroy CGB hardware");
    return 0;
}

static int test_dmg_rejection(void)
{
    GB_Error error;
    clear_error(&error);
    GB_Memory memory;
    GB_CGB cgb;

    CHECK(gb_memory_init(&memory, GB_MEMORY_MODE_DMG, &error) == GB_RESULT_OK,
          "DMG memory initializes");
    CHECK(gb_cgb_init(&cgb, &memory, &error) == GB_RESULT_UNSUPPORTED,
          "CGB hardware rejects DMG memory mode");
    return 0;
}

int main(void)
{
    if (test_key1_and_speed_switch() != 0) return 1;
    if (test_ir_register() != 0) return 1;
    if (test_general_purpose_hdma() != 0) return 1;
    if (test_hblank_hdma() != 0) return 1;
    if (test_dmg_rejection() != 0) return 1;

    printf("All CGB hardware tests passed.\n");
    return 0;
}
