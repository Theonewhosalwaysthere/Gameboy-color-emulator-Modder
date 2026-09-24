#ifndef GB_AUDIO_H
#define GB_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../memory/gb_memory.h"

struct GB_Timer;

#ifdef __cplusplus
extern "C" {
#endif

#define GB_AUDIO_MASTER_CLOCK_HZ 4194304u
#define GB_AUDIO_SAMPLE_RATE_HZ 48000u
#define GB_AUDIO_CHANNEL_COUNT 2u
#define GB_AUDIO_RING_FRAMES 16384u

enum {
    GB_AUDIO_ADDR_NR10  = 0xFF10u,
    GB_AUDIO_ADDR_NR11  = 0xFF11u,
    GB_AUDIO_ADDR_NR12  = 0xFF12u,
    GB_AUDIO_ADDR_NR13  = 0xFF13u,
    GB_AUDIO_ADDR_NR14  = 0xFF14u,
    GB_AUDIO_ADDR_NR21  = 0xFF16u,
    GB_AUDIO_ADDR_NR22  = 0xFF17u,
    GB_AUDIO_ADDR_NR23  = 0xFF18u,
    GB_AUDIO_ADDR_NR24  = 0xFF19u,
    GB_AUDIO_ADDR_NR30  = 0xFF1Au,
    GB_AUDIO_ADDR_NR31  = 0xFF1Bu,
    GB_AUDIO_ADDR_NR32  = 0xFF1Cu,
    GB_AUDIO_ADDR_NR33  = 0xFF1Du,
    GB_AUDIO_ADDR_NR34  = 0xFF1Eu,
    GB_AUDIO_ADDR_NR41  = 0xFF20u,
    GB_AUDIO_ADDR_NR42  = 0xFF21u,
    GB_AUDIO_ADDR_NR43  = 0xFF22u,
    GB_AUDIO_ADDR_NR44  = 0xFF23u,
    GB_AUDIO_ADDR_NR50  = 0xFF24u,
    GB_AUDIO_ADDR_NR51  = 0xFF25u,
    GB_AUDIO_ADDR_NR52  = 0xFF26u,
    GB_AUDIO_ADDR_WAVE  = 0xFF30u,
    GB_AUDIO_ADDR_WAVE_END = 0xFF3Fu,
    GB_AUDIO_ADDR_PCM12 = 0xFF76u,
    GB_AUDIO_ADDR_PCM34 = 0xFF77u
};

typedef struct GB_AudioPulse {
    bool active;
    bool dac_enabled;
    uint8_t duty;
    uint8_t duty_step;
    uint8_t length;
    bool length_enabled;
    uint8_t volume;
    uint8_t envelope_initial;
    bool envelope_add;
    uint8_t envelope_period;
    uint8_t envelope_timer;
    uint16_t frequency;
    uint16_t timer;
    uint8_t sweep_period;
    bool sweep_negate;
    uint8_t sweep_shift;
    uint8_t sweep_timer;
    uint16_t sweep_shadow;
    bool sweep_enabled;
    bool sweep_negate_used;
} GB_AudioPulse;

typedef struct GB_AudioWave {
    bool active;
    bool dac_enabled;
    uint16_t length;
    bool length_enabled;
    uint8_t output_level;
    uint16_t frequency;
    uint16_t timer;
    uint8_t position;
    uint8_t sample_buffer;
} GB_AudioWave;

typedef struct GB_AudioNoise {
    bool active;
    bool dac_enabled;
    uint8_t length;
    bool length_enabled;
    uint8_t volume;
    uint8_t envelope_initial;
    bool envelope_add;
    uint8_t envelope_period;
    uint8_t envelope_timer;
    uint8_t divisor_code;
    uint8_t clock_shift;
    bool width_mode;
    uint16_t timer;
    uint16_t lfsr;
} GB_AudioNoise;

typedef struct GB_Audio {
    bool initialized;
    bool mapped;
    bool cgb_mode;
    bool powered_on;

    GB_Memory *memory;
    size_t io_device_index;
    size_t wave_device_index;
    size_t pcm_device_index;

    uint8_t nr10;
    uint8_t nr11;
    uint8_t nr12;
    uint8_t nr13;
    uint8_t nr14;
    uint8_t nr21;
    uint8_t nr22;
    uint8_t nr23;
    uint8_t nr24;
    uint8_t nr30;
    uint8_t nr31;
    uint8_t nr32;
    uint8_t nr33;
    uint8_t nr34;
    uint8_t nr41;
    uint8_t nr42;
    uint8_t nr43;
    uint8_t nr44;
    uint8_t nr50;
    uint8_t nr51;
    uint8_t nr52;

    uint8_t wave_ram[16];

    GB_AudioPulse ch1;
    GB_AudioPulse ch2;
    GB_AudioWave ch3;
    GB_AudioNoise ch4;

    uint8_t frame_step;
    uint32_t fallback_frame_cycles;

    uint32_t sample_rate_hz;
    uint64_t sample_accumulator;
    float ring[GB_AUDIO_RING_FRAMES][GB_AUDIO_CHANNEL_COUNT];
    uint32_t ring_read;
    uint32_t ring_write;
    uint32_t ring_count;
    uint64_t dropped_frames;

    float hp_left_input;
    float hp_left_output;
    float hp_right_input;
    float hp_right_output;
    float hpf_charge_factor;

    void *timer;
    GB_Result (*timer_attach)(void *timer, void *audio, GB_Error *error);
} GB_Audio;

GB_Result gb_audio_init(GB_Audio *audio, GB_Memory *memory, GB_Error *error);
GB_Result gb_audio_reset(GB_Audio *audio, GB_Error *error);
GB_Result gb_audio_destroy(GB_Audio *audio, GB_Error *error);

GB_Result gb_audio_tick(GB_Audio *audio, uint32_t t_cycles, GB_Error *error);
GB_Result gb_audio_set_sample_rate(GB_Audio *audio, uint32_t sample_rate_hz, GB_Error *error);
GB_Result gb_audio_clock_frame_sequencer(GB_Audio *audio, GB_Error *error);

GB_Result gb_audio_power(GB_Audio *audio, bool enabled, GB_Error *error);
bool gb_audio_is_powered(const GB_Audio *audio);

/* Optional timer attachment. The timer callback makes frame-sequencer timing
 * follow DIV-APU, including DIV writes, instead of relying on a fallback clock. */
GB_Result gb_audio_attach_timer(GB_Audio *audio, struct GB_Timer *timer,
                                GB_Error *error);

/* Read interleaved float32 stereo frames from the generated PCM queue. */
size_t gb_audio_read_samples(GB_Audio *audio, float *out_interleaved,
                             size_t max_frames, GB_Error *error);
size_t gb_audio_queued_frames(const GB_Audio *audio);
uint64_t gb_audio_dropped_frames(const GB_Audio *audio);

/* Current 4-bit digital generator outputs used by CGB PCM12/PCM34. */
uint8_t gb_audio_pcm12(const GB_Audio *audio);
uint8_t gb_audio_pcm34(const GB_Audio *audio);

GB_Result gb_audio_read8(void *user, uint16_t address, uint8_t *value,
                         GB_Error *error);
GB_Result gb_audio_write8(void *user, uint16_t address, uint8_t value,
                          GB_Error *error);
GB_Result gb_audio_device_tick(void *user, uint32_t t_cycles,
                               GB_Error *error);

#ifdef __cplusplus
}
#endif

#endif /* GB_AUDIO_H */
