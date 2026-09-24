#include "gb_audio.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../timer/gb_timer.h"

GB_Result gb_audio_timer_apu_clock(void *user, GB_Error *error);

static void audio_error(GB_Error *error, GB_Result code, uint16_t address,
                        const char *message)
{
    if (error == NULL) return;
    gb_error_clear(error);
    error->code = code;
    error->pc = address;
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static GB_Result require_audio(const GB_Audio *audio, GB_Error *error)
{
    if (audio == NULL) {
        audio_error(error, GB_RESULT_NULL_ARGUMENT, 0u,
                    "Audio pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!audio->initialized) {
        audio_error(error, GB_RESULT_BAD_STATE, 0u,
                    "Audio has not been initialized");
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static float calculate_hpf_charge_factor(const GB_Audio *audio)
{
    double per_master_clock = audio->cgb_mode ? 0.998943 : 0.999958;
    double output_rate = audio->sample_rate_hz != 0u
        ? (double)audio->sample_rate_hz
        : (double)GB_AUDIO_SAMPLE_RATE_HZ;
    double clocks_per_sample = (double)GB_AUDIO_MASTER_CLOCK_HZ / output_rate;
    return (float)pow(per_master_clock, clocks_per_sample);
}

static uint8_t pulse_output(const GB_AudioPulse *channel)
{
    static const uint8_t duty_table[4][8] = {
        {0u,0u,0u,0u,0u,0u,0u,1u},
        {1u,0u,0u,0u,0u,0u,0u,1u},
        {1u,0u,0u,0u,0u,1u,1u,1u},
        {0u,1u,1u,1u,1u,1u,1u,0u}
    };
    if (!channel->dac_enabled) return 0u;
    return duty_table[channel->duty & 3u][channel->duty_step & 7u] != 0u
        ? channel->volume : 0u;
}

static uint8_t noise_output(const GB_AudioNoise *channel)
{
    if (!channel->dac_enabled || !channel->active) return 0u;
    return (channel->lfsr & 1u) == 0u ? channel->volume : 0u;
}

static uint8_t wave_output(const GB_Audio *audio)
{
    if (!audio->ch3.active || !audio->ch3.dac_enabled) return 0u;
    uint8_t sample = audio->ch3.sample_buffer;
    switch (audio->ch3.output_level & 3u) {
    case 0u: return 0u;
    case 1u: return sample;
    case 2u: return (uint8_t)(sample >> 1u);
    default: return (uint8_t)(sample >> 2u);
    }
}

static uint16_t pulse_period(uint16_t frequency)
{
    uint16_t period_value = (uint16_t)(2048u - (frequency & 0x07FFu));
    return (uint16_t)(period_value * 4u);
}

static uint16_t wave_period(uint16_t frequency)
{
    uint16_t period_value = (uint16_t)(2048u - (frequency & 0x07FFu));
    return (uint16_t)(period_value * 2u);
}

static uint16_t noise_period(uint8_t divisor_code, uint8_t shift)
{
    static const uint8_t base_periods[8] = {8u,16u,32u,48u,64u,80u,96u,112u};
    if ((shift & 0x0Fu) >= 14u) return 0u;
    return (uint16_t)((uint32_t)base_periods[divisor_code & 7u] << (shift & 0x0Fu));
}

static bool dac_enabled_pulse(uint8_t nrx2)
{
    return (nrx2 & 0xF8u) != 0u;
}

static bool dac_enabled_wave(uint8_t nr30)
{
    return (nr30 & 0x80u) != 0u;
}

static void set_length_from_write_pulse(GB_AudioPulse *channel, uint8_t value)
{
    channel->length = (uint8_t)(64u - (value & 0x3Fu));
}

static void update_pulse_register_state(GB_Audio *audio)
{
    audio->ch1.duty = (uint8_t)(audio->nr11 >> 6u);
    audio->ch1.frequency = (uint16_t)(((uint16_t)(audio->nr14 & 0x07u) << 8u) | audio->nr13);
    audio->ch1.length_enabled = (audio->nr14 & 0x40u) != 0u;
    audio->ch1.dac_enabled = dac_enabled_pulse(audio->nr12);
    audio->ch1.envelope_initial = (uint8_t)(audio->nr12 >> 4u);
    audio->ch1.envelope_add = (audio->nr12 & 0x08u) != 0u;
    audio->ch1.envelope_period = (uint8_t)(audio->nr12 & 0x07u);

    audio->ch2.duty = (uint8_t)(audio->nr21 >> 6u);
    audio->ch2.frequency = (uint16_t)(((uint16_t)(audio->nr24 & 0x07u) << 8u) | audio->nr23);
    audio->ch2.length_enabled = (audio->nr24 & 0x40u) != 0u;
    audio->ch2.dac_enabled = dac_enabled_pulse(audio->nr22);
    audio->ch2.envelope_initial = (uint8_t)(audio->nr22 >> 4u);
    audio->ch2.envelope_add = (audio->nr22 & 0x08u) != 0u;
    audio->ch2.envelope_period = (uint8_t)(audio->nr22 & 0x07u);

    audio->ch3.frequency = (uint16_t)(((uint16_t)(audio->nr34 & 0x07u) << 8u) | audio->nr33);
    audio->ch3.length_enabled = (audio->nr34 & 0x40u) != 0u;
    audio->ch3.dac_enabled = dac_enabled_wave(audio->nr30);
    audio->ch3.output_level = (uint8_t)((audio->nr32 >> 5u) & 0x03u);

    audio->ch4.length_enabled = (audio->nr44 & 0x40u) != 0u;
    audio->ch4.dac_enabled = dac_enabled_pulse(audio->nr42);
    audio->ch4.envelope_initial = (uint8_t)(audio->nr42 >> 4u);
    audio->ch4.envelope_add = (audio->nr42 & 0x08u) != 0u;
    audio->ch4.envelope_period = (uint8_t)(audio->nr42 & 0x07u);
    audio->ch4.divisor_code = (uint8_t)(audio->nr43 & 0x07u);
    audio->ch4.clock_shift = (uint8_t)(audio->nr43 >> 4u);
    audio->ch4.width_mode = (audio->nr43 & 0x08u) != 0u;
}

static void clear_audio_registers(GB_Audio *audio)
{
    audio->nr10 = 0u;
    audio->nr11 = 0u;
    audio->nr12 = 0u;
    audio->nr13 = 0u;
    audio->nr14 = 0u;
    audio->nr21 = 0u;
    audio->nr22 = 0u;
    audio->nr23 = 0u;
    audio->nr24 = 0u;
    audio->nr30 = 0u;
    audio->nr31 = 0u;
    audio->nr32 = 0u;
    audio->nr33 = 0u;
    audio->nr34 = 0u;
    audio->nr41 = 0u;
    audio->nr42 = 0u;
    audio->nr43 = 0u;
    audio->nr44 = 0u;
    audio->nr50 = 0u;
    audio->nr51 = 0u;
    audio->nr52 = 0u;
    memset(&audio->ch1, 0, sizeof(audio->ch1));
    memset(&audio->ch2, 0, sizeof(audio->ch2));
    memset(&audio->ch3, 0, sizeof(audio->ch3));
    memset(&audio->ch4, 0, sizeof(audio->ch4));
    audio->ch4.lfsr = 0x7FFFu;
}

static void trigger_pulse(GB_Audio *audio, GB_AudioPulse *channel, bool first)
{
    update_pulse_register_state(audio);
    if (!channel->dac_enabled) {
        channel->active = false;
        return;
    }

    if (channel->length == 0u) channel->length = 64u;
    channel->active = true;
    channel->volume = channel->envelope_initial;
    channel->envelope_timer = channel->envelope_period == 0u ? 8u : channel->envelope_period;
    channel->timer = pulse_period(channel->frequency);
    if (channel->timer == 0u) channel->timer = 4u;
    /* Pulse duty phase is not reset by a trigger. It is reset only when the APU is powered on. */

    if (first) {
        uint8_t period = (uint8_t)((audio->nr10 >> 4u) & 7u);
        channel->sweep_period = period;
        channel->sweep_negate = (audio->nr10 & 0x08u) != 0u;
        channel->sweep_shift = (uint8_t)(audio->nr10 & 7u);
        channel->sweep_timer = period == 0u ? 8u : period;
        channel->sweep_shadow = channel->frequency;
        channel->sweep_enabled = (period != 0u || channel->sweep_shift != 0u);
        channel->sweep_negate_used = false;
        if (channel->sweep_shift != 0u) {
            uint16_t next = (uint16_t)(channel->sweep_shadow +
                (channel->sweep_negate
                    ? (uint16_t)(-(int16_t)(channel->sweep_shadow >> channel->sweep_shift))
                    : (uint16_t)(channel->sweep_shadow >> channel->sweep_shift)));
            if (next > 2047u) channel->active = false;
        }
    }
}

static void trigger_wave(GB_Audio *audio)
{
    update_pulse_register_state(audio);
    if (!audio->ch3.dac_enabled) {
        audio->ch3.active = false;
        return;
    }
    if (audio->ch3.length == 0u) audio->ch3.length = 256u;
    audio->ch3.active = true;
    audio->ch3.timer = wave_period(audio->ch3.frequency);
    if (audio->ch3.timer == 0u) audio->ch3.timer = 2u;
    audio->ch3.position = 0u;
    /* Trigger resets the position, but does not refill the sample buffer. */
}

static void trigger_noise(GB_Audio *audio)
{
    update_pulse_register_state(audio);
    if (!audio->ch4.dac_enabled) {
        audio->ch4.active = false;
        return;
    }
    if (audio->ch4.length == 0u) audio->ch4.length = 64u;
    audio->ch4.active = true;
    audio->ch4.volume = audio->ch4.envelope_initial;
    audio->ch4.envelope_timer = audio->ch4.envelope_period == 0u ? 8u : audio->ch4.envelope_period;
    audio->ch4.timer = noise_period(audio->ch4.divisor_code, audio->ch4.clock_shift);
    audio->ch4.lfsr = 0x7FFFu;
}

static uint16_t sweep_calculate(GB_AudioPulse *channel, bool *overflow)
{
    uint16_t delta = (uint16_t)(channel->sweep_shadow >> channel->sweep_shift);
    uint16_t result;
    if (channel->sweep_negate) {
        channel->sweep_negate_used = true;
        if (delta > channel->sweep_shadow) {
            *overflow = false;
            return 0u;
        }
        result = (uint16_t)(channel->sweep_shadow - delta);
    } else {
        result = (uint16_t)(channel->sweep_shadow + delta);
    }
    *overflow = result > 2047u;
    return result;
}

static void clock_sweep(GB_Audio *audio)
{
    GB_AudioPulse *channel = &audio->ch1;
    if (!channel->sweep_enabled) return;
    if (channel->sweep_timer > 0u) channel->sweep_timer--;
    if (channel->sweep_timer != 0u) return;

    channel->sweep_timer = channel->sweep_period == 0u ? 8u : channel->sweep_period;
    if (channel->sweep_shift == 0u) return;

    bool overflow = false;
    uint16_t next = sweep_calculate(channel, &overflow);
    if (overflow || next > 2047u) {
        channel->active = false;
        return;
    }
    channel->sweep_shadow = next;
    channel->frequency = next;
    audio->nr13 = (uint8_t)(next & 0xFFu);
    audio->nr14 = (uint8_t)((audio->nr14 & 0xF8u) | ((next >> 8u) & 0x07u));

    overflow = false;
    (void)sweep_calculate(channel, &overflow);
    if (overflow) channel->active = false;
}

static void clock_envelope(GB_Audio *audio)
{
    GB_AudioPulse *pulses[2] = {&audio->ch1, &audio->ch2};
    for (size_t i = 0; i < 2u; ++i) {
        GB_AudioPulse *channel = pulses[i];
        if (channel->envelope_period == 0u) continue;
        if (channel->envelope_timer > 0u) channel->envelope_timer--;
        if (channel->envelope_timer != 0u) continue;
        channel->envelope_timer = channel->envelope_period;
        if (channel->envelope_add) {
            if (channel->volume < 15u) channel->volume++;
        } else if (channel->volume > 0u) {
            channel->volume--;
        }
    }

    GB_AudioNoise *noise = &audio->ch4;
    if (noise->envelope_period != 0u) {
        if (noise->envelope_timer > 0u) noise->envelope_timer--;
        if (noise->envelope_timer == 0u) {
            noise->envelope_timer = noise->envelope_period;
            if (noise->envelope_add) {
                if (noise->volume < 15u) noise->volume++;
            } else if (noise->volume > 0u) {
                noise->volume--;
            }
        }
    }
}

static void clock_length(GB_Audio *audio)
{
    GB_AudioPulse *pulses[2] = {&audio->ch1, &audio->ch2};
    for (size_t i = 0; i < 2u; ++i) {
        if (pulses[i]->length_enabled && pulses[i]->length > 0u) {
            pulses[i]->length--;
            if (pulses[i]->length == 0u) pulses[i]->active = false;
        }
    }
    if (audio->ch3.length_enabled && audio->ch3.length > 0u) {
        audio->ch3.length--;
        if (audio->ch3.length == 0u) audio->ch3.active = false;
    }
    if (audio->ch4.length_enabled && audio->ch4.length > 0u) {
        audio->ch4.length--;
        if (audio->ch4.length == 0u) audio->ch4.active = false;
    }
}

static void clock_frame_sequencer(GB_Audio *audio)
{
    switch (audio->frame_step & 7u) {
    case 0u:
    case 2u:
    case 4u:
    case 6u:
        clock_length(audio);
        if (audio->frame_step == 2u || audio->frame_step == 6u) clock_sweep(audio);
        break;
    case 7u:
        clock_envelope(audio);
        break;
    case 1u:
    case 3u:
    case 5u:
        break;
    default:
        break;
    }
    audio->frame_step = (uint8_t)((audio->frame_step + 1u) & 7u);
}

static void tick_channels_one_cycle(GB_Audio *audio)
{
    if (audio->ch1.active) {
        if (audio->ch1.timer > 0u) audio->ch1.timer--;
        if (audio->ch1.timer == 0u) {
            audio->ch1.timer = pulse_period(audio->ch1.frequency);
            if (audio->ch1.timer == 0u) audio->ch1.timer = 4u;
            audio->ch1.duty_step = (uint8_t)((audio->ch1.duty_step + 1u) & 7u);
        }
    }
    if (audio->ch2.active) {
        if (audio->ch2.timer > 0u) audio->ch2.timer--;
        if (audio->ch2.timer == 0u) {
            audio->ch2.timer = pulse_period(audio->ch2.frequency);
            if (audio->ch2.timer == 0u) audio->ch2.timer = 4u;
            audio->ch2.duty_step = (uint8_t)((audio->ch2.duty_step + 1u) & 7u);
        }
    }
    if (audio->ch3.active) {
        if (audio->ch3.timer > 0u) audio->ch3.timer--;
        if (audio->ch3.timer == 0u) {
            audio->ch3.timer = wave_period(audio->ch3.frequency);
            if (audio->ch3.timer == 0u) audio->ch3.timer = 2u;
            audio->ch3.position = (uint8_t)((audio->ch3.position + 1u) & 31u);
            {
                uint8_t wave_byte = audio->wave_ram[audio->ch3.position >> 1u];
                audio->ch3.sample_buffer = (audio->ch3.position & 1u) == 0u
                    ? (uint8_t)(wave_byte >> 4u)
                    : (uint8_t)(wave_byte & 0x0Fu);
            }
        }
    }
    if (audio->ch4.active) {
        if (audio->ch4.timer > 0u) audio->ch4.timer--;
        if (audio->ch4.timer == 0u) {
            audio->ch4.timer = noise_period(audio->ch4.divisor_code, audio->ch4.clock_shift);
            if (audio->ch4.timer != 0u) {
                uint16_t bit = (uint16_t)((audio->ch4.lfsr ^ (audio->ch4.lfsr >> 1u)) & 1u);
                audio->ch4.lfsr = (uint16_t)((audio->ch4.lfsr >> 1u) | (bit << 14u));
                if (audio->ch4.width_mode) {
                    uint16_t width_clear = (uint16_t)~(uint16_t)(1u << 6u);
                    uint16_t width_set = (uint16_t)(bit << 6u);
                    audio->ch4.lfsr = (uint16_t)((audio->ch4.lfsr & width_clear) | width_set);
                }
            }
        }
    }
}

static float digital_to_analog(uint8_t value)
{
    return 1.0f - ((float)value / 7.5f);
}

static void render_sample(GB_Audio *audio)
{
    uint8_t s1 = pulse_output(&audio->ch1);
    uint8_t s2 = pulse_output(&audio->ch2);
    uint8_t s3 = wave_output(audio);
    uint8_t s4 = noise_output(&audio->ch4);
    uint8_t samples[4] = {s1, s2, s3, s4};

    float left = 0.0f;
    float right = 0.0f;
    for (uint8_t i = 0u; i < 4u; ++i) {
        float analog = digital_to_analog(samples[i]);
        if ((audio->nr51 & (uint8_t)(0x10u << i)) != 0u) left += analog;
        if ((audio->nr51 & (uint8_t)(0x01u << i)) != 0u) right += analog;
    }

    uint8_t left_volume = (uint8_t)((audio->nr50 >> 4u) & 7u);
    uint8_t right_volume = (uint8_t)(audio->nr50 & 7u);
    left *= (float)(left_volume + 1u) / 8.0f;
    right *= (float)(right_volume + 1u) / 8.0f;
    left *= 0.25f;
    right *= 0.25f;

    /* A simple stable HPF models the analog DC-blocking stage and prevents
     * long-lived register changes from leaving a DC offset in the stream. */
    bool any_dac_enabled = audio->ch1.dac_enabled || audio->ch2.dac_enabled ||
                           audio->ch3.dac_enabled || audio->ch4.dac_enabled;
    float filtered_left = 0.0f;
    float filtered_right = 0.0f;
    if (any_dac_enabled) {
        const float hpf = audio->hpf_charge_factor;
        filtered_left = left - audio->hp_left_input + hpf * audio->hp_left_output;
        filtered_right = right - audio->hp_right_input + hpf * audio->hp_right_output;
        audio->hp_left_input = left;
        audio->hp_left_output = filtered_left;
        audio->hp_right_input = right;
        audio->hp_right_output = filtered_right;
    }

    if (audio->ring_count == GB_AUDIO_RING_FRAMES) {
        audio->ring_read = (audio->ring_read + 1u) % GB_AUDIO_RING_FRAMES;
        audio->ring_count--;
        audio->dropped_frames++;
    }
    audio->ring[audio->ring_write][0] = filtered_left;
    audio->ring[audio->ring_write][1] = filtered_right;
    audio->ring_write = (audio->ring_write + 1u) % GB_AUDIO_RING_FRAMES;
    audio->ring_count++;
}

static void fallback_frame_clock(GB_Audio *audio, uint32_t t_cycles)
{
    audio->fallback_frame_cycles += t_cycles;
    while (audio->fallback_frame_cycles >= (GB_AUDIO_MASTER_CLOCK_HZ / 512u)) {
        audio->fallback_frame_cycles -= GB_AUDIO_MASTER_CLOCK_HZ / 512u;
        clock_frame_sequencer(audio);
    }
}

GB_Result gb_audio_reset(GB_Audio *audio, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return result;

    bool saved_cgb = audio->cgb_mode;
    GB_Memory *saved_memory = audio->memory;
    size_t saved_io = audio->io_device_index;
    size_t saved_wave = audio->wave_device_index;
    size_t saved_pcm = audio->pcm_device_index;
    uint32_t saved_sample_rate = audio->sample_rate_hz;
    void *saved_timer = audio->timer;
    memset(audio, 0, sizeof(*audio));
    audio->cgb_mode = saved_cgb;
    audio->memory = saved_memory;
    audio->io_device_index = saved_io;
    audio->wave_device_index = saved_wave;
    audio->pcm_device_index = saved_pcm;
    audio->sample_rate_hz = saved_sample_rate != 0u ? saved_sample_rate : GB_AUDIO_SAMPLE_RATE_HZ;
    audio->timer = saved_timer;
    audio->initialized = true;
    audio->powered_on = false;
    audio->ch4.lfsr = 0x7FFFu;
    audio->ch3.sample_buffer = 0u;
    audio->hpf_charge_factor = calculate_hpf_charge_factor(audio);
    memset(audio->wave_ram, 0, sizeof(audio->wave_ram));
    return GB_RESULT_OK;
}

GB_Result gb_audio_init(GB_Audio *audio, GB_Memory *memory, GB_Error *error)
{
    gb_error_clear(error);
    if (audio == NULL || memory == NULL) {
        audio_error(error, GB_RESULT_NULL_ARGUMENT, 0u,
                    "Audio and memory pointers are required");
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(audio, 0, sizeof(*audio));
    audio->initialized = true;
    audio->memory = memory;
    audio->cgb_mode = memory->mode == GB_MEMORY_MODE_CGB;
    audio->sample_rate_hz = GB_AUDIO_SAMPLE_RATE_HZ;
    audio->hpf_charge_factor = calculate_hpf_charge_factor(audio);
    audio->ch4.lfsr = 0x7FFFu;

    GB_Result result = gb_memory_map_io_device(memory,
                                               GB_AUDIO_ADDR_NR10,
                                               GB_AUDIO_ADDR_NR52,
                                               audio,
                                               gb_audio_read8,
                                               gb_audio_write8,
                                               gb_audio_device_tick,
                                               &audio->io_device_index,
                                               error);
    if (result != GB_RESULT_OK) goto fail;

    result = gb_memory_map_io_device(memory,
                                     GB_AUDIO_ADDR_WAVE,
                                     GB_AUDIO_ADDR_WAVE_END,
                                     audio,
                                     gb_audio_read8,
                                     gb_audio_write8,
                                     NULL,
                                     &audio->wave_device_index,
                                     error);
    if (result != GB_RESULT_OK) goto fail_io;

    if (audio->cgb_mode) {
        result = gb_memory_map_io_device(memory,
                                         GB_AUDIO_ADDR_PCM12,
                                         GB_AUDIO_ADDR_PCM34,
                                         audio,
                                         gb_audio_read8,
                                         gb_audio_write8,
                                         NULL,
                                         &audio->pcm_device_index,
                                         error);
        if (result != GB_RESULT_OK) goto fail_wave;
    }

    audio->mapped = true;
    return gb_audio_reset(audio, error);

fail_wave:
    (void)gb_memory_unmap_io_device(memory, audio->wave_device_index, NULL);
fail_io:
    (void)gb_memory_unmap_io_device(memory, audio->io_device_index, NULL);
fail:
    memset(audio, 0, sizeof(*audio));
    return result;
}

GB_Result gb_audio_destroy(GB_Audio *audio, GB_Error *error)
{
    gb_error_clear(error);
    if (audio == NULL) {
        audio_error(error, GB_RESULT_NULL_ARGUMENT, 0u,
                    "Audio pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!audio->initialized) return GB_RESULT_OK;

    if (audio->timer != NULL) {
        GB_Result timer_result = gb_timer_attach_apu_callback((GB_Timer *)audio->timer, NULL, NULL, error);
        if (timer_result != GB_RESULT_OK) return timer_result;
        audio->timer = NULL;
    }

    if (audio->mapped && audio->memory != NULL) {
        if (audio->cgb_mode) {
            GB_Result result = gb_memory_unmap_io_device(audio->memory,
                                                         audio->pcm_device_index,
                                                         error);
            if (result != GB_RESULT_OK) return result;
        }
        GB_Result result = gb_memory_unmap_io_device(audio->memory,
                                                     audio->wave_device_index,
                                                     error);
        if (result != GB_RESULT_OK) return result;
        result = gb_memory_unmap_io_device(audio->memory,
                                           audio->io_device_index,
                                           error);
        if (result != GB_RESULT_OK) return result;
    }
    memset(audio, 0, sizeof(*audio));
    return GB_RESULT_OK;
}

GB_Result gb_audio_power(GB_Audio *audio, bool enabled, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return result;
    if (audio->powered_on == enabled) return GB_RESULT_OK;
    audio->powered_on = enabled;
    if (!enabled) {
        clear_audio_registers(audio);
        audio->frame_step = 0u;
        audio->fallback_frame_cycles = 0u;
        audio->sample_accumulator = 0u;
    }
    return GB_RESULT_OK;
}

bool gb_audio_is_powered(const GB_Audio *audio)
{
    return audio != NULL && audio->initialized && audio->powered_on;
}

GB_Result gb_audio_attach_timer(GB_Audio *audio, struct GB_Timer *timer,
                                GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return result;
    audio->timer = timer;
    if (timer == NULL) return GB_RESULT_OK;
    return gb_timer_attach_apu_callback(timer, audio, gb_audio_timer_apu_clock, error);
}

GB_Result gb_audio_clock_frame_sequencer(GB_Audio *audio, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return result;
    if (audio->powered_on) clock_frame_sequencer(audio);
    return GB_RESULT_OK;
}

GB_Result gb_audio_tick(GB_Audio *audio, uint32_t t_cycles, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return result;
    if (t_cycles == 0u) return GB_RESULT_OK;

    if (!audio->powered_on) return GB_RESULT_OK;

    for (uint32_t i = 0u; i < t_cycles; ++i) {
        tick_channels_one_cycle(audio);
        audio->sample_accumulator += audio->sample_rate_hz;
        if (audio->timer == NULL) fallback_frame_clock(audio, 1u);
        while (audio->sample_accumulator >= GB_AUDIO_MASTER_CLOCK_HZ) {
            audio->sample_accumulator -= GB_AUDIO_MASTER_CLOCK_HZ;
            render_sample(audio);
        }
    }
    return GB_RESULT_OK;
}

GB_Result gb_audio_set_sample_rate(GB_Audio *audio, uint32_t sample_rate_hz,
                                  GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return result;
    if (sample_rate_hz < 8000u || sample_rate_hz > 192000u) {
        audio_error(error, GB_RESULT_INVALID_ARGUMENT, 0u,
                    "Audio sample rate must be between 8000 and 192000 Hz");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    audio->sample_rate_hz = sample_rate_hz;
    audio->hpf_charge_factor = calculate_hpf_charge_factor(audio);
    audio->sample_accumulator = 0u;
    return GB_RESULT_OK;
}

GB_Result gb_audio_device_tick(void *user, uint32_t t_cycles, GB_Error *error)
{
    return gb_audio_tick((GB_Audio *)user, t_cycles, error);
}

size_t gb_audio_read_samples(GB_Audio *audio, float *out_interleaved,
                             size_t max_frames, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return 0u;
    if (out_interleaved == NULL && max_frames != 0u) {
        audio_error(error, GB_RESULT_NULL_ARGUMENT, 0u,
                    "Audio output buffer is NULL");
        return 0u;
    }
    size_t count = audio->ring_count < max_frames ? audio->ring_count : max_frames;
    for (size_t i = 0u; i < count; ++i) {
        out_interleaved[i * 2u] = audio->ring[audio->ring_read][0];
        out_interleaved[i * 2u + 1u] = audio->ring[audio->ring_read][1];
        audio->ring_read = (audio->ring_read + 1u) % GB_AUDIO_RING_FRAMES;
        audio->ring_count--;
    }
    return count;
}

size_t gb_audio_queued_frames(const GB_Audio *audio)
{
    return (audio != NULL && audio->initialized) ? audio->ring_count : 0u;
}

uint64_t gb_audio_dropped_frames(const GB_Audio *audio)
{
    return (audio != NULL && audio->initialized) ? audio->dropped_frames : 0u;
}

uint8_t gb_audio_pcm12(const GB_Audio *audio)
{
    if (audio == NULL || !audio->initialized) return 0u;
    return (uint8_t)(pulse_output(&audio->ch1) |
                     (uint8_t)(pulse_output(&audio->ch2) << 4u));
}

uint8_t gb_audio_pcm34(const GB_Audio *audio)
{
    if (audio == NULL || !audio->initialized) return 0u;
    return (uint8_t)(wave_output(audio) |
                     (uint8_t)(noise_output(&audio->ch4) << 4u));
}

static uint8_t active_mask(const GB_Audio *audio)
{
    return (uint8_t)((audio->ch4.active ? 0x08u : 0u) |
                     (audio->ch3.active ? 0x04u : 0u) |
                     (audio->ch2.active ? 0x02u : 0u) |
                     (audio->ch1.active ? 0x01u : 0u));
}

static uint8_t audio_read_register(const GB_Audio *audio, uint16_t address)
{
    switch (address) {
    case GB_AUDIO_ADDR_NR10: return (uint8_t)(0x80u | audio->nr10);
    case GB_AUDIO_ADDR_NR11: return (uint8_t)(0x3Fu | audio->nr11);
    case GB_AUDIO_ADDR_NR12: return audio->nr12;
    case GB_AUDIO_ADDR_NR13: return 0xFFu;
    case GB_AUDIO_ADDR_NR14: return (uint8_t)(0xBFu | audio->nr14);
    case GB_AUDIO_ADDR_NR21: return (uint8_t)(0x3Fu | audio->nr21);
    case GB_AUDIO_ADDR_NR22: return audio->nr22;
    case GB_AUDIO_ADDR_NR23: return 0xFFu;
    case GB_AUDIO_ADDR_NR24: return (uint8_t)(0xBFu | audio->nr24);
    case GB_AUDIO_ADDR_NR30: return (uint8_t)(0x7Fu | audio->nr30);
    case GB_AUDIO_ADDR_NR31: return 0xFFu;
    case GB_AUDIO_ADDR_NR32: return (uint8_t)(0x9Fu | audio->nr32);
    case GB_AUDIO_ADDR_NR33: return 0xFFu;
    case GB_AUDIO_ADDR_NR34: return (uint8_t)(0xBFu | audio->nr34);
    case GB_AUDIO_ADDR_NR41: return 0xFFu;
    case GB_AUDIO_ADDR_NR42: return audio->nr42;
    case GB_AUDIO_ADDR_NR43: return audio->nr43;
    case GB_AUDIO_ADDR_NR44: return (uint8_t)(0xBFu | audio->nr44);
    case GB_AUDIO_ADDR_NR50: return audio->nr50;
    case GB_AUDIO_ADDR_NR51: return audio->nr51;
    case GB_AUDIO_ADDR_NR52: return (uint8_t)(0x70u | (audio->powered_on ? 0x80u : 0u) | active_mask(audio));
    default: return 0xFFu;
    }
}

GB_Result gb_audio_read8(void *user, uint16_t address, uint8_t *value,
                         GB_Error *error)
{
    gb_error_clear(error);
    GB_Audio *audio = (GB_Audio *)user;
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return result;
    if (value == NULL) {
        audio_error(error, GB_RESULT_NULL_ARGUMENT, address,
                    "Audio read output is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (address >= GB_AUDIO_ADDR_WAVE && address <= GB_AUDIO_ADDR_WAVE_END) {
        *value = audio->wave_ram[address - GB_AUDIO_ADDR_WAVE];
        return GB_RESULT_OK;
    }
    if (address == GB_AUDIO_ADDR_PCM12) {
        if (!audio->cgb_mode) { *value = 0xFFu; return GB_RESULT_OK; }
        *value = gb_audio_pcm12(audio);
        return GB_RESULT_OK;
    }
    if (address == GB_AUDIO_ADDR_PCM34) {
        if (!audio->cgb_mode) { *value = 0xFFu; return GB_RESULT_OK; }
        *value = gb_audio_pcm34(audio);
        return GB_RESULT_OK;
    }
    if (address < GB_AUDIO_ADDR_NR10 || address > GB_AUDIO_ADDR_NR52) {
        audio_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                    "Invalid audio register address");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    *value = audio_read_register(audio, address);
    return GB_RESULT_OK;
}

GB_Result gb_audio_write8(void *user, uint16_t address, uint8_t value,
                          GB_Error *error)
{
    gb_error_clear(error);
    GB_Audio *audio = (GB_Audio *)user;
    GB_Result result = require_audio(audio, error);
    if (result != GB_RESULT_OK) return result;

    if (address >= GB_AUDIO_ADDR_WAVE && address <= GB_AUDIO_ADDR_WAVE_END) {
        audio->wave_ram[address - GB_AUDIO_ADDR_WAVE] = value;
        return GB_RESULT_OK;
    }
    if (address == GB_AUDIO_ADDR_PCM12 || address == GB_AUDIO_ADDR_PCM34) {
        return GB_RESULT_OK;
    }
    if (address < GB_AUDIO_ADDR_NR10 || address > GB_AUDIO_ADDR_NR52) {
        audio_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                    "Invalid audio register address");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    if (address == GB_AUDIO_ADDR_NR52) {
        return gb_audio_power(audio, (value & 0x80u) != 0u, error);
    }
    if (!audio->powered_on) return GB_RESULT_OK;

    switch (address) {
    case GB_AUDIO_ADDR_NR10:
        audio->nr10 = (uint8_t)(value & 0x7Fu);
        audio->ch1.sweep_period = (uint8_t)((value >> 4u) & 7u);
        audio->ch1.sweep_negate = (value & 0x08u) != 0u;
        audio->ch1.sweep_shift = (uint8_t)(value & 7u);
        if ((value & 0x08u) == 0u && audio->ch1.sweep_negate_used) audio->ch1.active = false;
        break;
    case GB_AUDIO_ADDR_NR11:
        audio->nr11 = value;
        set_length_from_write_pulse(&audio->ch1, value);
        audio->ch1.duty = (uint8_t)(value >> 6u);
        break;
    case GB_AUDIO_ADDR_NR12:
        audio->nr12 = value;
        update_pulse_register_state(audio);
        if (!audio->ch1.dac_enabled) audio->ch1.active = false;
        break;
    case GB_AUDIO_ADDR_NR13:
        audio->nr13 = value;
        audio->ch1.frequency = (uint16_t)(((uint16_t)(audio->nr14 & 7u) << 8u) | audio->nr13);
        break;
    case GB_AUDIO_ADDR_NR14:
        audio->nr14 = (uint8_t)(value & 0xC7u);
        audio->ch1.length_enabled = (value & 0x40u) != 0u;
        audio->ch1.frequency = (uint16_t)(((uint16_t)(value & 7u) << 8u) | audio->nr13);
        if ((value & 0x80u) != 0u) trigger_pulse(audio, &audio->ch1, true);
        break;
    case GB_AUDIO_ADDR_NR21:
        audio->nr21 = value;
        set_length_from_write_pulse(&audio->ch2, value);
        audio->ch2.duty = (uint8_t)(value >> 6u);
        break;
    case GB_AUDIO_ADDR_NR22:
        audio->nr22 = value;
        update_pulse_register_state(audio);
        if (!audio->ch2.dac_enabled) audio->ch2.active = false;
        break;
    case GB_AUDIO_ADDR_NR23:
        audio->nr23 = value;
        audio->ch2.frequency = (uint16_t)(((uint16_t)(audio->nr24 & 7u) << 8u) | audio->nr23);
        break;
    case GB_AUDIO_ADDR_NR24:
        audio->nr24 = (uint8_t)(value & 0xC7u);
        audio->ch2.length_enabled = (value & 0x40u) != 0u;
        audio->ch2.frequency = (uint16_t)(((uint16_t)(value & 7u) << 8u) | audio->nr23);
        if ((value & 0x80u) != 0u) trigger_pulse(audio, &audio->ch2, false);
        break;
    case GB_AUDIO_ADDR_NR30:
        audio->nr30 = (uint8_t)(value & 0x80u);
        audio->ch3.dac_enabled = dac_enabled_wave(audio->nr30);
        if (!audio->ch3.dac_enabled) audio->ch3.active = false;
        break;
    case GB_AUDIO_ADDR_NR31:
        audio->nr31 = value;
        audio->ch3.length = (uint16_t)(256u - value);
        break;
    case GB_AUDIO_ADDR_NR32:
        audio->nr32 = (uint8_t)(value & 0xE0u);
        audio->ch3.output_level = (uint8_t)((value >> 5u) & 3u);
        break;
    case GB_AUDIO_ADDR_NR33:
        audio->nr33 = value;
        audio->ch3.frequency = (uint16_t)(((uint16_t)(audio->nr34 & 7u) << 8u) | audio->nr33);
        break;
    case GB_AUDIO_ADDR_NR34:
        audio->nr34 = (uint8_t)(value & 0xC7u);
        audio->ch3.length_enabled = (value & 0x40u) != 0u;
        audio->ch3.frequency = (uint16_t)(((uint16_t)(value & 7u) << 8u) | audio->nr33);
        if ((value & 0x80u) != 0u) trigger_wave(audio);
        break;
    case GB_AUDIO_ADDR_NR41:
        audio->nr41 = value;
        audio->ch4.length = (uint8_t)(64u - (value & 0x3Fu));
        break;
    case GB_AUDIO_ADDR_NR42:
        audio->nr42 = value;
        update_pulse_register_state(audio);
        if (!audio->ch4.dac_enabled) audio->ch4.active = false;
        break;
    case GB_AUDIO_ADDR_NR43:
        audio->nr43 = value;
        audio->ch4.divisor_code = (uint8_t)(value & 7u);
        audio->ch4.clock_shift = (uint8_t)(value >> 4u);
        audio->ch4.width_mode = (value & 8u) != 0u;
        break;
    case GB_AUDIO_ADDR_NR44:
        audio->nr44 = (uint8_t)(value & 0xC0u);
        audio->ch4.length_enabled = (value & 0x40u) != 0u;
        if ((value & 0x80u) != 0u) trigger_noise(audio);
        break;
    case GB_AUDIO_ADDR_NR50:
        audio->nr50 = value;
        break;
    case GB_AUDIO_ADDR_NR51:
        audio->nr51 = value;
        break;
    default:
        audio_error(error, GB_RESULT_INVALID_ARGUMENT, address,
                    "Unhandled audio register");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    return GB_RESULT_OK;
}

/* Kept separate so a timer implementation can call the callback without
 * exposing any SDL or memory details. */
GB_Result gb_audio_timer_apu_clock(void *user, GB_Error *error)
{
    return gb_audio_clock_frame_sequencer((GB_Audio *)user, error);
}
