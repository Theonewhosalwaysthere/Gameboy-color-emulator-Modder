#ifndef GB_PPU_H
#define GB_PPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../memory/gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_PPU_WIDTH               160u
#define GB_PPU_HEIGHT              144u
#define GB_PPU_FRAMEBUFFER_PIXELS  (GB_PPU_WIDTH * GB_PPU_HEIGHT)
#define GB_PPU_SCANLINE_DOTS       456u
#define GB_PPU_VISIBLE_SCANLINES   144u
#define GB_PPU_TOTAL_SCANLINES     154u
#define GB_PPU_MODE2_DOTS          80u
#define GB_PPU_MODE3_MIN_DOTS      172u
#define GB_PPU_MODE3_MAX_DOTS      289u
#define GB_PPU_CGB_PALETTE_BYTES   64u
#define GB_PPU_MAX_LINE_SPRITES    10u

enum {
    GB_PPU_ADDR_LCDC = 0xFF40u,
    GB_PPU_ADDR_STAT = 0xFF41u,
    GB_PPU_ADDR_SCY  = 0xFF42u,
    GB_PPU_ADDR_SCX  = 0xFF43u,
    GB_PPU_ADDR_LY   = 0xFF44u,
    GB_PPU_ADDR_LYC  = 0xFF45u,
    GB_PPU_ADDR_BGP  = 0xFF47u,
    GB_PPU_ADDR_OBP0 = 0xFF48u,
    GB_PPU_ADDR_OBP1 = 0xFF49u,
    GB_PPU_ADDR_WY   = 0xFF4Au,
    GB_PPU_ADDR_WX   = 0xFF4Bu,
    GB_PPU_ADDR_BCPS = 0xFF68u,
    GB_PPU_ADDR_BCPD = 0xFF69u,
    GB_PPU_ADDR_OCPS = 0xFF6Au,
    GB_PPU_ADDR_OCPD = 0xFF6Bu,
    GB_PPU_ADDR_OPRI = 0xFF6Cu
};

/* Native RGB555 output values used for DMG shades when no CGB palette exists. */
#define GB_PPU_RGB555_WHITE      0x7FFFu
#define GB_PPU_RGB555_LIGHT_GRAY 0x56B5u
#define GB_PPU_RGB555_DARK_GRAY  0x294Au
#define GB_PPU_RGB555_BLACK      0x0000u

typedef enum GB_PPU_Mode {
    GB_PPU_MODE_HBLANK = 0,
    GB_PPU_MODE_VBLANK = 1,
    GB_PPU_MODE_OAM    = 2,
    GB_PPU_MODE_XFER   = 3
} GB_PPU_Mode;

typedef struct GB_PPU_Sprite {
    uint8_t oam_index;
    uint8_t y;
    uint8_t x;
    uint8_t tile;
    uint8_t attributes;
} GB_PPU_Sprite;

typedef struct GB_PPU {
    bool initialized;
    bool mapped;
    bool memory_connected;

    GB_Memory *memory;

    size_t io_lcdc_stat_device;
    size_t io_scroll_device;
    size_t io_cgb_palette_device;

    uint8_t lcdc;
    uint8_t stat_select;
    uint8_t scy;
    uint8_t scx;
    uint8_t ly;
    uint8_t lyc;
    uint8_t bgp;
    uint8_t obp0;
    uint8_t obp1;
    uint8_t wy;
    uint8_t wx;

    uint8_t bcps;
    uint8_t ocps;
    uint8_t opri;
    uint8_t bg_palette_ram[GB_PPU_CGB_PALETTE_BYTES];
    uint8_t obj_palette_ram[GB_PPU_CGB_PALETTE_BYTES];

    GB_PPU_Mode mode;
    uint16_t dot;
    uint16_t mode3_dots;

    uint8_t window_line;
    bool window_used_this_line;
    bool stat_irq_line;
    bool frame_ready;

    GB_PPU_Sprite line_sprites[GB_PPU_MAX_LINE_SPRITES];
    uint8_t line_sprite_count;

    /* Final display pixels in native RGB555 (15-bit color in a uint16_t). */
    uint16_t framebuffer[GB_PPU_FRAMEBUFFER_PIXELS];

    /* Current scanline BG/window source information used for OBJ priority. */
    uint8_t bg_color_index[GB_PPU_WIDTH];
    bool bg_priority[GB_PPU_WIDTH];
} GB_PPU;

GB_Result gb_ppu_init(GB_PPU *ppu, GB_Memory *memory, GB_Error *error);
GB_Result gb_ppu_reset(GB_PPU *ppu, GB_Error *error);
GB_Result gb_ppu_destroy(GB_PPU *ppu, GB_Error *error);

/* PPU time is measured in dots/T-cycles and does not double in CGB CPU double-speed mode. */
GB_Result gb_ppu_tick(GB_PPU *ppu, uint32_t t_cycles, GB_Error *error);
GB_Result gb_ppu_device_tick(void *user, uint32_t t_cycles, GB_Error *error);

GB_Result gb_ppu_read8(void *user, uint16_t address, uint8_t *value, GB_Error *error);
GB_Result gb_ppu_write8(void *user, uint16_t address, uint8_t value, GB_Error *error);

GB_MemoryVideoController gb_ppu_memory_controller(GB_PPU *ppu);

GB_PPU_Mode gb_ppu_get_mode(const GB_PPU *ppu);
uint8_t gb_ppu_get_ly(const GB_PPU *ppu);
uint8_t gb_ppu_get_lyc(const GB_PPU *ppu);
uint8_t gb_ppu_get_lcdc(const GB_PPU *ppu);
uint8_t gb_ppu_get_stat(const GB_PPU *ppu);
uint16_t gb_ppu_get_dot(const GB_PPU *ppu);
uint16_t gb_ppu_get_mode3_dots(const GB_PPU *ppu);
bool gb_ppu_is_lcd_enabled(const GB_PPU *ppu);
bool gb_ppu_frame_ready(const GB_PPU *ppu);
void gb_ppu_clear_frame_ready(GB_PPU *ppu);

const uint16_t *gb_ppu_framebuffer(const GB_PPU *ppu);

#ifdef __cplusplus
}
#endif

#endif /* GB_PPU_H */
