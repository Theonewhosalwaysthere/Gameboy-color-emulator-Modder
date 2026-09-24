#include "gb_cartridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t NINTENDO_LOGO[GB_CARTRIDGE_NINTENDO_LOGO_SIZE] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
};

typedef struct CartridgeTypeInfo {
    uint8_t code;
    GB_CartridgeMapper mapper;
    const char *name;
    bool has_ram;
    bool battery;
    bool rtc;
    bool rumble;
} CartridgeTypeInfo;

static const CartridgeTypeInfo CARTRIDGE_TYPES[] = {
    {0x00u, GB_CARTRIDGE_MAPPER_NONE,        "ROM ONLY",                         false, false, false, false},
    {0x01u, GB_CARTRIDGE_MAPPER_MBC1,        "MBC1",                             false, false, false, false},
    {0x02u, GB_CARTRIDGE_MAPPER_MBC1,        "MBC1+RAM",                         true,  false, false, false},
    {0x03u, GB_CARTRIDGE_MAPPER_MBC1,        "MBC1+RAM+BATTERY",                 true,  true,  false, false},
    {0x05u, GB_CARTRIDGE_MAPPER_MBC2,        "MBC2",                             true,  false, false, false},
    {0x06u, GB_CARTRIDGE_MAPPER_MBC2,        "MBC2+BATTERY",                     true,  true,  false, false},
    {0x08u, GB_CARTRIDGE_MAPPER_NONE,        "ROM+RAM",                          true,  false, false, false},
    {0x09u, GB_CARTRIDGE_MAPPER_NONE,        "ROM+RAM+BATTERY",                  true,  true,  false, false},
    {0x0Bu, GB_CARTRIDGE_MAPPER_MMM01,       "MMM01",                            false, false, false, false},
    {0x0Cu, GB_CARTRIDGE_MAPPER_MMM01,       "MMM01+RAM",                        true,  false, false, false},
    {0x0Du, GB_CARTRIDGE_MAPPER_MMM01,       "MMM01+RAM+BATTERY",                true,  true,  false, false},
    {0x0Fu, GB_CARTRIDGE_MAPPER_MBC3,        "MBC3+TIMER+BATTERY",               false, true,  true,  false},
    {0x10u, GB_CARTRIDGE_MAPPER_MBC3,        "MBC3+TIMER+RAM+BATTERY",           true,  true,  true,  false},
    {0x11u, GB_CARTRIDGE_MAPPER_MBC3,        "MBC3",                             false, false, false, false},
    {0x12u, GB_CARTRIDGE_MAPPER_MBC3,        "MBC3+RAM",                         true,  false, false, false},
    {0x13u, GB_CARTRIDGE_MAPPER_MBC3,        "MBC3+RAM+BATTERY",                 true,  true,  false, false},
    {0x19u, GB_CARTRIDGE_MAPPER_MBC5,        "MBC5",                             false, false, false, false},
    {0x1Au, GB_CARTRIDGE_MAPPER_MBC5,        "MBC5+RAM",                         true,  false, false, false},
    {0x1Bu, GB_CARTRIDGE_MAPPER_MBC5,        "MBC5+RAM+BATTERY",                 true,  true,  false, false},
    {0x1Cu, GB_CARTRIDGE_MAPPER_MBC5,        "MBC5+RUMBLE",                     false, false, false, true},
    {0x1Du, GB_CARTRIDGE_MAPPER_MBC5,        "MBC5+RUMBLE+RAM",                 true,  false, false, true},
    {0x1Eu, GB_CARTRIDGE_MAPPER_MBC5,        "MBC5+RUMBLE+RAM+BATTERY",         true,  true,  false, true},
    {0x20u, GB_CARTRIDGE_MAPPER_MBC6,        "MBC6",                             true,  true,  false, false},
    {0x22u, GB_CARTRIDGE_MAPPER_MBC7,        "MBC7+SENSOR+RUMBLE+RAM+BATTERY",   true,  true,  false, true},
    {0xFCu, GB_CARTRIDGE_MAPPER_POCKET_CAMERA,"POCKET CAMERA",                   true,  true,  false, false},
    {0xFDu, GB_CARTRIDGE_MAPPER_TAMA5,       "TAMA5",                            true,  true,  false, false},
    {0xFEu, GB_CARTRIDGE_MAPPER_HUC3,        "HuC3",                             true,  true,  false, false},
    {0xFFu, GB_CARTRIDGE_MAPPER_HUC1,        "HuC1",                             true,  true,  false, false}
};

static void cartridge_error(GB_Error *error,
                            GB_Result code,
                            const char *message,
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

static GB_Result require_cartridge(GB_Cartridge *cartridge, GB_Error *error)
{
    if (cartridge == NULL) {
        cartridge_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Cartridge pointer is NULL", 0u);
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!cartridge->initialized) {
        cartridge_error(error, GB_RESULT_BAD_STATE,
                        "Cartridge has not been initialized", 0u);
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static const CartridgeTypeInfo *find_type_info(uint8_t code)
{
    for (size_t i = 0u; i < sizeof(CARTRIDGE_TYPES) / sizeof(CARTRIDGE_TYPES[0]); ++i) {
        if (CARTRIDGE_TYPES[i].code == code) {
            return &CARTRIDGE_TYPES[i];
        }
    }
    return NULL;
}

static bool decode_rom_size(uint8_t code, size_t *size_bytes, uint32_t *banks)
{
    if (size_bytes == NULL || banks == NULL) {
        return false;
    }

    if (code <= 0x08u) {
        *banks = (uint32_t)2u << code;
        *size_bytes = (size_t)(*banks) * 0x4000u;
        return true;
    }

    switch (code) {
    case 0x52u:
        *banks = 72u;
        *size_bytes = (size_t)(*banks) * 0x4000u;
        return true;
    case 0x53u:
        *banks = 80u;
        *size_bytes = (size_t)(*banks) * 0x4000u;
        return true;
    case 0x54u:
        *banks = 96u;
        *size_bytes = (size_t)(*banks) * 0x4000u;
        return true;
    default:
        return false;
    }
}

static bool decode_ram_size(uint8_t code, size_t *size_bytes, uint32_t *banks)
{
    if (size_bytes == NULL || banks == NULL) {
        return false;
    }

    switch (code) {
    case 0x00u:
        *size_bytes = 0u;
        *banks = 0u;
        return true;
    case 0x01u:
        *size_bytes = 0x0800u;
        *banks = 1u;
        return true;
    case 0x02u:
        *size_bytes = 0x2000u;
        *banks = 1u;
        return true;
    case 0x03u:
        *size_bytes = 0x8000u;
        *banks = 4u;
        return true;
    case 0x04u:
        *size_bytes = 0x20000u;
        *banks = 16u;
        return true;
    case 0x05u:
        *size_bytes = 0x10000u;
        *banks = 8u;
        return true;
    default:
        return false;
    }
}

static bool supported_cartridge_mapper(const GB_Cartridge *cartridge)
{
    switch (cartridge->mapper) {
    case GB_CARTRIDGE_MAPPER_NONE:
    case GB_CARTRIDGE_MAPPER_MBC1:
    case GB_CARTRIDGE_MAPPER_MBC2:
    case GB_CARTRIDGE_MAPPER_MBC3:
    case GB_CARTRIDGE_MAPPER_MBC5:
        return true;
    default:
        return false;
    }
}

static void reset_mbc_state(GB_Cartridge *cartridge)
{
    cartridge->ram_enabled = false;
    cartridge->mbc1_rom_bank_low5 = 1u;
    cartridge->mbc1_bank_high2 = 0u;
    cartridge->mbc1_banking_mode = 0u;

    cartridge->mbc2_rom_bank = 1u;

    cartridge->mbc3_rom_bank = 1u;
    cartridge->mbc3_ram_rtc_select = 0u;
    cartridge->mbc3_latch_state = 0u;

    cartridge->mbc5_rom_bank = 1u;
    cartridge->mbc5_ram_bank = 0u;
    cartridge->mbc5_rumble_active = false;

    cartridge->rtc_seconds = 0u;
    cartridge->rtc_minutes = 0u;
    cartridge->rtc_hours = 0u;
    cartridge->rtc_days = 0u;
    cartridge->rtc_halt = false;
    cartridge->rtc_carry = false;

    cartridge->rtc_latched_seconds = 0u;
    cartridge->rtc_latched_minutes = 0u;
    cartridge->rtc_latched_hours = 0u;
    cartridge->rtc_latched_days = 0u;
    cartridge->rtc_latched_halt = false;
    cartridge->rtc_latched_carry = false;
    cartridge->rtc_cycle_remainder = 0u;
}

static void reset_loaded_fields(GB_Cartridge *cartridge)
{
    bool initialized = cartridge->initialized;
    memset(cartridge, 0, sizeof(*cartridge));
    cartridge->initialized = initialized;
    cartridge->mapper = GB_CARTRIDGE_MAPPER_UNKNOWN;
    cartridge->cgb_support = GB_CARTRIDGE_CGB_DMG_COMPATIBLE;
}

static GB_Result prepare_metadata(GB_Cartridge *cartridge,
                                  const GB_CartridgeLoadOptions *options,
                                  GB_Error *error)
{
    const CartridgeTypeInfo *type_info;
    size_t declared_rom_size;
    uint32_t declared_rom_banks;
    size_t declared_ram_size;
    uint32_t declared_ram_banks;

    if (cartridge->rom_size < GB_CARTRIDGE_HEADER_MIN_SIZE) {
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "ROM is smaller than the Game Boy cartridge header", 0x0100u);
        return GB_RESULT_INVALID_ROM;
    }

    if (cartridge->rom_size > GB_CARTRIDGE_MAX_ROM_SIZE) {
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "ROM exceeds the supported 8 MiB cartridge capacity", 0u);
        return GB_RESULT_INVALID_ROM;
    }

    if ((cartridge->rom_size % 0x4000u) != 0u) {
        cartridge_error(error, GB_RESULT_ROM_SIZE,
                        "ROM file size is not a whole number of 16 KiB banks", 0u);
        return GB_RESULT_ROM_SIZE;
    }

    memcpy(cartridge->entry_point, cartridge->rom + 0x0100u, sizeof(cartridge->entry_point));
    memcpy(cartridge->nintendo_logo, cartridge->rom + 0x0104u,
           sizeof(cartridge->nintendo_logo));
    memcpy(cartridge->title, cartridge->rom + 0x0134u, sizeof(cartridge->title));
    memcpy(cartridge->manufacturer_code, cartridge->rom + 0x013Fu,
           sizeof(cartridge->manufacturer_code));
    memcpy(cartridge->new_licensee_code, cartridge->rom + 0x0144u,
           sizeof(cartridge->new_licensee_code));

    cartridge->cgb_flag = cartridge->rom[0x0143u];
    cartridge->sgb_flag = cartridge->rom[0x0146u];
    cartridge->cartridge_type_code = cartridge->rom[0x0147u];
    cartridge->rom_size_code = cartridge->rom[0x0148u];
    cartridge->ram_size_code = cartridge->rom[0x0149u];
    cartridge->destination_code = cartridge->rom[0x014Au];
    cartridge->old_licensee_code = cartridge->rom[0x014Bu];
    cartridge->rom_version = cartridge->rom[0x014Cu];
    cartridge->header_checksum = cartridge->rom[0x014Du];
    cartridge->global_checksum = (uint16_t)(((uint16_t)cartridge->rom[0x014Eu] << 8u) |
                                             cartridge->rom[0x014Fu]);

    cartridge->cgb_support = (cartridge->cgb_flag == 0xC0u)
                                 ? GB_CARTRIDGE_CGB_ONLY
                                 : (cartridge->cgb_flag == 0x80u)
                                       ? GB_CARTRIDGE_CGB_COMPATIBLE
                                       : GB_CARTRIDGE_CGB_DMG_COMPATIBLE;
    cartridge->sgb_compatible = cartridge->sgb_flag == 0x03u &&
                                cartridge->old_licensee_code == 0x33u;

    cartridge->nintendo_logo_valid =
        memcmp(cartridge->nintendo_logo, NINTENDO_LOGO,
               GB_CARTRIDGE_NINTENDO_LOGO_SIZE) == 0;

    uint8_t checksum = 0u;
    for (size_t address = 0x0134u; address <= 0x014Cu; ++address) {
        checksum = (uint8_t)(checksum - cartridge->rom[address] - 1u);
    }
    cartridge->header_checksum_valid = checksum == cartridge->header_checksum;

    uint16_t global_checksum = 0u;
    for (size_t address = 0u; address < cartridge->rom_size; ++address) {
        if (address == 0x014Eu || address == 0x014Fu) {
            continue;
        }
        global_checksum = (uint16_t)(global_checksum + cartridge->rom[address]);
    }
    cartridge->global_checksum_valid = global_checksum == cartridge->global_checksum;

    type_info = find_type_info(cartridge->cartridge_type_code);
    if (type_info == NULL) {
        cartridge_error(error, GB_RESULT_UNSUPPORTED_CARTRIDGE,
                        "Unknown cartridge type code", 0x0147u);
        return GB_RESULT_UNSUPPORTED_CARTRIDGE;
    }
    cartridge->mapper = type_info->mapper;
    cartridge->has_ram = type_info->has_ram;
    cartridge->has_battery = type_info->battery;
    cartridge->has_rtc = type_info->rtc;
    cartridge->has_rumble = type_info->rumble;

    if (!decode_rom_size(cartridge->rom_size_code, &declared_rom_size,
                         &declared_rom_banks)) {
        cartridge_error(error, GB_RESULT_ROM_SIZE,
                        "Unknown Game Boy ROM size code", 0x0148u);
        return GB_RESULT_ROM_SIZE;
    }
    cartridge->declared_rom_size = declared_rom_size;
    cartridge->declared_rom_bank_count = declared_rom_banks;

    if (!decode_ram_size(cartridge->ram_size_code, &declared_ram_size,
                         &declared_ram_banks)) {
        cartridge_error(error, GB_RESULT_ROM_SIZE,
                        "Unknown Game Boy RAM size code", 0x0149u);
        return GB_RESULT_ROM_SIZE;
    }
    cartridge->declared_ram_size = declared_ram_size;
    cartridge->declared_ram_bank_count = declared_ram_banks;

    if (!supported_cartridge_mapper(cartridge)) {
        cartridge_error(error, GB_RESULT_UNSUPPORTED_CARTRIDGE,
                        "Cartridge mapper is recognized but not implemented by this emulator",
                        0x0147u);
        return GB_RESULT_UNSUPPORTED_CARTRIDGE;
    }

    if (cartridge->mapper == GB_CARTRIDGE_MAPPER_MBC2) {
        if (cartridge->declared_rom_size > 0x40000u || cartridge->declared_ram_size != 0u) {
            cartridge_error(error, GB_RESULT_INVALID_ROM,
                            "MBC2 cartridge size configuration is invalid", 0x0148u);
            return GB_RESULT_INVALID_ROM;
        }
    }

    if (cartridge->mapper == GB_CARTRIDGE_MAPPER_MBC1 &&
        cartridge->declared_rom_size > 0x200000u) {
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "MBC1 cartridge exceeds the supported 2 MiB ROM range", 0x0148u);
        return GB_RESULT_INVALID_ROM;
    }

    if (cartridge->mapper == GB_CARTRIDGE_MAPPER_MBC3 &&
        cartridge->declared_rom_size > 0x200000u) {
        cartridge_error(error, GB_RESULT_UNSUPPORTED_CARTRIDGE,
                        "MBC3 cartridges above 2 MiB require MBC30-specific hardware", 0x0148u);
        return GB_RESULT_UNSUPPORTED_CARTRIDGE;
    }

    if (!options->allow_header_rom_size_mismatch &&
        cartridge->rom_size != cartridge->declared_rom_size) {
        cartridge_error(error, GB_RESULT_ROM_SIZE,
                        "ROM file size does not match the cartridge header", 0x0148u);
        return GB_RESULT_ROM_SIZE;
    }

    /* Validate configurations which can be directly mapped without an MBC. */
    if (cartridge->mapper == GB_CARTRIDGE_MAPPER_NONE) {
        if (cartridge->declared_rom_size != 0x8000u) {
            cartridge_error(error, GB_RESULT_INVALID_ROM,
                            "A non-MBC cartridge must declare a 32 KiB ROM", 0x0147u);
            return GB_RESULT_INVALID_ROM;
        }

        if (cartridge->cartridge_type_code == 0x00u && cartridge->declared_ram_size != 0u) {
            cartridge_error(error, GB_RESULT_INVALID_ROM,
                            "ROM ONLY cartridges cannot declare cartridge RAM", 0x0147u);
            return GB_RESULT_INVALID_ROM;
        }

        if ((cartridge->cartridge_type_code == 0x08u ||
             cartridge->cartridge_type_code == 0x09u) &&
            cartridge->declared_ram_size != 0x2000u) {
            cartridge_error(error, GB_RESULT_INVALID_ROM,
                            "ROM+RAM cartridges must declare 8 KiB of RAM", 0x0147u);
            return GB_RESULT_INVALID_ROM;
        }
    }

    if (options->validate_nintendo_logo && !cartridge->nintendo_logo_valid) {
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "Nintendo logo in cartridge header is invalid", 0x0104u);
        return GB_RESULT_INVALID_ROM;
    }

    if (options->validate_header_checksum && !cartridge->header_checksum_valid) {
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "Cartridge header checksum is invalid", 0x014Du);
        return GB_RESULT_INVALID_ROM;
    }

    if (options->validate_global_checksum && !cartridge->global_checksum_valid) {
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "Cartridge global checksum is invalid", 0x014Eu);
        return GB_RESULT_INVALID_ROM;
    }

    cartridge->bus_supported = supported_cartridge_mapper(cartridge);
    return GB_RESULT_OK;
}

void gb_cartridge_load_options_default(GB_CartridgeLoadOptions *options)
{
    if (options == NULL) {
        return;
    }

    options->validate_nintendo_logo = true;
    options->validate_header_checksum = true;
    options->validate_global_checksum = false;
    options->allow_header_rom_size_mismatch = false;
}

GB_Result gb_cartridge_init(GB_Cartridge *cartridge, GB_Error *error)
{
    gb_error_clear(error);

    if (cartridge == NULL) {
        cartridge_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Cartridge pointer is NULL", 0u);
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(cartridge, 0, sizeof(*cartridge));
    cartridge->initialized = true;
    cartridge->mapper = GB_CARTRIDGE_MAPPER_UNKNOWN;
    cartridge->cgb_support = GB_CARTRIDGE_CGB_DMG_COMPATIBLE;
    return GB_RESULT_OK;
}

GB_Result gb_cartridge_unload(GB_Cartridge *cartridge, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_cartridge(cartridge, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    free(cartridge->rom);
    free(cartridge->ram);
    reset_loaded_fields(cartridge);
    return GB_RESULT_OK;
}

GB_Result gb_cartridge_load_buffer(GB_Cartridge *cartridge,
                                   const uint8_t *data,
                                   size_t size,
                                   const GB_CartridgeLoadOptions *options,
                                   GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_cartridge(cartridge, error);
    if (result != GB_RESULT_OK) {
        return result;
    }
    if (data == NULL) {
        cartridge_error(error, GB_RESULT_NULL_ARGUMENT,
                        "ROM buffer is NULL", 0u);
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (size < GB_CARTRIDGE_HEADER_MIN_SIZE) {
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "ROM buffer is too small to contain a cartridge header", 0u);
        return GB_RESULT_INVALID_ROM;
    }
    if (size > GB_CARTRIDGE_MAX_ROM_SIZE) {
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "ROM buffer exceeds the supported 8 MiB cartridge capacity", 0u);
        return GB_RESULT_INVALID_ROM;
    }
    if ((size % 0x4000u) != 0u) {
        cartridge_error(error, GB_RESULT_ROM_SIZE,
                        "ROM buffer size is not a whole number of 16 KiB banks", 0u);
        return GB_RESULT_ROM_SIZE;
    }

    GB_Cartridge temp;
    memset(&temp, 0, sizeof(temp));
    temp.initialized = true;
    temp.mapper = GB_CARTRIDGE_MAPPER_UNKNOWN;

    temp.rom = (uint8_t *)malloc(size);
    if (temp.rom == NULL) {
        cartridge_error(error, GB_RESULT_ALLOCATION,
                        "Failed to allocate memory for the cartridge ROM", 0u);
        return GB_RESULT_ALLOCATION;
    }
    memcpy(temp.rom, data, size);
    temp.rom_size = size;

    GB_CartridgeLoadOptions defaults;
    if (options == NULL) {
        gb_cartridge_load_options_default(&defaults);
        options = &defaults;
    }

    result = prepare_metadata(&temp, options, error);
    if (result != GB_RESULT_OK) {
        free(temp.rom);
        free(temp.ram);
        return result;
    }

    size_t mapper_ram_size = temp.declared_ram_size;
    if (temp.mapper == GB_CARTRIDGE_MAPPER_MBC2) {
        mapper_ram_size = 0x0200u;
    }

    if (mapper_ram_size != 0u) {
        temp.ram = (uint8_t *)calloc(mapper_ram_size, sizeof(uint8_t));
        if (temp.ram == NULL) {
            free(temp.rom);
            cartridge_error(error, GB_RESULT_ALLOCATION,
                            "Failed to allocate cartridge RAM", 0x0149u);
            return GB_RESULT_ALLOCATION;
        }
        temp.ram_size = mapper_ram_size;
    }

    reset_mbc_state(&temp);
    temp.loaded = true;
    temp.initialized = true;

    free(cartridge->rom);
    free(cartridge->ram);
    *cartridge = temp;
    return GB_RESULT_OK;
}

GB_Result gb_cartridge_load(GB_Cartridge *cartridge,
                            const char *path,
                            const GB_CartridgeLoadOptions *options,
                            GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_cartridge(cartridge, error);
    if (result != GB_RESULT_OK) {
        return result;
    }
    if (path == NULL || path[0] == '\0') {
        cartridge_error(error, GB_RESULT_NULL_ARGUMENT,
                        "ROM file path is NULL or empty", 0u);
        return GB_RESULT_NULL_ARGUMENT;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        cartridge_error(error, GB_RESULT_FILE_OPEN,
                        "Failed to open cartridge ROM file", 0u);
        return GB_RESULT_FILE_OPEN;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        cartridge_error(error, GB_RESULT_FILE_IO,
                        "Failed to seek to the end of the cartridge ROM file", 0u);
        return GB_RESULT_FILE_IO;
    }

    long file_size_long = ftell(file);
    if (file_size_long < 0L) {
        (void)fclose(file);
        cartridge_error(error, GB_RESULT_FILE_IO,
                        "Failed to determine cartridge ROM file size", 0u);
        return GB_RESULT_FILE_IO;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        cartridge_error(error, GB_RESULT_FILE_IO,
                        "Failed to rewind the cartridge ROM file", 0u);
        return GB_RESULT_FILE_IO;
    }

    unsigned long file_size = (unsigned long)file_size_long;
    if (file_size > (unsigned long)SIZE_MAX) {
        (void)fclose(file);
        cartridge_error(error, GB_RESULT_FILE_IO,
                        "Cartridge ROM file size cannot be represented", 0u);
        return GB_RESULT_FILE_IO;
    }

    size_t size = (size_t)file_size;
    if (size == 0u) {
        (void)fclose(file);
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "Cartridge ROM file is empty", 0u);
        return GB_RESULT_INVALID_ROM;
    }

    if (size > GB_CARTRIDGE_MAX_ROM_SIZE) {
        (void)fclose(file);
        cartridge_error(error, GB_RESULT_INVALID_ROM,
                        "Cartridge ROM file exceeds the supported 8 MiB capacity", 0u);
        return GB_RESULT_INVALID_ROM;
    }

    uint8_t *buffer = (uint8_t *)malloc(size);
    if (buffer == NULL) {
        (void)fclose(file);
        cartridge_error(error, GB_RESULT_ALLOCATION,
                        "Failed to allocate temporary ROM file buffer", 0u);
        return GB_RESULT_ALLOCATION;
    }

    size_t read_count = fread(buffer, 1u, size, file);
    int close_result = fclose(file);

    if (read_count != size) {
        free(buffer);
        cartridge_error(error, GB_RESULT_FILE_READ,
                        "Failed to read the complete cartridge ROM file", 0u);
        return GB_RESULT_FILE_READ;
    }
    if (close_result != 0) {
        free(buffer);
        cartridge_error(error, GB_RESULT_FILE_IO,
                        "Failed to close the cartridge ROM file cleanly", 0u);
        return GB_RESULT_FILE_IO;
    }

    result = gb_cartridge_load_buffer(cartridge, buffer, size, options, error);
    free(buffer);
    return result;
}

bool gb_cartridge_is_loaded(const GB_Cartridge *cartridge)
{
    return cartridge != NULL && cartridge->initialized && cartridge->loaded;
}

const char *gb_cartridge_mapper_name(GB_CartridgeMapper mapper)
{
    switch (mapper) {
    case GB_CARTRIDGE_MAPPER_NONE:         return "None";
    case GB_CARTRIDGE_MAPPER_MBC1:         return "MBC1";
    case GB_CARTRIDGE_MAPPER_MBC2:         return "MBC2";
    case GB_CARTRIDGE_MAPPER_MMM01:        return "MMM01";
    case GB_CARTRIDGE_MAPPER_MBC3:         return "MBC3";
    case GB_CARTRIDGE_MAPPER_MBC5:         return "MBC5";
    case GB_CARTRIDGE_MAPPER_MBC6:         return "MBC6";
    case GB_CARTRIDGE_MAPPER_MBC7:         return "MBC7";
    case GB_CARTRIDGE_MAPPER_POCKET_CAMERA: return "Pocket Camera";
    case GB_CARTRIDGE_MAPPER_TAMA5:        return "TAMA5";
    case GB_CARTRIDGE_MAPPER_HUC3:         return "HuC3";
    case GB_CARTRIDGE_MAPPER_HUC1:         return "HuC1";
    case GB_CARTRIDGE_MAPPER_UNKNOWN:      return "Unknown";
    default:                               return "Invalid";
    }
}

const char *gb_cartridge_cgb_support_name(GB_CartridgeCGBSupport support)
{
    switch (support) {
    case GB_CARTRIDGE_CGB_DMG_COMPATIBLE: return "DMG-compatible";
    case GB_CARTRIDGE_CGB_COMPATIBLE:     return "CGB-compatible";
    case GB_CARTRIDGE_CGB_ONLY:           return "CGB-only";
    default:                              return "Unknown";
    }
}

const char *gb_cartridge_type_name(uint8_t cartridge_type_code)
{
    const CartridgeTypeInfo *info = find_type_info(cartridge_type_code);
    return info != NULL ? info->name : "Unknown";
}

static uint32_t rom_bank_count(const GB_Cartridge *cartridge)
{
    return cartridge->declared_rom_bank_count != 0u
               ? cartridge->declared_rom_bank_count
               : (uint32_t)(cartridge->rom_size / 0x4000u);
}

static uint32_t ram_bank_count(const GB_Cartridge *cartridge)
{
    if (cartridge->mapper == GB_CARTRIDGE_MAPPER_MBC2) {
        return 1u;
    }
    return cartridge->declared_ram_bank_count;
}

static uint32_t wrap_bank(uint32_t bank, uint32_t count)
{
    return count == 0u ? 0u : bank % count;
}

static uint32_t mbc1_upper_rom_bank(const GB_Cartridge *cartridge)
{
    uint32_t low = cartridge->mbc1_rom_bank_low5 & 0x1Fu;
    uint32_t bank = ((uint32_t)(cartridge->mbc1_bank_high2 & 0x03u) << 5u) | low;
    if (low == 0u) {
        bank += 1u;
    }
    return wrap_bank(bank, rom_bank_count(cartridge));
}

static uint32_t mbc1_lower_rom_bank(const GB_Cartridge *cartridge)
{
    if (cartridge->mbc1_banking_mode == 0u) {
        return 0u;
    }
    return wrap_bank((uint32_t)(cartridge->mbc1_bank_high2 & 0x03u) << 5u,
                     rom_bank_count(cartridge));
}

static uint32_t current_rom_bank(const GB_Cartridge *cartridge, bool upper)
{
    uint32_t count = rom_bank_count(cartridge);
    if (count == 0u) {
        return 0u;
    }

    /* The $0000-$3FFF window is always the fixed ROM bank for MBC2,
     * MBC3, and MBC5. Only the $4000-$7FFF window is bank switched. */
    if (!upper) {
        switch (cartridge->mapper) {
        case GB_CARTRIDGE_MAPPER_NONE:
        case GB_CARTRIDGE_MAPPER_MBC2:
        case GB_CARTRIDGE_MAPPER_MBC3:
        case GB_CARTRIDGE_MAPPER_MBC5:
            return 0u;
        case GB_CARTRIDGE_MAPPER_MBC1:
            return mbc1_lower_rom_bank(cartridge);
        default:
            return 0u;
        }
    }

    switch (cartridge->mapper) {
    case GB_CARTRIDGE_MAPPER_NONE:
        return 0u;
    case GB_CARTRIDGE_MAPPER_MBC1:
        return mbc1_upper_rom_bank(cartridge);
    case GB_CARTRIDGE_MAPPER_MBC2: {
        uint32_t bank = cartridge->mbc2_rom_bank & 0x0Fu;
        if (bank == 0u) bank = 1u;
        return wrap_bank(bank, count);
    }
    case GB_CARTRIDGE_MAPPER_MBC3: {
        uint32_t bank = cartridge->mbc3_rom_bank & 0x7Fu;
        if (bank == 0u) bank = 1u;
        return wrap_bank(bank, count);
    }
    case GB_CARTRIDGE_MAPPER_MBC5:
        return wrap_bank(cartridge->mbc5_rom_bank & 0x01FFu, count);
    default:
        return 0u;
    }
}

static uint32_t current_ram_bank(const GB_Cartridge *cartridge)
{
    uint32_t count = ram_bank_count(cartridge);
    if (count == 0u) {
        return 0u;
    }

    switch (cartridge->mapper) {
    case GB_CARTRIDGE_MAPPER_NONE:
        return 0u;
    case GB_CARTRIDGE_MAPPER_MBC1:
        return cartridge->mbc1_banking_mode == 0u
                   ? 0u
                   : wrap_bank(cartridge->mbc1_bank_high2 & 0x03u, count);
    case GB_CARTRIDGE_MAPPER_MBC3:
        return wrap_bank(cartridge->mbc3_ram_rtc_select & 0x03u, count);
    case GB_CARTRIDGE_MAPPER_MBC5:
        return wrap_bank(cartridge->mbc5_ram_bank & 0x0Fu, count);
    default:
        return 0u;
    }
}

static size_t rom_offset(uint32_t bank, uint16_t address)
{
    return ((size_t)bank * 0x4000u) + (size_t)(address & 0x3FFFu);
}

static size_t ram_offset(uint32_t bank, uint16_t address)
{
    return ((size_t)bank * 0x2000u) + (size_t)(address - 0xA000u);
}

static GB_Result cartridge_access_error(GB_Cartridge *cartridge,
                                        uint16_t address,
                                        const char *operation,
                                        GB_Error *error)
{
    char message[256];
    (void)snprintf(message, sizeof(message),
                   "%s is unsupported for cartridge type $%02X (%s)",
                   operation, cartridge->cartridge_type_code,
                   gb_cartridge_type_name(cartridge->cartridge_type_code));
    cartridge_error(error, GB_RESULT_UNSUPPORTED_CARTRIDGE, message, address);
    return GB_RESULT_UNSUPPORTED_CARTRIDGE;
}

static bool ram_access_enabled(const GB_Cartridge *cartridge)
{
    switch (cartridge->mapper) {
    case GB_CARTRIDGE_MAPPER_NONE:
        return cartridge->ram != NULL && cartridge->ram_size != 0u;
    case GB_CARTRIDGE_MAPPER_MBC1:
    case GB_CARTRIDGE_MAPPER_MBC2:
    case GB_CARTRIDGE_MAPPER_MBC3:
    case GB_CARTRIDGE_MAPPER_MBC5:
        return cartridge->ram_enabled;
    default:
        return false;
    }
}

static void rtc_latch(GB_Cartridge *cartridge)
{
    cartridge->rtc_latched_seconds = cartridge->rtc_seconds;
    cartridge->rtc_latched_minutes = cartridge->rtc_minutes;
    cartridge->rtc_latched_hours = cartridge->rtc_hours;
    cartridge->rtc_latched_days = cartridge->rtc_days;
    cartridge->rtc_latched_halt = cartridge->rtc_halt;
    cartridge->rtc_latched_carry = cartridge->rtc_carry;
}

static void rtc_advance_one_second(GB_Cartridge *cartridge)
{
    if (cartridge->rtc_halt) {
        return;
    }

    cartridge->rtc_seconds = (uint8_t)(cartridge->rtc_seconds + 1u);
    if (cartridge->rtc_seconds < 60u) return;
    cartridge->rtc_seconds = 0u;

    cartridge->rtc_minutes = (uint8_t)(cartridge->rtc_minutes + 1u);
    if (cartridge->rtc_minutes < 60u) return;
    cartridge->rtc_minutes = 0u;

    cartridge->rtc_hours = (uint8_t)(cartridge->rtc_hours + 1u);
    if (cartridge->rtc_hours < 24u) return;
    cartridge->rtc_hours = 0u;

    cartridge->rtc_days = (uint16_t)(cartridge->rtc_days + 1u);
    if (cartridge->rtc_days <= 511u) return;

    cartridge->rtc_days = 0u;
    cartridge->rtc_carry = true;
}

static uint8_t rtc_read_register(const GB_Cartridge *cartridge, uint8_t select)
{
    switch (select) {
    case 0x08u: return cartridge->rtc_latched_seconds & 0x3Fu;
    case 0x09u: return cartridge->rtc_latched_minutes & 0x3Fu;
    case 0x0Au: return cartridge->rtc_latched_hours & 0x1Fu;
    case 0x0Bu: return (uint8_t)(cartridge->rtc_latched_days & 0xFFu);
    case 0x0Cu:
        return (uint8_t)(((cartridge->rtc_latched_days >> 8u) & 0x01u) |
                         (cartridge->rtc_latched_halt ? 0x40u : 0u) |
                         (cartridge->rtc_latched_carry ? 0x80u : 0u));
    default: return 0xFFu;
    }
}

static void rtc_write_live_register(GB_Cartridge *cartridge,
                                    uint8_t select, uint8_t value)
{
    switch (select) {
    case 0x08u:
        cartridge->rtc_seconds = (uint8_t)(value % 60u);
        break;
    case 0x09u:
        cartridge->rtc_minutes = (uint8_t)(value % 60u);
        break;
    case 0x0Au:
        cartridge->rtc_hours = (uint8_t)(value % 24u);
        break;
    case 0x0Bu:
        cartridge->rtc_days = (uint16_t)((cartridge->rtc_days & 0x100u) | value);
        break;
    case 0x0Cu:
        cartridge->rtc_days = (uint16_t)(((uint16_t)(value & 0x01u) << 8u) |
                                         (uint16_t)(cartridge->rtc_days & 0xFFu));
        cartridge->rtc_halt = (value & 0x40u) != 0u;
        cartridge->rtc_carry = (value & 0x80u) != 0u;
        break;
    default:
        break;
    }
}

static GB_Result mbc_write_control(GB_Cartridge *cartridge,
                                    uint16_t address, uint8_t value,
                                    GB_Error *error)
{
    (void)error;

    switch (cartridge->mapper) {
    case GB_CARTRIDGE_MAPPER_NONE:
        return GB_RESULT_OK;

    case GB_CARTRIDGE_MAPPER_MBC1:
        if (address <= 0x1FFFu) {
            cartridge->ram_enabled = (value & 0x0Fu) == 0x0Au;
        } else if (address <= 0x3FFFu) {
            uint8_t bank = (uint8_t)(value & 0x1Fu);
            cartridge->mbc1_rom_bank_low5 = bank == 0u ? 1u : bank;
        } else if (address <= 0x5FFFu) {
            cartridge->mbc1_bank_high2 = (uint8_t)(value & 0x03u);
        } else {
            cartridge->mbc1_banking_mode = (uint8_t)(value & 0x01u);
        }
        return GB_RESULT_OK;

    case GB_CARTRIDGE_MAPPER_MBC2:
        if (address <= 0x1FFFu) {
            if ((address & 0x0100u) == 0u) {
                cartridge->ram_enabled = (value & 0x0Fu) == 0x0Au;
            }
        } else if (address <= 0x3FFFu) {
            if ((address & 0x0100u) != 0u) {
                uint8_t bank = (uint8_t)(value & 0x0Fu);
                cartridge->mbc2_rom_bank = bank == 0u ? 1u : bank;
            }
        }
        return GB_RESULT_OK;

    case GB_CARTRIDGE_MAPPER_MBC3:
        if (address <= 0x1FFFu) {
            cartridge->ram_enabled = (value & 0x0Fu) == 0x0Au;
        } else if (address <= 0x3FFFu) {
            uint8_t bank = (uint8_t)(value & 0x7Fu);
            cartridge->mbc3_rom_bank = bank == 0u ? 1u : bank;
        } else if (address <= 0x5FFFu) {
            cartridge->mbc3_ram_rtc_select = value;
        } else {
            if (value == 0u) {
                cartridge->mbc3_latch_state = 1u;
            } else if (value == 1u && cartridge->mbc3_latch_state == 1u) {
                rtc_latch(cartridge);
                cartridge->mbc3_latch_state = 0u;
            } else {
                cartridge->mbc3_latch_state = 0u;
            }
        }
        return GB_RESULT_OK;

    case GB_CARTRIDGE_MAPPER_MBC5:
        if (address <= 0x1FFFu) {
            cartridge->ram_enabled = (value & 0x0Fu) == 0x0Au;
        } else if (address <= 0x2FFFu) {
            cartridge->mbc5_rom_bank = (uint16_t)((cartridge->mbc5_rom_bank & 0x0100u) |
                                                  value);
        } else if (address <= 0x3FFFu) {
            cartridge->mbc5_rom_bank = (uint16_t)((uint16_t)(cartridge->mbc5_rom_bank & 0x00FFu) |
                                                  (uint16_t)((uint16_t)(value & 0x01u) << 8u));
        } else if (address <= 0x5FFFu) {
            cartridge->mbc5_rumble_active = cartridge->has_rumble && ((value & 0x08u) != 0u);
            cartridge->mbc5_ram_bank = (uint8_t)(value & (cartridge->has_rumble ? 0x07u : 0x0Fu));
        }
        return GB_RESULT_OK;

    default:
        return cartridge_access_error(cartridge, address, "MBC register write", error);
    }
}

GB_Result gb_cartridge_read8(GB_Cartridge *cartridge,
                             uint16_t address,
                             uint8_t *value,
                             GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_cartridge(cartridge, error);
    if (result != GB_RESULT_OK) return result;
    if (!cartridge->loaded) {
        cartridge_error(error, GB_RESULT_BAD_STATE,
                        "Cartridge has no loaded ROM", address);
        return GB_RESULT_BAD_STATE;
    }
    if (value == NULL) {
        cartridge_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Cartridge read output pointer is NULL", address);
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!cartridge->bus_supported) {
        return cartridge_access_error(cartridge, address, "Cartridge access", error);
    }

    if (address <= 0x3FFFu) {
        size_t offset = rom_offset(current_rom_bank(cartridge, false), address);
        if (offset >= cartridge->rom_size) {
            cartridge_error(error, GB_RESULT_BAD_STATE,
                            "Selected cartridge ROM bank is outside the loaded ROM", address);
            return GB_RESULT_BAD_STATE;
        }
        *value = cartridge->rom[offset];
        return GB_RESULT_OK;
    }

    if (address <= 0x7FFFu) {
        size_t offset = rom_offset(current_rom_bank(cartridge, true), address);
        if (offset >= cartridge->rom_size) {
            cartridge_error(error, GB_RESULT_BAD_STATE,
                            "Selected cartridge ROM bank is outside the loaded ROM", address);
            return GB_RESULT_BAD_STATE;
        }
        *value = cartridge->rom[offset];
        return GB_RESULT_OK;
    }

    if (address >= 0xA000u && address <= 0xBFFFu) {
        if (cartridge->mapper == GB_CARTRIDGE_MAPPER_MBC3 &&
            cartridge->mbc3_ram_rtc_select >= 0x08u &&
            cartridge->mbc3_ram_rtc_select <= 0x0Cu) {
            if (!cartridge->ram_enabled) {
                *value = 0xFFu;
                return GB_RESULT_OK;
            }
            *value = rtc_read_register(cartridge, cartridge->mbc3_ram_rtc_select);
            return GB_RESULT_OK;
        }

        if (!ram_access_enabled(cartridge) || cartridge->ram == NULL || cartridge->ram_size == 0u) {
            *value = 0xFFu;
            return GB_RESULT_OK;
        }

        size_t offset;
        if (cartridge->mapper == GB_CARTRIDGE_MAPPER_MBC2) {
            offset = (size_t)(address & 0x01FFu);
            *value = (uint8_t)(0xF0u | (cartridge->ram[offset] & 0x0Fu));
            return GB_RESULT_OK;
        }

        uint32_t bank = current_ram_bank(cartridge);
        offset = ram_offset(bank, address);
        if (offset >= cartridge->ram_size) {
            offset %= cartridge->ram_size;
        }
        *value = cartridge->ram[offset];
        return GB_RESULT_OK;
    }

    cartridge_error(error, GB_RESULT_INVALID_ARGUMENT,
                    "Cartridge bus received an address outside its mapped ranges", address);
    return GB_RESULT_INVALID_ARGUMENT;
}

GB_Result gb_cartridge_write8(GB_Cartridge *cartridge,
                              uint16_t address,
                              uint8_t value,
                              GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_cartridge(cartridge, error);
    if (result != GB_RESULT_OK) return result;
    if (!cartridge->loaded) {
        cartridge_error(error, GB_RESULT_BAD_STATE,
                        "Cartridge has no loaded ROM", address);
        return GB_RESULT_BAD_STATE;
    }
    if (!cartridge->bus_supported) {
        return cartridge_access_error(cartridge, address, "Cartridge access", error);
    }

    if (address <= 0x7FFFu) {
        return mbc_write_control(cartridge, address, value, error);
    }

    if (address >= 0xA000u && address <= 0xBFFFu) {
        if (cartridge->mapper == GB_CARTRIDGE_MAPPER_MBC3 &&
            cartridge->mbc3_ram_rtc_select >= 0x08u &&
            cartridge->mbc3_ram_rtc_select <= 0x0Cu) {
            if (cartridge->ram_enabled) {
                rtc_write_live_register(cartridge, cartridge->mbc3_ram_rtc_select, value);
            }
            return GB_RESULT_OK;
        }

        if (!ram_access_enabled(cartridge) || cartridge->ram == NULL || cartridge->ram_size == 0u) {
            return GB_RESULT_OK;
        }

        if (cartridge->mapper == GB_CARTRIDGE_MAPPER_MBC2) {
            size_t offset = (size_t)(address & 0x01FFu);
            uint8_t new_value = (uint8_t)(value & 0x0Fu);
            if (cartridge->ram[offset] != new_value) {
                cartridge->ram[offset] = new_value;
                cartridge->ram_dirty = true;
            }
            return GB_RESULT_OK;
        }

        size_t offset = ram_offset(current_ram_bank(cartridge), address);
        if (offset >= cartridge->ram_size) {
            offset %= cartridge->ram_size;
        }
        if (cartridge->ram[offset] != value) {
            cartridge->ram[offset] = value;
            cartridge->ram_dirty = true;
        }
        return GB_RESULT_OK;
    }

    cartridge_error(error, GB_RESULT_INVALID_ARGUMENT,
                    "Cartridge bus received an address outside its mapped ranges", address);
    return GB_RESULT_INVALID_ARGUMENT;
}

GB_Result gb_cartridge_tick(void *user, uint32_t t_cycles, GB_Error *error)
{
    GB_Cartridge *cartridge = (GB_Cartridge *)user;
    gb_error_clear(error);

    GB_Result result = require_cartridge(cartridge, error);
    if (result != GB_RESULT_OK) return result;
    if (!cartridge->loaded || !cartridge->has_rtc || cartridge->mapper != GB_CARTRIDGE_MAPPER_MBC3 ||
        cartridge->rtc_halt || t_cycles == 0u) {
        return GB_RESULT_OK;
    }

    uint64_t accumulated = (uint64_t)cartridge->rtc_cycle_remainder + (uint64_t)t_cycles;
    uint32_t seconds = (uint32_t)(accumulated / 4194304u);
    cartridge->rtc_cycle_remainder = (uint32_t)(accumulated % 4194304u);

    for (uint32_t i = 0u; i < seconds; ++i) {
        rtc_advance_one_second(cartridge);
    }
    return GB_RESULT_OK;
}

bool gb_cartridge_rumble_active(const GB_Cartridge *cartridge)
{
    return cartridge != NULL && cartridge->loaded && cartridge->has_rumble &&
           cartridge->mbc5_rumble_active;
}

bool gb_cartridge_ram_dirty(const GB_Cartridge *cartridge)
{
    return cartridge != NULL && cartridge->loaded && cartridge->ram_dirty;
}

void gb_cartridge_clear_ram_dirty(GB_Cartridge *cartridge)
{
    if (cartridge != NULL) {
        cartridge->ram_dirty = false;
    }
}

static GB_Result cartridge_bus_read8(void *user,
                                      uint16_t address,
                                      uint8_t *value,
                                      GB_Error *error)
{
    return gb_cartridge_read8((GB_Cartridge *)user, address, value, error);
}

static GB_Result cartridge_bus_write8(void *user,
                                      uint16_t address,
                                      uint8_t value,
                                      GB_Error *error)
{
    return gb_cartridge_write8((GB_Cartridge *)user, address, value, error);
}

GB_Result gb_cartridge_get_memory_bus(GB_Cartridge *cartridge,
                                      GB_MemoryCartridgeBus *bus,
                                      GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_cartridge(cartridge, error);
    if (result != GB_RESULT_OK) return result;
    if (bus == NULL) {
        cartridge_error(error, GB_RESULT_NULL_ARGUMENT,
                        "Memory cartridge bus output pointer is NULL", 0u);
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!cartridge->loaded) {
        cartridge_error(error, GB_RESULT_BAD_STATE,
                        "Cannot create a cartridge bus before loading a ROM", 0u);
        return GB_RESULT_BAD_STATE;
    }
    if (!cartridge->bus_supported) {
        return cartridge_access_error(cartridge, 0u, "Cartridge bus", error);
    }

    bus->user = cartridge;
    bus->read8 = cartridge_bus_read8;
    bus->write8 = cartridge_bus_write8;
    bus->tick = gb_cartridge_tick;
    return GB_RESULT_OK;
}
