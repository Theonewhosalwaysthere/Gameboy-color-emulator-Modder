#include "gb_sdl3.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#define GB_SDL3_MAX_EMULATION_BATCH_T_CYCLES 4096u
#define GB_SDL3_AUDIO_PUMP_INTERVAL_NS 16000000ULL

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

static void set_rom_window_title(GB_SDL3Platform *platform,
                                 const GB_Emulator *emulator)
{
    if (platform == NULL || platform->window == NULL ||
        emulator == NULL || !emulator->cartridge.loaded) {
        return;
    }

    char title[128];
    (void)snprintf(title, sizeof(title),
                   "GBC Emulator - %.16s (%s)",
                   (const char *)emulator->cartridge.title,
                   gb_cartridge_cgb_support_name(emulator->cartridge.cgb_support));
    (void)SDL_SetWindowTitle(platform->window, title);
}

static void set_waiting_window_title(GB_SDL3Platform *platform)
{
    if (platform != NULL && platform->window != NULL) {
        (void)SDL_SetWindowTitle(platform->window,
                                 "GBC Emulator - Drag a .gb/.gbc ROM onto this window");
    }
}

static GB_Result prepare_loaded_rom_input(GB_SDL3Platform *platform,
                                          GB_Emulator *emulator,
                                          GB_Error *error)
{
    gb_error_clear(error);
    if (platform == NULL || emulator == NULL) {
        platform_error(error, GB_RESULT_NULL_ARGUMENT,
                       "ROM input setup requires platform and emulator objects");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!emulator->cartridge.loaded) {
        return GB_RESULT_OK;
    }

    GB_Result result;
    if (!platform->input_initialized) {
        result = gb_input_sdl3_init(&platform->input, &emulator->input, error);
        if (result != GB_RESULT_OK) return result;
        platform->input_initialized = true;

        result = gb_input_sdl3_open_first_gamepad(&platform->input, error);
        if (result != GB_RESULT_OK) return result;
    } else {
        result = gb_input_sdl3_reset(&platform->input, error);
        if (result != GB_RESULT_OK) return result;
    }

    set_rom_window_title(platform, emulator);
    platform->menu_state = GB_SDL3_MENU_GAMEPLAY;
    platform->menu_selection = 0;
    platform->keybind_selection = 0;
    platform->waiting_for_keybind = false;
    platform->timing_reset_requested = true;
    return GB_RESULT_OK;
}


static const char *const menu_items[] = {
    "Resume",
    "Keybinds",
    "Settings",
    "Exit"
};

static const char *const keybind_names[] = {
    "A",
    "B",
    "Select",
    "Start",
    "Right",
    "Left",
    "Up",
    "Down"
};

static bool is_navigation_key(const SDL_Event *event)
{
    if (event == NULL || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    switch (event->key.scancode) {
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_RIGHT:
        return true;
    default:
        return false;
    }
}

static void set_audio_output_state(GB_SDL3Platform *platform, bool paused,
                                   GB_Error *error)
{
    if (platform == NULL || !platform->audio_initialized) return;
    bool should_pause = paused || platform->audio_muted;
    GB_Error ignored;
    gb_error_clear(&ignored);
    GB_Result result = gb_audio_sdl3_pause(&platform->audio, should_pause, &ignored);
    if (result != GB_RESULT_OK && error != NULL) {
        *error = ignored;
    }
}

static void clear_held_game_input(GB_SDL3Platform *platform, GB_Error *error)
{
    if (platform == NULL || !platform->input_initialized) return;
    (void)gb_input_sdl3_reset(&platform->input, error);
}

static void set_paused(GB_SDL3Platform *platform, bool paused, GB_Error *error)
{
    if (platform == NULL) return;
    platform->menu_state = paused ? GB_SDL3_MENU_PAUSE : GB_SDL3_MENU_GAMEPLAY;
    platform->menu_selection = 0;
    platform->keybind_selection = 0;
    platform->waiting_for_keybind = false;
    clear_held_game_input(platform, error);
    if (error != NULL && error->code != GB_RESULT_OK) return;
    set_audio_output_state(platform, paused, error);
}

static const char *scancode_name(SDL_Scancode scancode)
{
    const char *name = SDL_GetScancodeName(scancode);
    if (name == NULL || name[0] == '\0') return "Unknown";
    return name;
}

static GB_Result apply_window_scale(GB_SDL3Platform *platform, int scale,
                                    GB_Error *error)
{
    if (platform == NULL || platform->window == NULL) {
        platform_state_error(error, GB_RESULT_BAD_STATE,
                             "SDL3 window is unavailable");
        return GB_RESULT_BAD_STATE;
    }
    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;
    if (!SDL_SetWindowSize(platform->window,
                           (int)GB_PPU_WIDTH * scale,
                           (int)GB_PPU_HEIGHT * scale)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 window resize failed");
        return GB_RESULT_UNSUPPORTED;
    }
    platform->window_scale = scale;
    return GB_RESULT_OK;
}

static GB_Result handle_menu_event(GB_SDL3Platform *platform,
                                   GB_Emulator *emulator,
                                   const SDL_Event *event,
                                   bool *handled,
                                   bool *quit,
                                   GB_Error *error)
{
    if (handled != NULL) *handled = false;
    if (platform == NULL || emulator == NULL || event == NULL) {
        platform_state_error(error, GB_RESULT_NULL_ARGUMENT,
                             "Pause-menu event arguments are invalid");
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return GB_RESULT_OK;
    }

    SDL_Scancode key = event->key.scancode;

    if (platform->menu_state == GB_SDL3_MENU_GAMEPLAY) {
        if (key == SDL_SCANCODE_ESCAPE && emulator->cartridge.loaded) {
            if (handled != NULL) *handled = true;
            set_paused(platform, true, error);
            return error != NULL && error->code != GB_RESULT_OK ? error->code : GB_RESULT_OK;
        }
        return GB_RESULT_OK;
    }

    if (platform->waiting_for_keybind) {
        if (key == SDL_SCANCODE_ESCAPE) {
            platform->waiting_for_keybind = false;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (is_navigation_key(event)) {
            /* Navigation keys are valid physical keys for the emulator too,
             * but keeping them reserved here prevents accidental loss of the
             * menu controls while rebinding. */
        }
        GB_Result result = gb_input_sdl3_set_keyboard_binding(
            &platform->input,
            (GB_InputButton)platform->keybind_selection,
            key,
            error);
        if (result != GB_RESULT_OK) return result;
        platform->waiting_for_keybind = false;
        if (handled != NULL) *handled = true;
        return GB_RESULT_OK;
    }

    switch (platform->menu_state) {
    case GB_SDL3_MENU_PAUSE:
        if (key == SDL_SCANCODE_ESCAPE) {
            if (handled != NULL) *handled = true;
            set_paused(platform, false, error);
            return error != NULL && error->code != GB_RESULT_OK ? error->code : GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_UP) {
            platform->menu_selection = (platform->menu_selection + 3) % 4;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_DOWN) {
            platform->menu_selection = (platform->menu_selection + 1) % 4;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_RETURN) {
            if (handled != NULL) *handled = true;
            switch (platform->menu_selection) {
            case 0:
                set_paused(platform, false, error);
                break;
            case 1:
                platform->menu_state = GB_SDL3_MENU_KEYBINDS;
                platform->keybind_selection = 0;
                break;
            case 2:
                platform->menu_state = GB_SDL3_MENU_SETTINGS;
                platform->menu_selection = 0;
                break;
            case 3:
                if (quit != NULL) *quit = true;
                break;
            default:
                break;
            }
            return error != NULL && error->code != GB_RESULT_OK ? error->code : GB_RESULT_OK;
        }
        break;

    case GB_SDL3_MENU_KEYBINDS:
        if (key == SDL_SCANCODE_ESCAPE) {
            platform->menu_state = GB_SDL3_MENU_PAUSE;
            platform->keybind_selection = 0;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_UP) {
            platform->keybind_selection =
                (platform->keybind_selection + GB_INPUT_BUTTON_COUNT - 1) % GB_INPUT_BUTTON_COUNT;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_DOWN) {
            platform->keybind_selection =
                (platform->keybind_selection + 1) % GB_INPUT_BUTTON_COUNT;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_RETURN) {
            platform->waiting_for_keybind = true;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        break;

    case GB_SDL3_MENU_SETTINGS:
        if (key == SDL_SCANCODE_ESCAPE) {
            platform->menu_state = GB_SDL3_MENU_PAUSE;
            platform->menu_selection = 0;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_UP) {
            platform->menu_selection = (platform->menu_selection + 2) % 3;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_DOWN) {
            platform->menu_selection = (platform->menu_selection + 1) % 3;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if ((key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT) &&
            platform->menu_selection == 1) {
            int delta = key == SDL_SCANCODE_RIGHT ? 1 : -1;
            GB_Result result = apply_window_scale(platform,
                                                   platform->window_scale + delta,
                                                   error);
            if (result != GB_RESULT_OK) return result;
            if (handled != NULL) *handled = true;
            return GB_RESULT_OK;
        }
        if (key == SDL_SCANCODE_RETURN) {
            if (handled != NULL) *handled = true;
            if (platform->menu_selection == 0) {
                platform->audio_muted = !platform->audio_muted;
                set_audio_output_state(platform, true, error);
                if (error != NULL && error->code != GB_RESULT_OK) return error->code;
                set_audio_output_state(platform, false, error);
            } else if (platform->menu_selection == 1) {
                int next_scale = platform->window_scale >= 6 ? 1 : platform->window_scale + 1;
                GB_Result result = apply_window_scale(platform, next_scale, error);
                if (result != GB_RESULT_OK) return result;
            } else {
                platform->menu_state = GB_SDL3_MENU_PAUSE;
                platform->menu_selection = 0;
            }
            return error != NULL && error->code != GB_RESULT_OK ? error->code : GB_RESULT_OK;
        }
        break;

    default:
        platform->menu_state = GB_SDL3_MENU_GAMEPLAY;
        break;
    }

    if (handled != NULL) *handled = true;
    return GB_RESULT_OK;
}

static void render_text(GB_SDL3Platform *platform, float x, float y,
                        const char *text)
{
    if (platform == NULL || platform->renderer == NULL || text == NULL) return;
    (void)SDL_RenderDebugText(platform->renderer, x, y, text);
}

static void render_menu_overlay(GB_SDL3Platform *platform)
{
    if (platform == NULL || platform->renderer == NULL ||
        platform->menu_state == GB_SDL3_MENU_GAMEPLAY) return;

    SDL_SetRenderDrawBlendMode(platform->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(platform->renderer, 0, 0, 0, 210);
    SDL_FRect overlay = {0.0f, 0.0f, (float)GB_PPU_WIDTH, (float)GB_PPU_HEIGHT};
    (void)SDL_RenderFillRect(platform->renderer, &overlay);

    SDL_SetRenderDrawColor(platform->renderer, 255, 255, 255, 255);

    if (platform->menu_state == GB_SDL3_MENU_PAUSE) {
        render_text(platform, 10.0f, 8.0f, "PAUSED");
        for (int i = 0; i < 4; ++i) {
            char line[64];
            (void)snprintf(line, sizeof(line), "%c %s",
                           i == platform->menu_selection ? '>' : ' ',
                           menu_items[i]);
            render_text(platform, 16.0f, 30.0f + (float)(i * 14), line);
        }
        render_text(platform, 16.0f, 92.0f, "UP/DOWN + ENTER");
        render_text(platform, 16.0f, 102.0f, "ESC: CLOSE");
    } else if (platform->menu_state == GB_SDL3_MENU_KEYBINDS) {
        render_text(platform, 6.0f, 5.0f, "KEYBINDS");
        for (int i = 0; i < (int)GB_INPUT_BUTTON_COUNT; ++i) {
            char line[64];
            const char *key_name = scancode_name(platform->input.bindings.keyboard[i]);
            if (i == platform->keybind_selection) {
                (void)snprintf(line, sizeof(line), "> %-6s %s%s",
                               keybind_names[i], key_name,
                               platform->waiting_for_keybind ? " ?" : "");
            } else {
                (void)snprintf(line, sizeof(line), "  %-6s %s", keybind_names[i], key_name);
            }
            render_text(platform, 3.0f, 17.0f + (float)(i * 14), line);
        }
        if (platform->waiting_for_keybind) {
            render_text(platform, 3.0f, 132.0f, "PRESS KEY / ESC BACK");
        } else {
            render_text(platform, 3.0f, 132.0f, "ENTER CHANGE / ESC BACK");
        }
    } else if (platform->menu_state == GB_SDL3_MENU_SETTINGS) {
        render_text(platform, 10.0f, 8.0f, "SETTINGS");
        char line0[64];
        char line1[64];
        (void)snprintf(line0, sizeof(line0), "%c Audio: %s",
                       platform->menu_selection == 0 ? '>' : ' ',
                       platform->audio_muted ? "Muted" : "On");
        (void)snprintf(line1, sizeof(line1), "%c Scale: %dx",
                       platform->menu_selection == 1 ? '>' : ' ',
                       platform->window_scale);
        render_text(platform, 10.0f, 30.0f, line0);
        render_text(platform, 10.0f, 44.0f, line1);
        char line2[64];
        (void)snprintf(line2, sizeof(line2), "%c Back",
                       platform->menu_selection == 2 ? '>' : ' ');
        render_text(platform, 10.0f, 58.0f, line2);
        render_text(platform, 10.0f, 88.0f, "LEFT/RIGHT: SCALE");
        render_text(platform, 10.0f, 98.0f, "ENTER CHANGE / ESC BACK");
    }
    SDL_SetRenderDrawBlendMode(platform->renderer, SDL_BLENDMODE_NONE);
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
    platform->menu_state = GB_SDL3_MENU_GAMEPLAY;
    platform->window_scale = GB_SDL3_WINDOW_SCALE;

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

    GB_Result result = GB_RESULT_OK;
    if (emulator->cartridge.loaded) {
        result = gb_input_sdl3_init(&platform->input, &emulator->input, error);
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

        if (event.type == SDL_EVENT_DROP_FILE) {
            const char *path = event.drop.data;
            if (path != NULL && path[0] != '\0') {
                GB_Result load_result = gb_emulator_load_rom(emulator, path, error);
                if (load_result != GB_RESULT_OK) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Could not load dropped ROM '%s': %s",
                                path,
                                error != NULL ? error->message : "unknown error");
                    gb_error_clear(error);
                    if (!emulator->cartridge.loaded && platform->input_initialized) {
                        (void)gb_input_sdl3_destroy(&platform->input, NULL);
                        platform->input_initialized = false;
                    }
                    set_waiting_window_title(platform);
                } else {
                    platform->menu_state = GB_SDL3_MENU_GAMEPLAY;
                    platform->menu_selection = 0;
                    platform->waiting_for_keybind = false;
                    result = prepare_loaded_rom_input(platform, emulator, error);
                    if (result != GB_RESULT_OK) return result;
                    set_audio_output_state(platform, false, error);
                    if (error != NULL && error->code != GB_RESULT_OK) return error->code;
                }
            }
            continue;
        }

        bool handled = false;
        bool menu_handled = false;
        result = handle_menu_event(platform, emulator, &event,
                                   &menu_handled, quit, error);
        if (result != GB_RESULT_OK) return result;
        if (menu_handled) continue;
        handled = false;
        if (platform->menu_state != GB_SDL3_MENU_GAMEPLAY) continue;
        if (platform->input_initialized && emulator->cartridge.loaded) {
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

        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
            platform->input_initialized && emulator->cartridge.loaded) {
            result = gb_input_sdl3_reset(&platform->input, error);
            if (result != GB_RESULT_OK) return result;
        }
        (void)handled;
    }

    if (platform->menu_state == GB_SDL3_MENU_GAMEPLAY &&
        platform->input_initialized && emulator->cartridge.loaded) {
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
    bool frame_ready = gb_ppu_frame_ready(&emulator->ppu);
    if (!frame_ready && platform->menu_state == GB_SDL3_MENU_GAMEPLAY) {
        return GB_RESULT_OK;
    }

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
    render_menu_overlay(platform);
    if (!SDL_RenderPresent(platform->renderer)) {
        platform_error(error, GB_RESULT_UNSUPPORTED,
                       "SDL3 present failed");
        return GB_RESULT_UNSUPPORTED;
    }

    if (frame_ready) {
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
    if (!platform->audio_initialized || platform->menu_state != GB_SDL3_MENU_GAMEPLAY ||
        platform->audio_muted) return GB_RESULT_OK;
    return gb_audio_sdl3_pump(&platform->audio, &emulator->audio, 2048u, error);
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

        if (!emulator->cartridge.loaded) {
            last_ns = SDL_GetTicksNS();
            fractional_ns = 0u;
            cycle_budget = 0;
            last_audio_ns = last_ns;
            platform->timing_reset_requested = false;
            SDL_DelayNS(10000000ULL);
            continue;
        }

        if (platform->menu_state != GB_SDL3_MENU_GAMEPLAY) {
            result = gb_sdl3_render(platform, emulator, error);
            if (result != GB_RESULT_OK) return result;
            result = gb_sdl3_pump_audio(platform, emulator, error);
            if (result != GB_RESULT_OK) return result;
            SDL_DelayNS(16000000ULL);
            continue;
        }

        if (platform->timing_reset_requested) {
            last_ns = SDL_GetTicksNS();
            fractional_ns = 0u;
            cycle_budget = 0;
            last_audio_ns = last_ns;
            platform->timing_reset_requested = false;
        }

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
