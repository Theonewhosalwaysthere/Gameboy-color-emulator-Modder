#include "gb_input_sdl3.h"

#include <stdio.h>
#include <string.h>

static const GB_InputSDL3Bindings default_bindings = {
    .keyboard = {
        [GB_INPUT_BUTTON_A]      = SDL_SCANCODE_Z,
        [GB_INPUT_BUTTON_B]      = SDL_SCANCODE_X,
        [GB_INPUT_BUTTON_SELECT] = SDL_SCANCODE_RSHIFT,
        [GB_INPUT_BUTTON_START]  = SDL_SCANCODE_RETURN,
        [GB_INPUT_BUTTON_RIGHT]  = SDL_SCANCODE_RIGHT,
        [GB_INPUT_BUTTON_LEFT]   = SDL_SCANCODE_LEFT,
        [GB_INPUT_BUTTON_UP]     = SDL_SCANCODE_UP,
        [GB_INPUT_BUTTON_DOWN]   = SDL_SCANCODE_DOWN
    }
};

static void sdl3_error(GB_Error *error, GB_Result code,
                       const char *message)
{
    if (error == NULL) {
        return;
    }

    gb_error_clear(error);
    error->code = code;
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static GB_Result require_adapter(const GB_InputSDL3 *adapter, GB_Error *error)
{
    if (adapter == NULL) {
        sdl3_error(error, GB_RESULT_NULL_ARGUMENT,
                   "SDL3 input adapter pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!adapter->initialized || adapter->input == NULL) {
        sdl3_error(error, GB_RESULT_BAD_STATE,
                   "SDL3 input adapter is not initialized");
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static uint8_t keyboard_binding_mask(const GB_InputSDL3 *adapter,
                                     SDL_Scancode scancode)
{
    uint8_t mask = 0u;
    for (unsigned i = 0u; i < GB_INPUT_BUTTON_COUNT; ++i) {
        if (adapter->bindings.keyboard[i] == scancode) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

static uint8_t gamepad_button_mask(SDL_GamepadButton button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:    return GB_INPUT_MASK_A;
    case SDL_GAMEPAD_BUTTON_EAST:     return GB_INPUT_MASK_B;
    case SDL_GAMEPAD_BUTTON_BACK:     return GB_INPUT_MASK_SELECT;
    case SDL_GAMEPAD_BUTTON_START:    return GB_INPUT_MASK_START;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:return GB_INPUT_MASK_RIGHT;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GB_INPUT_MASK_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:   return GB_INPUT_MASK_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GB_INPUT_MASK_DOWN;
    default:                           return 0u;
    }
}

static GB_Result apply_aggregate(GB_InputSDL3 *adapter, GB_Error *error)
{
    return gb_input_set_pressed_mask(adapter->input,
                                     (uint8_t)(adapter->keyboard_pressed |
                                               adapter->gamepad_pressed),
                                     error);
}

static GB_Result sync_gamepad_state(GB_InputSDL3 *adapter, GB_Error *error)
{
    adapter->gamepad_pressed = 0u;
    if (adapter->gamepad == NULL) {
        return apply_aggregate(adapter, error);
    }

    static const SDL_GamepadButton buttons[] = {
        SDL_GAMEPAD_BUTTON_SOUTH,
        SDL_GAMEPAD_BUTTON_EAST,
        SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
        SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_UP,
        SDL_GAMEPAD_BUTTON_DPAD_DOWN
    };

    for (size_t i = 0u; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        if (SDL_GetGamepadButton(adapter->gamepad, buttons[i])) {
            adapter->gamepad_pressed |= gamepad_button_mask(buttons[i]);
        }
    }

    return apply_aggregate(adapter, error);
}

GB_Result gb_input_sdl3_init(GB_InputSDL3 *adapter,
                             GB_Input *input,
                             GB_Error *error)
{
    gb_error_clear(error);

    if (adapter == NULL || input == NULL) {
        sdl3_error(error, GB_RESULT_NULL_ARGUMENT,
                   "SDL3 adapter and Game Boy input pointers are required");
        return GB_RESULT_NULL_ARGUMENT;
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->input = input;
    adapter->bindings = default_bindings;
    adapter->initialized = true;

    return gb_input_sdl3_reset(adapter, error);
}

GB_Result gb_input_sdl3_reset(GB_InputSDL3 *adapter, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_adapter(adapter, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    adapter->keyboard_pressed = 0u;
    adapter->gamepad_pressed = 0u;
    return apply_aggregate(adapter, error);
}

GB_Result gb_input_sdl3_destroy(GB_InputSDL3 *adapter, GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_adapter(adapter, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    gb_input_sdl3_close_gamepad(adapter);
    memset(adapter, 0, sizeof(*adapter));
    return GB_RESULT_OK;
}

GB_Result gb_input_sdl3_set_keyboard_binding(GB_InputSDL3 *adapter,
                                             GB_InputButton button,
                                             SDL_Scancode scancode,
                                             GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_adapter(adapter, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (button < GB_INPUT_BUTTON_A || button >= GB_INPUT_BUTTON_COUNT_ENUM) {
        sdl3_error(error, GB_RESULT_INVALID_ARGUMENT,
                   "Invalid Game Boy input button");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    adapter->bindings.keyboard[button] = scancode;
    return GB_RESULT_OK;
}

GB_Result gb_input_sdl3_sync_keyboard(GB_InputSDL3 *adapter,
                                      GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_adapter(adapter, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    SDL_PumpEvents();
    int count = 0;
    const bool *state = SDL_GetKeyboardState(&count);
    if (state == NULL || count < 0) {
        sdl3_error(error, GB_RESULT_INPUT_ERROR,
                   "SDL3 keyboard state is unavailable");
        return GB_RESULT_INPUT_ERROR;
    }

    adapter->keyboard_pressed = 0u;
    for (unsigned i = 0u; i < GB_INPUT_BUTTON_COUNT; ++i) {
        SDL_Scancode scancode = adapter->bindings.keyboard[i];
        int scancode_index = (int)scancode;
        if (scancode_index >= 0 && scancode_index < count && state[scancode_index]) {
            adapter->keyboard_pressed |= (uint8_t)(1u << i);
        }
    }

    return apply_aggregate(adapter, error);
}

GB_Result gb_input_sdl3_open_gamepad(GB_InputSDL3 *adapter,
                                     SDL_JoystickID instance_id,
                                     GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_adapter(adapter, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    SDL_Gamepad *gamepad = SDL_OpenGamepad(instance_id);
    if (gamepad == NULL) {
        char message[256];
        (void)snprintf(message, sizeof(message),
                       "SDL3 failed to open gamepad instance %d: %s",
                       (int)instance_id, SDL_GetError());
        sdl3_error(error, GB_RESULT_INPUT_ERROR, message);
        return GB_RESULT_INPUT_ERROR;
    }

    gb_input_sdl3_close_gamepad(adapter);
    adapter->gamepad = gamepad;
    adapter->gamepad_id = instance_id;
    return sync_gamepad_state(adapter, error);
}

GB_Result gb_input_sdl3_open_first_gamepad(GB_InputSDL3 *adapter,
                                           GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_adapter(adapter, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (!SDL_HasGamepad()) {
        return GB_RESULT_OK;
    }

    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids == NULL) {
        const char *message = SDL_GetError();
        if (message == NULL || message[0] == '\0') {
            return GB_RESULT_OK;
        }
        char buffer[256];
        (void)snprintf(buffer, sizeof(buffer),
                       "SDL3 failed to enumerate gamepads: %s", message);
        sdl3_error(error, GB_RESULT_INPUT_ERROR, buffer);
        return GB_RESULT_INPUT_ERROR;
    }

    result = GB_RESULT_OK;
    if (count > 0) {
        result = gb_input_sdl3_open_gamepad(adapter, ids[0], error);
    }

    SDL_free(ids);
    return result;
}

void gb_input_sdl3_close_gamepad(GB_InputSDL3 *adapter)
{
    if (adapter == NULL) {
        return;
    }

    if (adapter->gamepad != NULL) {
        SDL_CloseGamepad(adapter->gamepad);
        adapter->gamepad = NULL;
        adapter->gamepad_id = 0;
        adapter->gamepad_pressed = 0u;
        if (adapter->initialized && adapter->input != NULL) {
            GB_Error ignored;
            gb_error_clear(&ignored);
            (void)apply_aggregate(adapter, &ignored);
        }
    }
}

GB_Result gb_input_sdl3_process_event(GB_InputSDL3 *adapter,
                                      const SDL_Event *event,
                                      bool *handled,
                                      GB_Error *error)
{
    gb_error_clear(error);

    GB_Result result = require_adapter(adapter, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (event == NULL || handled == NULL) {
        sdl3_error(error, GB_RESULT_NULL_ARGUMENT,
                   "SDL3 event and handled output pointers are required");
        return GB_RESULT_NULL_ARGUMENT;
    }

    *handled = false;

    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        uint8_t mask = keyboard_binding_mask(adapter, event->key.scancode);
        if (mask == 0u) {
            return GB_RESULT_OK;
        }

        *handled = true;
        if (event->key.repeat && event->type == SDL_EVENT_KEY_DOWN) {
            return GB_RESULT_OK;
        }

        if (event->type == SDL_EVENT_KEY_DOWN) {
            adapter->keyboard_pressed |= mask;
        } else {
            adapter->keyboard_pressed &= (uint8_t)~mask;
        }
        return apply_aggregate(adapter, error);
    }

    case SDL_EVENT_GAMEPAD_ADDED:
        *handled = true;
        if (adapter->gamepad == NULL) {
            return gb_input_sdl3_open_gamepad(adapter, event->gdevice.which,
                                              error);
        }
        return GB_RESULT_OK;

    case SDL_EVENT_GAMEPAD_REMOVED:
        if (adapter->gamepad != NULL &&
            adapter->gamepad_id == event->gdevice.which) {
            *handled = true;
            gb_input_sdl3_close_gamepad(adapter);
        }
        return GB_RESULT_OK;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        if (adapter->gamepad == NULL ||
            adapter->gamepad_id != event->gbutton.which) {
            return GB_RESULT_OK;
        }

        uint8_t mask = gamepad_button_mask(
            (SDL_GamepadButton)event->gbutton.button);
        if (mask == 0u) {
            return GB_RESULT_OK;
        }

        *handled = true;
        if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            adapter->gamepad_pressed |= mask;
        } else {
            adapter->gamepad_pressed &= (uint8_t)~mask;
        }
        return apply_aggregate(adapter, error);
    }

    default:
        return GB_RESULT_OK;
    }
}

const GB_InputSDL3Bindings *gb_input_sdl3_default_bindings(void)
{
    return &default_bindings;
}
