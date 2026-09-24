#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "emulator/gb_emulator.h"
#include "platform/gb_sdl3.h"

static void print_error(const char *context, const GB_Error *error)
{
    if (error != NULL && error->message[0] != '\0') {
        if (error->has_opcode) {
            fprintf(stderr, "%s: %s (PC=$%04X opcode=$%02X)\n",
                    context,
                    error->message,
                    error->pc,
                    error->opcode);
        } else {
            fprintf(stderr, "%s: %s\n", context, error->message);
        }
        return;
    }
    fprintf(stderr, "%s: unknown emulator error\n", context);
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--dmg|--cgb] [--debug|--trace] [--breakpoint $ADDR] [--no-save] [rom.gb|rom.gbc]\n"
            "\n"
            "The ROM argument is optional. Without one, the SDL3 window opens and you can drag a .gb or .gbc ROM onto it.\n"
            "\n"
            "Diagnostics:\n"
            "  --debug        Enable debug logging\n"
            "  --trace        Trace CPU instructions\n"
            "  --breakpoint A Stop before executing address A (repeatable)\n"
            "  --no-save      Disable battery-backed .sav loading/saving\n"
            "\n"
            "Controls:\n"
            "  Arrow keys    D-pad\n"
            "  Z             A\n"
            "  X             B\n"
            "  Right Shift   Select\n"
            "  Enter         Start\n",
            program);
}

static bool parse_u16_address(const char *text, uint16_t *value)
{
    if (text == NULL || value == NULL || text[0] == '\0') return false;
    const char *number = text;
    if (number[0] == '$') {
        ++number;
    }
    errno = 0;
    char *end = NULL;
    unsigned long parsed = strtoul(number, &end, 16);
    if (errno != 0 || end == number || *end != '\0' || parsed > 0xFFFFul) {
        return false;
    }
    *value = (uint16_t)parsed;
    return true;
}

int main(int argc, char **argv)
{
    GB_EmulatorConfig config;
    GB_Error error;
    gb_error_clear(&error);
    gb_emulator_config_default(&config);

    const char *rom_path = NULL;
    uint16_t breakpoints[GB_DEBUG_MAX_BREAKPOINTS];
    size_t breakpoint_count = 0u;
    bool dump_on_break = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dmg") == 0) {
            config.mode = GB_EMULATOR_MODE_DMG;
        } else if (strcmp(argv[i], "--cgb") == 0) {
            config.mode = GB_EMULATOR_MODE_CGB;
        } else if (strcmp(argv[i], "--debug") == 0) {
            config.debug_config.log_level = GB_DEBUG_LOG_DEBUG;
        } else if (strcmp(argv[i], "--trace") == 0) {
            config.debug_config.log_level = GB_DEBUG_LOG_TRACE;
            config.debug_config.trace_cpu = true;
        } else if (strcmp(argv[i], "--no-save") == 0) {
            config.enable_save_ram = false;
        } else if (strcmp(argv[i], "--breakpoint") == 0) {
            if (i + 1 >= argc || breakpoint_count >= GB_DEBUG_MAX_BREAKPOINTS ||
                !parse_u16_address(argv[++i], &breakpoints[breakpoint_count])) {
                print_usage(argv[0]);
                return 2;
            }
            ++breakpoint_count;
            dump_on_break = true;
            config.debug_config.log_level = config.debug_config.log_level < GB_DEBUG_LOG_INFO
                                                 ? GB_DEBUG_LOG_INFO
                                                 : config.debug_config.log_level;
        } else if (argv[i][0] == '-') {
            print_usage(argv[0]);
            return 2;
        } else if (rom_path == NULL) {
            rom_path = argv[i];
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    GB_Emulator emulator;
    memset(&emulator, 0, sizeof(emulator));

    GB_Result result = gb_emulator_init(&emulator, &config, &error);
    if (result != GB_RESULT_OK) {
        print_error("Emulator initialization failed", &error);
        return 1;
    }

    GB_Debug *debug = gb_emulator_debug(&emulator);
    if (debug == NULL) {
        print_error("Debug subsystem initialization failed", &error);
        (void)gb_emulator_destroy(&emulator, NULL);
        return 1;
    }
    for (size_t i = 0u; i < breakpoint_count; ++i) {
        result = gb_debug_add_breakpoint(debug, breakpoints[i], &error);
        if (result != GB_RESULT_OK) {
            print_error("Could not add breakpoint", &error);
            (void)gb_emulator_destroy(&emulator, NULL);
            return 1;
        }
    }

    if (rom_path != NULL) {
        result = gb_emulator_load_rom(&emulator, rom_path, &error);
        if (result != GB_RESULT_OK) {
            print_error("ROM loading failed", &error);
            (void)gb_emulator_destroy(&emulator, NULL);
            return 1;
        }
    }

    GB_SDL3Platform platform;
    memset(&platform, 0, sizeof(platform));
    result = gb_sdl3_init(&platform, &emulator, &error);
    if (result != GB_RESULT_OK) {
        print_error("SDL3 initialization failed", &error);
        (void)gb_emulator_destroy(&emulator, NULL);
        return 1;
    }

    if (emulator.cartridge.loaded) {
        char title[128];
        (void)snprintf(title, sizeof(title),
                       "GBC Emulator - %.16s (%s)",
                       (const char *)emulator.cartridge.title,
                       gb_cartridge_cgb_support_name(emulator.cartridge.cgb_support));
        if (!SDL_SetWindowTitle(platform.window, title)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Could not set ROM window title: %s", SDL_GetError());
        }
    } else if (!SDL_SetWindowTitle(platform.window,
                                   "GBC Emulator - Drag a .gb/.gbc ROM onto this window")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not set SDL3 window title: %s", SDL_GetError());
    }

    result = gb_sdl3_run(&platform, &emulator, &error);
    if (result != GB_RESULT_OK) {
        print_error("Emulator loop failed", &error);
    }

    debug = gb_emulator_debug(&emulator);
    if (debug != NULL && gb_debug_break_requested(debug)) {
        fprintf(stderr, "Execution breakpoint hit at PC=$%04X.\n",
                gb_debug_break_pc(debug));
        if (dump_on_break) {
            (void)gb_debug_dump_cpu(stderr, &emulator.cpu, NULL);
            const GB_DebugCounters *counters = gb_debug_counters(debug);
            if (counters != NULL) {
                fprintf(stderr, "DEBUG: steps=%llu cpu_t=%llu hw_t=%llu frames=%llu breaks=%llu\n",
                        (unsigned long long)counters->cpu_steps,
                        (unsigned long long)counters->cpu_t_cycles,
                        (unsigned long long)counters->hardware_t_cycles,
                        (unsigned long long)counters->frames,
                        (unsigned long long)counters->execution_breakpoints);
            }
            (void)gb_debug_dump_cartridge(stderr, &emulator.cartridge, NULL);
            (void)gb_debug_dump_ppu(stderr, &emulator.ppu, NULL);
        }
    }

    GB_Error destroy_error;
    gb_error_clear(&destroy_error);
    GB_Result platform_result = gb_sdl3_destroy(&platform, &destroy_error);
    GB_Result emulator_result = gb_emulator_destroy(&emulator, &destroy_error);

    if (result != GB_RESULT_OK) return 1;
    if (platform_result != GB_RESULT_OK) {
        print_error("SDL3 cleanup failed", &destroy_error);
        return 1;
    }
    if (emulator_result != GB_RESULT_OK) {
        print_error("Emulator cleanup failed", &destroy_error);
        return 1;
    }

    return 0;
}
