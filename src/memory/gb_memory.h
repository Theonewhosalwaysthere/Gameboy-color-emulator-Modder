#ifndef GB_MEMORY_H
#define GB_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../cpu/gb_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_MEMORY_VRAM_BANK_COUNT 2u
#define GB_MEMORY_VRAM_BANK_SIZE 0x2000u
#define GB_MEMORY_WRAM_BANK_COUNT 8u
#define GB_MEMORY_WRAM_BANK_SIZE 0x1000u
#define GB_MEMORY_OAM_SIZE        0x00A0u
#define GB_MEMORY_HRAM_SIZE       0x007Fu
#define GB_MEMORY_IO_SIZE         0x0080u
#define GB_MEMORY_MAX_IO_DEVICES  16u

#define GB_MEMORY_INTERRUPT_MASK 0x1Fu

enum {
    GB_ADDR_JOYP = 0xFF00u,
    GB_ADDR_SB   = 0xFF01u,
    GB_ADDR_SC   = 0xFF02u,
    GB_ADDR_DIV  = 0xFF04u,
    GB_ADDR_TIMA = 0xFF05u,
    GB_ADDR_TMA  = 0xFF06u,
    GB_ADDR_TAC  = 0xFF07u,
    GB_ADDR_IF   = 0xFF0Fu,
    GB_ADDR_LCDC = 0xFF40u,
    GB_ADDR_STAT = 0xFF41u,
    GB_ADDR_LY   = 0xFF44u,
    GB_ADDR_DMA  = 0xFF46u,
    GB_ADDR_BGP  = 0xFF47u,
    GB_ADDR_OBP0 = 0xFF48u,
    GB_ADDR_OBP1 = 0xFF49u,
    GB_ADDR_WY   = 0xFF4Au,
    GB_ADDR_WX   = 0xFF4Bu,
    GB_ADDR_KEY1 = 0xFF4Du,
    GB_ADDR_VBK  = 0xFF4Fu,
    GB_ADDR_BOOT = 0xFF50u,
    GB_ADDR_HDMA1 = 0xFF51u,
    GB_ADDR_HDMA2 = 0xFF52u,
    GB_ADDR_HDMA3 = 0xFF53u,
    GB_ADDR_HDMA4 = 0xFF54u,
    GB_ADDR_HDMA5 = 0xFF55u,
    GB_ADDR_RP    = 0xFF56u,
    GB_ADDR_BCPS  = 0xFF68u,
    GB_ADDR_BCPD  = 0xFF69u,
    GB_ADDR_OCPS  = 0xFF6Au,
    GB_ADDR_OCPD  = 0xFF6Bu,
    GB_ADDR_OPRI  = 0xFF6Cu,
    GB_ADDR_SVBK  = 0xFF70u,
    GB_ADDR_PCM12 = 0xFF76u,
    GB_ADDR_PCM34 = 0xFF77u,
    GB_ADDR_IE    = 0xFFFFu
};

typedef enum GB_MemoryMode {
    GB_MEMORY_MODE_DMG = 0,
    GB_MEMORY_MODE_CGB
} GB_MemoryMode;

typedef enum GB_MemoryRegion {
    GB_MEMORY_REGION_CARTRIDGE_ROM = 0,
    GB_MEMORY_REGION_CARTRIDGE_RAM,
    GB_MEMORY_REGION_VRAM,
    GB_MEMORY_REGION_WRAM,
    GB_MEMORY_REGION_ECHO,
    GB_MEMORY_REGION_OAM,
    GB_MEMORY_REGION_UNUSABLE,
    GB_MEMORY_REGION_IO,
    GB_MEMORY_REGION_HRAM,
    GB_MEMORY_REGION_IE
} GB_MemoryRegion;

/* Cartridge callbacks are intentionally limited to the external-bus regions.
 * MBC logic and ROM/RAM file ownership belong to the later cartridge subsystem. */
typedef struct GB_MemoryCartridgeBus {
    void *user;
    GB_BusRead8 read8;
    GB_BusWrite8 write8;
    GB_BusTick tick;
} GB_MemoryCartridgeBus;

typedef GB_Result (*GB_MemoryIORead8)(void *user, uint16_t address,
                                      uint8_t *value, GB_Error *error);
typedef GB_Result (*GB_MemoryIOWrite8)(void *user, uint16_t address,
                                       uint8_t value, GB_Error *error);
typedef GB_Result (*GB_MemoryIOTick)(void *user, uint32_t t_cycles, GB_Error *error);

typedef GB_Result (*GB_MemoryRequestInterrupt)(void *user, uint8_t interrupt_mask,
                                                GB_Error *error);

typedef bool (*GB_MemoryVideoAccessAllowed)(void *user, uint16_t address, bool write);

typedef struct GB_MemoryVideoController {
    void *user;
    GB_MemoryVideoAccessAllowed cpu_access_allowed;
} GB_MemoryVideoController;

typedef struct GB_MemoryDMAController {
    void *user;
    GB_MemoryVideoAccessAllowed cpu_access_allowed;
} GB_MemoryDMAController;

typedef struct GB_MemoryIODevice {
    bool used;
    uint16_t start;
    uint16_t end;
    void *user;
    GB_MemoryIORead8 read8;
    GB_MemoryIOWrite8 write8;
    GB_MemoryIOTick tick;
} GB_MemoryIODevice;

/* The interrupt controller owns IF ($FF0F) and IE ($FFFF). */
typedef struct GB_MemoryInterruptController {
    void *user;
    GB_MemoryIORead8 read8;
    GB_MemoryIOWrite8 write8;
    GB_GetPendingInterrupts get_pending_interrupts;
    GB_AcknowledgeInterrupt acknowledge_interrupt;
    GB_MemoryRequestInterrupt request_interrupt;
} GB_MemoryInterruptController;

typedef struct GB_Memory {
    GB_MemoryMode mode;

    /* C000-CFFF is bank 0; D000-DFFF uses wram_bank in CGB mode. */
    uint8_t wram[GB_MEMORY_WRAM_BANK_COUNT][GB_MEMORY_WRAM_BANK_SIZE];
    uint8_t vram[GB_MEMORY_VRAM_BANK_COUNT][GB_MEMORY_VRAM_BANK_SIZE];
    uint8_t oam[GB_MEMORY_OAM_SIZE];
    uint8_t hram[GB_MEMORY_HRAM_SIZE];

    /* Shadow storage for I/O registers not yet owned by a hardware device. */
    uint8_t io[GB_MEMORY_IO_SIZE];
    uint8_t io_present[GB_MEMORY_IO_SIZE];
    uint8_t io_readable[GB_MEMORY_IO_SIZE];
    uint8_t io_writable[GB_MEMORY_IO_SIZE];

    uint8_t vram_bank;
    uint8_t wram_bank;

    GB_MemoryCartridgeBus cartridge_bus;
    GB_MemoryInterruptController interrupt_controller;
    GB_MemoryVideoController video_controller;
    GB_MemoryDMAController dma_controller;
    GB_MemoryIODevice io_devices[GB_MEMORY_MAX_IO_DEVICES];
} GB_Memory;

GB_Result gb_memory_init(GB_Memory *memory, GB_MemoryMode mode, GB_Error *error);
GB_Result gb_memory_reset(GB_Memory *memory, GB_Error *error);

GB_Result gb_memory_set_cartridge_bus(GB_Memory *memory,
                                      const GB_MemoryCartridgeBus *cartridge_bus,
                                      GB_Error *error);
GB_Result gb_memory_clear_cartridge_bus(GB_Memory *memory, GB_Error *error);

GB_Result gb_memory_set_interrupt_controller(GB_Memory *memory,
                                              const GB_MemoryInterruptController *controller,
                                              GB_Error *error);
GB_Result gb_memory_clear_interrupt_controller(GB_Memory *memory, GB_Error *error);

GB_Result gb_memory_set_video_controller(GB_Memory *memory,
                                           const GB_MemoryVideoController *controller,
                                           GB_Error *error);
GB_Result gb_memory_clear_video_controller(GB_Memory *memory, GB_Error *error);

GB_Result gb_memory_set_dma_controller(GB_Memory *memory,
                                        const GB_MemoryDMAController *controller,
                                        GB_Error *error);
GB_Result gb_memory_clear_dma_controller(GB_Memory *memory, GB_Error *error);

/*
 * Map a hardware device over an I/O range. Ranges may not overlap. Device
 * callbacks may be NULL for read-only/write-only/timing-only devices.
 */
GB_Result gb_memory_map_io_device(GB_Memory *memory,
                                  uint16_t start,
                                  uint16_t end,
                                  void *user,
                                  GB_MemoryIORead8 read8,
                                  GB_MemoryIOWrite8 write8,
                                  GB_MemoryIOTick tick,
                                  size_t *device_index,
                                  GB_Error *error);
GB_Result gb_memory_unmap_io_device(GB_Memory *memory,
                                    size_t device_index,
                                    GB_Error *error);

GB_Result gb_memory_read8(GB_Memory *memory, uint16_t address,
                          uint8_t *value, GB_Error *error);
GB_Result gb_memory_write8(GB_Memory *memory, uint16_t address,
                           uint8_t value, GB_Error *error);

/* Advance all currently registered hardware devices and the cartridge clock. */
GB_Result gb_memory_tick(GB_Memory *memory, uint32_t t_cycles, GB_Error *error);

/* Raw bus operations used by hardware DMA. These bypass CPU DMA/PPU access
 * restrictions and never recurse through the CPU-visible access policy. */
GB_Result gb_memory_dma_read8(GB_Memory *memory, uint16_t address,
                              uint8_t *value, GB_Error *error);
GB_Result gb_memory_dma_write_oam(GB_Memory *memory, uint16_t oam_offset,
                                  uint8_t value, GB_Error *error);
GB_Result gb_memory_dma_write_vram8(GB_Memory *memory, uint8_t bank,
                                    uint16_t vram_offset, uint8_t value,
                                    GB_Error *error);

/* Interrupt-register services used by the CPU bus and future hardware. */
GB_Result gb_memory_get_pending_interrupts(GB_Memory *memory,
                                           uint8_t *pending_mask,
                                           GB_Error *error);
GB_Result gb_memory_acknowledge_interrupt(GB_Memory *memory,
                                          uint8_t interrupt_mask,
                                          GB_Error *error);
GB_Result gb_memory_request_interrupt(GB_Memory *memory,
                                      uint8_t interrupt_mask,
                                      GB_Error *error);

/* Build the callback bundle consumed by GB_CPU. No ownership is transferred. */
GB_CPU_BUS gb_memory_cpu_bus(GB_Memory *memory);

GB_MemoryRegion gb_memory_classify_address(uint16_t address);
const char *gb_memory_region_name(GB_MemoryRegion region);

/* Pointers are views into GB_Memory and remain valid until memory is destroyed. */
uint8_t *gb_memory_vram_bank_ptr(GB_Memory *memory, uint8_t bank, GB_Error *error);
uint8_t *gb_memory_wram_bank_ptr(GB_Memory *memory, uint8_t bank, GB_Error *error);
uint8_t *gb_memory_oam_ptr(GB_Memory *memory);
uint8_t *gb_memory_hram_ptr(GB_Memory *memory);

#ifdef __cplusplus
}
#endif

#endif /* GB_MEMORY_H */
