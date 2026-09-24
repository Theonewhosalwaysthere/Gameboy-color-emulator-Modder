#include "../src/cartridge/gb_cartridge.h"

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

static void make_header(uint8_t *rom, size_t rom_size, uint8_t type,
                        uint8_t rom_size_code, uint8_t ram_size_code,
                        uint8_t cgb_flag)
{
    memset(rom, 0, rom_size);

    for (size_t bank = 0u; bank < rom_size / BANK_SIZE; ++bank) {
        memset(rom + bank * BANK_SIZE, (int)(bank & 0xFFu), BANK_SIZE);
    }

    rom[0x0100u] = 0x00u;
    rom[0x0101u] = 0xC3u;
    rom[0x0102u] = 0x50u;
    rom[0x0103u] = 0x01u;
    memcpy(&rom[0x0104u], TEST_LOGO, sizeof(TEST_LOGO));
    memcpy(&rom[0x0134u], "GBC TEST CART", 13u);
    memcpy(&rom[0x013Fu], "TEST", 4u);
    memcpy(&rom[0x0144u], "01", 2u);
    rom[0x0143u] = cgb_flag;
    rom[0x0146u] = 0x00u;
    rom[0x0147u] = type;
    rom[0x0148u] = rom_size_code;
    rom[0x0149u] = ram_size_code;
    rom[0x014Au] = 0x01u;
    rom[0x014Bu] = 0x33u;
    rom[0x014Cu] = 0x00u;

    uint8_t header_checksum = 0u;
    for (size_t i = 0x0134u; i <= 0x014Cu; ++i) {
        header_checksum = (uint8_t)(header_checksum - rom[i] - 1u);
    }
    rom[0x014Du] = header_checksum;

    uint16_t global_checksum = 0u;
    for (size_t i = 0u; i < rom_size; ++i) {
        if (i == 0x014Eu || i == 0x014Fu) continue;
        global_checksum = (uint16_t)(global_checksum + rom[i]);
    }
    rom[0x014Eu] = (uint8_t)(global_checksum >> 8u);
    rom[0x014Fu] = (uint8_t)global_checksum;
}

static int load_cart(GB_Cartridge *cartridge, uint8_t *rom, size_t size,
                     GB_Memory *memory, GB_Error *error)
{
    REQUIRE(gb_cartridge_init(cartridge, error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_load_buffer(cartridge, rom, size, NULL, error) == GB_RESULT_OK);
    if (memory != NULL) {
        REQUIRE(gb_memory_init(memory, GB_MEMORY_MODE_CGB, error) == GB_RESULT_OK);
        GB_MemoryCartridgeBus bus;
        REQUIRE(gb_cartridge_get_memory_bus(cartridge, &bus, error) == GB_RESULT_OK);
        REQUIRE(gb_memory_set_cartridge_bus(memory, &bus, error) == GB_RESULT_OK);
    }
    return 0;
}

static int test_metadata_and_direct_rom(void)
{
    const size_t size = 0x8000u;
    uint8_t *rom = (uint8_t *)malloc(size);
    REQUIRE(rom != NULL);
    make_header(rom, size, 0x00u, 0x00u, 0x00u, 0x80u);
    rom[0x0150u] = 0x42u;

    GB_Cartridge cartridge;
    GB_Error error;
    REQUIRE(load_cart(&cartridge, rom, size, NULL, &error) == 0);
    REQUIRE(cartridge.mapper == GB_CARTRIDGE_MAPPER_NONE);
    REQUIRE(cartridge.bus_supported);
    REQUIRE(cartridge.cgb_support == GB_CARTRIDGE_CGB_COMPATIBLE);

    uint8_t value = 0u;
    REQUIRE(gb_cartridge_read8(&cartridge, 0x0150u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0x42u);

    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);
    free(rom);
    return 0;
}

static int test_mbc1(void)
{
    const size_t size = 0x100000u; /* 64 banks */
    uint8_t *rom = (uint8_t *)malloc(size);
    REQUIRE(rom != NULL);
    make_header(rom, size, 0x03u, 0x05u, 0x03u, 0x80u);

    GB_Cartridge cartridge;
    GB_Error error;
    REQUIRE(load_cart(&cartridge, rom, size, NULL, &error) == 0);

    uint8_t value = 0u;
    REQUIRE(gb_cartridge_read8(&cartridge, 0x4000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 1u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x2000u, 2u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0x4000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 2u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x4000u, 1u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0x4000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 34u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x6000u, 1u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0x0000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 32u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x0000u, 0x0Au, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x4000u, 2u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0xA000u, 0x5Au, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x4000u, 3u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0xA000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0u);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x4000u, 2u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0xA000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0x5Au);

    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);
    free(rom);
    return 0;
}

static int test_mbc2(void)
{
    const size_t size = 0x40000u; /* 16 banks */
    uint8_t *rom = (uint8_t *)malloc(size);
    REQUIRE(rom != NULL);
    make_header(rom, size, 0x05u, 0x03u, 0x00u, 0x00u);

    GB_Cartridge cartridge;
    GB_Error error;
    REQUIRE(load_cart(&cartridge, rom, size, NULL, &error) == 0);
    REQUIRE(cartridge.ram_size == 0x0200u);

    uint8_t value = 0u;
    REQUIRE(gb_cartridge_write8(&cartridge, 0x0000u, 0x0Au, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x2100u, 3u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0x4000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 3u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0xA200u, 0xABu, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0xA000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0xFBu);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x0000u, 0x00u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0xA000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0xFFu);

    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);
    free(rom);
    return 0;
}

static int test_mbc3_and_rtc(void)
{
    const size_t size = 0x200000u; /* 128 banks */
    uint8_t *rom = (uint8_t *)malloc(size);
    REQUIRE(rom != NULL);
    make_header(rom, size, 0x10u, 0x06u, 0x03u, 0x80u);

    /* Mark the two ROM windows differently so fixed-bank mapping is tested. */
    rom[0x0038u] = 0xFFu;
    rom[0x4038u] = 0xFCu;

    GB_Cartridge cartridge;
    GB_Memory memory;
    GB_Error error;
    REQUIRE(load_cart(&cartridge, rom, size, &memory, &error) == 0);

    uint8_t value = 0u;
    REQUIRE(gb_cartridge_read8(&cartridge, 0x0038u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0xFFu);
    REQUIRE(gb_cartridge_read8(&cartridge, 0x4000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 1u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x2000u, 7u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0x4000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 7u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x0000u, 0x0Au, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x4000u, 0u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0xA000u, 0xA5u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0xA000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0xA5u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x4000u, 8u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0xA000u, 59u, &error) == GB_RESULT_OK);
    REQUIRE(gb_memory_tick(&memory, 4194304u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x6000u, 0u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x6000u, 1u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0xA000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x4000u, 9u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0xA000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 1u);

    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);
    free(rom);
    return 0;
}

static int test_mbc5(void)
{
    const size_t size = 0x800000u; /* 512 banks */
    uint8_t *rom = (uint8_t *)malloc(size);
    REQUIRE(rom != NULL);
    make_header(rom, size, 0x1Eu, 0x08u, 0x04u, 0x80u);

    GB_Cartridge cartridge;
    GB_Error error;
    REQUIRE(load_cart(&cartridge, rom, size, NULL, &error) == 0);

    uint8_t value = 0u;
    REQUIRE(gb_cartridge_write8(&cartridge, 0x2000u, 0x01u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x3000u, 0x01u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_read8(&cartridge, 0x4000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0x01u);
    REQUIRE(cartridge.mbc5_rom_bank == 0x0101u);

    REQUIRE(gb_cartridge_write8(&cartridge, 0x0000u, 0x0Au, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_write8(&cartridge, 0x4000u, 0x08u, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_rumble_active(&cartridge));
    REQUIRE(gb_cartridge_write8(&cartridge, 0xA000u, 0x5Au, &error) == GB_RESULT_OK);

    REQUIRE(gb_cartridge_unload(&cartridge, &error) == GB_RESULT_OK);
    free(rom);
    return 0;
}

static int test_unsupported_mapper(void)
{
    const size_t size = 0x8000u;
    uint8_t *rom = (uint8_t *)malloc(size);
    REQUIRE(rom != NULL);
    make_header(rom, size, 0x22u, 0x00u, 0x03u, 0x80u);

    GB_Cartridge cartridge;
    GB_Error error;
    REQUIRE(gb_cartridge_init(&cartridge, &error) == GB_RESULT_OK);
    REQUIRE(gb_cartridge_load_buffer(&cartridge, rom, size, NULL, &error) ==
            GB_RESULT_UNSUPPORTED_CARTRIDGE);

    free(rom);
    return 0;
}

int main(void)
{
    if (test_metadata_and_direct_rom() != 0) return 1;
    if (test_mbc1() != 0) return 1;
    if (test_mbc2() != 0) return 1;
    if (test_mbc3_and_rtc() != 0) return 1;
    if (test_mbc5() != 0) return 1;
    if (test_unsupported_mapper() != 0) return 1;

    printf("All cartridge and MBC tests passed.\n");
    return 0;
}
