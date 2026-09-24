#include "gb_timer.h"
#include "gb_interrupt.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", (message)); \
            ++failures; \
        } \
    } while (0)

#define CHECK_RESULT(result, label) \
    do { \
        GB_Result _result = (result); \
        if (_result != GB_RESULT_OK) { \
            fprintf(stderr, "FAIL: %s (code=%d)\n", \
                    (label), (int)_result); \
            ++failures; \
        } \
    } while (0)

static void init_timer(GB_Memory *memory, GB_Interrupt *interrupts,
                        GB_Timer *timer, GB_Error *error)
{
    CHECK_RESULT(gb_memory_init(memory, GB_MEMORY_MODE_DMG, error),
                 "memory init");
    CHECK_RESULT(gb_interrupt_init(interrupts, error), "interrupt init");
    CHECK_RESULT(gb_interrupt_connect_memory(interrupts, memory, error),
                 "interrupt connect");
    CHECK_RESULT(gb_timer_init(timer, memory, error), "timer init");
}

static void test_registers(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Timer timer;
    GB_Error error;
    uint8_t value = 0u;

    init_timer(&memory, &interrupts, &timer, &error);

    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_DIV, &value, &error),
                 "read DIV");
    CHECK(value == 0u, "DIV resets to zero");

    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TMA, 0x73u, &error),
                 "write TMA");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TIMA, 0x19u, &error),
                 "write TIMA");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TAC, 0x05u, &error),
                 "write TAC");

    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TMA, &value, &error),
                 "read TMA");
    CHECK(value == 0x73u, "TMA readback");

    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                 "read TIMA");
    CHECK(value == 0x19u, "TIMA readback");

    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TAC, &value, &error),
                 "read TAC");
    CHECK(value == 0xFDu, "TAC upper bits read as one");

    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_DIV, 0xFFu, &error),
                 "write DIV");
    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_DIV, &value, &error),
                 "read reset DIV");
    CHECK(value == 0u, "writing DIV resets internal divider");

    CHECK_RESULT(gb_timer_destroy(&timer, &error), "timer destroy");
}

static void test_divider_frequency(void)
{
    static const struct {
        uint8_t tac;
        uint32_t clocks;
    } cases[] = {
        { 0x04u, 1024u },
        { 0x05u, 16u },
        { 0x06u, 64u },
        { 0x07u, 256u }
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        GB_Memory memory;
        GB_Interrupt interrupts;
        GB_Timer timer;
        GB_Error error;
        uint8_t value = 0u;

        init_timer(&memory, &interrupts, &timer, &error);
        CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TIMA, 0u, &error),
                     "clear TIMA");
        CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TAC, cases[i].tac,
                                      &error), "enable timer");
        CHECK_RESULT(gb_memory_tick(&memory, cases[i].clocks - 1u, &error),
                     "advance before timer edge");
        CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                     "read TIMA before edge");
        CHECK(value == 0u, "TIMA must not increment before selected falling edge");

        CHECK_RESULT(gb_memory_tick(&memory, 1u, &error),
                     "advance through timer edge");
        CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                     "read TIMA after edge");
        CHECK(value == 1u, "TIMA increments on selected divider falling edge");

        CHECK_RESULT(gb_timer_destroy(&timer, &error), "timer destroy");
    }
}

static void test_div_write_trigger(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Timer timer;
    GB_Error error;
    uint8_t value = 0u;

    init_timer(&memory, &interrupts, &timer, &error);
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TIMA, 0u, &error),
                 "clear TIMA");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TAC, 0x05u, &error),
                 "enable 262144 Hz timer");
    CHECK_RESULT(gb_memory_tick(&memory, 8u, &error),
                 "reach selected divider bit high");

    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_DIV, 0xABu, &error),
                 "write DIV while timer signal high");
    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                 "read TIMA after DIV reset");
    CHECK(value == 1u, "DIV reset causes a falling-edge TIMA increment");

    CHECK_RESULT(gb_timer_destroy(&timer, &error), "timer destroy");
}

static void test_tac_toggle_trigger(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Timer timer;
    GB_Error error;
    uint8_t value = 0u;

    init_timer(&memory, &interrupts, &timer, &error);
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TIMA, 0u, &error),
                 "clear TIMA");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TAC, 0x05u, &error),
                 "enable timer");
    CHECK_RESULT(gb_memory_tick(&memory, 8u, &error),
                 "set selected bit high");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TAC, 0x04u, &error),
                 "change TAC selection");
    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                 "read TIMA after TAC change");
    CHECK(value == 1u, "TAC change from high signal to low signal increments TIMA");

    CHECK_RESULT(gb_timer_destroy(&timer, &error), "timer destroy");
}

static void test_overflow_reload_and_interrupt(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Timer timer;
    GB_Error error;
    uint8_t value = 0u;
    uint8_t pending = 0u;

    init_timer(&memory, &interrupts, &timer, &error);
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TMA, 0x42u, &error),
                 "set TMA");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TIMA, 0xFFu, &error),
                 "set TIMA to overflow");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TAC, 0x05u, &error),
                 "enable timer");

    CHECK_RESULT(gb_memory_tick(&memory, 8u, &error),
                 "set selected divider bit high");
    CHECK_RESULT(gb_memory_tick(&memory, 8u, &error),
                 "force timer overflow edge");

    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                 "read TIMA during reload delay");
    CHECK(value == 0u, "TIMA is zero during delayed reload");
    CHECK(gb_timer_get_reload_delay(&timer) == GB_TIMER_RELOAD_DELAY,
          "reload delay begins at four timer clocks");

    CHECK_RESULT(gb_memory_tick(&memory, 3u, &error),
                 "advance three reload clocks");
    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                 "read TIMA before reload");
    CHECK(value == 0u, "TIMA remains zero until reload");

    CHECK_RESULT(gb_memory_tick(&memory, 1u, &error),
                 "advance reload clock");
    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                 "read TIMA after reload");
    CHECK(value == 0x42u, "TIMA reloads from TMA after four timer clocks");

    CHECK_RESULT(gb_memory_get_pending_interrupts(&memory, &pending, &error),
                 "read pending interrupts");
    CHECK((pending & GB_INTERRUPT_TIMER) == 0u,
          "IF is independent of IE and pending query needs IE enabled");

    CHECK_RESULT(gb_memory_read8(&memory, GB_ADDR_IF, &value, &error),
                 "read IF");
    CHECK((value & GB_INTERRUPT_TIMER) != 0u,
          "timer overflow requests the timer interrupt in IF");

    CHECK_RESULT(gb_timer_destroy(&timer, &error), "timer destroy");
}

static void test_tima_write_cancels_reload(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Timer timer;
    GB_Error error;
    uint8_t value = 0u;

    init_timer(&memory, &interrupts, &timer, &error);
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TMA, 0xAAu, &error),
                 "set TMA");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TIMA, 0xFFu, &error),
                 "set TIMA");
    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TAC, 0x05u, &error),
                 "enable timer");
    CHECK_RESULT(gb_memory_tick(&memory, 16u, &error),
                 "overflow timer");
    CHECK(gb_timer_get_reload_delay(&timer) != 0u,
          "overflow must create reload window");

    CHECK_RESULT(gb_memory_write8(&memory, GB_TIMER_ADDR_TIMA, 0x77u, &error),
                 "write TIMA during reload window");
    CHECK_RESULT(gb_memory_tick(&memory, 4u, &error),
                 "advance past canceled reload");
    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TIMA, &value, &error),
                 "read TIMA after canceled reload");
    CHECK(value == 0x77u, "TIMA write cancels pending reload");

    CHECK_RESULT(gb_timer_destroy(&timer, &error), "timer destroy");
}

static void test_double_speed(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Timer timer;
    GB_Error error;
    uint8_t value = 0u;

    init_timer(&memory, &interrupts, &timer, &error);
    CHECK_RESULT(gb_timer_set_double_speed(&timer, true, &error),
                 "enable timer double speed");
    CHECK_RESULT(gb_memory_tick(&memory, 512u, &error),
                 "advance 512 base T-cycles at double speed");
    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_DIV, &value, &error),
                 "read DIV at double speed");
    CHECK(value == 4u, "DIV advances twice as fast in CGB double speed");
    CHECK(gb_timer_is_double_speed(&timer), "double-speed state is reported");

    CHECK_RESULT(gb_timer_destroy(&timer, &error), "timer destroy");
}

static void test_cpu_bus_integration(void)
{
    GB_Memory memory;
    GB_Interrupt interrupts;
    GB_Timer timer;
    GB_CPU cpu;
    GB_CPU_BUS bus;
    GB_Error error;
    uint32_t cycles = 0u;
    uint8_t value = 0u;

    init_timer(&memory, &interrupts, &timer, &error);
    const uint8_t program[] = {
        0x3Eu, 0x04u,             /* LD A,$04 */
        0xE0u, 0x07u,             /* LDH ($07),A -> TAC */
        0x76u                      /* HALT */
    };
    for (size_t i = 0u; i < sizeof(program); ++i) {
        CHECK_RESULT(gb_memory_write8(&memory, (uint16_t)(0xC000u + i),
                                      program[i], &error),
                     "write CPU timer test program");
    }

    bus = gb_memory_cpu_bus(&memory);
    CHECK_RESULT(gb_cpu_init(&cpu, &bus, &error), "CPU init");
    CHECK_RESULT(gb_cpu_reset(&cpu, GB_CPU_STARTUP_COLD, &error), "CPU reset");
    cpu.r.pc = 0xC000u;
    cpu.r.sp = 0xFFFEu;

    CHECK_RESULT(gb_cpu_step(&cpu, &cycles, &error), "execute LD A");
    CHECK_RESULT(gb_cpu_step(&cpu, &cycles, &error), "execute TAC write");
    CHECK_RESULT(gb_cpu_step(&cpu, &cycles, &error), "execute HALT");
    CHECK(gb_timer_get_tac(&timer) == 0x04u,
          "timer TAC is driven by CPU memory-mapped writes");

    CHECK_RESULT(gb_memory_read8(&memory, GB_TIMER_ADDR_TAC, &value, &error),
                 "read TAC through memory bus");
    CHECK(value == 0xFCu, "CPU-visible TAC readback uses timer hardware bits");

    CHECK_RESULT(gb_timer_destroy(&timer, &error), "timer destroy");
}

int main(void)
{
    test_registers();
    test_divider_frequency();
    test_div_write_trigger();
    test_tac_toggle_trigger();
    test_overflow_reload_and_interrupt();
    test_tima_write_cancels_reload();
    test_double_speed();
    test_cpu_bus_integration();

    if (failures != 0) {
        fprintf(stderr, "%d timer test(s) failed.\n", failures);
        return 1;
    }

    puts("All timer tests passed.");
    return 0;
}
