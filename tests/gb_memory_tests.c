#include "../src/memory/gb_memory.h"
#include "../src/interrupt/gb_interrupt.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        ++failures; \
    } \
} while (0)

#define CHECK_RESULT(result, message) CHECK((result) == GB_RESULT_OK, message)

static void init_memory_with_interrupts(GB_Memory *memory, GB_Interrupt *interrupts,
                                        GB_MemoryMode mode)
{
    GB_Error error;
    CHECK_RESULT(gb_memory_init(memory, mode, &error),
                 "memory init must succeed");
    CHECK_RESULT(gb_interrupt_init(interrupts, &error),
                 "interrupt init must succeed");
    CHECK_RESULT(gb_interrupt_connect_memory(interrupts, memory, &error),
                 "interrupt memory connection must succeed");
}

static uint8_t test_read(GB_Memory *memory, uint16_t address)
{
    GB_Error error;
    uint8_t value = 0;
    GB_Result result = gb_memory_read8(memory, address, &value, &error);
    if (result != GB_RESULT_OK) {
        fprintf(stderr, "read $%04X failed: %s\n", address, error.message);
        ++failures;
        return 0;
    }
    return value;
}

static void test_write(GB_Memory *memory, uint16_t address, uint8_t value)
{
    GB_Error error;
    GB_Result result = gb_memory_write8(memory, address, value, &error);
    if (result != GB_RESULT_OK) {
        fprintf(stderr, "write $%04X failed: %s\n", address, error.message);
        ++failures;
    }
}

typedef struct DeviceState {
    uint8_t value;
    uint32_t ticks;
} DeviceState;

static GB_Result device_read(void *user, uint16_t address, uint8_t *value, GB_Error *error)
{
    (void)address;
    (void)error;
    DeviceState *state = (DeviceState *)user;
    *value = state->value;
    return GB_RESULT_OK;
}

static GB_Result device_write(void *user, uint16_t address, uint8_t value, GB_Error *error)
{
    (void)address;
    (void)error;
    DeviceState *state = (DeviceState *)user;
    state->value = value;
    return GB_RESULT_OK;
}

static GB_Result device_tick(void *user, uint32_t t_cycles, GB_Error *error)
{
    (void)error;
    DeviceState *state = (DeviceState *)user;
    state->ticks += t_cycles;
    return GB_RESULT_OK;
}

static void test_address_classification(void)
{
    CHECK(gb_memory_classify_address(0x0000u) == GB_MEMORY_REGION_CARTRIDGE_ROM,
          "0000 must be cartridge ROM");
    CHECK(gb_memory_classify_address(0x8000u) == GB_MEMORY_REGION_VRAM,
          "8000 must be VRAM");
    CHECK(gb_memory_classify_address(0xA000u) == GB_MEMORY_REGION_CARTRIDGE_RAM,
          "A000 must be cartridge RAM");
    CHECK(gb_memory_classify_address(0xC000u) == GB_MEMORY_REGION_WRAM,
          "C000 must be WRAM");
    CHECK(gb_memory_classify_address(0xE000u) == GB_MEMORY_REGION_ECHO,
          "E000 must be echo RAM");
    CHECK(gb_memory_classify_address(0xFE00u) == GB_MEMORY_REGION_OAM,
          "FE00 must be OAM");
    CHECK(gb_memory_classify_address(0xFEA0u) == GB_MEMORY_REGION_UNUSABLE,
          "FEA0 must be unusable");
    CHECK(gb_memory_classify_address(0xFF00u) == GB_MEMORY_REGION_IO,
          "FF00 must be I/O");
    CHECK(gb_memory_classify_address(0xFF80u) == GB_MEMORY_REGION_HRAM,
          "FF80 must be HRAM");
    CHECK(gb_memory_classify_address(0xFFFFu) == GB_MEMORY_REGION_IE,
          "FFFF must be IE");
}

static void test_dmg_memory(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    init_memory_with_interrupts(&memory, &interrupts, GB_MEMORY_MODE_DMG);

    test_write(&memory, 0xC000u, 0x12u);
    CHECK(test_read(&memory, 0xC000u) == 0x12u, "WRAM bank 0 read/write");
    CHECK(test_read(&memory, 0xE000u) == 0x12u, "E000 must mirror C000");

    test_write(&memory, 0xDDFFu, 0x34u);
    CHECK(test_read(&memory, 0xFDFFu) == 0x34u, "FDFF must mirror DDFF");

    test_write(&memory, 0x8000u, 0x56u);
    CHECK(test_read(&memory, 0x8000u) == 0x56u, "DMG VRAM read/write");

    test_write(&memory, 0xFE00u, 0x78u);
    CHECK(test_read(&memory, 0xFE00u) == 0x78u, "OAM read/write");

    test_write(&memory, 0xFF80u, 0x9Au);
    CHECK(test_read(&memory, 0xFF80u) == 0x9Au, "HRAM read/write");

    test_write(&memory, 0xFEA0u, 0xFFu);
    CHECK(test_read(&memory, 0xFEA0u) == 0xFFu,
          "unusable area must not expose writable storage");

    CHECK(test_read(&memory, 0xFF03u) == 0xFFu,
          "unmapped FF03 must read as open bus high");
    test_write(&memory, 0xFF03u, 0x00u);
    CHECK(test_read(&memory, 0xFF03u) == 0xFFu,
          "writes to unmapped FF03 must have no effect");

    CHECK(test_read(&memory, GB_ADDR_JOYP) == 0xCFu,
          "standalone JOYP default must have inputs high");
    test_write(&memory, GB_ADDR_JOYP, 0x10u);
    CHECK((test_read(&memory, GB_ADDR_JOYP) & 0x30u) == 0x10u,
          "JOYP selection bits must be writable");
    CHECK((test_read(&memory, GB_ADDR_JOYP) & 0xCFu) == 0xCFu,
          "JOYP fixed and unpressed bits must remain high");

    CHECK((test_read(&memory, GB_ADDR_IF) & 0xE0u) == 0xE0u,
          "IF upper bits must read high");
    test_write(&memory, GB_ADDR_IF, 0x1Fu);
    CHECK((test_read(&memory, GB_ADDR_IF) & 0x1Fu) == 0x1Fu,
          "IF low five bits must be writable");

    test_write(&memory, GB_ADDR_IE, 0x1Bu);
    CHECK((test_read(&memory, GB_ADDR_IE) & 0x1Fu) == 0x1Bu,
          "IE low five bits must be writable");
}

static void test_cgb_banks(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    init_memory_with_interrupts(&memory, &interrupts, GB_MEMORY_MODE_CGB);

    test_write(&memory, 0x8000u, 0x11u);
    test_write(&memory, GB_ADDR_VBK, 0x01u);
    CHECK(test_read(&memory, GB_ADDR_VBK) == 0xFFu,
          "VBK bank 1 read must be FF");
    test_write(&memory, 0x8000u, 0x22u);
    CHECK(test_read(&memory, 0x8000u) == 0x22u, "CGB VRAM bank 1 read/write");

    test_write(&memory, GB_ADDR_VBK, 0x00u);
    CHECK(test_read(&memory, 0x8000u) == 0x11u, "VRAM bank switch back to bank 0");

    test_write(&memory, 0xD000u, 0x33u);
    test_write(&memory, GB_ADDR_SVBK, 0x02u);
    test_write(&memory, 0xD000u, 0x44u);
    CHECK(test_read(&memory, 0xD000u) == 0x44u, "CGB WRAM bank 2 read/write");

    test_write(&memory, GB_ADDR_SVBK, 0x00u);
    CHECK((test_read(&memory, GB_ADDR_SVBK) & 0x07u) == 0x01u,
          "SVBK zero must select bank 1");
    CHECK(test_read(&memory, 0xD000u) == 0x33u,
          "SVBK bank 1 must remain separate from bank 2");

    test_write(&memory, GB_ADDR_SVBK, 0x07u);
    test_write(&memory, 0xD000u, 0x55u);
    test_write(&memory, 0xF000u, 0x66u);
    CHECK(test_read(&memory, 0xD000u) == 0x66u,
          "echo F000 must mirror D000 selected WRAM bank");
    CHECK(test_read(&memory, 0xE000u) == test_read(&memory, 0xC000u),
          "E000 must always mirror fixed WRAM bank 0");
}

static void test_interrupt_bus(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Error error;
    uint8_t pending = 0;
    init_memory_with_interrupts(&memory, &interrupts, GB_MEMORY_MODE_CGB);

    CHECK_RESULT(gb_memory_request_interrupt(&memory,
                                             GB_INTERRUPT_VBLANK | GB_INTERRUPT_TIMER,
                                             &error),
                  "request interrupts");
    test_write(&memory, GB_ADDR_IE, GB_INTERRUPT_TIMER | GB_INTERRUPT_SERIAL);
    CHECK_RESULT(gb_memory_get_pending_interrupts(&memory, &pending, &error),
                  "get pending interrupts");
    CHECK(pending == GB_INTERRUPT_TIMER, "pending interrupts must be IF & IE");

    CHECK_RESULT(gb_memory_acknowledge_interrupt(&memory, GB_INTERRUPT_TIMER, &error),
                  "acknowledge timer interrupt");
    CHECK_RESULT(gb_memory_get_pending_interrupts(&memory, &pending, &error),
                  "get pending after acknowledge");
    CHECK(pending == 0u, "acknowledged interrupt must be cleared");

    GB_CPU_BUS bus = gb_memory_cpu_bus(&memory);
    CHECK(bus.user == &memory, "CPU bus user must point at memory");
    CHECK(bus.read8 != NULL && bus.write8 != NULL && bus.tick != NULL,
          "CPU bus must expose read/write/tick callbacks");
    CHECK(bus.get_pending_interrupts != NULL && bus.acknowledge_interrupt != NULL,
          "CPU bus must expose interrupt callbacks");
}

static void test_io_device_mapping(void)
{
    GB_Memory memory;
    GB_Error error;
    DeviceState state;
    memset(&state, 0, sizeof(state));
    size_t device_index = SIZE_MAX;

    CHECK_RESULT(gb_memory_init(&memory, GB_MEMORY_MODE_DMG, &error),
                  "device mapping memory init");
    CHECK_RESULT(gb_memory_map_io_device(&memory, 0xFF40u, 0xFF43u,
                                         &state, device_read, device_write,
                                         device_tick, &device_index, &error),
                  "map I/O device");

    test_write(&memory, 0xFF40u, 0xA5u);
    CHECK(test_read(&memory, 0xFF40u) == 0xA5u,
          "mapped I/O device must own reads/writes");

    CHECK_RESULT(gb_memory_tick(&memory, 32u, &error), "tick mapped device");
    CHECK(state.ticks == 32u, "mapped device must receive timing");

    CHECK_RESULT(gb_memory_map_io_device(&memory, 0xFF42u, 0xFF44u,
                                         NULL, device_read, NULL, NULL,
                                         NULL, &error) == GB_RESULT_INVALID_ARGUMENT
                     ? GB_RESULT_OK : GB_RESULT_BAD_STATE,
                  "overlapping device mapping must fail");

    CHECK_RESULT(gb_memory_unmap_io_device(&memory, device_index, &error),
                  "unmap I/O device");
    CHECK(test_read(&memory, 0xFF40u) != 0xA5u,
          "unmapped I/O address must fall back to memory-owned register storage");
}

static void test_cpu_integration(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_CPU cpu;
    GB_Error error;
    GB_CPU_BUS bus;
    uint8_t value = 0;
    uint32_t cycles = 0;

    init_memory_with_interrupts(&memory, &interrupts, GB_MEMORY_MODE_CGB);

    /* Execute entirely from WRAM so this stage does not implement cartridges. */
    const uint8_t program[] = {
        0x3Eu, 0x42u,       /* LD A,$42 */
        0xEAu, 0x80u, 0xFFu, /* LD ($FF80),A */
        0x76u              /* HALT */
    };
    for (size_t i = 0; i < sizeof(program); ++i) {
        gb_memory_write8(&memory, (uint16_t)(0xC000u + i), program[i], &error);
    }

    bus = gb_memory_cpu_bus(&memory);
    CHECK_RESULT(gb_cpu_init(&cpu, &bus, &error), "CPU init with memory bus");
    CHECK_RESULT(gb_cpu_reset(&cpu, GB_CPU_STARTUP_COLD, &error), "CPU cold reset");
    cpu.r.pc = 0xC000u;
    cpu.r.sp = 0xFFFEu;

    CHECK_RESULT(gb_cpu_step(&cpu, &cycles, &error), "execute LD A");
    CHECK(cpu.r.a == 0x42u && cycles == 8u, "LD A immediate must execute through memory bus");
    CHECK_RESULT(gb_cpu_step(&cpu, &cycles, &error), "execute LD (a16),A");
    CHECK(cycles == 16u, "LD (a16),A timing through memory bus");
    CHECK_RESULT(gb_memory_read8(&memory, 0xFF80u, &value, &error), "read written HRAM");
    CHECK(value == 0x42u, "CPU memory write must reach HRAM");
    CHECK_RESULT(gb_cpu_step(&cpu, &cycles, &error), "execute HALT");
    CHECK(gb_cpu_is_halted(&cpu), "HALT must enter halted state");
}

int main(void)
{
    test_address_classification();
    test_dmg_memory();
    test_cgb_banks();
    test_interrupt_bus();
    test_io_device_mapping();
    test_cpu_integration();

    if (failures != 0) {
        fprintf(stderr, "%d memory/bus test(s) failed.\n", failures);
        return 1;
    }

    puts("All memory/bus tests passed.");
    return 0;
}
