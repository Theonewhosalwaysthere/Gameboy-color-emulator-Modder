#include <stdio.h>
#include <string.h>

#include "gb_audio.h"
#include "gb_interrupt.h"
#include "gb_timer.h"

#define TEST_CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); return 1; } \
} while (0)

static int init_memory(GB_Memory *memory, GB_MemoryMode mode, GB_Interrupt *interrupt,
                       GB_Error *error)
{
    memset(interrupt, 0, sizeof(*interrupt));
    if (gb_memory_init(memory, mode, error) != GB_RESULT_OK) return 0;
    if (gb_interrupt_init(interrupt, error) != GB_RESULT_OK) return 0;
    if (gb_interrupt_connect_memory(interrupt, memory, error) != GB_RESULT_OK) return 0;
    return 1;
}

static int test_power_and_registers(void)
{
    GB_Memory memory;
    GB_Interrupt interrupt;
    GB_Audio audio;
    GB_Error error;
    gb_error_clear(&error);
    TEST_CHECK(init_memory(&memory, GB_MEMORY_MODE_DMG, &interrupt, &error), error.message);
    TEST_CHECK(gb_audio_init(&audio, &memory, &error) == GB_RESULT_OK, error.message);

    uint8_t value = 0u;
    TEST_CHECK(gb_memory_read8(&memory, GB_AUDIO_ADDR_NR52, &value, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK((value & 0x80u) == 0u, "APU should start powered off");

    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR12, 0xF3u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_read8(&memory, GB_AUDIO_ADDR_NR12, &value, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(value == 0u, "Audio registers are cleared while APU is off");

    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR52, 0x80u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR12, 0xF3u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_read8(&memory, GB_AUDIO_ADDR_NR12, &value, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(value == 0xF3u, "Powered APU accepts channel register writes");

    TEST_CHECK(gb_audio_destroy(&audio, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_interrupt_disconnect_memory(&interrupt, &error) == GB_RESULT_OK, error.message);
    return 0;
}

static int test_pulse_and_mixer(void)
{
    GB_Memory memory;
    GB_Interrupt interrupt;
    GB_Audio audio;
    GB_Error error;
    gb_error_clear(&error);
    TEST_CHECK(init_memory(&memory, GB_MEMORY_MODE_DMG, &interrupt, &error), error.message);
    TEST_CHECK(gb_audio_init(&audio, &memory, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR52, 0x80u, &error) == GB_RESULT_OK, error.message);

    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR11, 0x80u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR12, 0xF0u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR13, 0x40u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR14, 0x87u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR51, 0x11u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR50, 0x77u, &error) == GB_RESULT_OK, error.message);

    uint8_t nr52 = 0u;
    TEST_CHECK(gb_memory_read8(&memory, GB_AUDIO_ADDR_NR52, &nr52, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK((nr52 & 0x01u) != 0u, "Pulse channel 1 should be active after trigger");

    TEST_CHECK(gb_audio_tick(&audio, 10000u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_audio_queued_frames(&audio) > 0u, "Audio should generate PCM samples");

    float samples[512u * 2u];
    size_t frames = gb_audio_read_samples(&audio, samples, 512u, &error);
    TEST_CHECK(error.code == GB_RESULT_OK, error.message);
    TEST_CHECK(frames > 0u, "PCM queue should be readable");
    int nonzero = 0;
    for (size_t i = 0u; i < frames * 2u; ++i) {
        if (samples[i] > 0.0001f || samples[i] < -0.0001f) { nonzero = 1; break; }
    }
    TEST_CHECK(nonzero, "Triggered pulse should produce nonzero PCM");

    TEST_CHECK(gb_audio_destroy(&audio, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_interrupt_disconnect_memory(&interrupt, &error) == GB_RESULT_OK, error.message);
    return 0;
}

static int test_wave_and_pcm_registers(void)
{
    GB_Memory memory;
    GB_Interrupt interrupt;
    GB_Audio audio;
    GB_Error error;
    uint8_t value = 0u;
    gb_error_clear(&error);
    TEST_CHECK(init_memory(&memory, GB_MEMORY_MODE_CGB, &interrupt, &error), error.message);
    TEST_CHECK(gb_audio_init(&audio, &memory, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR52, 0x80u, &error) == GB_RESULT_OK, error.message);

    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_WAVE, 0xF0u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR30, 0x80u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR32, 0x20u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR33, 0x40u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR34, 0x87u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_read8(&memory, GB_AUDIO_ADDR_PCM34, &value, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK((value & 0x0Fu) <= 0x0Fu, "CGB PCM34 must expose a 4-bit CH3 value");

    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR52, 0x00u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_read8(&memory, GB_AUDIO_ADDR_WAVE, &value, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(value == 0xF0u, "Wave RAM survives APU power off");

    TEST_CHECK(gb_audio_destroy(&audio, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_interrupt_disconnect_memory(&interrupt, &error) == GB_RESULT_OK, error.message);
    return 0;
}

static int test_timer_frame_sequencer(void)
{
    GB_Memory memory;
    GB_Interrupt interrupt;
    GB_Timer timer;
    GB_Audio audio;
    GB_Error error;
    gb_error_clear(&error);
    TEST_CHECK(init_memory(&memory, GB_MEMORY_MODE_DMG, &interrupt, &error), error.message);
    TEST_CHECK(gb_timer_init(&timer, &memory, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_audio_init(&audio, &memory, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_audio_attach_timer(&audio, &timer, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR52, 0x80u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR12, 0xF9u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_memory_write8(&memory, GB_AUDIO_ADDR_NR14, 0x80u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_timer_tick(&timer, 8192u, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(error.code == GB_RESULT_OK, error.message);

    TEST_CHECK(gb_audio_destroy(&audio, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_timer_destroy(&timer, &error) == GB_RESULT_OK, error.message);
    TEST_CHECK(gb_interrupt_disconnect_memory(&interrupt, &error) == GB_RESULT_OK, error.message);
    return 0;
}

int main(void)
{
    if (test_power_and_registers() != 0) return 1;
    if (test_pulse_and_mixer() != 0) return 1;
    if (test_wave_and_pcm_registers() != 0) return 1;
    if (test_timer_frame_sequencer() != 0) return 1;
    printf("All audio tests passed.\n");
    return 0;
}
