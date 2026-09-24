#include "gb_memory.h"

#include <stdio.h>
#include <string.h>

#define GB_MEMORY_IO_BASE 0xFF00u
#define GB_MEMORY_IO_END  0xFF7Fu

static void memory_error(GB_Error *error, GB_Result code, const char *message,
                         uint16_t address)
{
    if (error == NULL) {
        return;
    }

    gb_error_clear(error);
    error->code = code;
    error->pc = address;
    error->message[0] = '\0';
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static GB_Result require_memory(GB_Memory *memory, GB_Error *error)
{
    if (memory == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Memory pointer is NULL", 0);
        return GB_RESULT_NULL_ARGUMENT;
    }
    return GB_RESULT_OK;
}

static bool address_in_range(uint16_t address, uint16_t start, uint16_t end)
{
    return address >= start && address <= end;
}

GB_MemoryRegion gb_memory_classify_address(uint16_t address)
{
    if (address <= 0x7FFFu) return GB_MEMORY_REGION_CARTRIDGE_ROM;
    if (address <= 0x9FFFu) return GB_MEMORY_REGION_VRAM;
    if (address <= 0xBFFFu) return GB_MEMORY_REGION_CARTRIDGE_RAM;
    if (address <= 0xDFFFu) return GB_MEMORY_REGION_WRAM;
    if (address <= 0xFDFFu) return GB_MEMORY_REGION_ECHO;
    if (address <= 0xFE9Fu) return GB_MEMORY_REGION_OAM;
    if (address <= 0xFEFFu) return GB_MEMORY_REGION_UNUSABLE;
    if (address <= 0xFF7Fu) return GB_MEMORY_REGION_IO;
    if (address <= 0xFFFEu) return GB_MEMORY_REGION_HRAM;
    return GB_MEMORY_REGION_IE;
}

const char *gb_memory_region_name(GB_MemoryRegion region)
{
    switch (region) {
    case GB_MEMORY_REGION_CARTRIDGE_ROM: return "cartridge ROM";
    case GB_MEMORY_REGION_CARTRIDGE_RAM: return "cartridge RAM";
    case GB_MEMORY_REGION_VRAM: return "VRAM";
    case GB_MEMORY_REGION_WRAM: return "WRAM";
    case GB_MEMORY_REGION_ECHO: return "echo RAM";
    case GB_MEMORY_REGION_OAM: return "OAM";
    case GB_MEMORY_REGION_UNUSABLE: return "unusable";
    case GB_MEMORY_REGION_IO: return "I/O";
    case GB_MEMORY_REGION_HRAM: return "HRAM";
    case GB_MEMORY_REGION_IE: return "IE";
    default: return "unknown";
    }
}

static void io_set_register(GB_Memory *memory, uint8_t offset,
                            uint8_t value, bool readable, bool writable)
{
    memory->io[offset] = value;
    memory->io_present[offset] = 1u;
    memory->io_readable[offset] = readable ? 1u : 0u;
    memory->io_writable[offset] = writable ? 1u : 0u;
}

static void io_set_range(GB_Memory *memory, uint8_t start, uint8_t end,
                         uint8_t value, bool readable, bool writable)
{
    for (uint16_t address = start; address <= end; ++address) {
        io_set_register(memory, (uint8_t)address, value, readable, writable);
    }
}

static void initialize_io_map(GB_Memory *memory)
{
    memset(memory->io, 0, sizeof(memory->io));
    memset(memory->io_present, 0, sizeof(memory->io_present));
    memset(memory->io_readable, 0, sizeof(memory->io_readable));
    memset(memory->io_writable, 0, sizeof(memory->io_writable));

    io_set_register(memory, 0x00u, 0xCFu, true, true);   /* JOYP */
    io_set_register(memory, 0x01u, 0x00u, true, true);   /* SB */
    io_set_register(memory, 0x02u, 0x7Eu, true, true);   /* SC */
    io_set_register(memory, 0x04u, 0x00u, true, true);   /* DIV */
    io_set_register(memory, 0x05u, 0x00u, true, true);   /* TIMA */
    io_set_register(memory, 0x06u, 0x00u, true, true);   /* TMA */
    io_set_register(memory, 0x07u, 0xF8u, true, true);   /* TAC */

    io_set_register(memory, 0x0Fu, 0x00u, true, true);   /* IF */

    io_set_range(memory, 0x10u, 0x14u, 0x00u, true, true);
    io_set_register(memory, 0x16u, 0x00u, true, true);
    io_set_register(memory, 0x17u, 0x00u, true, true);
    io_set_register(memory, 0x18u, 0x00u, true, true);
    io_set_register(memory, 0x19u, 0x00u, true, true);
    io_set_register(memory, 0x1Au, 0x00u, true, true);
    io_set_register(memory, 0x1Bu, 0x00u, true, true);
    io_set_register(memory, 0x1Cu, 0x00u, true, true);
    io_set_register(memory, 0x1Du, 0x00u, true, true);
    io_set_register(memory, 0x1Eu, 0x00u, true, true);
    io_set_register(memory, 0x20u, 0x00u, true, true);
    io_set_register(memory, 0x21u, 0x00u, true, true);
    io_set_register(memory, 0x22u, 0x00u, true, true);
    io_set_register(memory, 0x23u, 0x00u, true, true);
    io_set_register(memory, 0x24u, 0x00u, true, true);
    io_set_register(memory, 0x25u, 0x00u, true, true);
    io_set_register(memory, 0x26u, 0x70u, true, true);
    io_set_range(memory, 0x30u, 0x3Fu, 0x00u, true, true); /* Wave RAM */

    io_set_register(memory, 0x40u, 0x00u, true, true);   /* LCDC */
    io_set_register(memory, 0x41u, 0x80u, true, true);   /* STAT */
    io_set_register(memory, 0x42u, 0x00u, true, true);   /* SCY */
    io_set_register(memory, 0x43u, 0x00u, true, true);   /* SCX */
    io_set_register(memory, 0x44u, 0x00u, true, false);  /* LY */
    io_set_register(memory, 0x45u, 0x00u, true, true);   /* LYC */
    io_set_register(memory, 0x46u, 0x00u, false, true);  /* DMA */
    io_set_register(memory, 0x47u, 0xFCu, true, true);   /* BGP */
    io_set_register(memory, 0x48u, 0xFFu, true, true);   /* OBP0 */
    io_set_register(memory, 0x49u, 0xFFu, true, true);   /* OBP1 */
    io_set_register(memory, 0x4Au, 0x00u, true, true);   /* WY */
    io_set_register(memory, 0x4Bu, 0x00u, true, true);   /* WX */

    /* CGB registers. The mode-specific read/write restrictions are enforced
     * during I/O access; this table just records that the address exists. */
    io_set_register(memory, 0x4Du, 0x7Eu, true, true);   /* KEY1 */
    io_set_register(memory, 0x4Fu, 0xFEu, true, true);   /* VBK */
    io_set_register(memory, 0x50u, 0x00u, true, true);   /* boot ROM control */
    io_set_range(memory, 0x51u, 0x55u, 0x00u, true, true); /* HDMA */
    io_set_register(memory, 0x56u, 0x00u, true, true);   /* RP */
    io_set_register(memory, 0x68u, 0x00u, true, true);   /* BCPS */
    io_set_register(memory, 0x69u, 0x00u, true, true);   /* BCPD */
    io_set_register(memory, 0x6Au, 0x00u, true, true);   /* OCPS */
    io_set_register(memory, 0x6Bu, 0x00u, true, true);   /* OCPD */
    io_set_register(memory, 0x6Cu, 0x00u, true, true);   /* OPRI */
    io_set_range(memory, 0x70u, 0x75u, 0x00u, true, true); /* SVBK + CGB regs */
    io_set_register(memory, 0x76u, 0x00u, true, false);  /* PCM12 */
    io_set_register(memory, 0x77u, 0x00u, true, false);  /* PCM34 */
}

GB_Result gb_memory_reset(GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    memset(memory->wram, 0, sizeof(memory->wram));
    memset(memory->vram, 0, sizeof(memory->vram));
    memset(memory->oam, 0, sizeof(memory->oam));
    memset(memory->hram, 0, sizeof(memory->hram));

    memory->vram_bank = 0u;
    memory->wram_bank = 1u;

    initialize_io_map(memory);
    return GB_RESULT_OK;
}

GB_Result gb_memory_init(GB_Memory *memory, GB_MemoryMode mode, GB_Error *error)
{
    gb_error_clear(error);

    if (memory == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Memory pointer is NULL", 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (mode != GB_MEMORY_MODE_DMG && mode != GB_MEMORY_MODE_CGB) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "Invalid Game Boy memory mode", 0);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    memset(memory, 0, sizeof(*memory));
    memory->mode = mode;
    memory->wram_bank = 1u;
    return gb_memory_reset(memory, error);
}

GB_Result gb_memory_set_cartridge_bus(GB_Memory *memory,
                                      const GB_MemoryCartridgeBus *cartridge_bus,
                                      GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (cartridge_bus == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Cartridge bus pointer is NULL", 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (cartridge_bus->read8 == NULL || cartridge_bus->write8 == NULL) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "Cartridge bus requires read8 and write8 callbacks", 0);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    memory->cartridge_bus = *cartridge_bus;
    return GB_RESULT_OK;
}

GB_Result gb_memory_clear_cartridge_bus(GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    memset(&memory->cartridge_bus, 0, sizeof(memory->cartridge_bus));
    return GB_RESULT_OK;
}

static bool io_device_overlaps(const GB_MemoryIODevice *device,
                               uint16_t start, uint16_t end)
{
    if (!device->used) {
        return false;
    }
    return !(end < device->start || start > device->end);
}

GB_Result gb_memory_map_io_device(GB_Memory *memory,
                                  uint16_t start,
                                  uint16_t end,
                                  void *user,
                                  GB_MemoryIORead8 read8,
                                  GB_MemoryIOWrite8 write8,
                                  GB_MemoryIOTick tick,
                                  size_t *device_index,
                                  GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (start < GB_MEMORY_IO_BASE || end > GB_MEMORY_IO_END || start > end) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "I/O device range must stay within FF00-FF7F", start);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    if (read8 == NULL && write8 == NULL && tick == NULL) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "I/O device must provide at least one callback", start);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < GB_MEMORY_MAX_IO_DEVICES; ++i) {
        if (io_device_overlaps(&memory->io_devices[i], start, end)) {
            memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                         "I/O device range overlaps an existing device", start);
            return GB_RESULT_INVALID_ARGUMENT;
        }
    }

    for (size_t i = 0; i < GB_MEMORY_MAX_IO_DEVICES; ++i) {
        if (!memory->io_devices[i].used) {
            memory->io_devices[i].used = true;
            memory->io_devices[i].start = start;
            memory->io_devices[i].end = end;
            memory->io_devices[i].user = user;
            memory->io_devices[i].read8 = read8;
            memory->io_devices[i].write8 = write8;
            memory->io_devices[i].tick = tick;
            if (device_index != NULL) {
                *device_index = i;
            }
            return GB_RESULT_OK;
        }
    }

    memory_error(error, GB_RESULT_BAD_STATE,
                 "No free I/O device slots remain", start);
    return GB_RESULT_BAD_STATE;
}

GB_Result gb_memory_unmap_io_device(GB_Memory *memory,
                                    size_t device_index,
                                    GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (device_index >= GB_MEMORY_MAX_IO_DEVICES) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "I/O device index is out of range", (uint16_t)device_index);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    if (!memory->io_devices[device_index].used) {
        memory_error(error, GB_RESULT_BAD_STATE,
                     "I/O device slot is not mapped", (uint16_t)device_index);
        return GB_RESULT_BAD_STATE;
    }

    memset(&memory->io_devices[device_index], 0,
           sizeof(memory->io_devices[device_index]));
    return GB_RESULT_OK;
}

static GB_MemoryIODevice *find_io_device(GB_Memory *memory, uint16_t address)
{
    for (size_t i = 0; i < GB_MEMORY_MAX_IO_DEVICES; ++i) {
        if (address_in_range(address, memory->io_devices[i].start,
                             memory->io_devices[i].end) &&
            memory->io_devices[i].used) {
            return &memory->io_devices[i];
        }
    }
    return NULL;
}

static bool is_cgb_only_register(uint16_t address)
{
    if (address == GB_ADDR_KEY1 || address == GB_ADDR_VBK ||
        address == GB_ADDR_HDMA1 || address == GB_ADDR_HDMA2 ||
        address == GB_ADDR_HDMA3 || address == GB_ADDR_HDMA4 ||
        address == GB_ADDR_HDMA5 || address == GB_ADDR_RP ||
        address == GB_ADDR_BCPS || address == GB_ADDR_BCPD ||
        address == GB_ADDR_OCPS || address == GB_ADDR_OCPD ||
        address == GB_ADDR_OPRI || address == GB_ADDR_SVBK ||
        address == GB_ADDR_PCM12 || address == GB_ADDR_PCM34) {
        return true;
    }
    return false;
}

static uint8_t io_default_read(GB_Memory *memory, uint16_t address)
{
    uint8_t offset = (uint8_t)(address - GB_MEMORY_IO_BASE);
    uint8_t value = memory->io[offset];

    switch (address) {
    case GB_ADDR_JOYP:
        /* The input subsystem will replace the low four input bits later. */
        return (uint8_t)(0xC0u | (value & 0x30u) | 0x0Fu);

    case GB_ADDR_SC:
        if (memory->mode == GB_MEMORY_MODE_CGB) {
            return (uint8_t)((value & 0x83u) | 0x7Cu);
        }
        return (uint8_t)((value & 0x81u) | 0x7Eu);

    case GB_ADDR_TAC:
        return (uint8_t)((value & 0x07u) | 0xF8u);

    case GB_ADDR_STAT:
        /* PPU owns bits 0-6 later; the standalone memory core only provides
         * the fixed high bit until a device is mapped over this register. */
        return (uint8_t)((value & 0x78u) | 0x80u);

    case GB_ADDR_KEY1:
        if (memory->mode != GB_MEMORY_MODE_CGB) return 0xFFu;
        return (uint8_t)((value & 0x81u) | 0x7Eu);

    case GB_ADDR_VBK:
        if (memory->mode != GB_MEMORY_MODE_CGB) return 0xFFu;
        return (uint8_t)(0xFEu | memory->vram_bank);

    case GB_ADDR_SVBK:
        if (memory->mode != GB_MEMORY_MODE_CGB) return 0xFFu;
        return (uint8_t)(0xF8u | memory->wram_bank);

    default:
        return value;
    }
}

static void io_default_write(GB_Memory *memory, uint16_t address, uint8_t value)
{
    uint8_t offset = (uint8_t)(address - GB_MEMORY_IO_BASE);

    switch (address) {
    case GB_ADDR_JOYP:
        /* Bits 4-5 are CPU-controlled selection bits; the input subsystem
         * supplies bits 0-3. Bits 6-7 read back as one. */
        memory->io[offset] = (uint8_t)(0xC0u | (value & 0x30u) | 0x0Fu);
        break;

    case GB_ADDR_DIV:
        /* Timer hardware is not present yet; the register's write behavior is
         * still correct: any CPU write resets DIV to zero. */
        memory->io[offset] = 0u;
        break;

    case GB_ADDR_TAC:
        memory->io[offset] = (uint8_t)(value & 0x07u);
        break;

    case GB_ADDR_STAT:
        /* Bits 3-6 are writable. Bit 7 is fixed high and bits 0-2 are PPU-owned. */
        memory->io[offset] = (uint8_t)(0x80u | (value & 0x78u) |
                                       (memory->io[offset] & 0x07u));
        break;

    case GB_ADDR_KEY1:
        if (memory->mode == GB_MEMORY_MODE_CGB) {
            memory->io[offset] = (uint8_t)((memory->io[offset] & 0x80u) |
                                           (value & 0x01u));
        }
        break;

    case GB_ADDR_VBK:
        if (memory->mode == GB_MEMORY_MODE_CGB) {
            memory->vram_bank = (uint8_t)(value & 0x01u);
            memory->io[offset] = (uint8_t)(0xFEu | memory->vram_bank);
        }
        break;

    case GB_ADDR_SVBK:
        if (memory->mode == GB_MEMORY_MODE_CGB) {
            uint8_t bank = (uint8_t)(value & 0x07u);
            if (bank == 0u) bank = 1u;
            memory->wram_bank = bank;
            memory->io[offset] = (uint8_t)(0xF8u | bank);
        }
        break;

    default:
        if (memory->io_writable[offset] != 0u) {
            memory->io[offset] = value;
        }
        break;
    }
}

static GB_Result cartridge_read(GB_Memory *memory, uint16_t address,
                                uint8_t *value, GB_Error *error)
{
    if (memory->cartridge_bus.read8 == NULL) {
        memory_error(error, GB_RESULT_BUS_READ,
                     "No cartridge bus is attached for this address", address);
        return GB_RESULT_BUS_READ;
    }

    GB_Error child_error;
    gb_error_clear(&child_error);
    GB_Result result = memory->cartridge_bus.read8(memory->cartridge_bus.user,
                                                    address, value, &child_error);
    if (result != GB_RESULT_OK) {
        if (error != NULL) {
            *error = child_error;
            if (error->message[0] == '\0') {
                (void)snprintf(error->message, sizeof(error->message),
                               "Cartridge read failed at $%04X", address);
            }
            error->code = GB_RESULT_BUS_READ;
            error->pc = address;
        }
        return GB_RESULT_BUS_READ;
    }

    return GB_RESULT_OK;
}

static GB_Result cartridge_write(GB_Memory *memory, uint16_t address,
                                 uint8_t value, GB_Error *error)
{
    if (memory->cartridge_bus.write8 == NULL) {
        memory_error(error, GB_RESULT_BUS_WRITE,
                     "No cartridge bus is attached for this address", address);
        return GB_RESULT_BUS_WRITE;
    }

    GB_Error child_error;
    gb_error_clear(&child_error);
    GB_Result result = memory->cartridge_bus.write8(memory->cartridge_bus.user,
                                                     address, value, &child_error);
    if (result != GB_RESULT_OK) {
        if (error != NULL) {
            *error = child_error;
            if (error->message[0] == '\0') {
                (void)snprintf(error->message, sizeof(error->message),
                               "Cartridge write failed at $%04X", address);
            }
            error->code = GB_RESULT_BUS_WRITE;
            error->pc = address;
        }
        return GB_RESULT_BUS_WRITE;
    }

    return GB_RESULT_OK;
}

static bool video_cpu_access_allowed(GB_Memory *memory, uint16_t address, bool write)
{
    if (memory->video_controller.cpu_access_allowed == NULL) {
        return true;
    }
    return memory->video_controller.cpu_access_allowed(memory->video_controller.user,
                                                        address, write);
}

static bool dma_cpu_access_allowed(GB_Memory *memory, uint16_t address, bool write)
{
    if (memory->dma_controller.cpu_access_allowed == NULL) {
        return true;
    }
    return memory->dma_controller.cpu_access_allowed(memory->dma_controller.user,
                                                      address, write);
}

GB_Result gb_memory_read8(GB_Memory *memory, uint16_t address,
                          uint8_t *value, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    if (value == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Memory read output pointer is NULL", address);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (!dma_cpu_access_allowed(memory, address, false)) {
        *value = 0xFFu;
        return GB_RESULT_OK;
    }

    switch (gb_memory_classify_address(address)) {
    case GB_MEMORY_REGION_CARTRIDGE_ROM:
    case GB_MEMORY_REGION_CARTRIDGE_RAM:
        return cartridge_read(memory, address, value, error);

    case GB_MEMORY_REGION_VRAM: {
        if (!video_cpu_access_allowed(memory, address, false)) {
            *value = 0xFFu;
            return GB_RESULT_OK;
        }
        uint8_t bank = memory->vram_bank;
        if (memory->mode == GB_MEMORY_MODE_DMG) bank = 0u;
        *value = memory->vram[bank][address - 0x8000u];
        return GB_RESULT_OK;
    }

    case GB_MEMORY_REGION_WRAM:
        if (address < 0xD000u) {
            *value = memory->wram[0][address - 0xC000u];
        } else {
            uint8_t bank = (memory->mode == GB_MEMORY_MODE_CGB) ? memory->wram_bank : 1u;
            *value = memory->wram[bank][address - 0xD000u];
        }
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_ECHO: {
        uint16_t mirrored = (uint16_t)(address - 0x2000u);
        return gb_memory_read8(memory, mirrored, value, error);
    }

    case GB_MEMORY_REGION_OAM:
        if (!video_cpu_access_allowed(memory, address, false)) {
            *value = 0xFFu;
            return GB_RESULT_OK;
        }
        *value = memory->oam[address - 0xFE00u];
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_UNUSABLE:
        *value = 0xFFu;
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_IO: {
        GB_MemoryIODevice *device = find_io_device(memory, address);
        if (device != NULL && device->read8 != NULL) {
            return device->read8(device->user, address, value, error);
        }

        uint8_t offset = (uint8_t)(address - GB_MEMORY_IO_BASE);
        if (device == NULL && memory->io_present[offset] == 0u) {
            *value = 0xFFu;
            return GB_RESULT_OK;
        }

        if (is_cgb_only_register(address) && memory->mode != GB_MEMORY_MODE_CGB) {
            *value = 0xFFu;
            return GB_RESULT_OK;
        }

        if (memory->io_readable[offset] == 0u) {
            *value = 0xFFu;
            return GB_RESULT_OK;
        }

        if (address == GB_ADDR_IF) {
            if (memory->interrupt_controller.read8 == NULL) {
                memory_error(error, GB_RESULT_BAD_STATE,
                             "Interrupt controller is not attached", address);
                return GB_RESULT_BAD_STATE;
            }
            return memory->interrupt_controller.read8(memory->interrupt_controller.user,
                                                      address, value, error);
        }

        *value = io_default_read(memory, address);
        return GB_RESULT_OK;
    }

    case GB_MEMORY_REGION_HRAM:
        *value = memory->hram[address - 0xFF80u];
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_IE:
        if (memory->interrupt_controller.read8 == NULL) {
            memory_error(error, GB_RESULT_BAD_STATE,
                         "Interrupt controller is not attached", address);
            return GB_RESULT_BAD_STATE;
        }
        return memory->interrupt_controller.read8(memory->interrupt_controller.user,
                                                  address, value, error);

    default:
        memory_error(error, GB_RESULT_BAD_STATE,
                     "Address classification returned an invalid region", address);
        return GB_RESULT_BAD_STATE;
    }
}

GB_Result gb_memory_write8(GB_Memory *memory, uint16_t address,
                           uint8_t value, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    if (!dma_cpu_access_allowed(memory, address, true)) {
        return GB_RESULT_OK;
    }

    switch (gb_memory_classify_address(address)) {
    case GB_MEMORY_REGION_CARTRIDGE_ROM:
    case GB_MEMORY_REGION_CARTRIDGE_RAM:
        return cartridge_write(memory, address, value, error);

    case GB_MEMORY_REGION_VRAM: {
        if (!video_cpu_access_allowed(memory, address, true)) {
            return GB_RESULT_OK;
        }
        uint8_t bank = memory->vram_bank;
        if (memory->mode == GB_MEMORY_MODE_DMG) bank = 0u;
        memory->vram[bank][address - 0x8000u] = value;
        return GB_RESULT_OK;
    }

    case GB_MEMORY_REGION_WRAM:
        if (address < 0xD000u) {
            memory->wram[0][address - 0xC000u] = value;
        } else {
            uint8_t bank = (memory->mode == GB_MEMORY_MODE_CGB) ? memory->wram_bank : 1u;
            memory->wram[bank][address - 0xD000u] = value;
        }
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_ECHO: {
        uint16_t mirrored = (uint16_t)(address - 0x2000u);
        return gb_memory_write8(memory, mirrored, value, error);
    }

    case GB_MEMORY_REGION_OAM:
        if (!video_cpu_access_allowed(memory, address, true)) {
            return GB_RESULT_OK;
        }
        memory->oam[address - 0xFE00u] = value;
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_UNUSABLE:
        return GB_RESULT_OK; /* Prohibited area; writes have no useful storage. */

    case GB_MEMORY_REGION_IO: {
        GB_MemoryIODevice *device = find_io_device(memory, address);
        if (device != NULL && device->write8 != NULL) {
            return device->write8(device->user, address, value, error);
        }

        uint8_t offset = (uint8_t)(address - GB_MEMORY_IO_BASE);
        if (device == NULL && memory->io_present[offset] == 0u) {
            return GB_RESULT_OK;
        }

        if (is_cgb_only_register(address) && memory->mode != GB_MEMORY_MODE_CGB) {
            return GB_RESULT_OK;
        }

        if (address == GB_ADDR_IF) {
            if (memory->interrupt_controller.write8 == NULL) {
                memory_error(error, GB_RESULT_BAD_STATE,
                             "Interrupt controller is not attached", address);
                return GB_RESULT_BAD_STATE;
            }
            return memory->interrupt_controller.write8(memory->interrupt_controller.user,
                                                       address, value, error);
        }

        if (memory->io_writable[offset] == 0u) {
            return GB_RESULT_OK;
        }

        io_default_write(memory, address, value);
        return GB_RESULT_OK;
    }

    case GB_MEMORY_REGION_HRAM:
        memory->hram[address - 0xFF80u] = value;
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_IE:
        if (memory->interrupt_controller.write8 == NULL) {
            memory_error(error, GB_RESULT_BAD_STATE,
                         "Interrupt controller is not attached", address);
            return GB_RESULT_BAD_STATE;
        }
        return memory->interrupt_controller.write8(memory->interrupt_controller.user,
                                                   address, value, error);

    default:
        memory_error(error, GB_RESULT_BAD_STATE,
                     "Address classification returned an invalid region", address);
        return GB_RESULT_BAD_STATE;
    }
}

GB_Result gb_memory_tick(GB_Memory *memory, uint32_t t_cycles, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    if (t_cycles == 0u) return GB_RESULT_OK;

    if (memory->cartridge_bus.tick != NULL) {
        GB_Error cartridge_error;
        gb_error_clear(&cartridge_error);
        result = memory->cartridge_bus.tick(memory->cartridge_bus.user,
                                            t_cycles, &cartridge_error);
        if (result != GB_RESULT_OK) {
            if (error != NULL) {
                *error = cartridge_error;
                if (error->message[0] == '\0') {
                    (void)snprintf(error->message, sizeof(error->message),
                                   "Cartridge tick failed");
                }
            }
            return GB_RESULT_BUS_TICK;
        }
    }

    for (size_t i = 0; i < GB_MEMORY_MAX_IO_DEVICES; ++i) {
        GB_MemoryIODevice *device = &memory->io_devices[i];
        if (!device->used || device->tick == NULL) continue;

        GB_Error device_error;
        gb_error_clear(&device_error);
        result = device->tick(device->user, t_cycles, &device_error);
        if (result != GB_RESULT_OK) {
            if (error != NULL) {
                *error = device_error;
                if (error->message[0] == '\0') {
                    (void)snprintf(error->message, sizeof(error->message),
                                   "Mapped I/O device tick failed for $%04X-$%04X",
                                   device->start, device->end);
                }
            }
            return result;
        }
    }

    return GB_RESULT_OK;
}

GB_Result gb_memory_set_dma_controller(GB_Memory *memory,
                                        const GB_MemoryDMAController *controller,
                                        GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    if (controller == NULL || controller->cpu_access_allowed == NULL) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "DMA controller requires a CPU access callback", GB_ADDR_DMA);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    memory->dma_controller = *controller;
    return GB_RESULT_OK;
}

GB_Result gb_memory_clear_dma_controller(GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    memset(&memory->dma_controller, 0, sizeof(memory->dma_controller));
    return GB_RESULT_OK;
}

GB_Result gb_memory_set_interrupt_controller(GB_Memory *memory,
                                              const GB_MemoryInterruptController *controller,
                                              GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    if (controller == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Interrupt controller pointer is NULL", GB_ADDR_IF);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (controller->read8 == NULL || controller->write8 == NULL ||
        controller->get_pending_interrupts == NULL ||
        controller->acknowledge_interrupt == NULL ||
        controller->request_interrupt == NULL) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "Interrupt controller callbacks are incomplete", GB_ADDR_IF);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    memory->interrupt_controller = *controller;
    return GB_RESULT_OK;
}

GB_Result gb_memory_clear_interrupt_controller(GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    memset(&memory->interrupt_controller, 0, sizeof(memory->interrupt_controller));
    return GB_RESULT_OK;
}

GB_Result gb_memory_set_video_controller(GB_Memory *memory,
                                           const GB_MemoryVideoController *controller,
                                           GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (controller == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Video controller pointer is NULL", 0x8000u);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (controller->cpu_access_allowed == NULL) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "Video controller requires a CPU access callback", 0x8000u);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    if (memory->video_controller.cpu_access_allowed != NULL &&
        memory->video_controller.user != controller->user) {
        memory_error(error, GB_RESULT_BAD_STATE,
                     "A different video controller is already attached", 0x8000u);
        return GB_RESULT_BAD_STATE;
    }

    memory->video_controller = *controller;
    return GB_RESULT_OK;
}

GB_Result gb_memory_clear_video_controller(GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    memset(&memory->video_controller, 0, sizeof(memory->video_controller));
    return GB_RESULT_OK;
}

GB_Result gb_memory_get_pending_interrupts(GB_Memory *memory,
                                           uint8_t *pending_mask,
                                           GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    if (pending_mask == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Pending interrupt output pointer is NULL", GB_ADDR_IF);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (memory->interrupt_controller.get_pending_interrupts == NULL) {
        memory_error(error, GB_RESULT_BAD_STATE,
                     "Interrupt controller is not attached", GB_ADDR_IF);
        return GB_RESULT_BAD_STATE;
    }

    return memory->interrupt_controller.get_pending_interrupts(
        memory->interrupt_controller.user, pending_mask, error);
}

GB_Result gb_memory_acknowledge_interrupt(GB_Memory *memory,
                                          uint8_t interrupt_mask,
                                          GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    if (memory->interrupt_controller.acknowledge_interrupt == NULL) {
        memory_error(error, GB_RESULT_BAD_STATE,
                     "Interrupt controller is not attached", GB_ADDR_IF);
        return GB_RESULT_BAD_STATE;
    }

    return memory->interrupt_controller.acknowledge_interrupt(
        memory->interrupt_controller.user, interrupt_mask, error);
}

GB_Result gb_memory_request_interrupt(GB_Memory *memory,
                                      uint8_t interrupt_mask,
                                      GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;

    if (memory->interrupt_controller.request_interrupt == NULL) {
        memory_error(error, GB_RESULT_BAD_STATE,
                     "Interrupt controller is not attached", GB_ADDR_IF);
        return GB_RESULT_BAD_STATE;
    }

    return memory->interrupt_controller.request_interrupt(
        memory->interrupt_controller.user, interrupt_mask, error);
}

static GB_Result memory_bus_read8(void *user, uint16_t address,
                                  uint8_t *value, GB_Error *error)
{
    return gb_memory_read8((GB_Memory *)user, address, value, error);
}

static GB_Result memory_bus_write8(void *user, uint16_t address,
                                   uint8_t value, GB_Error *error)
{
    return gb_memory_write8((GB_Memory *)user, address, value, error);
}

static GB_Result memory_bus_tick(void *user, uint32_t t_cycles, GB_Error *error)
{
    return gb_memory_tick((GB_Memory *)user, t_cycles, error);
}

static GB_Result memory_bus_get_pending_interrupts(void *user, uint8_t *pending_mask,
                                                    GB_Error *error)
{
    return gb_memory_get_pending_interrupts((GB_Memory *)user, pending_mask, error);
}

static GB_Result memory_bus_acknowledge_interrupt(void *user, uint8_t interrupt_mask,
                                                  GB_Error *error)
{
    return gb_memory_acknowledge_interrupt((GB_Memory *)user, interrupt_mask, error);
}

GB_CPU_BUS gb_memory_cpu_bus(GB_Memory *memory)
{
    GB_CPU_BUS bus;
    memset(&bus, 0, sizeof(bus));
    bus.user = memory;
    bus.read8 = memory_bus_read8;
    bus.write8 = memory_bus_write8;
    bus.tick = memory_bus_tick;
    bus.get_pending_interrupts = memory_bus_get_pending_interrupts;
    bus.acknowledge_interrupt = memory_bus_acknowledge_interrupt;
    return bus;
}

uint8_t *gb_memory_vram_bank_ptr(GB_Memory *memory, uint8_t bank, GB_Error *error)
{
    gb_error_clear(error);
    if (memory == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Memory pointer is NULL", 0);
        return NULL;
    }
    if (bank >= GB_MEMORY_VRAM_BANK_COUNT) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "VRAM bank is out of range", bank);
        return NULL;
    }
    return memory->vram[bank];
}

uint8_t *gb_memory_wram_bank_ptr(GB_Memory *memory, uint8_t bank, GB_Error *error)
{
    gb_error_clear(error);
    if (memory == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "Memory pointer is NULL", 0);
        return NULL;
    }
    if (bank >= GB_MEMORY_WRAM_BANK_COUNT) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "WRAM bank is out of range", bank);
        return NULL;
    }
    return memory->wram[bank];
}

uint8_t *gb_memory_oam_ptr(GB_Memory *memory)
{
    return memory == NULL ? NULL : memory->oam;
}

uint8_t *gb_memory_hram_ptr(GB_Memory *memory)
{
    return memory == NULL ? NULL : memory->hram;
}

GB_Result gb_memory_dma_read8(GB_Memory *memory, uint16_t address,
                              uint8_t *value, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;
    if (value == NULL) {
        memory_error(error, GB_RESULT_NULL_ARGUMENT,
                     "DMA read output pointer is NULL", address);
        return GB_RESULT_NULL_ARGUMENT;
    }

    switch (gb_memory_classify_address(address)) {
    case GB_MEMORY_REGION_CARTRIDGE_ROM:
    case GB_MEMORY_REGION_CARTRIDGE_RAM:
        return cartridge_read(memory, address, value, error);

    case GB_MEMORY_REGION_VRAM: {
        uint8_t bank = memory->vram_bank;
        if (memory->mode == GB_MEMORY_MODE_DMG) bank = 0u;
        *value = memory->vram[bank][address - 0x8000u];
        return GB_RESULT_OK;
    }

    case GB_MEMORY_REGION_WRAM:
        if (address < 0xD000u) {
            *value = memory->wram[0][address - 0xC000u];
        } else {
            uint8_t bank = (memory->mode == GB_MEMORY_MODE_CGB) ? memory->wram_bank : 1u;
            *value = memory->wram[bank][address - 0xD000u];
        }
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_ECHO:
        return gb_memory_dma_read8(memory, (uint16_t)(address - 0x2000u), value, error);

    case GB_MEMORY_REGION_OAM:
        *value = memory->oam[address - 0xFE00u];
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_UNUSABLE:
        *value = 0xFFu;
        return GB_RESULT_OK;

    case GB_MEMORY_REGION_IO:
    case GB_MEMORY_REGION_HRAM:
    case GB_MEMORY_REGION_IE:
        *value = 0xFFu;
        return GB_RESULT_OK;

    default:
        memory_error(error, GB_RESULT_BAD_STATE,
                     "Invalid address during DMA read", address);
        return GB_RESULT_BAD_STATE;
    }
}

GB_Result gb_memory_dma_write_oam(GB_Memory *memory, uint16_t oam_offset,
                                  uint8_t value, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;
    if (oam_offset >= GB_MEMORY_OAM_SIZE) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "DMA OAM destination is outside OAM", oam_offset);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    memory->oam[oam_offset] = value;
    return GB_RESULT_OK;
}

GB_Result gb_memory_dma_write_vram8(GB_Memory *memory, uint8_t bank,
                                    uint16_t vram_offset, uint8_t value,
                                    GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_memory(memory, error);
    if (result != GB_RESULT_OK) return result;
    if (memory->mode != GB_MEMORY_MODE_CGB) {
        memory_error(error, GB_RESULT_UNSUPPORTED,
                     "CGB VRAM DMA is unavailable in DMG mode",
                     (uint16_t)(0x8000u + vram_offset));
        return GB_RESULT_UNSUPPORTED;
    }
    if (bank >= GB_MEMORY_VRAM_BANK_COUNT) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "VRAM DMA bank is outside the CGB bank range", bank);
        return GB_RESULT_INVALID_ARGUMENT;
    }
    if (vram_offset >= GB_MEMORY_VRAM_BANK_SIZE) {
        memory_error(error, GB_RESULT_INVALID_ARGUMENT,
                     "VRAM DMA destination is outside VRAM", vram_offset);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    memory->vram[bank][vram_offset] = value;
    return GB_RESULT_OK;
}
