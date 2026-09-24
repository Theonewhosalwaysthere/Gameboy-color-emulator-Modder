#include "gb_timer.h"

#include <stdio.h>
#include <string.h>

#define GB_TIMER_TAC_ENABLE 0x04u
#define GB_TIMER_TAC_MASK   0x07u

/* The selected divider bits are the falling-edge detector inputs for the
 * four TAC frequency settings. */
static const uint16_t timer_trigger_masks[4] = {
    0x0200u, /* 4096 Hz    */
    0x0008u, /* 262144 Hz  */
    0x0020u, /* 65536 Hz   */
    0x0080u  /* 16384 Hz   */
};

static void timer_error(GB_Error *error, GB_Result code,
                        uint16_t address, const char *message)
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

static GB_Result require_timer(const GB_Timer *timer, GB_Error *error)
{
    if (timer == NULL) {
        timer_error(error, GB_RESULT_NULL_ARGUMENT, 0u,
                    "Timer pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (!timer->initialized) {
        timer_error(error, GB_RESULT_BAD_STATE, 0u,
                    "Timer has not been initialized");
        return GB_RESULT_BAD_STATE;
    }

    return GB_RESULT_OK;
}

static bool timer_apu_signal(uint16_t divider, bool double_speed)
{
    uint16_t mask = double_speed ? 0x0020u : 0x0010u;
    return (divider & mask) != 0u;
}

static GB_Result timer_clock_apu(GB_Timer *timer, GB_Error *error)
{
    if (timer->apu_clock_callback == NULL) return GB_RESULT_OK;
    return timer->apu_clock_callback(timer->apu_clock_user, error);
}

static bool timer_signal(uint16_t divider, uint8_t tac)
{
    if ((tac & GB_TIMER_TAC_ENABLE) == 0u) {
        return false;
    }

    return (divider & timer_trigger_masks[tac & 0x03u]) != 0u;
}

static GB_Result timer_request_interrupt(GB_Timer *timer, GB_Error *error)
{
    if (timer->memory == NULL) {
        timer_error(error, GB_RESULT_BAD_STATE, GB_TIMER_ADDR_TIMA,
                    "Timer has no memory/interrupt target attached");
        return GB_RESULT_BAD_STATE;
    }

    GB_Error memory_error;
    gb_error_clear(&memory_error);
    GB_Result result = gb_memory_request_interrupt(timer->memory,
                                                    GB_INTERRUPT_TIMER,
                                                    &memory_error);
    if (result != GB_RESULT_OK) {
        if (error != NULL) {
            *error = memory_error;
            if (error->message[0] == '\0') {
                (void)snprintf(error->message, sizeof(error->message),
                               "Failed to request the timer interrupt");
            }
            error->code = GB_RESULT_INTERRUPT_ERROR;
            error->pc = GB_TIMER_ADDR_TIMA;
        }
        return GB_RESULT_INTERRUPT_ERROR;
    }

    return GB_RESULT_OK;
}

static GB_Result timer_increment_tima(GB_Timer *timer, GB_Error *error)
{
    if (timer->tima != 0xFFu) {
        timer->tima = (uint8_t)(timer->tima + 1u);
        return GB_RESULT_OK;
    }

    /* Hardware exposes TIMA as zero during the delayed reload window. */
    timer->tima = 0x00u;
    timer->reload_delay = GB_TIMER_RELOAD_DELAY;
    timer->reload_tma = timer->tma;
    (void)error;
    return GB_RESULT_OK;
}

static GB_Result timer_advance_one_clock(GB_Timer *timer, GB_Error *error)
{
    /* The reload event is resolved before the next divider transition. This
     * gives the hardware-visible four-clock delay and preserves the priority
     * of the reload over a simultaneous timer increment. */
    if (timer->reload_delay != 0u) {
        timer->reload_delay = (uint8_t)(timer->reload_delay - 1u);
        if (timer->reload_delay == 0u) {
            timer->tima = timer->reload_tma;
            return timer_request_interrupt(timer, error);
        }
    }

    uint16_t old_divider = timer->divider;
    uint16_t new_divider = (uint16_t)(old_divider + 1u);

    if (timer_signal(old_divider, timer->tac) &&
        !timer_signal(new_divider, timer->tac)) {
        GB_Result result = timer_increment_tima(timer, error);
        if (result != GB_RESULT_OK) {
            return result;
        }
    }

    bool old_apu = timer_apu_signal(old_divider, timer->double_speed);
    bool new_apu = timer_apu_signal(new_divider, timer->double_speed);
    timer->divider = new_divider;

    if (old_apu && !new_apu) {
        GB_Result apu_result = timer_clock_apu(timer, error);
        if (apu_result != GB_RESULT_OK) return apu_result;
    }
    return GB_RESULT_OK;
}

static GB_Result timer_set_divider(GB_Timer *timer, uint16_t value,
                                   GB_Error *error)
{
    bool old_signal = timer_signal(timer->divider, timer->tac);
    bool new_signal = timer_signal(value, timer->tac);

    if (old_signal && !new_signal) {
        GB_Result result = timer_increment_tima(timer, error);
        if (result != GB_RESULT_OK) {
            return result;
        }
    }

    bool old_apu = timer_apu_signal(timer->divider, timer->double_speed);
    bool new_apu = timer_apu_signal(value, timer->double_speed);
    timer->divider = value;
    if (old_apu && !new_apu) {
        GB_Result apu_result = timer_clock_apu(timer, error);
        if (apu_result != GB_RESULT_OK) return apu_result;
    }
    return GB_RESULT_OK;
}

static GB_Result timer_set_tac(GB_Timer *timer, uint8_t value,
                               GB_Error *error)
{
    value &= GB_TIMER_TAC_MASK;

    bool old_signal = timer_signal(timer->divider, timer->tac);
    bool new_signal = timer_signal(timer->divider, value);

    if (old_signal && !new_signal) {
        GB_Result result = timer_increment_tima(timer, error);
        if (result != GB_RESULT_OK) {
            return result;
        }
    }

    timer->tac = value;
    return GB_RESULT_OK;
}

GB_Result gb_timer_reset(GB_Timer *timer, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_timer(timer, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    timer->divider = 0u;
    timer->tima = 0u;
    timer->tma = 0u;
    timer->tac = 0u;
    timer->reload_delay = 0u;
    timer->reload_tma = 0u;
    timer->double_speed = false;
    return GB_RESULT_OK;
}

GB_Result gb_timer_init(GB_Timer *timer, GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);

    if (timer == NULL || memory == NULL) {
        timer_error(error, GB_RESULT_NULL_ARGUMENT, 0u,
                    "Timer and memory pointers are required");
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(timer, 0, sizeof(*timer));
    timer->memory = memory;
    timer->initialized = true;

    GB_Result result = gb_timer_reset(timer, error);
    if (result != GB_RESULT_OK) {
        timer->initialized = false;
        timer->memory = NULL;
        return result;
    }

    result = gb_memory_map_io_device(memory,
                                     GB_TIMER_ADDR_DIV,
                                     GB_TIMER_ADDR_TAC,
                                     timer,
                                     gb_timer_read8,
                                     gb_timer_write8,
                                     gb_timer_device_tick,
                                     &timer->io_device_index,
                                     error);
    if (result != GB_RESULT_OK) {
        timer->initialized = false;
        timer->memory = NULL;
        return result;
    }

    timer->mapped = true;
    return GB_RESULT_OK;
}

GB_Result gb_timer_destroy(GB_Timer *timer, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_timer(timer, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (timer->mapped) {
        if (timer->memory == NULL) {
            timer_error(error, GB_RESULT_BAD_STATE, 0u,
                        "Timer mapping exists without a memory object");
            return GB_RESULT_BAD_STATE;
        }

        result = gb_memory_unmap_io_device(timer->memory,
                                           timer->io_device_index,
                                           error);
        if (result != GB_RESULT_OK) {
            return result;
        }
    }

    memset(timer, 0, sizeof(*timer));
    return GB_RESULT_OK;
}

GB_Result gb_timer_tick(GB_Timer *timer, uint32_t t_cycles, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_timer(timer, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (t_cycles == 0u) {
        return GB_RESULT_OK;
    }

    uint64_t clocks = (uint64_t)t_cycles;
    if (timer->double_speed) {
        clocks *= 2u;
    }

    for (uint64_t i = 0u; i < clocks; ++i) {
        result = timer_advance_one_clock(timer, error);
        if (result != GB_RESULT_OK) {
            return result;
        }
    }

    return GB_RESULT_OK;
}

GB_Result gb_timer_set_double_speed(GB_Timer *timer, bool enabled,
                                    GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_timer(timer, error);
    if (result != GB_RESULT_OK) return result;
    if (timer->double_speed == enabled) return GB_RESULT_OK;

    bool old_apu = timer_apu_signal(timer->divider, timer->double_speed);
    bool new_apu = timer_apu_signal(timer->divider, enabled);
    timer->double_speed = enabled;
    if (old_apu && !new_apu) {
        result = timer_clock_apu(timer, error);
        if (result != GB_RESULT_OK) return result;
    }
    return GB_RESULT_OK;
}

GB_Result gb_timer_attach_apu_callback(GB_Timer *timer, void *user,
                                       GB_TimerApuClockCallback callback,
                                       GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_timer(timer, error);
    if (result != GB_RESULT_OK) return result;
    if (callback == NULL) {
        timer->apu_clock_callback = NULL;
        timer->apu_clock_user = NULL;
        return GB_RESULT_OK;
    }
    if (user == NULL) {
        timer_error(error, GB_RESULT_NULL_ARGUMENT, GB_TIMER_ADDR_DIV,
                    "APU callback user pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    timer->apu_clock_callback = callback;
    timer->apu_clock_user = user;
    return GB_RESULT_OK;
}

bool gb_timer_is_double_speed(const GB_Timer *timer)
{
    return timer != NULL && timer->initialized && timer->double_speed;
}

uint16_t gb_timer_get_divider(const GB_Timer *timer)
{
    return (timer != NULL && timer->initialized) ? timer->divider : 0u;
}

uint8_t gb_timer_get_tima(const GB_Timer *timer)
{
    return (timer != NULL && timer->initialized) ? timer->tima : 0u;
}

uint8_t gb_timer_get_tma(const GB_Timer *timer)
{
    return (timer != NULL && timer->initialized) ? timer->tma : 0u;
}

uint8_t gb_timer_get_tac(const GB_Timer *timer)
{
    return (timer != NULL && timer->initialized) ? timer->tac : 0u;
}

uint8_t gb_timer_get_reload_delay(const GB_Timer *timer)
{
    return (timer != NULL && timer->initialized) ? timer->reload_delay : 0u;
}

GB_Result gb_timer_read8(void *user, uint16_t address,
                         uint8_t *value, GB_Error *error)
{
    gb_error_clear(error);

    if (user == NULL || value == NULL) {
        timer_error(error, GB_RESULT_NULL_ARGUMENT, address,
                    "Timer read requires timer and output pointers");
        return GB_RESULT_NULL_ARGUMENT;
    }

    GB_Timer *timer = (GB_Timer *)user;
    GB_Result result = require_timer(timer, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    switch (address) {
    case GB_TIMER_ADDR_DIV:
        *value = (uint8_t)(timer->divider >> 8);
        return GB_RESULT_OK;

    case GB_TIMER_ADDR_TIMA:
        *value = timer->tima;
        return GB_RESULT_OK;

    case GB_TIMER_ADDR_TMA:
        *value = timer->tma;
        return GB_RESULT_OK;

    case GB_TIMER_ADDR_TAC:
        *value = (uint8_t)(0xF8u | timer->tac);
        return GB_RESULT_OK;

    default:
        timer_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                    "Address is outside the timer register range");
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_Result gb_timer_write8(void *user, uint16_t address,
                          uint8_t value, GB_Error *error)
{
    gb_error_clear(error);

    if (user == NULL) {
        timer_error(error, GB_RESULT_NULL_ARGUMENT, address,
                    "Timer write requires a timer pointer");
        return GB_RESULT_NULL_ARGUMENT;
    }

    GB_Timer *timer = (GB_Timer *)user;
    GB_Result result = require_timer(timer, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    switch (address) {
    case GB_TIMER_ADDR_DIV:
        return timer_set_divider(timer, 0u, error);

    case GB_TIMER_ADDR_TIMA:
        /* Writing TIMA during the delayed reload window cancels the pending
         * reload and keeps the explicitly written value. */
        timer->tima = value;
        timer->reload_delay = 0u;
        timer->reload_tma = timer->tma;
        return GB_RESULT_OK;

    case GB_TIMER_ADDR_TMA:
        timer->tma = value;
        return GB_RESULT_OK;

    case GB_TIMER_ADDR_TAC:
        return timer_set_tac(timer, value, error);

    default:
        timer_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                    "Address is outside the timer register range");
        return GB_RESULT_INVALID_ARGUMENT;
    }
}

GB_Result gb_timer_device_tick(void *user, uint32_t t_cycles,
                               GB_Error *error)
{
    return gb_timer_tick((GB_Timer *)user, t_cycles, error);
}
