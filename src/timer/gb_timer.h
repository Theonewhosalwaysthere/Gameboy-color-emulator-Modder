#ifndef GB_TIMER_H
#define GB_TIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../memory/gb_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Game Boy timer register addresses. */
enum {
    GB_TIMER_ADDR_DIV  = 0xFF04u,
    GB_TIMER_ADDR_TIMA = 0xFF05u,
    GB_TIMER_ADDR_TMA  = 0xFF06u,
    GB_TIMER_ADDR_TAC  = 0xFF07u
};

/* TAC clock-select values. */
enum {
    GB_TIMER_TAC_FREQ_4096   = 0u,
    GB_TIMER_TAC_FREQ_262144 = 1u,
    GB_TIMER_TAC_FREQ_65536  = 2u,
    GB_TIMER_TAC_FREQ_16384  = 3u
};

/* A reload is delayed by four timer input clocks after TIMA overflows. */
#define GB_TIMER_RELOAD_DELAY 4u

typedef GB_Result (*GB_TimerApuClockCallback)(void *user, GB_Error *error);

typedef struct GB_Timer {
    bool initialized;
    bool mapped;

    /* Internal 16-bit divider. FF04 exposes bits 15..8. */
    uint16_t divider;

    uint8_t tima;
    uint8_t tma;
    uint8_t tac;

    /* 0 = no pending reload; 1..4 = timer input clocks until reload. */
    uint8_t reload_delay;

    /* Snapshot used by the pending reload event. */
    uint8_t reload_tma;

    /* Future CGB speed-switch hardware can toggle this. In double speed the
     * timer/divider input clock advances twice per normal CPU T-cycle. */
    bool double_speed;

    GB_TimerApuClockCallback apu_clock_callback;
    void *apu_clock_user;

    GB_Memory *memory;
    size_t io_device_index;
} GB_Timer;

GB_Result gb_timer_init(GB_Timer *timer, GB_Memory *memory, GB_Error *error);
GB_Result gb_timer_reset(GB_Timer *timer, GB_Error *error);
GB_Result gb_timer_destroy(GB_Timer *timer, GB_Error *error);

/* Advance timer/divider hardware by CPU time. t_cycles is expressed in the
 * same normal-speed T-cycle unit used by the existing CPU bus interface. */
GB_Result gb_timer_tick(GB_Timer *timer, uint32_t t_cycles, GB_Error *error);

/* Set the CGB timer/divider clock domain. Changing speed does not perform the
 * CGB speed-switch sequence itself; the future CGB hardware owns that event. */
GB_Result gb_timer_set_double_speed(GB_Timer *timer, bool enabled,
                                    GB_Error *error);

GB_Result gb_timer_attach_apu_callback(GB_Timer *timer, void *user,
                                       GB_TimerApuClockCallback callback,
                                       GB_Error *error);

bool gb_timer_is_double_speed(const GB_Timer *timer);

uint16_t gb_timer_get_divider(const GB_Timer *timer);
uint8_t gb_timer_get_tima(const GB_Timer *timer);
uint8_t gb_timer_get_tma(const GB_Timer *timer);
uint8_t gb_timer_get_tac(const GB_Timer *timer);
uint8_t gb_timer_get_reload_delay(const GB_Timer *timer);

/* Register callbacks used by the memory/device mapping layer. */
GB_Result gb_timer_read8(void *user, uint16_t address,
                         uint8_t *value, GB_Error *error);
GB_Result gb_timer_write8(void *user, uint16_t address,
                          uint8_t value, GB_Error *error);
GB_Result gb_timer_device_tick(void *user, uint32_t t_cycles,
                               GB_Error *error);

#ifdef __cplusplus
}
#endif

#endif /* GB_TIMER_H */
