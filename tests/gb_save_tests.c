#include "../src/save/gb_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BANK_SIZE 0x4000u

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

#define REQUIRE(expr) do { if (!(expr)) return fail(#expr); } while (0)

static void make_header(uint8_t *rom, size_t rom_size)
{
    memset(rom, 0, rom_size);
    memcpy(&rom[0x0104u], TEST_LOGO, sizeof(TEST_LOGO));
    memcpy(&rom[0x0134u], "SAVE TEST CART", 14u);
    memcpy(&rom[0x013Fu], "TEST", 4u);
    memcpy(&rom[0x0144u], "01", 2u);
    rom[0x0143u] = 0x00u;
    rom[0x0147u] = 0x03u; /* MBC1 + RAM + battery. */
    rom[0x0148u] = 0x00u; /* 32 KiB ROM. */
    rom[0x0149u] = 0x03u; /* 32 KiB RAM. */
    rom[0x014Au] = 0x01u;
    rom[0x014Bu] = 0x33u;

    uint8_t checksum = 0u;
    for (size_t i = 0x0134u; i <= 0x014Cu; ++i) {
        checksum = (uint8_t)(checksum - rom[i] - 1u);
    }
    rom[0x014Du] = checksum;
}

static int test_round_trip(void)
{
    const char *save_path = "gbc_save_test.sav";
    (void)remove(save_path);

    uint8_t rom[BANK_SIZE * 2u];
    make_header(rom, sizeof(rom));

    GB_Cartridge cartridge;
    GB_SaveRAM save;
    GB_Error error;
    memset(&cartridge, 0, sizeof(cartridge));
    memset(&save, 0, sizeof(save));

    REQUIRE(gb_cartridge_init(&cartridge, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_load_buffer(&cartridge, rom, sizeof(rom), NULL, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_init(&save, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_attach_path(&save, &cartridge, save_path, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_is_enabled(&save));
    REQUIRE(gb_save_ram_load(&save, &error) == GB_RESULT_OK);
    REQUIRE(!gb_save_ram_is_dirty(&save));

    REQUIRE(gb_cartridge_write8(&cartridge, 0x0000u, 0x0Au, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0xA000u, 0x5Au, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_is_dirty(&save));
    REQUIRE(gb_save_ram_save(&save, &error) == GB_RESULT_OK);
    REQUIRE(!gb_save_ram_is_dirty(&save));

    REQUIRE(gb_save_ram_destroy(&save, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);

    memset(&cartridge, 0, sizeof(cartridge));
    memset(&save, 0, sizeof(save));
    REQUIRE(gb_cartridge_init(&cartridge, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_load_buffer(&cartridge, rom, sizeof(rom), NULL, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_init(&save, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_attach_path(&save, &cartridge, save_path, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_load(&save, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x0000u, 0x0Au, &error) == GB_RESULT_OK);

    uint8_t value = 0u;
    REQUIRE(gb_cartridge_read8(&cartridge, 0xA000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0x5Au);
    REQUIRE(gb_save_ram_destroy(&save, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);
    (void)remove(save_path);
    return 0;
}

static int test_default_path(void)
{
    uint8_t rom[BANK_SIZE * 2u];
    make_header(rom, sizeof(rom));
    GB_Cartridge cartridge;
    GB_SaveRAM save;
    GB_Error error;
    memset(&cartridge, 0, sizeof(cartridge));
    memset(&save, 0, sizeof(save));

    REQUIRE(gb_cartridge_init(&cartridge, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_load_buffer(&cartridge, rom, sizeof(rom), NULL, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_init(&save, &error) == GB_RESULT_OK);
    REQUIRE(gb_save_ram_attach(&save, &cartridge, "example/game.gbc", &error) == GB_RESULT_OK);
    REQUIRE(strcmp(gb_save_ram_path(&save), "example/game.sav") == 0);
    REQUIRE(gb_save_ram_destroy(&save, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);
    return 0;
}

int main(void)
{
    if (test_round_trip() != 0) return 1;
    if (test_default_path() != 0) return 1;
    printf("All save RAM tests passed.\n");
    return 0;
}
