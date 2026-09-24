#ifndef GB_CARTRIDGE_H
#define GB_CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../cpu/gb_cpu.h"
#include "../memory/gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_CARTRIDGE_HEADER_START       0x0100u
#define GB_CARTRIDGE_HEADER_END         0x014Fu
#define GB_CARTRIDGE_HEADER_MIN_SIZE    0x0150u
#define GB_CARTRIDGE_MAX_ROM_SIZE       (8u * 1024u * 1024u)
#define GB_CARTRIDGE_TITLE_SIZE         16u
#define GB_CARTRIDGE_NEW_LICENSEE_SIZE 2u
#define GB_CARTRIDGE_MANUFACTURER_SIZE  4u
#define GB_CARTRIDGE_NINTENDO_LOGO_SIZE 48u

typedef struct GB_CartridgeLoadOptions {
    bool validate_nintendo_logo;
    bool validate_header_checksum;
    bool validate_global_checksum;
    bool allow_header_rom_size_mismatch;
} GB_CartridgeLoadOptions;

typedef enum GB_CartridgeCGBSupport {
    GB_CARTRIDGE_CGB_DMG_COMPATIBLE = 0,
    GB_CARTRIDGE_CGB_COMPATIBLE,
    GB_CARTRIDGE_CGB_ONLY
} GB_CartridgeCGBSupport;

typedef enum GB_CartridgeMapper {
    GB_CARTRIDGE_MAPPER_NONE = 0,
    GB_CARTRIDGE_MAPPER_MBC1,
    GB_CARTRIDGE_MAPPER_MBC2,
    GB_CARTRIDGE_MAPPER_MMM01,
    GB_CARTRIDGE_MAPPER_MBC3,
    GB_CARTRIDGE_MAPPER_MBC5,
    GB_CARTRIDGE_MAPPER_MBC6,
    GB_CARTRIDGE_MAPPER_MBC7,
    GB_CARTRIDGE_MAPPER_POCKET_CAMERA,
    GB_CARTRIDGE_MAPPER_TAMA5,
    GB_CARTRIDGE_MAPPER_HUC3,
    GB_CARTRIDGE_MAPPER_HUC1,
    GB_CARTRIDGE_MAPPER_UNKNOWN
} GB_CartridgeMapper;

typedef struct GB_Cartridge {
    bool initialized;
    bool loaded;

    uint8_t *rom;
    size_t rom_size;

    uint8_t *ram;
    size_t ram_size;

    uint8_t entry_point[4];
    uint8_t nintendo_logo[GB_CARTRIDGE_NINTENDO_LOGO_SIZE];
    uint8_t title[GB_CARTRIDGE_TITLE_SIZE];
    uint8_t manufacturer_code[GB_CARTRIDGE_MANUFACTURER_SIZE];
    uint8_t new_licensee_code[GB_CARTRIDGE_NEW_LICENSEE_SIZE];
    uint8_t cgb_flag;
    uint8_t sgb_flag;
    uint8_t cartridge_type_code;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    uint8_t destination_code;
    uint8_t old_licensee_code;
    uint8_t rom_version;
    uint8_t header_checksum;
    uint16_t global_checksum;

    size_t declared_rom_size;
    size_t declared_ram_size;
    uint32_t declared_rom_bank_count;
    uint32_t declared_ram_bank_count;
    GB_CartridgeMapper mapper;
    GB_CartridgeCGBSupport cgb_support;

    bool has_ram;
    bool has_battery;
    bool has_rtc;
    bool has_rumble;
    bool sgb_compatible;

    bool nintendo_logo_valid;
    bool header_checksum_valid;
    bool global_checksum_valid;

    /* Common MBC state. The fields are owned by this cartridge instance. */
    bool ram_enabled;
    uint8_t mbc1_rom_bank_low5;
    uint8_t mbc1_bank_high2;
    uint8_t mbc1_banking_mode;

    uint8_t mbc2_rom_bank;

    uint8_t mbc3_rom_bank;
    uint8_t mbc3_ram_rtc_select;
    uint8_t mbc3_latch_state;

    uint16_t mbc5_rom_bank;
    uint8_t mbc5_ram_bank;
    bool mbc5_rumble_active;

    /* MBC3 RTC live state and latched snapshot. Time is advanced from emulated
     * CPU T-cycles; persistent RTC storage belongs to the later save stage. */
    uint8_t rtc_seconds;
    uint8_t rtc_minutes;
    uint8_t rtc_hours;
    uint16_t rtc_days;
    bool rtc_halt;
    bool rtc_carry;

    uint8_t rtc_latched_seconds;
    uint8_t rtc_latched_minutes;
    uint8_t rtc_latched_hours;
    uint16_t rtc_latched_days;
    bool rtc_latched_halt;
    bool rtc_latched_carry;
    uint32_t rtc_cycle_remainder;

    bool bus_supported;
    bool ram_dirty;
} GB_Cartridge;

void gb_cartridge_load_options_default(GB_CartridgeLoadOptions *options);

GB_Result gb_cartridge_init(GB_Cartridge *cartridge, GB_Error *error);
GB_Result gb_cartridge_unload(GB_Cartridge *cartridge, GB_Error *error);

GB_Result gb_cartridge_load(GB_Cartridge *cartridge,
                            const char *path,
                            const GB_CartridgeLoadOptions *options,
                            GB_Error *error);

GB_Result gb_cartridge_load_buffer(GB_Cartridge *cartridge,
                                   const uint8_t *data,
                                   size_t size,
                                   const GB_CartridgeLoadOptions *options,
                                   GB_Error *error);

bool gb_cartridge_is_loaded(const GB_Cartridge *cartridge);
const char *gb_cartridge_mapper_name(GB_CartridgeMapper mapper);
const char *gb_cartridge_cgb_support_name(GB_CartridgeCGBSupport support);
const char *gb_cartridge_type_name(uint8_t cartridge_type_code);

GB_Result gb_cartridge_get_memory_bus(GB_Cartridge *cartridge,
                                      GB_MemoryCartridgeBus *bus,
                                      GB_Error *error);

GB_Result gb_cartridge_read8(GB_Cartridge *cartridge,
                             uint16_t address,
                             uint8_t *value,
                             GB_Error *error);
GB_Result gb_cartridge_write8(GB_Cartridge *cartridge,
                              uint16_t address,
                              uint8_t value,
                              GB_Error *error);

GB_Result gb_cartridge_tick(void *user, uint32_t t_cycles, GB_Error *error);

bool gb_cartridge_rumble_active(const GB_Cartridge *cartridge);

bool gb_cartridge_ram_dirty(const GB_Cartridge *cartridge);
void gb_cartridge_clear_ram_dirty(GB_Cartridge *cartridge);

#ifdef __cplusplus
}
#endif

#endif /* GB_CARTRIDGE_H */
