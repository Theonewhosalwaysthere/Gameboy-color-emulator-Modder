#ifndef GB_AUDIO_SDL3_H
#define GB_AUDIO_SDL3_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL_audio.h>

#include "gb_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GB_AudioSDL3 {
    bool initialized;
    bool audio_subsystem_owned;
    SDL_AudioStream *stream;
    SDL_AudioSpec input_spec;
    SDL_AudioDeviceID device;
    uint32_t submitted_frames;
} GB_AudioSDL3;

GB_Result gb_audio_sdl3_init(GB_AudioSDL3 *output, GB_Audio *audio,
                             int sample_rate, GB_Error *error);
GB_Result gb_audio_sdl3_pump(GB_AudioSDL3 *output, GB_Audio *audio,
                             uint32_t max_frames, GB_Error *error);
GB_Result gb_audio_sdl3_pause(GB_AudioSDL3 *output, bool pause, GB_Error *error);
GB_Result gb_audio_sdl3_destroy(GB_AudioSDL3 *output, GB_Error *error);

#ifdef __cplusplus
}
#endif

#endif /* GB_AUDIO_SDL3_H */
