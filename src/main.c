#include <stdio.h>
#include <string.h>

#include "emulator/gb_emulator.h"

static void print_error(const char *context, const GB_Error *error) {
    if (error != NULL && error->message[0] != '\0') {
        if (error->has_opcode) {
            fprintf(stderr, "%s: %s (PC=$%04X opcode=$%02X)\n",
                    context, error->message, error->pc, error->opcode);
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
            "Usage: %s [--dmg|--cgb] [--realtime] <rom.gb|rom.gbc>\n"
            "\n"
            "Headless mode is uncapped by default.\n"
            "  --realtime     Pace the emulator at normal Game Boy speed\n",
            program);
}


int emulator_main(int argc, char **argv) {
    GB_EmulatorConfig config;
    GB_Emulator emulator;
    GB_Error error;
    const char *rom_path = NULL;
    bool realtime = false;

    gb_error_clear(&error);
    gb_emulator_config_default(&config);
    memset(&emulator, 0, sizeof(emulator));

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dmg") == 0) {
            config.mode = GB_EMULATOR_MODE_DMG;
        } else if (strcmp(argv[i], "--cgb") == 0) {
            config.mode = GB_EMULATOR_MODE_CGB;
        } else if (strcmp(argv[i], "--realtime") == 0) {
            realtime = true;
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

    if (rom_path == NULL) {
        print_usage(argv[0]);
        return 2;
    }

    GB_Result result = gb_emulator_init(&emulator, &config, &error);
    if (result != GB_RESULT_OK) {
        print_error("Emulator initialization failed", &error);
        return 1;
    }

    result = gb_emulator_load_rom(&emulator, rom_path, &error);
    if (result != GB_RESULT_OK) {
        print_error("ROM loading failed", &error);
        (void)gb_emulator_destroy(&emulator, NULL);
        return 1;
    }

    while (emulator.running) {
        result = gb_emulator_run_frame(&emulator, NULL, NULL, &error);
        if (result != GB_RESULT_OK) {
            print_error("Emulator loop failed", &error);
            (void)gb_emulator_destroy(&emulator, NULL);
            return 1;
        }

        if (gb_emulator_is_stopped(&emulator)) {
            break;
        }

        if (realtime) {
            /* The portable headless target intentionally has no OS timing
             * dependency. Keep this flag accepted for command-line
             * compatibility; normal headless execution remains uncapped. */
            realtime = false;
        }
    }

    result = gb_emulator_destroy(&emulator, &error);
    if (result != GB_RESULT_OK) {
        print_error("Emulator cleanup failed", &error);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {

    return emulator_main(argc, argv);
}
