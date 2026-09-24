#include "gb_audio_sdl3.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

static void sdl_audio_error(GB_Error *error, GB_Result code, const char *message)
{
    if (error == NULL) return;
    gb_error_clear(error);
    error->code = code;
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s: %s",
                       message, SDL_GetError());
    }
}

GB_Result gb_audio_sdl3_init(GB_AudioSDL3 *output, GB_Audio *audio,
                             int sample_rate, GB_Error *error)
{
    gb_error_clear(error);
    if (output == NULL || audio == NULL) {
        sdl_audio_error(error, GB_RESULT_NULL_ARGUMENT,
                        "SDL3 audio requires output and core audio objects");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (sample_rate <= 0) {
        sdl_audio_error(error, GB_RESULT_INVALID_ARGUMENT,
                        "SDL3 audio sample rate must be positive");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    memset(output, 0, sizeof(*output));

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            sdl_audio_error(error, GB_RESULT_UNSUPPORTED,
                            "SDL3 audio subsystem initialization failed");
            return GB_RESULT_UNSUPPORTED;
        }
        output->audio_subsystem_owned = true;
    }

    output->input_spec.format = SDL_AUDIO_F32;
    output->input_spec.channels = 2;
    output->input_spec.freq = sample_rate;
    output->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                               &output->input_spec,
                                               NULL, NULL);
    if (output->stream == NULL) {
        sdl_audio_error(error, GB_RESULT_UNSUPPORTED,
                        "SDL3 audio device stream creation failed");
        if (output->audio_subsystem_owned) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            output->audio_subsystem_owned = false;
        }
        return GB_RESULT_UNSUPPORTED;
    }

    output->device = SDL_GetAudioStreamDevice(output->stream);
    if (output->device == 0) {
        SDL_DestroyAudioStream(output->stream);
        output->stream = NULL;
        if (output->audio_subsystem_owned) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            output->audio_subsystem_owned = false;
        }
        sdl_audio_error(error, GB_RESULT_BAD_STATE,
                        "SDL3 audio stream did not expose a playback device");
        return GB_RESULT_BAD_STATE;
    }

    if (!SDL_ResumeAudioStreamDevice(output->stream)) {
        SDL_DestroyAudioStream(output->stream);
        output->stream = NULL;
        if (output->audio_subsystem_owned) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            output->audio_subsystem_owned = false;
        }
        sdl_audio_error(error, GB_RESULT_UNSUPPORTED,
                        "SDL3 audio stream could not be resumed");
        return GB_RESULT_UNSUPPORTED;
    }

    output->initialized = true;
    (void)audio;
    return GB_RESULT_OK;
}

GB_Result gb_audio_sdl3_pump(GB_AudioSDL3 *output, GB_Audio *audio,
                             uint32_t max_frames, GB_Error *error)
{
    gb_error_clear(error);
    if (output == NULL || audio == NULL) {
        sdl_audio_error(error, GB_RESULT_NULL_ARGUMENT,
                        "SDL3 audio pump requires output and core audio objects");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!output->initialized || output->stream == NULL) {
        sdl_audio_error(error, GB_RESULT_BAD_STATE,
                        "SDL3 audio output is not initialized");
        return GB_RESULT_BAD_STATE;
    }
    if (max_frames == 0u) return GB_RESULT_OK;

    uint32_t frames = max_frames;
    if (frames > 4096u) frames = 4096u;
    float *buffer = (float *)malloc((size_t)frames * 2u * sizeof(float));
    if (buffer == NULL) {
        sdl_audio_error(error, GB_RESULT_ALLOCATION,
                        "SDL3 audio pump buffer allocation failed");
        return GB_RESULT_ALLOCATION;
    }

    size_t available = gb_audio_read_samples(audio, buffer, frames, error);
    if (error != NULL && error->code != GB_RESULT_OK) {
        free(buffer);
        return error->code;
    }
    if (available != 0u) {
        size_t bytes = available * 2u * sizeof(float);
        if (bytes > (size_t)INT_MAX ||
            !SDL_PutAudioStreamData(output->stream, buffer, (int)bytes)) {
            free(buffer);
            sdl_audio_error(error, GB_RESULT_UNSUPPORTED,
                            "SDL3 audio stream rejected PCM data");
            return GB_RESULT_UNSUPPORTED;
        }
        output->submitted_frames += (uint32_t)available;
    }

    free(buffer);
    return GB_RESULT_OK;
}

GB_Result gb_audio_sdl3_pause(GB_AudioSDL3 *output, bool pause, GB_Error *error)
{
    gb_error_clear(error);
    if (output == NULL || !output->initialized || output->stream == NULL) {
        sdl_audio_error(error, GB_RESULT_BAD_STATE,
                        "SDL3 audio output is not initialized");
        return GB_RESULT_BAD_STATE;
    }
    if (pause) {
        if (!SDL_PauseAudioStreamDevice(output->stream)) {
            sdl_audio_error(error, GB_RESULT_UNSUPPORTED,
                            "SDL3 audio stream could not be paused");
            return GB_RESULT_UNSUPPORTED;
        }
    } else if (!SDL_ResumeAudioStreamDevice(output->stream)) {
        sdl_audio_error(error, GB_RESULT_UNSUPPORTED,
                        "SDL3 audio stream could not be resumed");
        return GB_RESULT_UNSUPPORTED;
    }
    return GB_RESULT_OK;
}

GB_Result gb_audio_sdl3_destroy(GB_AudioSDL3 *output, GB_Error *error)
{
    gb_error_clear(error);
    if (output == NULL) {
        sdl_audio_error(error, GB_RESULT_NULL_ARGUMENT,
                        "SDL3 audio output pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (output->stream != NULL) {
        SDL_DestroyAudioStream(output->stream);
        output->stream = NULL;
    }
    if (output->audio_subsystem_owned) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    memset(output, 0, sizeof(*output));
    return GB_RESULT_OK;
}
