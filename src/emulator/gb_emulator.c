#include "gb_emulator.h"

#include <stdio.h>
#include <string.h>

static void emulator_error(GB_Error *error, GB_Result code, const char *message)
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

static GB_Result require_emulator(const GB_Emulator *emulator, GB_Error *error)
{
    if (emulator == NULL) {
        emulator_error(error, GB_RESULT_NULL_ARGUMENT,
                       "Emulator pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!emulator->initialized) {
        emulator_error(error, GB_RESULT_BAD_STATE,
                       "Emulator is not initialized");
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static bool cartridge_requires_cgb(const GB_Cartridge *cartridge)
{
    return cartridge != NULL &&
           cartridge->cgb_support != GB_CARTRIDGE_CGB_DMG_COMPATIBLE;
}

static void preserve_first_error(GB_Result *first_error, GB_Error *first_error_detail,
                                  GB_Result result, const GB_Error *detail)
{
    if (result != GB_RESULT_OK && *first_error == GB_RESULT_OK) {
        *first_error = result;
        if (first_error_detail != NULL && detail != NULL) {
            *first_error_detail = *detail;
        }
    }
}

static GB_Result destroy_hardware(GB_Emulator *emulator, GB_Error *error)
{
    GB_Result first_error = GB_RESULT_OK;
    GB_Error first_detail;
    GB_Error child;
    GB_Result result;
    gb_error_clear(&first_detail);

    if (emulator->cgb_initialized) {
        gb_error_clear(&child);
        result = gb_cgb_destroy(&emulator->cgb, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
        emulator->cgb_initialized = false;
    }

    if (emulator->audio.initialized) {
        gb_error_clear(&child);
        result = gb_audio_destroy(&emulator->audio, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
    }

    if (emulator->dma.initialized) {
        gb_error_clear(&child);
        result = gb_dma_destroy(&emulator->dma, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
    }

    if (emulator->input.initialized) {
        gb_error_clear(&child);
        result = gb_input_destroy(&emulator->input, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
    }

    if (emulator->timer.initialized) {
        gb_error_clear(&child);
        result = gb_timer_destroy(&emulator->timer, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
    }

    if (emulator->ppu.initialized) {
        gb_error_clear(&child);
        result = gb_ppu_destroy(&emulator->ppu, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
    }

    if (emulator->interrupt.initialized) {
        if (emulator->interrupt.memory_connected) {
            gb_error_clear(&child);
            result = gb_interrupt_disconnect_memory(&emulator->interrupt, &child);
            preserve_first_error(&first_error, &first_detail, result, &child);
        }
        gb_error_clear(&child);
        result = gb_interrupt_reset(&emulator->interrupt, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
    }

    if (first_error != GB_RESULT_OK && error != NULL) {
        *error = first_detail;
    }
    return first_error;
}

void gb_emulator_config_default(GB_EmulatorConfig *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->mode = GB_EMULATOR_MODE_AUTO;
    gb_cartridge_load_options_default(&config->cartridge_options);
    config->cartridge_options.validate_global_checksum = false;
    config->use_cpu_startup_override = false;
    config->enable_save_ram = true;
    gb_debug_config_default(&config->debug_config);
}

GB_Result gb_emulator_init(GB_Emulator *emulator,
                           const GB_EmulatorConfig *config,
                           GB_Error *error)
{
    gb_error_clear(error);
    if (emulator == NULL) {
        emulator_error(error, GB_RESULT_NULL_ARGUMENT,
                       "Emulator pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (emulator->initialized) {
        emulator_error(error, GB_RESULT_BAD_STATE,
                       "Emulator is already initialized");
        return GB_RESULT_BAD_STATE;
    }

    GB_EmulatorConfig local;
    gb_emulator_config_default(&local);
    if (config != NULL) {
        local = *config;
    }

    memset(emulator, 0, sizeof(*emulator));
    emulator->requested_mode = local.mode;
    emulator->running = true;
    emulator->initialized = true;
    emulator->cartridge_options = local.cartridge_options;
    emulator->cpu_startup_override = local.cpu_startup_override;
    emulator->use_cpu_startup_override = local.use_cpu_startup_override;
    emulator->enable_save_ram = local.enable_save_ram;

    GB_Result result = gb_save_ram_init(&emulator->save_ram, error);
    if (result != GB_RESULT_OK) {
        memset(emulator, 0, sizeof(*emulator));
        return result;
    }
    emulator->save_ram_initialized = true;

    result = gb_debug_init(&emulator->debug, &local.debug_config, error);
    if (result != GB_RESULT_OK) {
        (void)gb_save_ram_destroy(&emulator->save_ram, NULL);
        memset(emulator, 0, sizeof(*emulator));
        return result;
    }
    emulator->debug_initialized = true;
    return GB_RESULT_OK;
}

static GB_Result emulator_cpu_bus_read8(void *user, uint16_t address,
                                          uint8_t *value, GB_Error *error)
{
    GB_Emulator *emulator = (GB_Emulator *)user;
    if (emulator == NULL) {
        emulator_error(error, GB_RESULT_NULL_ARGUMENT,
                       "CPU read wrapper received a NULL emulator");
        return GB_RESULT_NULL_ARGUMENT;
    }
    GB_CPU_BUS memory_bus = gb_memory_cpu_bus(&emulator->memory);
    return memory_bus.read8(memory_bus.user, address, value, error);
}

static GB_Result emulator_cpu_bus_write8(void *user, uint16_t address,
                                           uint8_t value, GB_Error *error)
{
    GB_Emulator *emulator = (GB_Emulator *)user;
    if (emulator == NULL) {
        emulator_error(error, GB_RESULT_NULL_ARGUMENT,
                       "CPU write wrapper received a NULL emulator");
        return GB_RESULT_NULL_ARGUMENT;
    }
    GB_CPU_BUS memory_bus = gb_memory_cpu_bus(&emulator->memory);
    return memory_bus.write8(memory_bus.user, address, value, error);
}

static GB_Result emulator_cpu_bus_pending(void *user, uint8_t *pending_mask,
                                           GB_Error *error)
{
    GB_Emulator *emulator = (GB_Emulator *)user;
    if (emulator == NULL) {
        emulator_error(error, GB_RESULT_NULL_ARGUMENT,
                       "CPU interrupt wrapper received a NULL emulator");
        return GB_RESULT_NULL_ARGUMENT;
    }
    GB_CPU_BUS memory_bus = gb_memory_cpu_bus(&emulator->memory);
    return memory_bus.get_pending_interrupts(memory_bus.user, pending_mask, error);
}

static GB_Result emulator_cpu_bus_acknowledge(void *user, uint8_t interrupt_mask,
                                               GB_Error *error)
{
    GB_Emulator *emulator = (GB_Emulator *)user;
    if (emulator == NULL) {
        emulator_error(error, GB_RESULT_NULL_ARGUMENT,
                       "CPU interrupt acknowledgement wrapper received a NULL emulator");
        return GB_RESULT_NULL_ARGUMENT;
    }
    GB_CPU_BUS memory_bus = gb_memory_cpu_bus(&emulator->memory);
    return memory_bus.acknowledge_interrupt(memory_bus.user, interrupt_mask, error);
}

static GB_Result emulator_cpu_bus_tick(void *user, uint32_t cpu_t_cycles, GB_Error *error)
{
    GB_Emulator *emulator = (GB_Emulator *)user;
    if (emulator == NULL) {
        emulator_error(error, GB_RESULT_NULL_ARGUMENT,
                       "CPU timing wrapper received a NULL emulator");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if ((cpu_t_cycles & 1u) != 0u) {
        emulator_error(error, GB_RESULT_INVALID_ARGUMENT,
                       "CPU bus timing must use an even number of T-cycles");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    /* The PPU/APU/memory fabric is clocked at the 4.194304 MHz base rate.
     * In CGB double-speed mode each CPU T-cycle spans half a base hardware
     * tick, so a four-T-cycle CPU machine cycle advances hardware by two. */
    uint32_t hardware_t_cycles = cpu_t_cycles;
    if (emulator->cgb_initialized &&
        gb_cgb_get_speed(&emulator->cgb) == GB_CGB_SPEED_DOUBLE) {
        hardware_t_cycles = cpu_t_cycles / 2u;
    }
    return gb_memory_tick(&emulator->memory, hardware_t_cycles, error);
}

static uint32_t cpu_cycles_to_hardware_cycles(const GB_Emulator *emulator,
                                               uint32_t cpu_t_cycles,
                                               GB_CGBSpeed speed_before_step)
{
    if (emulator != NULL && emulator->cgb_initialized &&
        speed_before_step == GB_CGB_SPEED_DOUBLE) {
        return cpu_t_cycles / 2u;
    }
    return cpu_t_cycles;
}

static uint32_t hardware_cycles_to_cpu_cycles(const GB_Emulator *emulator,
                                               uint32_t hardware_t_cycles)
{
    if (emulator != NULL && emulator->cgb_initialized &&
        gb_cgb_get_speed(&emulator->cgb) == GB_CGB_SPEED_DOUBLE) {
        return hardware_t_cycles * 2u;
    }
    return hardware_t_cycles;
}

static GB_Result build_hardware(GB_Emulator *emulator, GB_Error *error)
{
    GB_Result result = gb_memory_init(&emulator->memory, emulator->memory_mode, error);
    if (result != GB_RESULT_OK) return result;

    GB_MemoryCartridgeBus cartridge_bus;
    result = gb_cartridge_get_memory_bus(&emulator->cartridge, &cartridge_bus, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_memory_set_cartridge_bus(&emulator->memory, &cartridge_bus, error);
    if (result != GB_RESULT_OK) return result;

    result = gb_interrupt_init(&emulator->interrupt, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_interrupt_connect_memory(&emulator->interrupt, &emulator->memory, error);
    if (result != GB_RESULT_OK) return result;

    result = gb_ppu_init(&emulator->ppu, &emulator->memory, error);
    if (result != GB_RESULT_OK) return result;

    result = gb_timer_init(&emulator->timer, &emulator->memory, error);
    if (result != GB_RESULT_OK) return result;

    result = gb_input_init(&emulator->input, &emulator->memory, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_input_connect_interrupt(&emulator->input,
                                        gb_interrupt_request,
                                        &emulator->interrupt,
                                        error);
    if (result != GB_RESULT_OK) return result;

    result = gb_dma_init(&emulator->dma, &emulator->memory, error);
    if (result != GB_RESULT_OK) return result;

    result = gb_audio_init(&emulator->audio, &emulator->memory, error);
    if (result != GB_RESULT_OK) return result;
    return gb_audio_attach_timer(&emulator->audio, &emulator->timer, error);
}

static GB_Result initialize_loaded_cartridge(GB_Emulator *emulator,
                                             GB_Error *error)
{
    bool use_cgb = false;
    if (emulator->requested_mode == GB_EMULATOR_MODE_CGB) {
        use_cgb = true;
    } else if (emulator->requested_mode == GB_EMULATOR_MODE_DMG) {
        use_cgb = false;
    } else {
        use_cgb = cartridge_requires_cgb(&emulator->cartridge);
    }

    if (emulator->requested_mode == GB_EMULATOR_MODE_DMG &&
        emulator->cartridge.cgb_support == GB_CARTRIDGE_CGB_ONLY) {
        emulator_error(error, GB_RESULT_UNSUPPORTED,
                       "CGB-only cartridge cannot be forced into DMG mode");
        return GB_RESULT_UNSUPPORTED;
    }

    emulator->memory_mode = use_cgb ? GB_MEMORY_MODE_CGB : GB_MEMORY_MODE_DMG;

    GB_Result result = build_hardware(emulator, error);
    if (result != GB_RESULT_OK) return result;

    if (use_cgb) {
        result = gb_cgb_init(&emulator->cgb, &emulator->memory, error);
        if (result != GB_RESULT_OK) return result;
        emulator->cgb_initialized = true;

        result = gb_cgb_attach_timer(&emulator->cgb, &emulator->timer, error);
        if (result != GB_RESULT_OK) return result;
        result = gb_cgb_attach_dma(&emulator->cgb, &emulator->dma, error);
        if (result != GB_RESULT_OK) return result;
        result = gb_cgb_attach_ppu(&emulator->cgb, &emulator->ppu, error);
        if (result != GB_RESULT_OK) return result;
    }

    GB_CPU_STARTUP startup = GB_CPU_STARTUP_DMG;
    if (use_cgb) {
        startup = (emulator->cartridge.cgb_support == GB_CARTRIDGE_CGB_ONLY)
                      ? GB_CPU_STARTUP_CGB
                      : GB_CPU_STARTUP_CGB_DMG_COMPAT;
    }
    if (emulator->use_cpu_startup_override) {
        startup = emulator->cpu_startup_override;
    }

    GB_CPU_BUS bus = gb_memory_cpu_bus(&emulator->memory);
    bus.user = emulator;
    bus.read8 = emulator_cpu_bus_read8;
    bus.write8 = emulator_cpu_bus_write8;
    bus.tick = emulator_cpu_bus_tick;
    bus.get_pending_interrupts = emulator_cpu_bus_pending;
    bus.acknowledge_interrupt = emulator_cpu_bus_acknowledge;
    result = gb_cpu_init(&emulator->cpu, &bus, error);
    if (result != GB_RESULT_OK) return result;

    if (emulator->cgb_initialized) {
        result = gb_cgb_attach_cpu(&emulator->cgb, &emulator->cpu, error);
        if (result != GB_RESULT_OK) return result;
    }

    result = gb_cpu_reset(&emulator->cpu, startup, error);
    if (result != GB_RESULT_OK) return result;

    emulator->frame_t_cycles = 0u;
    emulator->total_t_cycles = 0u;
    emulator->frame_count = 0u;
    return GB_RESULT_OK;
}

GB_Result gb_emulator_load_rom(GB_Emulator *emulator,
                               const char *path,
                               GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_emulator(emulator, error);
    if (result != GB_RESULT_OK) return result;
    if (path == NULL || path[0] == '\0') {
        emulator_error(error, GB_RESULT_INVALID_ARGUMENT,
                       "ROM path is empty");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    if (emulator->cartridge.initialized) {
        if (emulator->save_ram_initialized) {
            result = gb_save_ram_destroy(&emulator->save_ram, error);
            emulator->save_ram_initialized = false;
            if (result != GB_RESULT_OK) return result;
        }
        result = destroy_hardware(emulator, error);
        if (result != GB_RESULT_OK) return result;
        result = gb_cartridge_unload(&emulator->cartridge, error);
        if (result != GB_RESULT_OK) return result;
        memset(&emulator->memory, 0, sizeof(emulator->memory));
        memset(&emulator->interrupt, 0, sizeof(emulator->interrupt));
        memset(&emulator->ppu, 0, sizeof(emulator->ppu));
        memset(&emulator->timer, 0, sizeof(emulator->timer));
        memset(&emulator->input, 0, sizeof(emulator->input));
        memset(&emulator->dma, 0, sizeof(emulator->dma));
        memset(&emulator->cgb, 0, sizeof(emulator->cgb));
        memset(&emulator->audio, 0, sizeof(emulator->audio));
        memset(&emulator->cpu, 0, sizeof(emulator->cpu));
        emulator->cgb_initialized = false;
    }

    result = gb_cartridge_init(&emulator->cartridge, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_cartridge_load(&emulator->cartridge, path,
                               &emulator->cartridge_options, error);
    if (result != GB_RESULT_OK) {
        (void)gb_cartridge_unload(&emulator->cartridge, NULL);
        return result;
    }

    result = gb_save_ram_init(&emulator->save_ram, error);
    if (result != GB_RESULT_OK) {
        (void)gb_cartridge_unload(&emulator->cartridge, NULL);
        return result;
    }
    emulator->save_ram_initialized = true;
    if (emulator->enable_save_ram) {
        result = gb_save_ram_attach(&emulator->save_ram, &emulator->cartridge, path, error);
        if (result != GB_RESULT_OK) {
            (void)gb_save_ram_destroy(&emulator->save_ram, NULL);
            emulator->save_ram_initialized = false;
            (void)gb_cartridge_unload(&emulator->cartridge, NULL);
            return result;
        }
        result = gb_save_ram_load(&emulator->save_ram, error);
        if (result != GB_RESULT_OK) {
            (void)gb_save_ram_destroy(&emulator->save_ram, NULL);
            emulator->save_ram_initialized = false;
            (void)gb_cartridge_unload(&emulator->cartridge, NULL);
            return result;
        }
    }

    result = initialize_loaded_cartridge(emulator, error);
    if (result != GB_RESULT_OK) {
        (void)destroy_hardware(emulator, NULL);
        if (emulator->save_ram_initialized) {
            (void)gb_save_ram_destroy(&emulator->save_ram, NULL);
            emulator->save_ram_initialized = false;
        }
        (void)gb_cartridge_unload(&emulator->cartridge, NULL);
        memset(&emulator->memory, 0, sizeof(emulator->memory));
        return result;
    }

    emulator->running = true;
    return GB_RESULT_OK;
}

GB_Result gb_emulator_load_rom_buffer(GB_Emulator *emulator,
                                      const uint8_t *data,
                                      size_t size,
                                      GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_emulator(emulator, error);
    if (result != GB_RESULT_OK) return result;
    if (data == NULL || size == 0u) {
        emulator_error(error, GB_RESULT_INVALID_ARGUMENT,
                       "ROM buffer is NULL or empty");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    if (emulator->cartridge.initialized) {
        result = destroy_hardware(emulator, error);
        if (result != GB_RESULT_OK) return result;
        if (emulator->save_ram_initialized) {
            result = gb_save_ram_destroy(&emulator->save_ram, error);
            emulator->save_ram_initialized = false;
            if (result != GB_RESULT_OK) return result;
        }
        result = gb_cartridge_unload(&emulator->cartridge, error);
        if (result != GB_RESULT_OK) return result;
        memset(&emulator->memory, 0, sizeof(emulator->memory));
        memset(&emulator->interrupt, 0, sizeof(emulator->interrupt));
        memset(&emulator->ppu, 0, sizeof(emulator->ppu));
        memset(&emulator->timer, 0, sizeof(emulator->timer));
        memset(&emulator->input, 0, sizeof(emulator->input));
        memset(&emulator->dma, 0, sizeof(emulator->dma));
        memset(&emulator->cgb, 0, sizeof(emulator->cgb));
        memset(&emulator->audio, 0, sizeof(emulator->audio));
        memset(&emulator->cpu, 0, sizeof(emulator->cpu));
        emulator->cgb_initialized = false;
    }

    result = gb_cartridge_init(&emulator->cartridge, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_cartridge_load_buffer(&emulator->cartridge, data, size,
                                      &emulator->cartridge_options, error);
    if (result != GB_RESULT_OK) {
        (void)gb_cartridge_unload(&emulator->cartridge, NULL);
        return result;
    }

    result = gb_save_ram_init(&emulator->save_ram, error);
    if (result != GB_RESULT_OK) {
        (void)gb_cartridge_unload(&emulator->cartridge, NULL);
        return result;
    }
    emulator->save_ram_initialized = true;

    result = initialize_loaded_cartridge(emulator, error);
    if (result != GB_RESULT_OK) {
        (void)destroy_hardware(emulator, NULL);
        if (emulator->save_ram_initialized) {
            (void)gb_save_ram_destroy(&emulator->save_ram, NULL);
            emulator->save_ram_initialized = false;
        }
        (void)gb_cartridge_unload(&emulator->cartridge, NULL);
        memset(&emulator->memory, 0, sizeof(emulator->memory));
        return result;
    }

    emulator->running = true;
    return GB_RESULT_OK;
}

GB_Result gb_emulator_reset(GB_Emulator *emulator, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_emulator(emulator, error);
    if (result != GB_RESULT_OK) return result;
    if (!emulator->cartridge.loaded) {
        emulator_error(error, GB_RESULT_BAD_STATE, "No ROM is loaded");
        return GB_RESULT_BAD_STATE;
    }

    result = gb_memory_reset(&emulator->memory, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_interrupt_reset(&emulator->interrupt, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_ppu_reset(&emulator->ppu, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_timer_reset(&emulator->timer, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_input_reset(&emulator->input, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_dma_reset(&emulator->dma, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_audio_reset(&emulator->audio, error);
    if (result != GB_RESULT_OK) return result;
    if (emulator->cgb_initialized) {
        result = gb_cgb_reset(&emulator->cgb, error);
        if (result != GB_RESULT_OK) return result;
    }

    GB_CPU_STARTUP startup = GB_CPU_STARTUP_DMG;
    if (emulator->memory_mode == GB_MEMORY_MODE_CGB) {
        startup = (emulator->cartridge.cgb_support == GB_CARTRIDGE_CGB_ONLY)
                      ? GB_CPU_STARTUP_CGB
                      : GB_CPU_STARTUP_CGB_DMG_COMPAT;
    }
    if (emulator->use_cpu_startup_override) {
        startup = emulator->cpu_startup_override;
    }
    result = gb_cpu_reset(&emulator->cpu, startup, error);
    if (result != GB_RESULT_OK) return result;

    emulator->running = true;
    emulator->frame_t_cycles = 0u;
    emulator->total_t_cycles = 0u;
    emulator->frame_count = 0u;
    return GB_RESULT_OK;
}

GB_Result gb_emulator_destroy(GB_Emulator *emulator, GB_Error *error)
{
    gb_error_clear(error);
    if (emulator == NULL) {
        emulator_error(error, GB_RESULT_NULL_ARGUMENT,
                       "Emulator pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!emulator->initialized) return GB_RESULT_OK;

    GB_Result first_error = GB_RESULT_OK;
    GB_Error first_detail;
    gb_error_clear(&first_detail);

    if (emulator->cartridge.initialized) {
        GB_Error child;
        gb_error_clear(&child);
        GB_Result result = destroy_hardware(emulator, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);

        if (emulator->save_ram_initialized) {
            gb_error_clear(&child);
            result = gb_save_ram_destroy(&emulator->save_ram, &child);
            preserve_first_error(&first_error, &first_detail, result, &child);
            emulator->save_ram_initialized = false;
        }

        gb_error_clear(&child);
        result = gb_cartridge_unload(&emulator->cartridge, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
    } else if (emulator->save_ram_initialized) {
        GB_Error child;
        gb_error_clear(&child);
        GB_Result result = gb_save_ram_destroy(&emulator->save_ram, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
        emulator->save_ram_initialized = false;
    }

    if (emulator->debug_initialized) {
        GB_Error child;
        gb_error_clear(&child);
        GB_Result result = gb_debug_destroy(&emulator->debug, &child);
        preserve_first_error(&first_error, &first_detail, result, &child);
        emulator->debug_initialized = false;
    }

    memset(emulator, 0, sizeof(*emulator));
    if (first_error != GB_RESULT_OK && error != NULL) *error = first_detail;
    return first_error;
}

GB_Result gb_emulator_tick_hardware(GB_Emulator *emulator,
                                    uint32_t t_cycles,
                                    GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_emulator(emulator, error);
    if (result != GB_RESULT_OK) return result;
    if (!emulator->cartridge.loaded) {
        emulator_error(error, GB_RESULT_BAD_STATE, "No ROM is loaded");
        return GB_RESULT_BAD_STATE;
    }

    uint32_t remaining = t_cycles;
    while (remaining != 0u) {
        uint32_t slice = remaining > 4u ? 4u : remaining;
        result = gb_memory_tick(&emulator->memory, slice, error);
        if (result != GB_RESULT_OK) return result;
        emulator->total_t_cycles += slice;
        emulator->frame_t_cycles += slice;
        remaining -= slice;
    }
    return GB_RESULT_OK;
}

GB_Result gb_emulator_step(GB_Emulator *emulator,
                           uint32_t *t_cycles,
                           GB_Error *error)
{
    gb_error_clear(error);
    if (t_cycles != NULL) *t_cycles = 0u;
    GB_Result result = require_emulator(emulator, error);
    if (result != GB_RESULT_OK) return result;
    if (!emulator->cartridge.loaded) {
        emulator_error(error, GB_RESULT_BAD_STATE, "No ROM is loaded");
        return GB_RESULT_BAD_STATE;
    }

    if (emulator->cgb_initialized && gb_cgb_speed_switch_paused(&emulator->cgb)) {
        /* During the CGB STOP speed-switch pause, the CPU is stopped and the
         * divider/timer/APU clocks are frozen. The LCD still advances, as does
         * HBlank DMA. Tick only those domains rather than the whole memory
         * device fabric. */
        result = gb_ppu_tick(&emulator->ppu, 4u, error);
        if (result != GB_RESULT_OK) return result;
        result = gb_cgb_tick(&emulator->cgb, 4u, error);
        if (result != GB_RESULT_OK) return result;
        if (emulator->cgb.speed_switch_pause_t_cycles >= 4u) {
            emulator->cgb.speed_switch_pause_t_cycles -= 4u;
        } else {
            emulator->cgb.speed_switch_pause_t_cycles = 0u;
        }
        if (emulator->cgb.speed_switch_pause_t_cycles == 0u) {
            result = gb_cpu_wake_from_stop(&emulator->cpu, error);
            if (result != GB_RESULT_OK) return result;
        }
        if (t_cycles != NULL) {
            *t_cycles = hardware_cycles_to_cpu_cycles(emulator, 4u);
        }
        return GB_RESULT_OK;
    }

    if (emulator->cgb_initialized && gb_cgb_cpu_is_stalled(&emulator->cgb)) {
        result = gb_emulator_tick_hardware(emulator, 4u, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) {
            *t_cycles = hardware_cycles_to_cpu_cycles(emulator, 4u);
        }
        return GB_RESULT_OK;
    }

    if (emulator->debug_initialized) {
        result = gb_debug_before_cpu_step(&emulator->debug, &emulator->cpu, error);
        if (result != GB_RESULT_OK) return result;
    }

    GB_CGBSpeed speed_before_step = GB_CGB_SPEED_NORMAL;
    if (emulator->cgb_initialized) {
        speed_before_step = gb_cgb_get_speed(&emulator->cgb);
    }

    uint32_t consumed = 0u;
    result = gb_cpu_step(&emulator->cpu, &consumed, error);
    if (result != GB_RESULT_OK) return result;

    if (consumed != 0u) {
        uint32_t hardware_cycles = cpu_cycles_to_hardware_cycles(
            emulator, consumed, speed_before_step);
        emulator->total_t_cycles += hardware_cycles;
        emulator->frame_t_cycles += hardware_cycles;
    }

    if (emulator->cgb_initialized) {
        bool speed_switched = false;
        result = gb_cgb_handle_cpu_stop(&emulator->cgb, &speed_switched, error);
        if (result != GB_RESULT_OK) return result;
        (void)speed_switched;
    }

    if (emulator->debug_initialized) {
        uint32_t hardware_cycles = cpu_cycles_to_hardware_cycles(
            emulator, consumed, speed_before_step);
        result = gb_debug_after_cpu_step(&emulator->debug, &emulator->cpu,
                                         consumed, hardware_cycles, error);
        if (result != GB_RESULT_OK) return result;
    }

    if (t_cycles != NULL) *t_cycles = consumed;
    return GB_RESULT_OK;
}

GB_Result gb_emulator_run_frame(GB_Emulator *emulator,
                                uint32_t *t_cycles,
                                bool *frame_ready,
                                GB_Error *error)
{
    gb_error_clear(error);
    if (t_cycles != NULL) *t_cycles = 0u;
    if (frame_ready != NULL) *frame_ready = false;

    GB_Result result = require_emulator(emulator, error);
    if (result != GB_RESULT_OK) return result;

    gb_ppu_clear_frame_ready(&emulator->ppu);

    uint32_t consumed_total = 0u;
    while (!gb_ppu_frame_ready(&emulator->ppu)) {
        uint32_t consumed = 0u;
        result = gb_emulator_step(emulator, &consumed, error);
        if (result != GB_RESULT_OK) return result;
        if (consumed == 0u) {
            if (gb_emulator_is_stopped(emulator)) break;
            emulator_error(error, GB_RESULT_BAD_STATE,
                           "Emulator made no progress while running a frame");
            return GB_RESULT_BAD_STATE;
        }
        consumed_total += consumed;
    }

    bool ready = gb_ppu_frame_ready(&emulator->ppu);
    if (ready) {
        ++emulator->frame_count;
        emulator->frame_t_cycles = 0u;
        if (emulator->debug_initialized) {
            gb_debug_record_frame(&emulator->debug);
        }
    }
    if (t_cycles != NULL) *t_cycles = consumed_total;
    if (frame_ready != NULL) *frame_ready = ready;
    return GB_RESULT_OK;
}

bool gb_emulator_is_stopped(const GB_Emulator *emulator)
{
    return emulator != NULL && emulator->initialized &&
           gb_cpu_is_stopped(&emulator->cpu);
}

bool gb_emulator_is_halted(const GB_Emulator *emulator)
{
    return emulator != NULL && emulator->initialized &&
           gb_cpu_is_halted(&emulator->cpu);
}

bool gb_emulator_is_cgb(const GB_Emulator *emulator)
{
    return emulator != NULL && emulator->initialized && emulator->cgb_initialized;
}

uint64_t gb_emulator_cpu_clock_hz(const GB_Emulator *emulator)
{
    if (emulator == NULL || !emulator->initialized || !emulator->cgb_initialized) {
        return GB_EMULATOR_BASE_CLOCK_HZ;
    }
    return gb_cgb_get_speed(&emulator->cgb) == GB_CGB_SPEED_DOUBLE
               ? GB_EMULATOR_BASE_CLOCK_HZ * 2ULL
               : GB_EMULATOR_BASE_CLOCK_HZ;
}

GB_Result gb_emulator_save_ram(GB_Emulator *emulator, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_emulator(emulator, error);
    if (result != GB_RESULT_OK) return result;
    if (!emulator->save_ram_initialized) {
        emulator_error(error, GB_RESULT_BAD_STATE, "Save RAM subsystem is unavailable");
        return GB_RESULT_BAD_STATE;
    }
    return gb_save_ram_save(&emulator->save_ram, error);
}

GB_Debug *gb_emulator_debug(GB_Emulator *emulator)
{
    return emulator != NULL && emulator->debug_initialized ? &emulator->debug : NULL;
}

const GB_Debug *gb_emulator_debug_const(const GB_Emulator *emulator)
{
    return emulator != NULL && emulator->debug_initialized ? &emulator->debug : NULL;
}
