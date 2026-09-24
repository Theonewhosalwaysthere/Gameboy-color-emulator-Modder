#include "../src/emulator/gb_emulator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t TEST_LOGO[GB_CARTRIDGE_NINTENDO_LOGO_SIZE] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
};

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

#define CHECK(expr, message) do { if (!(expr)) return fail(message); } while (0)

static void make_rom(uint8_t *rom, size_t size, uint8_t cgb_flag)
{
    memset(rom, 0, size);
    memcpy(&rom[0x0104u], TEST_LOGO, sizeof(TEST_LOGO));
    memcpy(&rom[0x0134u], "GBC LOOP TEST", 13u);
    memcpy(&rom[0x0144u], "01", 2u);
    rom[0x0143u] = cgb_flag;
    rom[0x0147u] = 0x00u;
    rom[0x0148u] = 0x00u;
    rom[0x0149u] = 0x00u;
    rom[0x014Au] = 0x01u;
    rom[0x014Bu] = 0x33u;
    rom[0x014Cu] = 0x00u;

    /* NOP; JP $0100 -- deterministic executable cartridge entry. */
    rom[0x0100u] = 0x00u;
    rom[0x0101u] = 0xC3u;
    rom[0x0102u] = 0x00u;
    rom[0x0103u] = 0x01u;

    uint8_t checksum = 0u;
    for (size_t i = 0x0134u; i <= 0x014Cu; ++i) {
        checksum = (uint8_t)(checksum - rom[i] - 1u);
    }
    rom[0x014Du] = checksum;

    uint16_t global_checksum = 0u;
    for (size_t i = 0u; i < size; ++i) {
        if (i == 0x014Eu || i == 0x014Fu) continue;
        global_checksum = (uint16_t)(global_checksum + rom[i]);
    }
    rom[0x014Eu] = (uint8_t)(global_checksum >> 8u);
    rom[0x014Fu] = (uint8_t)global_checksum;
}

static void make_battery_rom(uint8_t *rom, size_t size)
{
    make_rom(rom, size, 0x00u);
    rom[0x0147u] = 0x03u; /* MBC1 + RAM + battery. */
    rom[0x0149u] = 0x03u; /* 32 KiB RAM. */

    uint8_t checksum = 0u;
    for (size_t i = 0x0134u; i <= 0x014Cu; ++i) {
        checksum = (uint8_t)(checksum - rom[i] - 1u);
    }
    rom[0x014Du] = checksum;
}

static int test_dmg_load_and_frame(void)
{
    uint8_t *rom = (uint8_t *)calloc(1u, 0x8000u);
    CHECK(rom != NULL, "allocate test ROM");
    make_rom(rom, 0x8000u, 0x00u);

    GB_EmulatorConfig config;
    gb_emulator_config_default(&config);

    GB_Emulator emulator;
    memset(&emulator, 0, sizeof(emulator));
    GB_Error error;
    gb_error_clear(&error);

    CHECK(gb_emulator_init(&emulator, &config, &error) == GB_RESULT_OK,
          "initialize emulator");
    CHECK(gb_emulator_load_rom_buffer(&emulator, rom, 0x8000u, &error) == GB_RESULT_OK,
          "load DMG ROM");
    CHECK(!gb_emulator_is_cgb(&emulator), "DMG ROM selects DMG hardware");
    CHECK(emulator.cpu.r.pc == 0x0100u, "CPU starts at cartridge entry point");

    uint32_t frame_cycles = 0u;
    bool frame_ready = false;
    CHECK(gb_emulator_run_frame(&emulator, &frame_cycles, &frame_ready, &error) == GB_RESULT_OK,
          "run one frame");
    CHECK(frame_ready, "one completed frame is reported");
    CHECK(frame_cycles > 0u, "frame consumes emulated cycles");
    CHECK(emulator.frame_count == 1u, "frame counter increments");
    CHECK(emulator.total_t_cycles >= (uint64_t)frame_cycles,
          "emulator clock accounting advances");

    CHECK(gb_emulator_destroy(&emulator, &error) == GB_RESULT_OK,
          "destroy DMG emulator");
    free(rom);
    return 0;
}

static int test_cgb_load(void)
{
    uint8_t *rom = (uint8_t *)calloc(1u, 0x8000u);
    CHECK(rom != NULL, "allocate CGB test ROM");
    make_rom(rom, 0x8000u, 0xC0u);

    GB_EmulatorConfig config;
    gb_emulator_config_default(&config);

    GB_Emulator emulator;
    memset(&emulator, 0, sizeof(emulator));
    GB_Error error;
    gb_error_clear(&error);

    CHECK(gb_emulator_init(&emulator, &config, &error) == GB_RESULT_OK,
          "initialize CGB emulator");
    CHECK(gb_emulator_load_rom_buffer(&emulator, rom, 0x8000u, &error) == GB_RESULT_OK,
          "load CGB ROM");
    CHECK(gb_emulator_is_cgb(&emulator), "CGB ROM selects CGB hardware");
    CHECK(emulator.memory.mode == GB_MEMORY_MODE_CGB, "CGB memory mode is active");
    CHECK(gb_cgb_get_speed(&emulator.cgb) == GB_CGB_SPEED_NORMAL,
          "CGB starts at normal speed");
    CHECK(gb_emulator_cpu_clock_hz(&emulator) == 4194304ULL,
          "normal CGB CPU clock is 4.194304 MHz");

    uint64_t before_t_cycles = emulator.total_t_cycles;
    uint16_t before_dot = gb_ppu_get_dot(&emulator.ppu);
    CHECK(gb_cgb_set_speed(&emulator.cgb, GB_CGB_SPEED_DOUBLE, &error) == GB_RESULT_OK,
          "switch emulator to CGB double speed");
    CHECK(gb_emulator_cpu_clock_hz(&emulator) == 8388608ULL,
          "double-speed CGB CPU clock is 8.388608 MHz");

    uint32_t consumed = 0u;
    CHECK(gb_emulator_step(&emulator, &consumed, &error) == GB_RESULT_OK,
          "execute instruction in CGB double speed");
    CHECK(consumed == 4u, "instruction still reports four CPU T-cycles");
    CHECK(emulator.total_t_cycles - before_t_cycles == 2u,
          "CGB double-speed CPU cycle advances two base hardware cycles");
    CHECK((uint16_t)((gb_ppu_get_dot(&emulator.ppu) - before_dot) & 0x1FFu) == 2u,
          "PPU advances at base speed while CPU runs double speed");

    CHECK(gb_emulator_destroy(&emulator, &error) == GB_RESULT_OK,
          "destroy CGB emulator");
    free(rom);
    return 0;
}

static int test_stop_progress_behavior(void)
{
    uint8_t rom[0x8000u];
    make_rom(rom, sizeof(rom), 0x00u);
    rom[0x0100u] = 0x10u;
    rom[0x0101u] = 0x00u;

    GB_EmulatorConfig config;
    gb_emulator_config_default(&config);
    GB_Emulator emulator;
    memset(&emulator, 0, sizeof(emulator));
    GB_Error error;
    gb_error_clear(&error);

    CHECK(gb_emulator_init(&emulator, &config, &error) == GB_RESULT_OK,
          "initialize STOP test emulator");
    CHECK(gb_emulator_load_rom_buffer(&emulator, rom, sizeof(rom), &error) == GB_RESULT_OK,
          "load STOP test ROM");

    uint32_t consumed = 0u;
    CHECK(gb_emulator_step(&emulator, &consumed, &error) == GB_RESULT_OK,
          "execute STOP instruction");
    CHECK(consumed == 4u, "STOP consumes its documented timing");
    CHECK(gb_emulator_is_stopped(&emulator), "CPU enters STOP state");

    CHECK(gb_cpu_wake_from_stop(&emulator.cpu, &error) == GB_RESULT_OK,
          "wake CPU from STOP");
    CHECK(!gb_emulator_is_stopped(&emulator), "CPU leaves STOP state");

    CHECK(gb_emulator_destroy(&emulator, &error) == GB_RESULT_OK,
          "destroy STOP test emulator");
    return 0;
}

static int test_final_save_integration(void)
{
    const char *rom_path = "gb_emulator_final_test.gb";
    const char *save_path = "gb_emulator_final_test.sav";
    uint8_t rom[0x8000u];
    make_battery_rom(rom, sizeof(rom));

    FILE *file = fopen(rom_path, "wb");
    CHECK(file != NULL, "create integration ROM file");
    CHECK(fwrite(rom, 1u, sizeof(rom), file) == sizeof(rom),
          "write integration ROM file");
    CHECK(fclose(file) == 0, "close integration ROM file");
    (void)remove(save_path);

    GB_EmulatorConfig config;
    gb_emulator_config_default(&config);
    GB_Emulator emulator;
    memset(&emulator, 0, sizeof(emulator));
    GB_Error error;
    gb_error_clear(&error);

    CHECK(gb_emulator_init(&emulator, &config, &error) == GB_RESULT_OK,
          "initialize save integration emulator");
    CHECK(gb_emulator_load_rom(&emulator, rom_path, &error) == GB_RESULT_OK,
          "load battery-backed ROM through final emulator path");
    CHECK(gb_memory_write8(&emulator.memory, 0x0000u, 0x0Au, &error) == GB_RESULT_OK,
          "enable cartridge RAM for integration test");
    CHECK(gb_memory_write8(&emulator.memory, 0xA000u, 0x5Au, &error) == GB_RESULT_OK,
          "write cartridge RAM through emulator bus");
    CHECK(gb_emulator_save_ram(&emulator, &error) == GB_RESULT_OK,
          "explicitly flush save RAM");
    CHECK(gb_save_ram_path(&emulator.save_ram) != NULL,
          "emulator exposes save path");
    CHECK(strcmp(gb_save_ram_path(&emulator.save_ram), save_path) == 0,
          "emulator uses ROM-derived save path");
    CHECK(gb_emulator_destroy(&emulator, &error) == GB_RESULT_OK,
          "destroy first save integration emulator");

    memset(&emulator, 0, sizeof(emulator));
    CHECK(gb_emulator_init(&emulator, &config, &error) == GB_RESULT_OK,
          "initialize second save integration emulator");
    CHECK(gb_emulator_load_rom(&emulator, rom_path, &error) == GB_RESULT_OK,
          "reload battery-backed ROM");
    CHECK(gb_memory_write8(&emulator.memory, 0x0000u, 0x0Au, &error) == GB_RESULT_OK,
          "re-enable cartridge RAM");
    uint8_t value = 0u;
    CHECK(gb_memory_read8(&emulator.memory, 0xA000u, &value, &error) == GB_RESULT_OK,
          "read persisted cartridge RAM");
    CHECK(value == 0x5Au, "persisted save data survives emulator restart");
    CHECK(gb_emulator_destroy(&emulator, &error) == GB_RESULT_OK,
          "destroy second save integration emulator");

    (void)remove(save_path);
    (void)remove(rom_path);
    return 0;
}

static int test_final_debug_integration(void)
{
    uint8_t rom[0x8000u];
    make_rom(rom, sizeof(rom), 0x00u);

    GB_EmulatorConfig config;
    gb_emulator_config_default(&config);
    config.debug_config.log_level = GB_DEBUG_LOG_INFO;

    GB_Emulator emulator;
    memset(&emulator, 0, sizeof(emulator));
    GB_Error error;
    gb_error_clear(&error);

    CHECK(gb_emulator_init(&emulator, &config, &error) == GB_RESULT_OK,
          "initialize debug integration emulator");
    GB_Debug *debug = gb_emulator_debug(&emulator);
    CHECK(debug != NULL, "emulator exposes debugger");
    CHECK(gb_debug_add_breakpoint(debug, 0x0100u, &error) == GB_RESULT_OK,
          "add debugger breakpoint through final emulator API");
    CHECK(gb_emulator_load_rom_buffer(&emulator, rom, sizeof(rom), &error) == GB_RESULT_OK,
          "load ROM for debug integration");

    uint32_t consumed = 0u;
    CHECK(gb_emulator_step(&emulator, &consumed, &error) == GB_RESULT_DEBUG_BREAK,
          "emulator step reaches execution breakpoint");
    CHECK(gb_debug_break_requested(debug), "breakpoint state reaches debugger");
    CHECK(gb_debug_break_pc(debug) == 0x0100u, "debugger records breakpoint PC");
    CHECK(gb_debug_counters(debug)->execution_breakpoints == 1u,
          "debugger increments breakpoint counter");

    gb_debug_clear_break(debug);
    CHECK(gb_debug_remove_breakpoint(debug, 0x0100u, &error) == GB_RESULT_OK,
          "remove integration breakpoint");
    CHECK(gb_emulator_step(&emulator, &consumed, &error) == GB_RESULT_OK,
          "CPU resumes after debugger clears breakpoint");
    CHECK(consumed != 0u, "resumed CPU advances");

    CHECK(gb_emulator_destroy(&emulator, &error) == GB_RESULT_OK,
          "destroy debug integration emulator");
    return 0;
}

int main(void)
{
    if (test_dmg_load_and_frame() != 0) return 1;
    if (test_cgb_load() != 0) return 1;
    if (test_stop_progress_behavior() != 0) return 1;
    if (test_final_save_integration() != 0) return 1;
    if (test_final_debug_integration() != 0) return 1;
    puts("All emulator core tests passed.");
    return 0;
}
