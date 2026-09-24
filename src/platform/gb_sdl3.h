#ifndef GB_SDL3_H
#define GB_SDL3_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "../audio/gb_audio_sdl3.h"
#include "../emulator/gb_emulator.h"
#include "../input/gb_input_sdl3.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_SDL3_WINDOW_SCALE 4
#define GB_SDL3_WINDOW_WIDTH  (GB_PPU_WIDTH * GB_SDL3_WINDOW_SCALE)
#define GB_SDL3_WINDOW_HEIGHT (GB_PPU_HEIGHT * GB_SDL3_WINDOW_SCALE)

typedef struct GB_SDL3Platform {
    bool initialized;
    bool sdl_initialized;

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    GB_InputSDL3 input;
    GB_AudioSDL3 audio;
    bool input_initialized;
    bool audio_initialized;
} GB_SDL3Platform;

GB_Result gb_sdl3_init(GB_SDL3Platform *platform,
                       GB_Emulator *emulator,
                       GB_Error *error);
GB_Result gb_sdl3_destroy(GB_SDL3Platform *platform, GB_Error *error);

GB_Result gb_sdl3_process_events(GB_SDL3Platform *platform,
                                 GB_Emulator *emulator,
                                 bool *quit,
                                 GB_Error *error);

GB_Result gb_sdl3_render(GB_SDL3Platform *platform,
                         GB_Emulator *emulator,
                         GB_Error *error);

GB_Result gb_sdl3_pump_audio(GB_SDL3Platform *platform,
                             GB_Emulator *emulator,
                             GB_Error *error);

/* Run the synchronized SDL3 host loop. Hardware time is derived from the
 * Game Boy's 4.194304 MHz base clock. The loop deliberately caps catch-up so
 * a paused/debugged process cannot turn a long stall into an unbounded burst. */
GB_Result gb_sdl3_run(GB_SDL3Platform *platform,
                      GB_Emulator *emulator,
                      GB_Error *error);

#ifdef __cplusplus
}
#endif

#endif /* GB_SDL3_H */
