#include "gb_sdl3.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#define GB_SDL3_MAX_EMULATION_BATCH_T_CYCLES 4096u
#define GB_SDL3_AUDIO_PUMP_INTERVAL_NS 4000000ULL

static void platform_error(GB_Error *error, GB_Result code, const char *message)
{
    if (error == NULL) return;
    gb_error_clear(error);
    error->code = code;
    if (message != NULL) {
        const char *sdl_error = SDL_GetError();
        if (sdl_error != NULL && sdl_error[0] != '\0') {
            (void)snprintf(error->message, sizeof(error->message), "%s: %s",
                           message, sdl_error);
        } else {
            (void)snprintf(error->message, sizeof(error->message), "%s", message);
        }
    }
}

static void platform_state_error(GB_Error *error, GB_Result code, const char *message)
{
    if (error == NULL) return;
    gb_error_clear(error);
    error->code = code;
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static GB_Result require_platform(const GB_SDL3Platform *platform, GB_Error *error)
{
    if (platform == NULL) {
        platform_error(error, GB_RESULT_NULL_ARGUMENT,
                       "SDL3 platform pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!platform->initialized || platform->window == NULL ||
        platform->renderer == NULL || platform->texture == NULL) {
        platform_error(error, GB_RESULT_BAD_STATE,
                       "SDL3 platform is not initialized");
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static uint64_t ns_to_tcycles(uint64_t ns, uint64_t *remainder)
{
    const uint64_t hz = GB_EMULATOR_BASE_CLOCK_HZ;
    const uint64_t one_second = 1000000000ULL;
    uint64_t seconds = ns / one_second;
    uint64_t subsecond = ns % one_second;
    uint64_t whole = seconds * hz;
    uint64_t scaled = subsecond * hz + *remainder;
    whole += scaled / one_second;
    *remainder = scaled % one_second;
    return whole;
}

static bool event_can_wake_stop(const SDL_Event *event)
{
    if (event == NULL) return false;
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
        return !event->key.repeat;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        return true;
    default:
        return false;
    }
}

GB_Result gb_sdl3_init(GB_SDL3Platform *platform,
                       GB_Emulator *emulator,
                       GB_Error *error)
{
    gb_error_clear(error);
    if (platform == NULL || emulator == NULL) {
        platform_error(error, GB_RESULT_NULL_ARGUMENT,
                       "SDL3 platform requires platform and emulator objects");
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(platform, 0, sizeof(*platform));

    if (!SDL_SetAppMetadata("GBC Emulator", "0.1", "local.gbc_emulator")) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 application metadata initialization failed");
        return GB_RESULT_UNSUPPORTED;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 initialization failed");
        return GB_RESULT_UNSUPPORTED;
    }
    platform->sdl_initialized = true;

    platform->window = SDL_CreateWindow("GBC Emulator",
                                        GB_SDL3_WINDOW_WIDTH,
                                        GB_SDL3_WINDOW_HEIGHT,
                                        SDL_WINDOW_RESIZABLE);
    if (platform->window == NULL) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 window creation failed");
        (void)gb_sdl3_destroy(platform, NULL);
        return GB_RESULT_UNSUPPORTED;
    }

    platform->renderer = SDL_CreateRenderer(platform->window, NULL);
    if (platform->renderer == NULL) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 renderer creation failed");
        (void)gb_sdl3_destroy(platform, NULL);
        return GB_RESULT_UNSUPPORTED;
    }

    if (!SDL_SetRenderLogicalPresentation(platform->renderer,
                                          (int)GB_PPU_WIDTH,
                                          (int)GB_PPU_HEIGHT,
                                          SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 logical presentation setup failed");
        (void)gb_sdl3_destroy(platform, NULL);
        return GB_RESULT_UNSUPPORTED;
    }

    platform->texture = SDL_CreateTexture(platform->renderer,
                                          SDL_PIXELFORMAT_XRGB1555,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          (int)GB_PPU_WIDTH,
                                          (int)GB_PPU_HEIGHT);
    if (platform->texture == NULL) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 framebuffer texture creation failed");
        (void)gb_sdl3_destroy(platform, NULL);
        return GB_RESULT_UNSUPPORTED;
    }

    if (!SDL_SetTextureScaleMode(platform->texture, SDL_SCALEMODE_NEAREST)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 nearest-neighbor texture setup failed");
        (void)gb_sdl3_destroy(platform, NULL);
        return GB_RESULT_UNSUPPORTED;
    }

    GB_Result result = gb_input_sdl3_init(&platform->input, &emulator->input, error);
    if (result != GB_RESULT_OK) {
        (void)gb_sdl3_destroy(platform, NULL);
        return result;
    }
    platform->input_initialized = true;
    result = gb_input_sdl3_open_first_gamepad(&platform->input, error);
    if (result != GB_RESULT_OK) {
        (void)gb_sdl3_destroy(platform, NULL);
        return result;
    }

    result = gb_audio_sdl3_init(&platform->audio, &emulator->audio,
                                (int)GB_AUDIO_SAMPLE_RATE_HZ, error);
    if (result != GB_RESULT_OK) {
        (void)gb_sdl3_destroy(platform, NULL);
        return result;
    }
    platform->audio_initialized = true;

    platform->initialized = true;
    return GB_RESULT_OK;
}

GB_Result gb_sdl3_destroy(GB_SDL3Platform *platform, GB_Error *error)
{
    gb_error_clear(error);
    if (platform == NULL) {
        platform_error(error, GB_RESULT_NULL_ARGUMENT,
                       "SDL3 platform pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    GB_Result first_error = GB_RESULT_OK;
    GB_Error first_detail;
    gb_error_clear(&first_detail);

    if (platform->audio_initialized) {
        GB_Error child;
        gb_error_clear(&child);
        GB_Result result = gb_audio_sdl3_destroy(&platform->audio, &child);
        if (result != GB_RESULT_OK && first_error == GB_RESULT_OK) {
            first_error = result;
            first_detail = child;
        }
        platform->audio_initialized = false;
    }

    if (platform->input_initialized) {
        GB_Error child;
        gb_error_clear(&child);
        GB_Result result = gb_input_sdl3_destroy(&platform->input, &child);
        if (result != GB_RESULT_OK && first_error == GB_RESULT_OK) {
            first_error = result;
            first_detail = child;
        }
        platform->input_initialized = false;
    }

    if (platform->texture != NULL) {
        SDL_DestroyTexture(platform->texture);
        platform->texture = NULL;
    }
    if (platform->renderer != NULL) {
        SDL_DestroyRenderer(platform->renderer);
        platform->renderer = NULL;
    }
    if (platform->window != NULL) {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    if (platform->sdl_initialized) {
        SDL_Quit();
        platform->sdl_initialized = false;
    }

    memset(platform, 0, sizeof(*platform));
    if (first_error != GB_RESULT_OK && error != NULL) *error = first_detail;
    return first_error;
}

GB_Result gb_sdl3_process_events(GB_SDL3Platform *platform,
                                 GB_Emulator *emulator,
                                 bool *quit,
                                 GB_Error *error)
{
    gb_error_clear(error);
    if (quit != NULL) *quit = false;
    GB_Result result = require_platform(platform, error);
    if (result != GB_RESULT_OK) return result;
    if (emulator == NULL) {
        platform_error(error, GB_RESULT_NULL_ARGUMENT,
                       "Emulator pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            if (quit != NULL) *quit = true;
            continue;
        }

        bool handled = false;
        if (platform->input_initialized) {
            result = gb_input_sdl3_process_event(&platform->input,
                                                 &event,
                                                 &handled,
                                                 error);
            if (result != GB_RESULT_OK) return result;

            if (event_can_wake_stop(&event) && gb_emulator_is_stopped(emulator)) {
                result = gb_cpu_wake_from_stop(&emulator->cpu, error);
                if (result != GB_RESULT_OK && result != GB_RESULT_BAD_STATE) {
                    return result;
                }
            }
        }

        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && platform->input_initialized) {
            result = gb_input_sdl3_reset(&platform->input, error);
            if (result != GB_RESULT_OK) return result;
        }
        (void)handled;
    }

    if (platform->input_initialized) {
        result = gb_input_sdl3_sync_keyboard(&platform->input, error);
        if (result != GB_RESULT_OK) return result;
    }
    return GB_RESULT_OK;
}

GB_Result gb_sdl3_render(GB_SDL3Platform *platform,
                         GB_Emulator *emulator,
                         GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_platform(platform, error);
    if (result != GB_RESULT_OK) return result;
    if (emulator == NULL) {
        platform_error(error, GB_RESULT_NULL_ARGUMENT,
                       "Emulator pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!gb_ppu_frame_ready(&emulator->ppu)) return GB_RESULT_OK;

    const uint16_t *framebuffer = gb_ppu_framebuffer(&emulator->ppu);
    if (framebuffer == NULL) {
        platform_error(error, GB_RESULT_PPU_ERROR,
                       "PPU framebuffer is unavailable");
        return GB_RESULT_PPU_ERROR;
    }

    if (!SDL_UpdateTexture(platform->texture,
                           NULL,
                           framebuffer,
                           (int)(GB_PPU_WIDTH * sizeof(uint16_t)))) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 framebuffer upload failed");
        return GB_RESULT_UNSUPPORTED;
    }

    if (!SDL_RenderClear(platform->renderer)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 render clear failed");
        return GB_RESULT_UNSUPPORTED;
    }
    if (!SDL_RenderTexture(platform->renderer, platform->texture, NULL, NULL)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 framebuffer render failed");
        return GB_RESULT_UNSUPPORTED;
    }

    /* Feed the newly generated audio before RenderPresent(). Present can block
     * for a display refresh; the SDL audio device continues consuming the
     * stream during that time, so feeding after Present can create a gap. */
    result = gb_sdl3_pump_audio(platform, emulator, error);
    if (result != GB_RESULT_OK) return result;

    if (!SDL_RenderPresent(platform->renderer)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 present failed");
        return GB_RESULT_UNSUPPORTED;
    }

    gb_ppu_clear_frame_ready(&emulator->ppu);
    return GB_RESULT_OK;
}

GB_Result gb_sdl3_pump_audio(GB_SDL3Platform *platform,
                             GB_Emulator *emulator,
                             GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_platform(platform, error);
    if (result != GB_RESULT_OK) return result;
    if (emulator == NULL) {
        platform_error(error, GB_RESULT_NULL_ARGUMENT,
                       "Emulator pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!platform->audio_initialized) return GB_RESULT_OK;
    return gb_audio_sdl3_pump(&platform->audio, &emulator->audio, 1024u, error);
}

GB_Result gb_sdl3_run(GB_SDL3Platform *platform,
                      GB_Emulator *emulator,
                      GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_platform(platform, error);
    if (result != GB_RESULT_OK) return result;
    if (emulator == NULL) {
        platform_error(error, GB_RESULT_NULL_ARGUMENT,
                       "Emulator pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    bool quit = false;
    uint64_t last_ns = SDL_GetTicksNS();
    uint64_t fractional_ns = 0u;
    /* The CPU advances in whole instructions, so one instruction can
     * legitimately overshoot the current wall-clock budget by a few T-cycles.
     * A signed budget records that small debt instead of treating it as a
     * fatal runtime error. */
    int64_t cycle_budget = 0;
    uint64_t last_audio_ns = last_ns;
    const uint64_t max_budget = (uint64_t)GB_EMULATOR_FRAME_T_CYCLES *
                                (uint64_t)GB_EMULATOR_MAX_CATCHUP_FRAMES;

    while (!quit) {
        /* Always service the OS/event queue before doing another emulation
         * batch. A catch-up budget can contain hundreds of thousands of
         * T-cycles, so processing the entire budget in one tight loop would
         * make the window appear frozen when the emulator is under load. */
        result = gb_sdl3_process_events(platform, emulator, &quit, error);
        if (result != GB_RESULT_OK) return result;
        if (quit) break;

        uint64_t now_ns = SDL_GetTicksNS();
        uint64_t elapsed_ns = now_ns >= last_ns ? now_ns - last_ns : 0u;
        last_ns = now_ns;
        if (elapsed_ns > 100000000ULL) {
            elapsed_ns = 100000000ULL;
        }

        cycle_budget += (int64_t)ns_to_tcycles(elapsed_ns, &fractional_ns);
        if (cycle_budget > (int64_t)max_budget) {
            cycle_budget = (int64_t)max_budget;
        }

        bool stopped = false;
        bool debug_break = false;

        /* Never spend more than about 1 ms of emulated DMG time without
         * returning to SDL event processing. This keeps keyboard/window input
         * responsive even when catch-up is required. */
        while (cycle_budget >= 4) {
            int64_t batch_budget = cycle_budget;
            if (batch_budget > (int64_t)GB_SDL3_MAX_EMULATION_BATCH_T_CYCLES) {
                batch_budget = (int64_t)GB_SDL3_MAX_EMULATION_BATCH_T_CYCLES;
            }

            int64_t batch_remaining = batch_budget;
            while (batch_remaining >= 4) {
                uint32_t consumed = 0u;
                result = gb_emulator_step(emulator, &consumed, error);
                if (result == GB_RESULT_DEBUG_BREAK) {
                    debug_break = true;
                    break;
                }
                if (result != GB_RESULT_OK) return result;

                if (consumed == 0u) {
                    stopped = gb_emulator_is_stopped(emulator);
                    break;
                }
                /* Instructions are indivisible. If an instruction consumes
                 * more T-cycles than remain in this batch, let the signed
                 * cycle budget go slightly negative and repay that debt on
                 * the next host-loop iteration. This is normal instruction
                 * granularity, not an emulator failure. */
                cycle_budget -= (int64_t)consumed;
                batch_remaining -= (int64_t)consumed;

                result = gb_sdl3_render(platform, emulator, error);
                if (result != GB_RESULT_OK) return result;
            }

            if (debug_break) {
                return GB_RESULT_OK;
            }

            /* Break the emulation budget into small chunks and service SDL
             * between them. This also lets STOP/input wake-up take effect
             * without waiting for a full frame catch-up window. */
            result = gb_sdl3_process_events(platform, emulator, &quit, error);
            if (result != GB_RESULT_OK) return result;
            if (quit) break;
            if (stopped) break;
        }

        if (debug_break) {
            return GB_RESULT_OK;
        }

        now_ns = SDL_GetTicksNS();
        if (now_ns - last_audio_ns >= GB_SDL3_AUDIO_PUMP_INTERVAL_NS) {
            result = gb_sdl3_pump_audio(platform, emulator, error);
            if (result != GB_RESULT_OK) return result;
            last_audio_ns = now_ns;
        }

        if (!quit && (stopped || cycle_budget < 4)) {
            SDL_DelayNS(250000ULL);
        }
    }

    return GB_RESULT_OK;
}
