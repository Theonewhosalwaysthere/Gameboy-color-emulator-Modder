#include "../src/interrupt/gb_interrupt.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", (message)); \
            ++failures; \
        } \
    } while (0)

#define CHECK_RESULT(result, message) CHECK((result) == GB_RESULT_OK, message)

static void init_interrupts(GB_Memory *memory, GB_Interrupt *interrupts,
                            GB_Error *error)
{
    CHECK_RESULT(gb_memory_init(memory, GB_MEMORY_MODE_CGB, error),
                 "memory init");
    CHECK_RESULT(gb_interrupt_init(interrupts, error),
                 "interrupt init");
    CHECK_RESULT(gb_interrupt_connect_memory(interrupts, memory, error),
                 "connect interrupt controller to memory");
}

static uint8_t read_memory(GB_Memory *memory, uint16_t address)
{
    GB_Error error;
    uint8_t value = 0u;
    GB_Result result = gb_memory_read8(memory, address, &value, &error);
    if (result != GB_RESULT_OK) {
        fprintf(stderr, "read $%04X failed: %s\n", address, error.message);
        ++failures;
        return 0u;
    }
    return value;
}

static void write_memory(GB_Memory *memory, uint16_t address, uint8_t value)
{
    GB_Error error;
    GB_Result result = gb_memory_write8(memory, address, value, &error);
    if (result != GB_RESULT_OK) {
        fprintf(stderr, "write $%04X failed: %s\n", address, error.message);
        ++failures;
    }
}

static void test_registers_and_masks(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Error error;
    init_interrupts(&memory, &interrupts, &error);

    CHECK(read_memory(&memory, GB_ADDR_IF) == 0xE0u,
          "IF resets with unused upper bits reading high");
    CHECK(read_memory(&memory, GB_ADDR_IE) == 0xE0u,
          "IE resets with unused upper bits reading high");

    write_memory(&memory, GB_ADDR_IF, 0xFFu);
    CHECK(read_memory(&memory, GB_ADDR_IF) == 0xFFu,
          "IF writes retain only the five interrupt request bits plus read-high bits");

    write_memory(&memory, GB_ADDR_IE, 0xFFu);
    CHECK(read_memory(&memory, GB_ADDR_IE) == 0xFFu,
          "IE writes retain only the five interrupt enable bits plus read-high bits");

    CHECK(gb_interrupt_requested_mask(&interrupts) == GB_INTERRUPT_VALID_MASK,
          "requested mask must expose only the five valid IF bits");
    CHECK(gb_interrupt_enabled_mask(&interrupts) == GB_INTERRUPT_VALID_MASK,
          "enabled mask must expose only the five valid IE bits");
}

static void test_request_enable_pending_and_acknowledge(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Error error;
    uint8_t pending = 0u;
    init_interrupts(&memory, &interrupts, &error);

    write_memory(&memory, GB_ADDR_IE, GB_INTERRUPT_TIMER | GB_INTERRUPT_SERIAL);

    CHECK_RESULT(gb_interrupt_request_source(&interrupts,
                                             GB_INTERRUPT_SOURCE_VBLANK, &error),
                 "request VBlank");
    CHECK_RESULT(gb_interrupt_request_source(&interrupts,
                                             GB_INTERRUPT_SOURCE_TIMER, &error),
                 "request Timer");
    CHECK_RESULT(gb_interrupt_request_source(&interrupts,
                                             GB_INTERRUPT_SOURCE_SERIAL, &error),
                 "request Serial");

    CHECK(gb_interrupt_requested_mask(&interrupts) ==
              (GB_INTERRUPT_VBLANK | GB_INTERRUPT_TIMER | GB_INTERRUPT_SERIAL),
          "requests must latch independently of IE");

    CHECK_RESULT(gb_interrupt_get_pending(&interrupts, &pending, &error),
                 "read pending through controller callback");
    CHECK(pending == (GB_INTERRUPT_TIMER | GB_INTERRUPT_SERIAL),
          "pending must be IF & IE, excluding disabled VBlank");

    CHECK(gb_interrupt_highest_priority_source(pending) ==
              GB_INTERRUPT_SOURCE_TIMER,
          "Timer must win priority over Serial");

    CHECK_RESULT(gb_interrupt_acknowledge(&interrupts, GB_INTERRUPT_TIMER, &error),
                 "acknowledge Timer");
    CHECK((gb_interrupt_requested_mask(&interrupts) & GB_INTERRUPT_TIMER) == 0u,
          "acknowledged source must clear IF");

    CHECK_RESULT(gb_interrupt_clear_source(&interrupts,
                                           GB_INTERRUPT_SOURCE_SERIAL, &error),
                 "clear Serial source");
    CHECK(gb_interrupt_pending_mask(&interrupts) == 0u,
          "clearing all enabled requests removes pending interrupts");
}

static void test_priority_and_vectors(void)
{
    static const GB_InterruptSource sources[] = {
        GB_INTERRUPT_SOURCE_VBLANK,
        GB_INTERRUPT_SOURCE_STAT,
        GB_INTERRUPT_SOURCE_TIMER,
        GB_INTERRUPT_SOURCE_SERIAL,
        GB_INTERRUPT_SOURCE_JOYPAD
    };
    static const uint8_t masks[] = {
        GB_INTERRUPT_VBLANK,
        GB_INTERRUPT_STAT,
        GB_INTERRUPT_TIMER,
        GB_INTERRUPT_SERIAL,
        GB_INTERRUPT_JOYPAD
    };
    static const uint16_t vectors[] = {
        0x0040u,
        0x0048u,
        0x0050u,
        0x0058u,
        0x0060u
    };

    CHECK(gb_interrupt_highest_priority_source(0u) == -1,
          "no pending interrupts must produce no source");

    for (size_t i = 0u; i < 5u; ++i) {
        CHECK(gb_interrupt_source_mask(sources[i]) == masks[i],
              "source mask must match hardware IF bit");
        CHECK(gb_interrupt_source_vector(sources[i]) == vectors[i],
              "source vector must match hardware interrupt vector");
        CHECK(gb_interrupt_source_name(sources[i])[0] != '\0',
              "valid interrupt source must have a diagnostic name");
    }

    CHECK(gb_interrupt_highest_priority_source(
              GB_INTERRUPT_JOYPAD | GB_INTERRUPT_TIMER | GB_INTERRUPT_VBLANK) ==
              GB_INTERRUPT_SOURCE_VBLANK,
          "VBlank must have highest priority");
    CHECK(gb_interrupt_highest_priority_source(
              GB_INTERRUPT_JOYPAD | GB_INTERRUPT_SERIAL) ==
              GB_INTERRUPT_SOURCE_SERIAL,
          "Serial must have priority over Joypad");
    CHECK(gb_interrupt_source_mask((GB_InterruptSource)99) == 0u,
          "invalid source must have no mask");
    CHECK(gb_interrupt_source_vector((GB_InterruptSource)99) == 0u,
          "invalid source must have no vector");
}

static void test_invalid_operations(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Error error;
    init_interrupts(&memory, &interrupts, &error);

    CHECK(gb_interrupt_request(&interrupts, 0x20u, &error) ==
              GB_RESULT_INVALID_ARGUMENT,
          "requesting a nonexistent interrupt bit must fail");
    CHECK(gb_interrupt_acknowledge(&interrupts, 0x03u, &error) ==
              GB_RESULT_INVALID_ARGUMENT,
          "acknowledging multiple interrupt bits at once must fail");
    CHECK(gb_interrupt_request_source(&interrupts,
                                      (GB_InterruptSource)99, &error) ==
              GB_RESULT_INVALID_ARGUMENT,
          "invalid interrupt source request must fail");

    CHECK_RESULT(gb_interrupt_disconnect_memory(&interrupts, &error),
                 "disconnect interrupt controller");
    CHECK(gb_memory_read8(&memory, GB_ADDR_IF, &(uint8_t){0u}, &error) ==
              GB_RESULT_BAD_STATE,
          "memory must reject IF access after controller disconnect");
    CHECK(gb_memory_write8(&memory, GB_ADDR_IE, 0u, &error) ==
              GB_RESULT_BAD_STATE,
          "memory must reject IE access after controller disconnect");
}

static void test_cpu_bus_integration(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_CPU cpu;
    GB_CPU_BUS bus;
    GB_Error error;
    uint32_t cycles = 0u;
    uint8_t value = 0u;

    init_interrupts(&memory, &interrupts, &error);
    bus = gb_memory_cpu_bus(&memory);

    CHECK_RESULT(gb_cpu_init(&cpu, &bus, &error), "CPU init");
    CHECK_RESULT(gb_cpu_reset(&cpu, GB_CPU_STARTUP_COLD, &error), "CPU reset");

    cpu.r.pc = 0x1234u;
    cpu.r.sp = 0xFFFEu;
    cpu.ime = true;

    write_memory(&memory, GB_ADDR_IE, GB_INTERRUPT_TIMER);
    CHECK_RESULT(gb_interrupt_request_source(&interrupts,
                                             GB_INTERRUPT_SOURCE_TIMER, &error),
                 "request Timer for CPU integration");

    CHECK_RESULT(gb_cpu_step(&cpu, &cycles, &error),
                 "CPU services enabled timer interrupt");
    CHECK(cycles == 20u, "interrupt service must consume 20 T-cycles");
    CHECK(cpu.r.pc == 0x0050u, "Timer interrupt must vector to 0050h");
    CHECK(cpu.r.sp == 0xFFFCu, "interrupt entry must push the current PC");
    CHECK(!cpu.ime, "interrupt entry must clear IME");

    CHECK_RESULT(gb_memory_read8(&memory, 0xFFFCu, &value, &error),
                 "read pushed PC low byte");
    CHECK(value == 0x34u, "interrupt stack must contain PC low byte");
    CHECK_RESULT(gb_memory_read8(&memory, 0xFFFDu, &value, &error),
                 "read pushed PC high byte");
    CHECK(value == 0x12u, "interrupt stack must contain PC high byte");

    CHECK((gb_interrupt_requested_mask(&interrupts) & GB_INTERRUPT_TIMER) == 0u,
          "CPU interrupt acknowledgement must clear IF");
}

static void test_memory_direct_register_access(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Error error;
    uint8_t value = 0u;
    init_interrupts(&memory, &interrupts, &error);

    write_memory(&memory, GB_ADDR_IE, GB_INTERRUPT_VBLANK | GB_INTERRUPT_JOYPAD);
    write_memory(&memory, GB_ADDR_IF, GB_INTERRUPT_VBLANK | GB_INTERRUPT_TIMER);

    CHECK_RESULT(gb_memory_get_pending_interrupts(&memory, &value, &error),
                 "memory pending wrapper");
    CHECK(value == GB_INTERRUPT_VBLANK,
          "memory pending wrapper must delegate to interrupt controller");

    CHECK_RESULT(gb_memory_request_interrupt(&memory, GB_INTERRUPT_JOYPAD, &error),
                 "memory interrupt request wrapper");
    CHECK((gb_interrupt_requested_mask(&interrupts) & GB_INTERRUPT_JOYPAD) != 0u,
          "memory request wrapper must reach controller");

    CHECK_RESULT(gb_memory_acknowledge_interrupt(&memory, GB_INTERRUPT_VBLANK, &error),
                 "memory interrupt acknowledge wrapper");
    CHECK((gb_interrupt_requested_mask(&interrupts) & GB_INTERRUPT_VBLANK) == 0u,
          "memory acknowledge wrapper must clear controller IF");
}

int main(void)
{
    test_registers_and_masks();
    test_request_enable_pending_and_acknowledge();
    test_priority_and_vectors();
    test_invalid_operations();
    test_cpu_bus_integration();
    test_memory_direct_register_access();

    if (failures != 0) {
        fprintf(stderr, "%d interrupt test(s) failed.\n", failures);
        return 1;
    }

    puts("All interrupt tests passed.");
    return 0;
}
