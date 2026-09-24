#include "../src/debug/gb_debug.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

#define REQUIRE(expr) do { if (!(expr)) return fail(#expr); } while (0)

static int test_breakpoints_and_trace(void)
{
    GB_Debug debug;
    GB_DebugConfig config;
    GB_Error error;
    GB_CPU cpu;
    memset(&debug, 0, sizeof(debug));
    memset(&cpu, 0, sizeof(cpu));

    gb_debug_config_default(&config);
    config.log_level = GB_DEBUG_LOG_TRACE;
    config.trace_cpu = true;
    REQUIRE(gb_debug_init(&debug, &config, &error) == GB_RESULT_OK);

    FILE *output = tmpfile();
    REQUIRE(output != NULL);
    REQUIRE(gb_debug_set_output(&debug, output, &error) == GB_RESULT_OK);
    REQUIRE(gb_debug_add_breakpoint(&debug, 0x0100u, &error) == GB_RESULT_OK);
    REQUIRE(gb_debug_has_breakpoint(&debug, 0x0100u));

    cpu.r.pc = 0x0100u;
    REQUIRE(gb_debug_before_cpu_step(&debug, &cpu, &error) == GB_RESULT_DEBUG_BREAK);
    REQUIRE(gb_debug_break_requested(&debug));
    REQUIRE(gb_debug_break_pc(&debug) == 0x0100u);
    REQUIRE(gb_debug_break_reason(&debug) == GB_DEBUG_BREAK_EXECUTION);
    REQUIRE(gb_debug_counters(&debug)->execution_breakpoints == 1u);

    gb_debug_clear_break(&debug);
    REQUIRE(gb_debug_remove_breakpoint(&debug, 0x0100u, &error) == GB_RESULT_OK);
    REQUIRE(gb_debug_after_cpu_step(&debug, &cpu, 4u, 4u, &error) == GB_RESULT_OK);
    REQUIRE(gb_debug_counters(&debug)->cpu_steps == 1u);
    REQUIRE(gb_debug_counters(&debug)->cpu_t_cycles == 4u);
    REQUIRE(gb_debug_counters(&debug)->hardware_t_cycles == 4u);

    REQUIRE(fseek(output, 0L, SEEK_SET) == 0);
    char buffer[512];
    size_t count = fread(buffer, 1u, sizeof(buffer) - 1u, output);
    buffer[count] = '\0';
    REQUIRE(strstr(buffer, "[TRACE]") != NULL);
    REQUIRE(fclose(output) == 0);
    REQUIRE(gb_debug_destroy(&debug, &error) == GB_RESULT_OK);
    return 0;
}

static int test_dumps(void)
{
    GB_Debug debug;
    GB_Error error;
    GB_CPU cpu;
    GB_Memory memory;
    GB_Cartridge cartridge;
    GB_PPU ppu;
    memset(&debug, 0, sizeof(debug));
    memset(&cpu, 0, sizeof(cpu));
    memset(&memory, 0, sizeof(memory));
    memset(&cartridge, 0, sizeof(cartridge));
    memset(&ppu, 0, sizeof(ppu));

    REQUIRE(gb_debug_init(&debug, NULL, &error) == GB_RESULT_OK);
    REQUIRE(gb_memory_init(&memory, GB_MEMORY_MODE_DMG, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_init(&cartridge, &error) == GB_RESULT_OK);

    FILE *output = tmpfile();
    REQUIRE(output != NULL);
    REQUIRE(gb_debug_dump_cpu(output, &cpu, &error) == GB_RESULT_OK);
    REQUIRE(gb_debug_dump_cartridge(output, &cartridge, &error) == GB_RESULT_OK);
    REQUIRE(gb_debug_dump_ppu(output, &ppu, &error) == GB_RESULT_OK);
    REQUIRE(gb_debug_dump_memory(output, &memory, 0xC000u, 16u, &error) == GB_RESULT_OK);
    REQUIRE(fclose(output) == 0);

    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);
    REQUIRE(gb_memory_reset(&memory, &error) == GB_RESULT_OK);
    REQUIRE(gb_debug_destroy(&debug, &error) == GB_RESULT_OK);
    return 0;
}

int main(void)
{
    if (test_breakpoints_and_trace() != 0) return 1;
    if (test_dumps() != 0) return 1;
    printf("All debug/diagnostic tests passed.\n");
    return 0;
}
