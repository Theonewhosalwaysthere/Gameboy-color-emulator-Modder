#include "gb_sdl3.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL_main.h>

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
    platform->window_scale = GB_SDL3_WINDOW_SCALE;
    platform->menu_screen = GB_SDL3_MENU_NONE;

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


static const char *menu_button_name(GB_InputButton button)
{
    switch (button) {
    case GB_INPUT_BUTTON_A: return "A";
    case GB_INPUT_BUTTON_B: return "B";
    case GB_INPUT_BUTTON_SELECT: return "SELECT";
    case GB_INPUT_BUTTON_START: return "START";
    case GB_INPUT_BUTTON_RIGHT: return "RIGHT";
    case GB_INPUT_BUTTON_LEFT: return "LEFT";
    case GB_INPUT_BUTTON_UP: return "UP";
    case GB_INPUT_BUTTON_DOWN: return "DOWN";
    default: return "?";
    }
}

static const char *menu_scancode_name(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN) return "Unbound";
    const char *name = SDL_GetScancodeName(scancode);
    return (name != NULL && name[0] != '\0') ? name : "Unknown";
}

static GB_Result gb_sdl3_apply_window_scale(GB_SDL3Platform *platform,
                                             int scale,
                                             GB_Error *error)
{
    if (scale < 1 || scale > 6) {
        platform_state_error(error, GB_RESULT_INVALID_ARGUMENT,
                             "Window scale must be between 1x and 6x");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    if (!SDL_SetWindowSize(platform->window,
                           GB_PPU_WIDTH * scale,
                           GB_PPU_HEIGHT * scale)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "Could not change the window scale");
        return GB_RESULT_UNSUPPORTED;
    }
    platform->window_scale = scale;
    return GB_RESULT_OK;
}

static GB_Result gb_sdl3_set_paused(GB_SDL3Platform *platform,
                                    bool paused,
                                    GB_Error *error)
{
    platform->paused = paused;
    platform->menu_screen = paused ? GB_SDL3_MENU_MAIN : GB_SDL3_MENU_NONE;
    platform->menu_selection = 0;
    if (platform->audio_initialized) {
        if (paused) {
            return gb_audio_sdl3_pause(&platform->audio, true, error);
        }
        if (!platform->audio_muted) {
            return gb_audio_sdl3_pause(&platform->audio, false, error);
        }
    }
    return GB_RESULT_OK;
}

static GB_Result gb_sdl3_handle_menu_event(GB_SDL3Platform *platform,
                                           const SDL_Event *event,
                                           bool *quit,
                                           bool *handled,
                                           GB_Error *error)
{
    if (event == NULL || quit == NULL || handled == NULL) {
        platform_state_error(error, GB_RESULT_NULL_ARGUMENT,
                             "Pause-menu event arguments are NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    *handled = false;
    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return GB_RESULT_OK;
    }

    SDL_Scancode scancode = event->key.scancode;
    *handled = true;

    if (platform->menu_screen == GB_SDL3_MENU_BINDING) {
        if (scancode == SDL_SCANCODE_ESCAPE) {
            platform->menu_screen = GB_SDL3_MENU_KEYBINDS;
            return GB_RESULT_OK;
        }

        SDL_Scancode previous =
            platform->input.bindings.keyboard[platform->binding_button];
        for (unsigned i = 0u; i < GB_INPUT_BUTTON_COUNT; ++i) {
            if ((GB_InputButton)i == platform->binding_button) continue;
            if (platform->input.bindings.keyboard[i] == scancode) {
                GB_Result result = gb_input_sdl3_set_keyboard_binding(
                    &platform->input,
                    (GB_InputButton)i,
                    previous,
                    error);
                if (result != GB_RESULT_OK) return result;
            }
        }
        GB_Result result = gb_input_sdl3_set_keyboard_binding(
            &platform->input, platform->binding_button, scancode, error);
        if (result != GB_RESULT_OK) return result;
        platform->menu_screen = GB_SDL3_MENU_KEYBINDS;
        return GB_RESULT_OK;
    }

    if (scancode == SDL_SCANCODE_ESCAPE) {
        if (platform->menu_screen == GB_SDL3_MENU_MAIN) {
            return gb_sdl3_set_paused(platform, false, error);
        }
        platform->menu_screen = GB_SDL3_MENU_MAIN;
        platform->menu_selection = 0;
        return GB_RESULT_OK;
    }

    int item_count = 0;
    if (platform->menu_screen == GB_SDL3_MENU_MAIN) {
        item_count = 4; /* Resume, Keybinds, Settings, Exit */
    } else if (platform->menu_screen == GB_SDL3_MENU_KEYBINDS) {
        item_count = (int)GB_INPUT_BUTTON_COUNT + 1; /* + Back */
    } else if (platform->menu_screen == GB_SDL3_MENU_SETTINGS) {
        item_count = 3; /* Mute, Window scale, Back */
    }

    if (item_count == 0) {
        platform->menu_screen = GB_SDL3_MENU_MAIN;
        platform->menu_selection = 0;
        return GB_RESULT_OK;
    }

    if (scancode == SDL_SCANCODE_UP || scancode == SDL_SCANCODE_W) {
        platform->menu_selection =
            (platform->menu_selection + item_count - 1) % item_count;
        return GB_RESULT_OK;
    }
    if (scancode == SDL_SCANCODE_DOWN || scancode == SDL_SCANCODE_S) {
        platform->menu_selection =
            (platform->menu_selection + 1) % item_count;
        return GB_RESULT_OK;
    }

    if (scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_RIGHT) {
        if (platform->menu_screen == GB_SDL3_MENU_SETTINGS &&
            platform->menu_selection == 1) {
            int delta = scancode == SDL_SCANCODE_RIGHT ? 1 : -1;
            int next = platform->window_scale + delta;
            if (next < 1) next = 6;
            if (next > 6) next = 1;
            return gb_sdl3_apply_window_scale(platform, next, error);
        }
        return GB_RESULT_OK;
    }

    if (scancode != SDL_SCANCODE_RETURN && scancode != SDL_SCANCODE_KP_ENTER &&
        scancode != SDL_SCANCODE_SPACE) {
        return GB_RESULT_OK;
    }

    if (platform->menu_screen == GB_SDL3_MENU_MAIN) {
        switch (platform->menu_selection) {
        case 0:
            return gb_sdl3_set_paused(platform, false, error);
        case 1:
            platform->menu_screen = GB_SDL3_MENU_KEYBINDS;
            platform->menu_selection = 0;
            return GB_RESULT_OK;
        case 2:
            platform->menu_screen = GB_SDL3_MENU_SETTINGS;
            platform->menu_selection = 0;
            return GB_RESULT_OK;
        case 3:
            *quit = true;
            platform->paused = false;
            platform->menu_screen = GB_SDL3_MENU_NONE;
            return GB_RESULT_OK;
        default:
            return GB_RESULT_OK;
        }
    }

    if (platform->menu_screen == GB_SDL3_MENU_KEYBINDS) {
        if (platform->menu_selection == (int)GB_INPUT_BUTTON_COUNT) {
            platform->menu_screen = GB_SDL3_MENU_MAIN;
            platform->menu_selection = 0;
            return GB_RESULT_OK;
        }
        platform->binding_button = (GB_InputButton)platform->menu_selection;
        platform->menu_screen = GB_SDL3_MENU_BINDING;
        return GB_RESULT_OK;
    }

    if (platform->menu_screen == GB_SDL3_MENU_SETTINGS) {
        switch (platform->menu_selection) {
        case 0:
            platform->audio_muted = !platform->audio_muted;
            if (platform->audio_initialized && platform->audio_muted) {
                return gb_audio_sdl3_pause(&platform->audio, true, error);
            }
            /* While the emulator is paused the stream stays paused even when
             * mute is turned off; Resume controls when playback starts again. */
            return GB_RESULT_OK;
        case 1: {
            int next = platform->window_scale + 1;
            if (next > 6) next = 1;
            return gb_sdl3_apply_window_scale(platform, next, error);
        }
        case 2:
            platform->menu_screen = GB_SDL3_MENU_MAIN;
            platform->menu_selection = 0;
            return GB_RESULT_OK;
        default:
            return GB_RESULT_OK;
        }
    }

    return GB_RESULT_OK;
}

static void gb_sdl3_draw_menu_text(GB_SDL3Platform *platform,
                                   float x,
                                   float y,
                                   const char *text,
                                   bool selected)
{
    if (selected) {
        SDL_SetRenderDrawColor(platform->renderer, 255, 255, 255, 255);
    } else {
        SDL_SetRenderDrawColor(platform->renderer, 220, 220, 220, 255);
    }
    (void)SDL_RenderDebugText(platform->renderer, x, y, text);
}

static void gb_sdl3_draw_pause_menu(GB_SDL3Platform *platform)
{
    SDL_FRect overlay = {0.0f, 0.0f, (float)GB_PPU_WIDTH, (float)GB_PPU_HEIGHT};
    SDL_FRect panel = {10.0f, 8.0f, 140.0f, 128.0f};
    SDL_SetRenderDrawBlendMode(platform->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(platform->renderer, 0, 0, 0, 165);
    (void)SDL_RenderFillRect(platform->renderer, &overlay);
    SDL_SetRenderDrawColor(platform->renderer, 24, 28, 38, 245);
    (void)SDL_RenderFillRect(platform->renderer, &panel);

    SDL_SetRenderDrawColor(platform->renderer, 255, 255, 255, 255);
    (void)SDL_RenderDebugText(platform->renderer, 18.0f, 14.0f, "PAUSED");

    char line[128];
    if (platform->menu_screen == GB_SDL3_MENU_MAIN) {
        static const char *items[] = {"Resume", "Keybinds", "Settings", "Exit"};
        for (int i = 0; i < 4; ++i) {
            if (i == platform->menu_selection) {
                SDL_SetRenderDrawColor(platform->renderer, 70, 110, 180, 255);
                SDL_FRect highlight = {15.0f, 30.0f + (float)i * 16.0f,
                                       130.0f, 13.0f};
                (void)SDL_RenderFillRect(platform->renderer, &highlight);
            }
            gb_sdl3_draw_menu_text(platform, 20.0f,
                                   33.0f + (float)i * 16.0f,
                                   items[i], i == platform->menu_selection);
        }
        SDL_SetRenderDrawColor(platform->renderer, 170, 170, 170, 255);
        (void)SDL_RenderDebugText(platform->renderer, 18.0f, 112.0f,
                                  "UP/DOWN + ENTER");
        (void)SDL_RenderDebugText(platform->renderer, 18.0f, 122.0f,
                                  "ESC: RESUME");
    } else if (platform->menu_screen == GB_SDL3_MENU_KEYBINDS) {
        SDL_SetRenderDrawColor(platform->renderer, 170, 170, 170, 255);
        (void)SDL_RenderDebugText(platform->renderer, 18.0f, 25.0f,
                                  "SELECT A BUTTON");
        for (unsigned i = 0u; i < GB_INPUT_BUTTON_COUNT; ++i) {
            (void)snprintf(line, sizeof(line), "%-6s %s",
                           menu_button_name((GB_InputButton)i),
                           menu_scancode_name(platform->input.bindings.keyboard[i]));
            if ((int)i == platform->menu_selection) {
                SDL_SetRenderDrawColor(platform->renderer, 70, 110, 180, 255);
                SDL_FRect highlight = {15.0f, 33.0f + (float)i * 10.0f,
                                       130.0f, 9.0f};
                (void)SDL_RenderFillRect(platform->renderer, &highlight);
            }
            gb_sdl3_draw_menu_text(platform, 18.0f,
                                   34.0f + (float)i * 10.0f,
                                   line, (int)i == platform->menu_selection);
        }
        if (platform->menu_selection == (int)GB_INPUT_BUTTON_COUNT) {
            SDL_SetRenderDrawColor(platform->renderer, 70, 110, 180, 255);
            SDL_FRect highlight = {15.0f, 113.0f, 130.0f, 10.0f};
            (void)SDL_RenderFillRect(platform->renderer, &highlight);
        }
        gb_sdl3_draw_menu_text(platform, 18.0f, 114.0f, "Back",
                               platform->menu_selection == (int)GB_INPUT_BUTTON_COUNT);
        SDL_SetRenderDrawColor(platform->renderer, 170, 170, 170, 255);
        (void)SDL_RenderDebugText(platform->renderer, 18.0f, 126.0f,
                                  "ENTER: CHANGE  ESC: BACK");
    } else if (platform->menu_screen == GB_SDL3_MENU_BINDING) {
        (void)snprintf(line, sizeof(line), "SET %s", menu_button_name(platform->binding_button));
        SDL_SetRenderDrawColor(platform->renderer, 255, 255, 255, 255);
        (void)SDL_RenderDebugText(platform->renderer, 25.0f, 42.0f, line);
        (void)SDL_RenderDebugText(platform->renderer, 25.0f, 58.0f, "PRESS A KEY");
        (void)SDL_RenderDebugText(platform->renderer, 25.0f, 68.0f, "ESC: CANCEL");
    } else if (platform->menu_screen == GB_SDL3_MENU_SETTINGS) {
        (void)snprintf(line, sizeof(line), "Mute Audio: %s",
                       platform->audio_muted ? "ON" : "OFF");
        if (platform->menu_selection == 0) {
            SDL_SetRenderDrawColor(platform->renderer, 70, 110, 180, 255);
            SDL_FRect highlight = {15.0f, 34.0f, 130.0f, 13.0f};
            (void)SDL_RenderFillRect(platform->renderer, &highlight);
        }
        gb_sdl3_draw_menu_text(platform, 20.0f, 37.0f, line,
                               platform->menu_selection == 0);

        (void)snprintf(line, sizeof(line), "Window Scale: %dx", platform->window_scale);
        if (platform->menu_selection == 1) {
            SDL_SetRenderDrawColor(platform->renderer, 70, 110, 180, 255);
            SDL_FRect highlight = {15.0f, 50.0f, 130.0f, 13.0f};
            (void)SDL_RenderFillRect(platform->renderer, &highlight);
        }
        gb_sdl3_draw_menu_text(platform, 20.0f, 53.0f, line,
                               platform->menu_selection == 1);

        if (platform->menu_selection == 2) {
            SDL_SetRenderDrawColor(platform->renderer, 70, 110, 180, 255);
            SDL_FRect highlight = {15.0f, 66.0f, 130.0f, 13.0f};
            (void)SDL_RenderFillRect(platform->renderer, &highlight);
        }
        gb_sdl3_draw_menu_text(platform, 20.0f, 69.0f, "Back",
                               platform->menu_selection == 2);
        SDL_SetRenderDrawColor(platform->renderer, 170, 170, 170, 255);
        (void)SDL_RenderDebugText(platform->renderer, 18.0f, 108.0f,
                                  "LEFT/RIGHT: SCALE");
        (void)SDL_RenderDebugText(platform->renderer, 18.0f, 118.0f,
                                  "ENTER: TOGGLE");
    }

    SDL_SetRenderDrawBlendMode(platform->renderer, SDL_BLENDMODE_NONE);
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

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_ESCAPE) {
            if (platform->menu_screen == GB_SDL3_MENU_NONE) {
                result = gb_sdl3_set_paused(platform, true, error);
            } else {
                bool handled = false;
                result = gb_sdl3_handle_menu_event(platform, &event, quit,
                                                   &handled, error);
            }
            if (result != GB_RESULT_OK) return result;
            continue;
        }

        if (platform->paused) {
            bool handled = false;
            result = gb_sdl3_handle_menu_event(platform, &event, quit,
                                               &handled, error);
            if (result != GB_RESULT_OK) return result;
            if (handled) continue;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && platform->input_initialized) {
                result = gb_input_sdl3_reset(&platform->input, error);
                if (result != GB_RESULT_OK) return result;
            }
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

    if (!platform->paused && platform->input_initialized) {
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

    bool new_frame = gb_ppu_frame_ready(&emulator->ppu);
    if (new_frame) {
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
    }

    if (!new_frame && !platform->paused) {
        return GB_RESULT_OK;
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

    if (platform->paused) {
        gb_sdl3_draw_pause_menu(platform);
    }

    if (!SDL_RenderPresent(platform->renderer)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 present failed");
        return GB_RESULT_UNSUPPORTED;
    }

    if (new_frame) {
        gb_ppu_clear_frame_ready(&emulator->ppu);
    }
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

        if (platform->paused) {
            cycle_budget = 0;
            result = gb_sdl3_render(platform, emulator, error);
            if (result != GB_RESULT_OK) return result;
            SDL_DelayNS(10000000ULL);
            continue;
        }

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
