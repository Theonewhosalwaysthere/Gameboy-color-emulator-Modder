#include "gb_ppu.h"

#include <stdio.h>
#include <string.h>

#define STAT_MODE0_ENABLE 0x08u
#define STAT_MODE1_ENABLE 0x10u
#define STAT_MODE2_ENABLE 0x20u
#define STAT_LYC_ENABLE   0x40u
#define STAT_FIXED_HIGH   0x80u

#define LCDC_BG_ENABLE     0x01u
#define LCDC_OBJ_ENABLE    0x02u
#define LCDC_OBJ_SIZE      0x04u
#define LCDC_BG_MAP        0x08u
#define LCDC_TILE_DATA     0x10u
#define LCDC_WINDOW_ENABLE 0x20u
#define LCDC_WINDOW_MAP    0x40u
#define LCDC_ENABLE        0x80u

static void ppu_error(GB_Error *error, GB_Result code, const char *message,
                      uint16_t address)
{
    if (error == NULL) {
        return;
    }

    gb_error_clear(error);
    error->code = code;
    error->pc = address;
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static GB_Result require_ppu(const GB_PPU *ppu, GB_Error *error)
{
    if (ppu == NULL) {
        ppu_error(error, GB_RESULT_NULL_ARGUMENT, "PPU pointer is NULL", 0u);
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!ppu->initialized) {
        ppu_error(error, GB_RESULT_BAD_STATE, "PPU is not initialized", 0u);
        return GB_RESULT_BAD_STATE;
    }
    if (ppu->memory == NULL || !ppu->memory_connected) {
        ppu_error(error, GB_RESULT_BAD_STATE,
                  "PPU is not connected to a memory instance", 0u);
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static bool ppu_cgb_mode(const GB_PPU *ppu)
{
    return ppu->memory != NULL && ppu->memory->mode == GB_MEMORY_MODE_CGB;
}

static uint16_t dmg_shade_color(uint8_t shade)
{
    switch (shade & 0x03u) {
    case 0u: return GB_PPU_RGB555_WHITE;
    case 1u: return GB_PPU_RGB555_LIGHT_GRAY;
    case 2u: return GB_PPU_RGB555_DARK_GRAY;
    default: return GB_PPU_RGB555_BLACK;
    }
}

static uint16_t cgb_palette_color(const uint8_t *palette_ram,
                                  uint8_t palette, uint8_t color)
{
    size_t index = (size_t)(palette & 0x07u) * 8u + (size_t)(color & 0x03u) * 2u;
    return (uint16_t)(palette_ram[index] |
                      ((uint16_t)palette_ram[index + 1u] << 8));
}

static uint16_t map_bg_color(const GB_PPU *ppu, uint8_t color, uint8_t palette)
{
    if (ppu_cgb_mode(ppu)) {
        return cgb_palette_color(ppu->bg_palette_ram, palette, color);
    }

    uint8_t shift = (uint8_t)((color & 0x03u) * 2u);
    return dmg_shade_color((uint8_t)((ppu->bgp >> shift) & 0x03u));
}

static uint16_t map_obj_color(const GB_PPU *ppu, uint8_t color, uint8_t palette)
{
    if (ppu_cgb_mode(ppu)) {
        return cgb_palette_color(ppu->obj_palette_ram, palette, color);
    }

    uint8_t palette_reg = (palette & 0x01u) != 0u ? ppu->obp1 : ppu->obp0;
    uint8_t shift = (uint8_t)((color & 0x03u) * 2u);
    uint8_t shade = (uint8_t)(palette_reg >> shift);
    return dmg_shade_color((uint8_t)(shade & 0x03u));
}

static void clear_framebuffer(GB_PPU *ppu, uint16_t color)
{
    for (size_t i = 0u; i < GB_PPU_FRAMEBUFFER_PIXELS; ++i) {
        ppu->framebuffer[i] = color;
    }
}

static bool window_active(const GB_PPU *ppu)
{
    if ((ppu->lcdc & LCDC_WINDOW_ENABLE) == 0u) {
        return false;
    }
    if (!ppu_cgb_mode(ppu) && (ppu->lcdc & LCDC_BG_ENABLE) == 0u) {
        return false;
    }
    if (ppu->ly < ppu->wy) {
        return false;
    }
    return ppu->wx <= 166u;
}

static int32_t window_screen_start(const GB_PPU *ppu)
{
    int32_t start = (int32_t)ppu->wx - 7;
    return start < 0 ? 0 : start;
}

static uint16_t tile_address(const GB_PPU *ppu, uint8_t tile_index, uint8_t row)
{
    if ((ppu->lcdc & LCDC_TILE_DATA) != 0u) {
        return (uint16_t)((uint16_t)tile_index * 16u + (uint16_t)row * 2u);
    }

    int8_t signed_index = (int8_t)tile_index;
    int32_t address = 0x1000 + ((int32_t)signed_index * 16) + ((int32_t)row * 2);
    return (uint16_t)address;
}

static uint8_t read_tile_pixel(const GB_PPU *ppu, uint16_t tile_data_address,
                               uint8_t bank, uint8_t pixel_x)
{
    const uint8_t *vram = ppu->memory->vram[bank & 0x01u];
    uint8_t lo = vram[tile_data_address];
    uint8_t hi = vram[(uint16_t)(tile_data_address + 1u)];
    uint8_t bit = (uint8_t)(7u - (pixel_x & 0x07u));
    uint8_t shifted_hi = (uint8_t)(hi >> bit);
    uint8_t shifted_lo = (uint8_t)(lo >> bit);
    uint8_t high_bit = (uint8_t)(shifted_hi & 1);
    uint8_t low_bit = (uint8_t)(shifted_lo & 1);
    return (uint8_t)((high_bit << 1u) | low_bit);
}

static uint8_t read_tile_index(const GB_PPU *ppu, uint16_t map_base,
                               uint8_t map_x, uint8_t map_y, uint8_t *attributes)
{
    uint16_t map_offset = (uint16_t)(map_base +
                                     ((uint16_t)(map_y & 31u) * 32u) +
                                     (uint16_t)(map_x & 31u));
    size_t vram_offset = (size_t)(map_offset - 0x8000u);
    uint8_t tile_index = ppu->memory->vram[0][vram_offset];

    if (attributes != NULL) {
        *attributes = ppu_cgb_mode(ppu) ? ppu->memory->vram[1][vram_offset] : 0u;
    }
    return tile_index;
}

static void render_background_pixel(GB_PPU *ppu, uint8_t x,
                                    uint8_t *color_index, bool *priority,
                                    uint16_t *rgb)
{
    *color_index = 0u;
    *priority = false;
    *rgb = map_bg_color(ppu, 0u, 0u);

    if (!ppu_cgb_mode(ppu) && (ppu->lcdc & LCDC_BG_ENABLE) == 0u) {
        return;
    }

    uint8_t map_x;
    uint8_t map_y;
    uint16_t map_base;
    uint8_t pixel_x;
    uint8_t pixel_y;

    bool use_window = window_active(ppu);
    int32_t window_start = window_screen_start(ppu);

    if (use_window && (int32_t)x >= window_start) {
        ppu->window_used_this_line = true;
        uint16_t wx = (uint16_t)((int32_t)x - window_start);
        map_x = (uint8_t)wx;
        map_y = ppu->window_line;
        map_base = (ppu->lcdc & LCDC_WINDOW_MAP) != 0u ? 0x9C00u : 0x9800u;
        pixel_x = (uint8_t)(map_x & 0x07u);
        pixel_y = (uint8_t)(map_y & 0x07u);
    } else {
        map_x = (uint8_t)((uint16_t)ppu->scx + x);
        map_y = (uint8_t)((uint16_t)ppu->scy + ppu->ly);
        map_base = (ppu->lcdc & LCDC_BG_MAP) != 0u ? 0x9C00u : 0x9800u;
        pixel_x = (uint8_t)(map_x & 0x07u);
        pixel_y = (uint8_t)(map_y & 0x07u);
    }

    uint8_t attributes = 0u;
    uint8_t tile_index = read_tile_index(ppu, map_base,
                                         (uint8_t)(map_x >> 3u),
                                         (uint8_t)(map_y >> 3u),
                                         &attributes);

    if ((attributes & 0x40u) != 0u) {
        pixel_y = (uint8_t)(7u - pixel_y);
    }
    if ((attributes & 0x20u) != 0u) {
        pixel_x = (uint8_t)(7u - pixel_x);
    }

    uint8_t bank = ppu_cgb_mode(ppu) && ((attributes & 0x08u) != 0u) ? 1u : 0u;
    uint16_t data_address = tile_address(ppu, tile_index, pixel_y);
    uint8_t color = read_tile_pixel(ppu, data_address, bank, pixel_x);
    uint8_t palette = ppu_cgb_mode(ppu) ? (uint8_t)(attributes & 0x07u) : 0u;

    *color_index = color;
    *priority = ppu_cgb_mode(ppu) &&
                (ppu->lcdc & LCDC_BG_ENABLE) != 0u &&
                ((attributes & 0x80u) != 0u) && color != 0u;
    *rgb = map_bg_color(ppu, color, palette);
}

static int sprite_compare_dmg(const GB_PPU_Sprite *a, const GB_PPU_Sprite *b)
{
    if (a->x != b->x) {
        return a->x < b->x ? -1 : 1;
    }
    if (a->oam_index != b->oam_index) {
        return a->oam_index < b->oam_index ? -1 : 1;
    }
    return 0;
}

static void sort_render_sprites(GB_PPU *ppu)
{
    if (ppu_cgb_mode(ppu) && (ppu->opri & 0x01u) == 0u) {
        return;
    }

    for (uint8_t i = 1u; i < ppu->line_sprite_count; ++i) {
        GB_PPU_Sprite key = ppu->line_sprites[i];
        uint8_t j = i;
        while (j > 0u && sprite_compare_dmg(&key, &ppu->line_sprites[j - 1u]) < 0) {
            ppu->line_sprites[j] = ppu->line_sprites[j - 1u];
            --j;
        }
        ppu->line_sprites[j] = key;
    }
}

static void evaluate_sprites(GB_PPU *ppu)
{
    ppu->line_sprite_count = 0u;
    if ((ppu->lcdc & LCDC_OBJ_ENABLE) == 0u) {
        return;
    }

    uint8_t height = (ppu->lcdc & LCDC_OBJ_SIZE) != 0u ? 16u : 8u;
    for (uint8_t i = 0u; i < 40u && ppu->line_sprite_count < GB_PPU_MAX_LINE_SPRITES; ++i) {
        size_t base = (size_t)i * 4u;
        int32_t sprite_y = (int32_t)ppu->memory->oam[base];
        uint8_t sprite_x = ppu->memory->oam[base + 1u];
        int32_t line = (int32_t)ppu->ly + 16;
        int32_t row = line - sprite_y;

        if (row < 0 || row >= (int32_t)height) {
            continue;
        }

        if (sprite_x >= 168u && sprite_x != 0u) {
            continue;
        }

        GB_PPU_Sprite *sprite = &ppu->line_sprites[ppu->line_sprite_count++];
        sprite->oam_index = i;
        sprite->y = (uint8_t)sprite_y;
        sprite->x = sprite_x;
        sprite->tile = ppu->memory->oam[base + 2u];
        sprite->attributes = ppu->memory->oam[base + 3u];
    }

    sort_render_sprites(ppu);
}

static uint16_t calculate_mode3_dots(const GB_PPU *ppu)
{
    uint16_t dots = (uint16_t)(GB_PPU_MODE3_MIN_DOTS + (ppu->scx & 0x07u));

    if (window_active(ppu) && window_screen_start(ppu) > 0) {
        dots = (uint16_t)(dots + 6u);
    }

    GB_PPU_Sprite sprites[GB_PPU_MAX_LINE_SPRITES];
    memcpy(sprites, ppu->line_sprites, sizeof(sprites));
    for (uint8_t i = 1u; i < ppu->line_sprite_count; ++i) {
        GB_PPU_Sprite key = sprites[i];
        uint8_t j = i;
        while (j > 0u && sprite_compare_dmg(&key, &sprites[j - 1u]) < 0) {
            sprites[j] = sprites[j - 1u];
            --j;
        }
        sprites[j] = key;
    }

    for (uint8_t i = 0u; i < ppu->line_sprite_count; ++i) {
        const GB_PPU_Sprite *sprite = &sprites[i];
        if (sprite->x == 0u) {
            dots = (uint16_t)(dots + 11u);
            continue;
        }

        int32_t screen_x = (int32_t)sprite->x - 8;
        int32_t tile_pixel;
        int32_t window_start = window_screen_start(ppu);
        if (window_active(ppu) && screen_x >= window_start) {
            tile_pixel = (screen_x - window_start) & 7;
        } else {
            tile_pixel = (screen_x + (int32_t)(ppu->scx & 0x07u)) & 7;
        }
        uint16_t penalty = 6u;
        if (tile_pixel <= 5) {
            penalty = (uint16_t)(penalty + (uint16_t)(5 - tile_pixel));
        }
        dots = (uint16_t)(dots + penalty);
    }

    if (dots < GB_PPU_MODE3_MIN_DOTS) {
        dots = GB_PPU_MODE3_MIN_DOTS;
    }
    if (dots > GB_PPU_MODE3_MAX_DOTS) {
        dots = GB_PPU_MODE3_MAX_DOTS;
    }
    return dots;
}

static void render_scanline_pixel(GB_PPU *ppu, uint8_t screen_x)
{
    uint8_t color_index = 0u;
    bool priority = false;
    uint16_t rgb = 0u;
    render_background_pixel(ppu, screen_x, &color_index, &priority, &rgb);
    ppu->bg_color_index[screen_x] = color_index;
    ppu->bg_priority[screen_x] = priority;
    ppu->framebuffer[(size_t)ppu->ly * GB_PPU_WIDTH + screen_x] = rgb;

    if ((ppu->lcdc & LCDC_OBJ_ENABLE) == 0u) return;

    uint8_t height = (ppu->lcdc & LCDC_OBJ_SIZE) != 0u ? 16u : 8u;
    for (uint8_t i = 0u; i < ppu->line_sprite_count; ++i) {
        const GB_PPU_Sprite *sprite = &ppu->line_sprites[i];
        int32_t sprite_screen_x = (int32_t)sprite->x - 8;
        int32_t pixel_x = (int32_t)screen_x - sprite_screen_x;
        if (pixel_x < 0 || pixel_x >= 8) continue;

        int32_t pixel_y = (int32_t)ppu->ly + 16 - (int32_t)sprite->y;
        if (pixel_y < 0 || pixel_y >= (int32_t)height) continue;

        if ((sprite->attributes & 0x40u) != 0u) pixel_y = (int32_t)height - 1 - pixel_y;
        if ((sprite->attributes & 0x20u) != 0u) pixel_x = 7 - pixel_x;

        uint8_t tile = sprite->tile;
        if (height == 16u) {
            tile &= 0xFEu;
            if (pixel_y >= 8) ++tile;
        }

        uint8_t row = (uint8_t)(pixel_y & 0x07);
        uint8_t bank = ppu_cgb_mode(ppu) && ((sprite->attributes & 0x08u) != 0u) ? 1u : 0u;
        uint16_t data_address = (uint16_t)((uint16_t)tile * 16u + (uint16_t)row * 2u);
        uint8_t color = read_tile_pixel(ppu, data_address, bank, (uint8_t)pixel_x);
        if (color == 0u) continue;

        bool bg_nonzero = ppu->bg_color_index[screen_x] != 0u;
        bool bg_wins = false;
        if (bg_nonzero) {
            if (ppu_cgb_mode(ppu)) {
                if ((ppu->lcdc & LCDC_BG_ENABLE) != 0u) {
                    bg_wins = ppu->bg_priority[screen_x] || ((sprite->attributes & 0x80u) != 0u);
                }
            } else {
                bg_wins = (sprite->attributes & 0x80u) != 0u;
            }
        }
        if (bg_wins) continue;

        uint8_t palette = ppu_cgb_mode(ppu)
            ? (uint8_t)(sprite->attributes & 0x07u)
            : (uint8_t)((sprite->attributes >> 4u) & 0x01u);
        ppu->framebuffer[(size_t)ppu->ly * GB_PPU_WIDTH + screen_x] =
            map_obj_color(ppu, color, palette);
        break;
    }
}

static bool stat_irq_condition(const GB_PPU *ppu)
{
    bool mode_condition = false;
    switch (ppu->mode) {
    case GB_PPU_MODE_HBLANK:
        mode_condition = (ppu->stat_select & STAT_MODE0_ENABLE) != 0u;
        break;
    case GB_PPU_MODE_VBLANK:
        mode_condition = (ppu->stat_select & STAT_MODE1_ENABLE) != 0u;
        break;
    case GB_PPU_MODE_OAM:
        mode_condition = (ppu->stat_select & STAT_MODE2_ENABLE) != 0u;
        break;
    case GB_PPU_MODE_XFER:
        mode_condition = false;
        break;
    default:
        break;
    }

    bool lyc_condition = ((ppu->stat_select & STAT_LYC_ENABLE) != 0u) &&
                         (ppu->ly == ppu->lyc);
    return mode_condition || lyc_condition;
}

static GB_Result update_stat_irq_line(GB_PPU *ppu, GB_Error *error)
{
    bool line = stat_irq_condition(ppu);
    if (line && !ppu->stat_irq_line) {
        GB_Result result = gb_memory_request_interrupt(ppu->memory,
                                                        GB_INTERRUPT_STAT,
                                                        error);
        if (result != GB_RESULT_OK) {
            ppu_error(error, GB_RESULT_PPU_ERROR,
                      "Failed to request LCD STAT interrupt", GB_PPU_ADDR_STAT);
            return GB_RESULT_PPU_ERROR;
        }
    }
    ppu->stat_irq_line = line;
    return GB_RESULT_OK;
}

static GB_Result set_mode(GB_PPU *ppu, GB_PPU_Mode mode, GB_Error *error)
{
    ppu->mode = mode;
    return update_stat_irq_line(ppu, error);
}

static void reset_scan_position(GB_PPU *ppu)
{
    ppu->dot = 0u;
    ppu->ly = 0u;
    ppu->window_line = 0u;
    ppu->window_used_this_line = false;
}

static GB_Result start_visible_line(GB_PPU *ppu, GB_Error *error)
{
    ppu->dot = 0u;
    ppu->window_used_this_line = false;
    GB_Result result = set_mode(ppu, GB_PPU_MODE_OAM, error);
    if (result != GB_RESULT_OK) {
        return result;
    }
    evaluate_sprites(ppu);
    return GB_RESULT_OK;
}

static GB_Result write_lcdc(GB_PPU *ppu, uint8_t value, GB_Error *error)
{
    bool was_enabled = (ppu->lcdc & LCDC_ENABLE) != 0u;
    bool now_enabled = (value & LCDC_ENABLE) != 0u;
    ppu->lcdc = value;

    if (was_enabled && !now_enabled) {
        reset_scan_position(ppu);
        ppu->mode = GB_PPU_MODE_HBLANK;
        ppu->stat_irq_line = false;
        clear_framebuffer(ppu, GB_PPU_RGB555_WHITE);
        return update_stat_irq_line(ppu, error);
    }

    if (!was_enabled && now_enabled) {
        reset_scan_position(ppu);
        ppu->frame_ready = false;
        ppu->mode = GB_PPU_MODE_OAM;
        evaluate_sprites(ppu);
        return update_stat_irq_line(ppu, error);
    }

    return update_stat_irq_line(ppu, error);
}

static bool video_cpu_access_allowed(void *user, uint16_t address, bool write)
{
    (void)write;
    GB_PPU *ppu = (GB_PPU *)user;
    if (ppu == NULL || !ppu->initialized) {
        return true;
    }
    if ((ppu->lcdc & LCDC_ENABLE) == 0u) {
        return true;
    }

    if (address >= 0x8000u && address <= 0x9FFFu) {
        return ppu->mode != GB_PPU_MODE_XFER;
    }
    if (address >= 0xFE00u && address <= 0xFE9Fu) {
        return ppu->mode == GB_PPU_MODE_HBLANK ||
               ppu->mode == GB_PPU_MODE_VBLANK;
    }
    return true;
}

static GB_Result cgb_palette_read(GB_PPU *ppu, uint16_t address,
                                  uint8_t *value, GB_Error *error)
{
    if (!ppu_cgb_mode(ppu)) {
        *value = 0xFFu;
        return GB_RESULT_OK;
    }

    switch (address) {
    case GB_PPU_ADDR_BCPS:
        *value = ppu->bcps;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_BCPD:
        *value = (ppu->mode == GB_PPU_MODE_XFER) ? 0xFFu
            : ppu->bg_palette_ram[ppu->bcps & 0x3Fu];
        return GB_RESULT_OK;
    case GB_PPU_ADDR_OCPS:
        *value = ppu->ocps;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_OCPD:
        *value = (ppu->mode == GB_PPU_MODE_XFER) ? 0xFFu
            : ppu->obj_palette_ram[ppu->ocps & 0x3Fu];
        return GB_RESULT_OK;
    case GB_PPU_ADDR_OPRI:
        *value = (uint8_t)(0xFEu | (ppu->opri & 0x01u));
        return GB_RESULT_OK;
    default:
        ppu_error(error, GB_RESULT_INVALID_ARGUMENT,
                  "Invalid CGB palette register", address);
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

static GB_Result cgb_palette_write(GB_PPU *ppu, uint16_t address,
                                   uint8_t value, GB_Error *error)
{
    if (!ppu_cgb_mode(ppu)) {
        return GB_RESULT_OK;
    }

    switch (address) {
    case GB_PPU_ADDR_BCPS:
        ppu->bcps = (uint8_t)(value & 0xBFu);
        return GB_RESULT_OK;
    case GB_PPU_ADDR_BCPD:
        if (ppu->mode != GB_PPU_MODE_XFER) {
            size_t index = ppu->bcps & 0x3Fu;
            ppu->bg_palette_ram[index] = (uint8_t)(value & ((index & 1u) != 0u ? 0x7Fu : 0xFFu));
        }
        if ((ppu->bcps & 0x80u) != 0u) {
            ppu->bcps = (uint8_t)((ppu->bcps & 0x80u) |
                                  ((ppu->bcps + 1u) & 0x3Fu));
        }
        return GB_RESULT_OK;
    case GB_PPU_ADDR_OCPS:
        ppu->ocps = (uint8_t)(value & 0xBFu);
        return GB_RESULT_OK;
    case GB_PPU_ADDR_OCPD:
        if (ppu->mode != GB_PPU_MODE_XFER) {
            size_t index = ppu->ocps & 0x3Fu;
            ppu->obj_palette_ram[index] = (uint8_t)(value & ((index & 1u) != 0u ? 0x7Fu : 0xFFu));
        }
        if ((ppu->ocps & 0x80u) != 0u) {
            ppu->ocps = (uint8_t)((ppu->ocps & 0x80u) |
                                  ((ppu->ocps + 1u) & 0x3Fu));
        }
        return GB_RESULT_OK;
    case GB_PPU_ADDR_OPRI:
        ppu->opri = (uint8_t)(value & 0x01u);
        return GB_RESULT_OK;
    default:
        ppu_error(error, GB_RESULT_INVALID_ARGUMENT,
                  "Invalid CGB palette register", address);
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_Result gb_ppu_init(GB_PPU *ppu, GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);
    if (ppu == NULL || memory == NULL) {
        ppu_error(error, GB_RESULT_NULL_ARGUMENT,
                  "PPU and memory pointers are required", 0u);
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(ppu, 0, sizeof(*ppu));
    ppu->memory = memory;
    ppu->initialized = true;
    ppu->memory_connected = true;

    GB_Result result = gb_memory_map_io_device(memory,
                                               GB_PPU_ADDR_LCDC,
                                               GB_PPU_ADDR_LYC,
                                               ppu,
                                               gb_ppu_read8,
                                               gb_ppu_write8,
                                               gb_ppu_device_tick,
                                               &ppu->io_lcdc_stat_device,
                                               error);
    if (result != GB_RESULT_OK) {
        memset(ppu, 0, sizeof(*ppu));
        return result;
    }

    result = gb_memory_map_io_device(memory,
                                     GB_PPU_ADDR_BGP,
                                     GB_PPU_ADDR_WX,
                                     ppu,
                                     gb_ppu_read8,
                                     gb_ppu_write8,
                                     NULL,
                                     &ppu->io_scroll_device,
                                     error);
    if (result != GB_RESULT_OK) {
        (void)gb_memory_unmap_io_device(memory, ppu->io_lcdc_stat_device, NULL);
        memset(ppu, 0, sizeof(*ppu));
        return result;
    }

    result = gb_memory_map_io_device(memory,
                                     GB_PPU_ADDR_BCPS,
                                     GB_PPU_ADDR_OPRI,
                                     ppu,
                                     gb_ppu_read8,
                                     gb_ppu_write8,
                                     NULL,
                                     &ppu->io_cgb_palette_device,
                                     error);
    if (result != GB_RESULT_OK) {
        (void)gb_memory_unmap_io_device(memory, ppu->io_scroll_device, NULL);
        (void)gb_memory_unmap_io_device(memory, ppu->io_lcdc_stat_device, NULL);
        memset(ppu, 0, sizeof(*ppu));
        return result;
    }

    GB_MemoryVideoController controller = gb_ppu_memory_controller(ppu);
    result = gb_memory_set_video_controller(memory, &controller, error);
    if (result != GB_RESULT_OK) {
        (void)gb_memory_unmap_io_device(memory, ppu->io_cgb_palette_device, NULL);
        (void)gb_memory_unmap_io_device(memory, ppu->io_scroll_device, NULL);
        (void)gb_memory_unmap_io_device(memory, ppu->io_lcdc_stat_device, NULL);
        memset(ppu, 0, sizeof(*ppu));
        return result;
    }

    ppu->mapped = true;
    return gb_ppu_reset(ppu, error);
}

GB_Result gb_ppu_reset(GB_PPU *ppu, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_ppu(ppu, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    ppu->lcdc = 0x91u;
    ppu->stat_select = 0u;
    ppu->scy = 0u;
    ppu->scx = 0u;
    ppu->ly = 0u;
    ppu->lyc = 0u;
    ppu->bgp = 0xFCu;
    ppu->obp0 = 0xFFu;
    ppu->obp1 = 0xFFu;
    ppu->wy = 0u;
    ppu->wx = 0u;
    ppu->bcps = 0u;
    ppu->ocps = 0u;
    ppu->opri = 0u;
    ppu->mode = GB_PPU_MODE_OAM;
    ppu->dot = 0u;
    ppu->mode3_dots = GB_PPU_MODE3_MIN_DOTS;
    ppu->window_line = 0u;
    ppu->window_used_this_line = false;
    ppu->stat_irq_line = false;
    ppu->frame_ready = false;
    ppu->line_sprite_count = 0u;

    for (size_t i = 0u; i < GB_PPU_CGB_PALETTE_BYTES; ++i) {
        ppu->bg_palette_ram[i] = (uint8_t)((i & 1u) == 0u ? 0xFFu : 0x7Fu);
        ppu->obj_palette_ram[i] = 0u;
    }

    clear_framebuffer(ppu, GB_PPU_RGB555_WHITE);
    evaluate_sprites(ppu);
    return update_stat_irq_line(ppu, error);
}

GB_Result gb_ppu_destroy(GB_PPU *ppu, GB_Error *error)
{
    gb_error_clear(error);
    if (ppu == NULL) {
        ppu_error(error, GB_RESULT_NULL_ARGUMENT, "PPU pointer is NULL", 0u);
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!ppu->initialized) {
        return GB_RESULT_OK;
    }

    if (ppu->memory != NULL && ppu->mapped) {
        GB_Result result = gb_memory_clear_video_controller(ppu->memory, error);
        if (result != GB_RESULT_OK) {
            return result;
        }
        result = gb_memory_unmap_io_device(ppu->memory, ppu->io_cgb_palette_device, error);
        if (result != GB_RESULT_OK) return result;
        result = gb_memory_unmap_io_device(ppu->memory, ppu->io_scroll_device, error);
        if (result != GB_RESULT_OK) return result;
        result = gb_memory_unmap_io_device(ppu->memory, ppu->io_lcdc_stat_device, error);
        if (result != GB_RESULT_OK) return result;
    }

    memset(ppu, 0, sizeof(*ppu));
    return GB_RESULT_OK;
}

GB_Result gb_ppu_tick(GB_PPU *ppu, uint32_t t_cycles, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_ppu(ppu, error);
    if (result != GB_RESULT_OK) return result;

    if ((ppu->lcdc & LCDC_ENABLE) == 0u || t_cycles == 0u) {
        return GB_RESULT_OK;
    }

    for (uint32_t i = 0u; i < t_cycles; ++i) {
        ++ppu->dot;

        switch (ppu->mode) {
        case GB_PPU_MODE_OAM:
            if (ppu->dot >= GB_PPU_MODE2_DOTS) {
                ppu->dot = 0u;
                ppu->window_used_this_line = false;
                evaluate_sprites(ppu);
                ppu->mode3_dots = calculate_mode3_dots(ppu);
                /* Make the first pixel visible at the Mode 2 -> Mode 3
                 * boundary. The remaining pixels are produced one dot at a
                 * time during Mode 3. This preserves the public tick
                 * behavior used by existing tests while keeping rendering
                 * progressively synchronized with Mode 3. */
                render_scanline_pixel(ppu, 0u);
                result = set_mode(ppu, GB_PPU_MODE_XFER, error);
                if (result != GB_RESULT_OK) return result;
            }
            break;

        case GB_PPU_MODE_XFER:
            if (ppu->dot > 0u && ppu->dot < GB_PPU_WIDTH) {
                render_scanline_pixel(ppu, (uint8_t)ppu->dot);
            }
            if (ppu->dot >= ppu->mode3_dots) {
                if (ppu->window_used_this_line) {
                    ppu->window_line = (uint8_t)(ppu->window_line + 1u);
                }
                ppu->dot = 0u;
                result = set_mode(ppu, GB_PPU_MODE_HBLANK, error);
                if (result != GB_RESULT_OK) return result;
            }
            break;

        case GB_PPU_MODE_HBLANK:
            if (ppu->dot >= (uint16_t)(GB_PPU_SCANLINE_DOTS - GB_PPU_MODE2_DOTS - ppu->mode3_dots)) {
                ppu->dot = 0u;
                ppu->ly = (uint8_t)(ppu->ly + 1u);

                if (ppu->ly == GB_PPU_VISIBLE_SCANLINES) {
                    ppu->frame_ready = true;
                    result = gb_memory_request_interrupt(ppu->memory,
                                                         GB_INTERRUPT_VBLANK,
                                                         error);
                    if (result != GB_RESULT_OK) {
                        ppu_error(error, GB_RESULT_PPU_ERROR,
                                  "Failed to request VBlank interrupt", GB_PPU_ADDR_LY);
                        return GB_RESULT_PPU_ERROR;
                    }
                    result = set_mode(ppu, GB_PPU_MODE_VBLANK, error);
                    if (result != GB_RESULT_OK) return result;
                } else {
                    result = start_visible_line(ppu, error);
                    if (result != GB_RESULT_OK) return result;
                }
            }
            break;

        case GB_PPU_MODE_VBLANK:
            if (ppu->dot >= GB_PPU_SCANLINE_DOTS) {
                ppu->dot = 0u;
                ppu->ly = (uint8_t)(ppu->ly + 1u);
                if (ppu->ly >= GB_PPU_TOTAL_SCANLINES) {
                    ppu->ly = 0u;
                    ppu->window_line = 0u;
                    result = start_visible_line(ppu, error);
                    if (result != GB_RESULT_OK) return result;
                }
            }
            break;

        default:
            ppu_error(error, GB_RESULT_PPU_ERROR,
                      "PPU entered an invalid mode", GB_PPU_ADDR_STAT);
            return GB_RESULT_PPU_ERROR;
        }

        result = update_stat_irq_line(ppu, error);
        if (result != GB_RESULT_OK) return result;
    }

    return GB_RESULT_OK;
}

GB_Result gb_ppu_device_tick(void *user, uint32_t t_cycles, GB_Error *error)
{
    return gb_ppu_tick((GB_PPU *)user, t_cycles, error);
}

GB_Result gb_ppu_read8(void *user, uint16_t address, uint8_t *value, GB_Error *error)
{
    gb_error_clear(error);
    if (user == NULL || value == NULL) {
        ppu_error(error, GB_RESULT_NULL_ARGUMENT,
                  "PPU register read requires PPU and output pointers", address);
        return GB_RESULT_NULL_ARGUMENT;
    }

    GB_PPU *ppu = (GB_PPU *)user;
    GB_Result result = require_ppu(ppu, error);
    if (result != GB_RESULT_OK) return result;

    switch (address) {
    case GB_PPU_ADDR_LCDC: *value = ppu->lcdc; return GB_RESULT_OK;
    case GB_PPU_ADDR_STAT:
        *value = (uint8_t)(STAT_FIXED_HIGH |
                           (ppu->stat_select & 0x78u) |
                           ((ppu->ly == ppu->lyc) ? 0x04u : 0u) |
                           ((uint8_t)ppu->mode & 0x03u));
        return GB_RESULT_OK;
    case GB_PPU_ADDR_SCY: *value = ppu->scy; return GB_RESULT_OK;
    case GB_PPU_ADDR_SCX: *value = ppu->scx; return GB_RESULT_OK;
    case GB_PPU_ADDR_LY: *value = ppu->ly; return GB_RESULT_OK;
    case GB_PPU_ADDR_LYC: *value = ppu->lyc; return GB_RESULT_OK;
    case GB_PPU_ADDR_BGP: *value = ppu->bgp; return GB_RESULT_OK;
    case GB_PPU_ADDR_OBP0: *value = ppu->obp0; return GB_RESULT_OK;
    case GB_PPU_ADDR_OBP1: *value = ppu->obp1; return GB_RESULT_OK;
    case GB_PPU_ADDR_WY: *value = ppu->wy; return GB_RESULT_OK;
    case GB_PPU_ADDR_WX: *value = ppu->wx; return GB_RESULT_OK;
    case GB_PPU_ADDR_BCPS:
    case GB_PPU_ADDR_BCPD:
    case GB_PPU_ADDR_OCPS:
    case GB_PPU_ADDR_OCPD:
    case GB_PPU_ADDR_OPRI:
        return cgb_palette_read(ppu, address, value, error);
    default:
        ppu_error(error, GB_RESULT_INVALID_ARGUMENT,
                  "Invalid PPU register address", address);
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_Result gb_ppu_write8(void *user, uint16_t address, uint8_t value, GB_Error *error)
{
    gb_error_clear(error);
    if (user == NULL) {
        ppu_error(error, GB_RESULT_NULL_ARGUMENT,
                  "PPU register write requires a PPU pointer", address);
        return GB_RESULT_NULL_ARGUMENT;
    }

    GB_PPU *ppu = (GB_PPU *)user;
    GB_Result result = require_ppu(ppu, error);
    if (result != GB_RESULT_OK) return result;

    switch (address) {
    case GB_PPU_ADDR_LCDC:
        return write_lcdc(ppu, value, error);
    case GB_PPU_ADDR_STAT:
        ppu->stat_select = (uint8_t)(value & 0x78u);
        return update_stat_irq_line(ppu, error);
    case GB_PPU_ADDR_SCY:
        ppu->scy = value;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_SCX:
        ppu->scx = value;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_LY:
        return GB_RESULT_OK;
    case GB_PPU_ADDR_LYC:
        ppu->lyc = value;
        return update_stat_irq_line(ppu, error);
    case GB_PPU_ADDR_BGP:
        ppu->bgp = value;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_OBP0:
        ppu->obp0 = value;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_OBP1:
        ppu->obp1 = value;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_WY:
        ppu->wy = value;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_WX:
        ppu->wx = value;
        return GB_RESULT_OK;
    case GB_PPU_ADDR_BCPS:
    case GB_PPU_ADDR_BCPD:
    case GB_PPU_ADDR_OCPS:
    case GB_PPU_ADDR_OCPD:
    case GB_PPU_ADDR_OPRI:
        return cgb_palette_write(ppu, address, value, error);
    default:
        ppu_error(error, GB_RESULT_INVALID_ARGUMENT,
                  "Invalid PPU register address", address);
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_MemoryVideoController gb_ppu_memory_controller(GB_PPU *ppu)
{
    GB_MemoryVideoController controller;
    controller.user = ppu;
    controller.cpu_access_allowed = video_cpu_access_allowed;
    return controller;
}

GB_PPU_Mode gb_ppu_get_mode(const GB_PPU *ppu)
{
    return (ppu != NULL && ppu->initialized) ? ppu->mode : GB_PPU_MODE_HBLANK;
}

uint8_t gb_ppu_get_ly(const GB_PPU *ppu)
{
    return (ppu != NULL && ppu->initialized) ? ppu->ly : 0u;
}

uint8_t gb_ppu_get_lyc(const GB_PPU *ppu)
{
    return (ppu != NULL && ppu->initialized) ? ppu->lyc : 0u;
}

uint8_t gb_ppu_get_lcdc(const GB_PPU *ppu)
{
    return (ppu != NULL && ppu->initialized) ? ppu->lcdc : 0u;
}

uint8_t gb_ppu_get_stat(const GB_PPU *ppu)
{
    if (ppu == NULL || !ppu->initialized) return 0u;
    return (uint8_t)(STAT_FIXED_HIGH |
                     (ppu->stat_select & 0x78u) |
                     ((ppu->ly == ppu->lyc) ? 0x04u : 0u) |
                     ((uint8_t)ppu->mode & 0x03u));
}

uint16_t gb_ppu_get_dot(const GB_PPU *ppu)
{
    return (ppu != NULL && ppu->initialized) ? ppu->dot : 0u;
}

uint16_t gb_ppu_get_mode3_dots(const GB_PPU *ppu)
{
    return (ppu != NULL && ppu->initialized) ? ppu->mode3_dots : 0u;
}

bool gb_ppu_is_lcd_enabled(const GB_PPU *ppu)
{
    return ppu != NULL && ppu->initialized && (ppu->lcdc & LCDC_ENABLE) != 0u;
}

bool gb_ppu_frame_ready(const GB_PPU *ppu)
{
    return ppu != NULL && ppu->initialized && ppu->frame_ready;
}

void gb_ppu_clear_frame_ready(GB_PPU *ppu)
{
    if (ppu != NULL && ppu->initialized) {
        ppu->frame_ready = false;
    }
}

const uint16_t *gb_ppu_framebuffer(const GB_PPU *ppu)
{
    return (ppu != NULL && ppu->initialized) ? ppu->framebuffer : NULL;
}
