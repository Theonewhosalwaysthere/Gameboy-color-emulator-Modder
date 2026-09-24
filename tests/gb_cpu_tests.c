#include "gb_cpu.h"
#include "gb_cpu_opcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

typedef struct TestBus {
    uint8_t mem[65536];
    uint8_t pending_interrupts;
    unsigned long ticks;
} TestBus;

static GB_Result test_read(void *user, uint16_t address, uint8_t *value, GB_Error *error)
{
    TestBus *bus = (TestBus *)user;
    if (bus == NULL || value == NULL) {
        gb_error_set(error, GB_RESULT_INVALID_ARGUMENT, "test read argument error");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    *value = bus->mem[address];
    return GB_RESULT_OK;
}

static GB_Result test_write(void *user, uint16_t address, uint8_t value, GB_Error *error)
{
    TestBus *bus = (TestBus *)user;
    if (bus == NULL) {
        gb_error_set(error, GB_RESULT_INVALID_ARGUMENT, "test write argument error");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    bus->mem[address] = value;
    return GB_RESULT_OK;
}

static GB_Result test_tick(void *user, uint32_t t_cycles, GB_Error *error)
{
    TestBus *bus = (TestBus *)user;
    if (bus == NULL) {
        gb_error_set(error, GB_RESULT_INVALID_ARGUMENT, "test tick argument error");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    bus->ticks += t_cycles;
    return GB_RESULT_OK;
}

static GB_Result test_pending(void *user, uint8_t *mask, GB_Error *error)
{
    TestBus *bus = (TestBus *)user;
    if (bus == NULL || mask == NULL) {
        gb_error_set(error, GB_RESULT_INVALID_ARGUMENT, "test pending argument error");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    *mask = bus->pending_interrupts;
    return GB_RESULT_OK;
}

static GB_Result test_ack(void *user, uint8_t mask, GB_Error *error)
{
    TestBus *bus = (TestBus *)user;
    if (bus == NULL) {
        gb_error_set(error, GB_RESULT_INVALID_ARGUMENT, "test ack argument error");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    bus->pending_interrupts &= (uint8_t)~mask;
    return GB_RESULT_OK;
}

static int init_cpu(GB_CPU *cpu, TestBus *bus)
{
    GB_CPU_BUS cpu_bus;
    memset(&cpu_bus, 0, sizeof(cpu_bus));
    cpu_bus.user = bus;
    cpu_bus.read8 = test_read;
    cpu_bus.write8 = test_write;
    cpu_bus.tick = test_tick;
    cpu_bus.get_pending_interrupts = test_pending;
    cpu_bus.acknowledge_interrupt = test_ack;

    GB_Error error;
    if (gb_cpu_init(cpu, &cpu_bus, &error) != GB_RESULT_OK) {
        fprintf(stderr, "init failed: %s\n", error.message);
        return 1;
    }
    if (gb_cpu_reset(cpu, GB_CPU_STARTUP_COLD, &error) != GB_RESULT_OK) {
        fprintf(stderr, "reset failed: %s\n", error.message);
        return 1;
    }
    return 0;
}

static int step_expect(GB_CPU *cpu, uint32_t expected_cycles)
{
    uint32_t cycles = 0;
    GB_Error error;
    GB_Result result = gb_cpu_step(cpu, &cycles, &error);
    if (result != GB_RESULT_OK) {
        fprintf(stderr, "CPU step failed: code=%d msg=%s pc=%04X op=%02X\n",
                (int)result, error.message, error.pc, error.opcode);
        return 1;
    }
    if (cycles != expected_cycles) {
        fprintf(stderr, "Wrong cycles: got %u expected %u\n", cycles, expected_cycles);
        return 1;
    }
    return 0;
}

static int test_basic_load_and_alu(void)
{
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    GB_CPU cpu;
    TEST_ASSERT(init_cpu(&cpu, &bus) == 0);

    bus.mem[0x0000] = 0x3E; /* LD A,n */
    bus.mem[0x0001] = 0x09;
    bus.mem[0x0002] = 0x06; /* LD B,n */
    bus.mem[0x0003] = 0x01;
    bus.mem[0x0004] = 0x80; /* ADD A,B */
    bus.mem[0x0005] = 0x27; /* DAA */

    TEST_ASSERT(step_expect(&cpu, 8) == 0);
    TEST_ASSERT(cpu.r.a == 0x09 && cpu.r.pc == 0x0002);
    TEST_ASSERT(step_expect(&cpu, 8) == 0);
    TEST_ASSERT(cpu.r.b == 0x01);
    TEST_ASSERT(step_expect(&cpu, 4) == 0);
    TEST_ASSERT(cpu.r.a == 0x0A);
    TEST_ASSERT((cpu.r.f & 0x20u) == 0);
    TEST_ASSERT(step_expect(&cpu, 4) == 0);
    TEST_ASSERT(cpu.r.a == 0x10);
    TEST_ASSERT((cpu.r.f & 0x0F) == 0);
    return 0;
}

static int test_stack_call_ret(void)
{
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    GB_CPU cpu;
    TEST_ASSERT(init_cpu(&cpu, &bus) == 0);
    cpu.r.sp = 0xFFFE;

    bus.mem[0x0000] = 0xCD; /* CALL 0010 */
    bus.mem[0x0001] = 0x10;
    bus.mem[0x0002] = 0x00;
    bus.mem[0x0010] = 0xC9; /* RET */

    TEST_ASSERT(step_expect(&cpu, 24) == 0);
    TEST_ASSERT(cpu.r.pc == 0x0010);
    TEST_ASSERT(cpu.r.sp == 0xFFFC);
    TEST_ASSERT(bus.mem[0xFFFC] == 0x03 && bus.mem[0xFFFD] == 0x00);
    TEST_ASSERT(step_expect(&cpu, 16) == 0);
    TEST_ASSERT(cpu.r.pc == 0x0003);
    TEST_ASSERT(cpu.r.sp == 0xFFFE);
    return 0;
}

static int test_cb(void)
{
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    GB_CPU cpu;
    TEST_ASSERT(init_cpu(&cpu, &bus) == 0);

    cpu.r.b = 0x81;
    bus.mem[0] = 0xCB;
    bus.mem[1] = 0x00; /* RLC B */
    TEST_ASSERT(step_expect(&cpu, 8) == 0);
    TEST_ASSERT(cpu.r.b == 0x03);
    TEST_ASSERT((cpu.r.f & 0x10u) != 0);

    bus.mem[2] = 0xCB;
    bus.mem[3] = 0x78; /* BIT 7,B */
    TEST_ASSERT(step_expect(&cpu, 8) == 0);
    TEST_ASSERT((cpu.r.f & 0x80u) != 0); /* bit 7 is clear */
    TEST_ASSERT((cpu.r.f & 0x20u) != 0);
    return 0;
}

static int test_ei_delay(void)
{
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    GB_CPU cpu;
    TEST_ASSERT(init_cpu(&cpu, &bus) == 0);

    bus.mem[0] = 0xFB; /* EI */
    bus.mem[1] = 0x00; /* NOP */
    bus.mem[2] = 0x00; /* NOP */

    TEST_ASSERT(step_expect(&cpu, 4) == 0);
    TEST_ASSERT(!gb_cpu_interrupts_enabled(&cpu));
    TEST_ASSERT(step_expect(&cpu, 4) == 0);
    TEST_ASSERT(gb_cpu_interrupts_enabled(&cpu));
    TEST_ASSERT(step_expect(&cpu, 4) == 0);
    return 0;
}

static int test_halt_bug(void)
{
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    GB_CPU cpu;
    TEST_ASSERT(init_cpu(&cpu, &bus) == 0);

    cpu.r.a = 0;
    cpu.ime = false;
    bus.pending_interrupts = GB_INTERRUPT_VBLANK;
    bus.mem[0] = 0x76; /* HALT */
    bus.mem[1] = 0x3E; /* LD A,n; HALT bug means this opcode byte is fetched without advancing PC. */
    bus.mem[2] = 0x42;

    TEST_ASSERT(step_expect(&cpu, 4) == 0);
    TEST_ASSERT(!gb_cpu_is_halted(&cpu));
    TEST_ASSERT(cpu.halt_bug);

    bus.pending_interrupts = 0;
    TEST_ASSERT(step_expect(&cpu, 8) == 0);
    /* The repeated opcode byte is consumed again as LD A,d8's immediate operand. */
    TEST_ASSERT(cpu.r.a == 0x3E);
    TEST_ASSERT(cpu.r.pc == 0x0002);
    return 0;
}

static int test_invalid_opcode_locks_cpu(void)
{
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    GB_CPU cpu;
    TEST_ASSERT(init_cpu(&cpu, &bus) == 0);
    bus.mem[0] = 0xD3; /* Architecturally unused opcode. */

    uint32_t cycles = 0;
    GB_Error error;
    TEST_ASSERT(gb_cpu_step(&cpu, &cycles, &error) == GB_RESULT_INVALID_OPCODE);
    TEST_ASSERT(gb_cpu_is_faulted(&cpu));
    TEST_ASSERT(gb_cpu_step(&cpu, &cycles, &error) == GB_RESULT_BAD_STATE);
    TEST_ASSERT(gb_cpu_reset(&cpu, GB_CPU_STARTUP_COLD, &error) == GB_RESULT_OK);
    TEST_ASSERT(!gb_cpu_is_faulted(&cpu));
    return 0;
}

static int test_interrupt_entry(void)
{
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    GB_CPU cpu;
    TEST_ASSERT(init_cpu(&cpu, &bus) == 0);
    cpu.r.pc = 0x1234;
    cpu.r.sp = 0xFFFE;
    cpu.ime = true;
    bus.pending_interrupts = GB_INTERRUPT_TIMER | GB_INTERRUPT_VBLANK;

    TEST_ASSERT(step_expect(&cpu, 20) == 0);
    TEST_ASSERT(cpu.r.pc == 0x0040);
    TEST_ASSERT(cpu.r.sp == 0xFFFC);
    TEST_ASSERT(bus.mem[0xFFFC] == 0x34);
    TEST_ASSERT(bus.mem[0xFFFD] == 0x12);
    TEST_ASSERT((bus.pending_interrupts & GB_INTERRUPT_VBLANK) == 0);
    TEST_ASSERT((bus.pending_interrupts & GB_INTERRUPT_TIMER) != 0);
    TEST_ASSERT(!cpu.ime);
    return 0;
}

static int test_opcode_coverage(void)
{
    unsigned invalid = 0;
    for (unsigned op = 0; op < 256; ++op) {
        if (gb_cpu_opcode_is_invalid((uint8_t)op)) {
            ++invalid;
            continue;
        }

        TestBus bus;
        memset(&bus, 0, sizeof(bus));
        GB_CPU cpu;
        TEST_ASSERT(init_cpu(&cpu, &bus) == 0);
        bus.mem[0] = (uint8_t)op;
        bus.mem[1] = 0x00;
        bus.mem[2] = 0x00;
        cpu.r.sp = 0xFFFE;

        uint32_t cycles = 0;
        GB_Error error;
        GB_Result result = gb_cpu_step(&cpu, &cycles, &error);
        if (result != GB_RESULT_OK) {
            fprintf(stderr, "valid opcode $%02X was rejected: result=%d msg=%s pc=%04X\n",
                    op, (int)result, error.message, error.pc);
            ++invalid;
            continue;
        }
        TEST_ASSERT(cycles > 0 || cpu.stopped);
    }
    TEST_ASSERT(invalid == 11);

    for (unsigned op = 0; op < 256; ++op) {
        TestBus bus;
        memset(&bus, 0, sizeof(bus));
        GB_CPU cpu;
        TEST_ASSERT(init_cpu(&cpu, &bus) == 0);
        bus.mem[0] = 0xCB;
        bus.mem[1] = (uint8_t)op;

        uint32_t cycles = 0;
        GB_Error error;
        GB_Result result = gb_cpu_step(&cpu, &cycles, &error);
        if (result != GB_RESULT_OK) {
            fprintf(stderr, "CB opcode $%02X was rejected: result=%d msg=%s pc=%04X\n",
                    op, (int)result, error.message, error.pc);
            return 1;
        }
        TEST_ASSERT(cycles == 8 || cycles == 12 || cycles == 16);
    }
    return 0;
}

int main(void)
{
    if (test_basic_load_and_alu() != 0) return 1;
    if (test_stack_call_ret() != 0) return 1;
    if (test_cb() != 0) return 1;
    if (test_ei_delay() != 0) return 1;
    if (test_halt_bug() != 0) return 1;
    if (test_invalid_opcode_locks_cpu() != 0) return 1;
    if (test_interrupt_entry() != 0) return 1;
    if (test_opcode_coverage() != 0) return 1;
    printf("All CPU tests passed.\n");
    return 0;
}
