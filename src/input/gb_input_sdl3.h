#ifndef GB_INPUT_SDL3_H
#define GB_INPUT_SDL3_H

#include <stdbool.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include "gb_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SDL3 platform adapter. The GB_Input hardware object remains independent of
 * SDL; this layer translates SDL keyboard/gamepad events into its aggregate
 * physical button state. */
typedef struct GB_InputSDL3Bindings {
    SDL_Scancode keyboard[GB_INPUT_BUTTON_COUNT];
} GB_InputSDL3Bindings;

typedef struct GB_InputSDL3 {
    bool initialized;
    GB_Input *input;
    GB_InputSDL3Bindings bindings;

    uint8_t keyboard_pressed;
    uint8_t gamepad_pressed;

    SDL_Gamepad *gamepad;
    SDL_JoystickID gamepad_id;
} GB_InputSDL3;

GB_Result gb_input_sdl3_init(GB_InputSDL3 *adapter,
                             GB_Input *input,
                             GB_Error *error);
GB_Result gb_input_sdl3_reset(GB_InputSDL3 *adapter, GB_Error *error);
GB_Result gb_input_sdl3_destroy(GB_InputSDL3 *adapter, GB_Error *error);

GB_Result gb_input_sdl3_set_keyboard_binding(GB_InputSDL3 *adapter,
                                             GB_InputButton button,
                                             SDL_Scancode scancode,
                                             GB_Error *error);

/* Apply the currently held SDL keyboard state. This calls SDL_PumpEvents(). */
GB_Result gb_input_sdl3_sync_keyboard(GB_InputSDL3 *adapter,
                                      GB_Error *error);

/* Open a specific SDL3 gamepad. Any previously opened gamepad is closed. */
GB_Result gb_input_sdl3_open_gamepad(GB_InputSDL3 *adapter,
                                     SDL_JoystickID instance_id,
                                     GB_Error *error);

/* Open the first gamepad SDL currently reports as connected. */
GB_Result gb_input_sdl3_open_first_gamepad(GB_InputSDL3 *adapter,
                                           GB_Error *error);

void gb_input_sdl3_close_gamepad(GB_InputSDL3 *adapter);

/* Process one SDL3 event. `handled` is set true when the event affects the
 * configured keyboard/gamepad input or gamepad lifetime. */
GB_Result gb_input_sdl3_process_event(GB_InputSDL3 *adapter,
                                      const SDL_Event *event,
                                      bool *handled,
                                      GB_Error *error);

const GB_InputSDL3Bindings *gb_input_sdl3_default_bindings(void);

#ifdef __cplusplus
}
#endif

#endif /* GB_INPUT_SDL3_H */
