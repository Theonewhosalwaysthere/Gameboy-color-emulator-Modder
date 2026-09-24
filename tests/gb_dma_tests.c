#include "../src/dma/gb_dma.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

#define REQUIRE(expr) do { if (!(expr)) return fail(#expr); } while (0)

static int test_dmg_oam_dma(void)
{
    GB_Memory memory;
    GB_DMA dma;
    GB_Error error;

    REQUIRE(gb_memory_init(&memory, GB_MEMORY_MODE_DMG, &error) == GB_RESULT_OK);
    for (uint16_t i = 0u; i < 0x00A0u; ++i) {
        REQUIRE(gb_memory_write8(&memory, (uint16_t)(0xC000u + i),
                                 (uint8_t)i, &error) == GB_RESULT_OK);
    }

    REQUIRE(gb_dma_init(&dma, &memory, &error) == GB_RESULT_OK);
    REQUIRE(gb_memory_write8(&memory, GB_DMA_ADDR, 0xC0u, &error) == GB_RESULT_OK);
    REQUIRE(gb_dma_is_active(&dma));

    uint8_t value = 0u;
    REQUIRE(gb_memory_read8(&memory, 0xC000u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0xFFu);

    REQUIRE(gb_memory_write8(&memory, 0xC000u, 0xAAu, &error) == GB_RESULT_OK);
    REQUIRE(gb_memory_read8(&memory, 0xFF80u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0u);
    REQUIRE(gb_memory_write8(&memory, 0xFF80u, 0x5Au, &error) == GB_RESULT_OK);
    REQUIRE(gb_memory_read8(&memory, 0xFF80u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0x5Au);

    REQUIRE(gb_dma_tick(&dma, 4u, &error) == GB_RESULT_OK);
    REQUIRE(gb_dma_bytes_transferred(&dma) == 0u);
    REQUIRE(gb_memory_read8(&memory, 0xFE00u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0xFFu);

    REQUIRE(gb_dma_tick(&dma, 640u, &error) == GB_RESULT_OK);
    REQUIRE(!gb_dma_is_active(&dma));
    REQUIRE(gb_dma_bytes_transferred(&dma) == 0x00A0u);

    for (uint16_t i = 0u; i < 0x00A0u; ++i) {
        REQUIRE(gb_memory_read8(&memory, (uint16_t)(0xFE00u + i), &value, &error) == GB_RESULT_OK);
        REQUIRE(value == (uint8_t)i);
    }

    REQUIRE(gb_dma_destroy(&dma, &error) == GB_RESULT_OK);
    return 0;
}

static int test_cgb_double_speed(void)
{
    GB_Memory memory;
    GB_DMA dma;
    GB_Error error;
    REQUIRE(gb_memory_init(&memory, GB_MEMORY_MODE_CGB, &error) == GB_RESULT_OK);
    for (uint16_t i = 0u; i < 0x00A0u; ++i) {
        REQUIRE(gb_memory_write8(&memory, (uint16_t)(0xC000u + i), 0xA0u, &error) == GB_RESULT_OK);
    }
    REQUIRE(gb_dma_init(&dma, &memory, &error) == GB_RESULT_OK);
    REQUIRE(gb_dma_set_cgb_double_speed(&dma, true, &error) == GB_RESULT_OK);
    REQUIRE(gb_memory_write8(&memory, GB_DMA_ADDR, 0xC0u, &error) == GB_RESULT_OK);
    REQUIRE(gb_dma_tick(&dma, 2u, &error) == GB_RESULT_OK);
    REQUIRE(gb_dma_bytes_transferred(&dma) == 0u);
    REQUIRE(gb_dma_tick(&dma, 320u, &error) == GB_RESULT_OK);
    REQUIRE(!gb_dma_is_active(&dma));
    REQUIRE(gb_dma_bytes_transferred(&dma) == 0x00A0u);
    REQUIRE(gb_dma_destroy(&dma, &error) == GB_RESULT_OK);
    return 0;
}

static int test_dma_restart(void)
{
    GB_Memory memory;
    GB_DMA dma;
    GB_Error error;
    REQUIRE(gb_memory_init(&memory, GB_MEMORY_MODE_DMG, &error) == GB_RESULT_OK);

    for (uint16_t i = 0u; i < 0x00A0u; ++i) {
        REQUIRE(gb_memory_write8(&memory, (uint16_t)(0xC000u + i), 0xC0u, &error) == GB_RESULT_OK);
        REQUIRE(gb_memory_write8(&memory, (uint16_t)(0xD000u + i), 0xD0u, &error) == GB_RESULT_OK);
    }

    REQUIRE(gb_dma_init(&dma, &memory, &error) == GB_RESULT_OK);
    REQUIRE(gb_memory_write8(&memory, GB_DMA_ADDR, 0xC0u, &error) == GB_RESULT_OK);
    REQUIRE(gb_dma_tick(&dma, 100u, &error) == GB_RESULT_OK);
    REQUIRE(gb_dma_bytes_transferred(&dma) > 0u);

    REQUIRE(gb_dma_write8(&dma, GB_DMA_ADDR, 0xD0u, &error) == GB_RESULT_OK);
    REQUIRE(gb_dma_bytes_transferred(&dma) == 0u);
    REQUIRE(gb_dma_tick(&dma, 644u, &error) == GB_RESULT_OK);
    REQUIRE(!gb_dma_is_active(&dma));

    uint8_t value = 0u;
    REQUIRE(gb_memory_read8(&memory, 0xFE00u, &value, &error) == GB_RESULT_OK);
    REQUIRE(value == 0xD0u);

    REQUIRE(gb_dma_destroy(&dma, &error) == GB_RESULT_OK);
    return 0;
}

int main(void)
{
    if (test_dmg_oam_dma() != 0) return 1;
    if (test_cgb_double_speed() != 0) return 1;
    if (test_dma_restart() != 0) return 1;

    printf("All DMA tests passed.\n");
    return 0;
}
