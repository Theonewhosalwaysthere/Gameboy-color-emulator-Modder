#include <stdio.h>
#include <string.h>

#include "emulator/gb_emulator.h"

static void print_error(const char *context, const GB_Error *error)
{
    if (error != NULL && error->message[0] != '\0') {
        if (error->has_opcode) {
            fprintf(stderr, "%s: %s (PC=$%04X opcode=$%02X)\n",
                    context, error->message, error->pc, error->opcode);
        } else {
            fprintf(stderr, "%s: %s\n", context, error->message);
        }
    } else {
        fprintf(stderr, "%s: unknown emulator error\n", context);
    }
}

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--dmg|--cgb] <rom.gb|rom.gbc>\n", program);
}

int main(int argc, char **argv)
{
    GB_EmulatorConfig config;
    GB_Emulator emulator;
    GB_Error error;
    const char *rom_path = NULL;
    if (argc == 3 && strcmp(argv[1], "--dmg") == 0) {
        gb_emulator_config_default(&config);
        config.mode = GB_EMULATOR_MODE_DMG;
        rom_path = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--cgb") == 0) {
        gb_emulator_config_default(&config);
        config.mode = GB_EMULATOR_MODE_CGB;
        rom_path = argv[2];
    } else if (argc == 2) {
        gb_emulator_config_default(&config);
        rom_path = argv[1];
    } else {
        print_usage(argv[0]);
        return 2;
    }

    memset(&emulator, 0, sizeof(emulator));
    gb_error_clear(&error);

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
    }

    result = gb_emulator_destroy(&emulator, &error);
    if (result != GB_RESULT_OK) {
        print_error("Emulator cleanup failed", &error);
        return 1;
    }

    return 0;
}
