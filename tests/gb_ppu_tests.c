#include "gb_ppu.h"
#include "gb_interrupt.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition, message_text) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", (message_text)); \
            ++failures; \
        } \
    } while (0)

#define CHECK_RESULT(result, label) \
    do { \
        GB_Result _result = (result); \
        if (_result != GB_RESULT_OK) { \
            fprintf(stderr, "FAIL: %s (code=%d)\n", (label), (int)_result); \
            ++failures; \
        } \
    } while (0)

static void setup(GB_Memory *memory, GB_Interrupt *interrupts,
                  GB_PPU *ppu, GB_Error *error, GB_MemoryMode mode)
{
    CHECK_RESULT(gb_memory_init(memory, mode, error), "memory init");
    CHECK_RESULT(gb_interrupt_init(interrupts, error), "interrupt init");
    CHECK_RESULT(gb_interrupt_connect_memory(interrupts, memory, error),
                 "interrupt connect");
    CHECK_RESULT(gb_ppu_init(ppu, memory, error), "ppu init");
}

static void test_registers_and_modes(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_PPU ppu;
    GB_Error error;
    uint8_t value = 0u;

    setup(&memory, &interrupts, &ppu, &error, GB_MEMORY_MODE_DMG);

    CHECK_RESULT(gb_memory_read8(&memory, GB_PPU_ADDR_LCDC, &value, &error), "read LCDC");
    CHECK(value == 0x91u, "LCDC reset is 0x91");
    CHECK(gb_ppu_get_mode(&ppu) == GB_PPU_MODE_OAM, "LCD starts in mode 2");

    CHECK_RESULT(gb_memory_read8(&memory, GB_PPU_ADDR_STAT, &value, &error), "read STAT");
    CHECK((value & 0x03u) == 0x02u, "STAT reports mode 2");
    CHECK((value & 0x04u) != 0u, "STAT reports LY=LYC coincidence");

    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_LYC, 0x17u, &error), "write LYC");
    CHECK_RESULT(gb_memory_read8(&memory, GB_PPU_ADDR_LYC, &value, &error), "read LYC");
    CHECK(value == 0x17u, "LYC stores value");

    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_LY, 0x55u, &error), "write LY");
    CHECK_RESULT(gb_memory_read8(&memory, GB_PPU_ADDR_LY, &value, &error), "read LY");
    CHECK(value == 0u, "LY is read-only");

    CHECK_RESULT(gb_ppu_destroy(&ppu, &error), "ppu destroy");
}

static void test_timing_and_vblank(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_PPU ppu;
    GB_Error error;
    uint8_t value = 0u;

    setup(&memory, &interrupts, &ppu, &error, GB_MEMORY_MODE_DMG);

    CHECK_RESULT(gb_memory_tick(&memory, 80u, &error), "mode 2 duration");
    CHECK(gb_ppu_get_mode(&ppu) == GB_PPU_MODE_XFER, "80 dots enters mode 3");
    CHECK(gb_ppu_get_dot(&ppu) == 0u, "mode 3 dot counter resets");

    uint16_t mode3 = gb_ppu_get_mode3_dots(&ppu);
    CHECK(mode3 >= GB_PPU_MODE3_MIN_DOTS && mode3 <= GB_PPU_MODE3_MAX_DOTS,
          "mode 3 duration stays in hardware range");

    CHECK_RESULT(gb_memory_tick(&memory, mode3, &error), "mode 3 duration");
    CHECK(gb_ppu_get_mode(&ppu) == GB_PPU_MODE_HBLANK, "mode 3 enters hblank");

    uint16_t remaining = (uint16_t)(GB_PPU_SCANLINE_DOTS -
                                      GB_PPU_MODE2_DOTS - mode3);
    CHECK_RESULT(gb_memory_tick(&memory, remaining, &error), "finish scanline");
    CHECK(gb_ppu_get_ly(&ppu) == 1u, "one scanline advances LY");
    CHECK(gb_ppu_get_mode(&ppu) == GB_PPU_MODE_OAM, "next scanline begins OAM search");

    CHECK_RESULT(gb_memory_tick(&memory, GB_PPU_SCANLINE_DOTS * 143u, &error),
                 "reach vblank");
    CHECK(gb_ppu_get_ly(&ppu) == 144u, "VBlank begins at LY=144");
    CHECK(gb_ppu_get_mode(&ppu) == GB_PPU_MODE_VBLANK, "mode 1 starts at LY=144");
    CHECK(gb_ppu_frame_ready(&ppu), "frame-ready flag is set at VBlank");

    CHECK_RESULT(gb_memory_read8(&memory, GB_ADDR_IF, &value, &error), "read IF");
    CHECK((value & GB_INTERRUPT_VBLANK) != 0u, "VBlank interrupt is requested");

    CHECK_RESULT(gb_ppu_destroy(&ppu, &error), "ppu destroy");
}

static void test_vram_oam_locking(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_PPU ppu;
    GB_Error error;
    uint8_t value = 0u;

    setup(&memory, &interrupts, &ppu, &error, GB_MEMORY_MODE_DMG);

    CHECK_RESULT(gb_memory_write8(&memory, 0x8000u, 0x12u, &error), "initial VRAM write");
    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_LCDC, 0x11u, &error),
                 "disable LCD for initial OAM write");
    CHECK_RESULT(gb_memory_write8(&memory, 0xFE00u, 0x34u, &error), "initial OAM write");
    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_LCDC, 0x91u, &error),
                 "re-enable LCD");

    CHECK_RESULT(gb_memory_tick(&memory, 80u, &error), "enter mode 3");
    CHECK(gb_ppu_get_mode(&ppu) == GB_PPU_MODE_XFER, "mode 3 for VRAM lock");

    CHECK_RESULT(gb_memory_read8(&memory, 0x8000u, &value, &error), "blocked VRAM read");
    CHECK(value == 0xFFu, "VRAM read is blocked in mode 3");
    CHECK_RESULT(gb_memory_write8(&memory, 0x8000u, 0x56u, &error), "blocked VRAM write");
    CHECK_RESULT(gb_memory_read8(&memory, 0x8000u, &value, &error), "verify VRAM unchanged via callback");
    /* Still blocked, so the read remains FF. Verify through the public bank pointer. */
    CHECK(memory.vram[0][0] == 0x12u, "blocked VRAM write leaves storage unchanged");

    CHECK_RESULT(gb_memory_read8(&memory, 0xFE00u, &value, &error), "blocked OAM read");
    CHECK(value == 0xFFu, "OAM read is blocked in mode 3");
    CHECK_RESULT(gb_memory_write8(&memory, 0xFE00u, 0x78u, &error), "blocked OAM write");
    CHECK(memory.oam[0] == 0x34u, "blocked OAM write leaves storage unchanged");

    /* Finish line and reach HBlank. */
    uint16_t mode3 = gb_ppu_get_mode3_dots(&ppu);
    CHECK_RESULT(gb_memory_tick(&memory, mode3, &error), "leave mode 3");
    CHECK(gb_ppu_get_mode(&ppu) == GB_PPU_MODE_HBLANK, "HBlank reached");

    CHECK_RESULT(gb_memory_write8(&memory, 0x8000u, 0x9Au, &error), "VRAM write in HBlank");
    CHECK_RESULT(gb_memory_read8(&memory, 0x8000u, &value, &error), "VRAM read in HBlank");
    CHECK(value == 0x9Au, "VRAM is accessible in HBlank");

    CHECK_RESULT(gb_memory_write8(&memory, 0xFE00u, 0xBCu, &error), "OAM write in HBlank");
    CHECK_RESULT(gb_memory_read8(&memory, 0xFE00u, &value, &error), "OAM read in HBlank");
    CHECK(value == 0xBCu, "OAM is accessible in HBlank");

    CHECK_RESULT(gb_ppu_destroy(&ppu, &error), "ppu destroy");
}

static void test_basic_tile_rendering(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_PPU ppu;
    GB_Error error;

    setup(&memory, &interrupts, &ppu, &error, GB_MEMORY_MODE_DMG);

    /* Tile 0: top row has color 3 in every pixel. */
    memory.vram[0][0] = 0xFFu;
    memory.vram[0][1] = 0xFFu;
    memory.vram[0][2] = 0u;
    memory.vram[0][3] = 0u;
    memory.vram[0][0x1800u] = 0u; /* BG map $9800 references tile 0. */

    CHECK_RESULT(gb_memory_tick(&memory, 80u, &error), "render line");
    const uint16_t *framebuffer = gb_ppu_framebuffer(&ppu);
    CHECK(framebuffer != NULL, "framebuffer is available");
    CHECK(framebuffer[0] == GB_PPU_RGB555_BLACK, "tile color 3 maps to BGP black");

    CHECK_RESULT(gb_ppu_destroy(&ppu, &error), "ppu destroy");
}

static void test_stat_lyc_interrupt(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_PPU ppu;
    GB_Error error;
    uint8_t value = 0u;

    setup(&memory, &interrupts, &ppu, &error, GB_MEMORY_MODE_DMG);
    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_STAT, 0x40u, &error),
                 "enable LYC STAT interrupt");
    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_LYC, 0u, &error),
                 "set matching LYC");
    CHECK_RESULT(gb_memory_read8(&memory, GB_ADDR_IF, &value, &error), "read IF");
    CHECK((value & GB_INTERRUPT_STAT) != 0u, "LYC match raises STAT interrupt");

    CHECK_RESULT(gb_ppu_destroy(&ppu, &error), "ppu destroy");
}

static void test_cgb_palettes_and_attributes(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_PPU ppu;
    GB_Error error;
    uint8_t value = 0u;

    setup(&memory, &interrupts, &ppu, &error, GB_MEMORY_MODE_CGB);

    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_BCPS, 0x80u, &error),
                 "set BG palette auto increment");
    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_BCPD, 0x1Fu, &error),
                 "write BG palette low byte");
    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_BCPD, 0x00u, &error),
                 "write BG palette high byte");
    CHECK_RESULT(gb_memory_write8(&memory, GB_PPU_ADDR_BCPS, 0x00u, &error),
                 "rewind BG palette index");
    CHECK_RESULT(gb_memory_read8(&memory, GB_PPU_ADDR_BCPD, &value, &error),
                 "read BG palette low byte");
    CHECK(value == 0x1Fu, "CGB BG palette stores RGB555 data");

    CHECK_RESULT(gb_ppu_destroy(&ppu, &error), "ppu destroy");
}

int main(void)
{
    test_registers_and_modes();
    test_timing_and_vblank();
    test_vram_oam_locking();
    test_basic_tile_rendering();
    test_stat_lyc_interrupt();
    test_cgb_palettes_and_attributes();

    if (failures != 0) {
        fprintf(stderr, "gb_ppu_tests: %d failure(s)\n", failures);
        return 1;
    }

    printf("All PPU tests passed.\n");
    return 0;
}
