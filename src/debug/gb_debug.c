#include "gb_debug.h"

#include <stdarg.h>
#include <string.h>

static void debug_error(GB_Error *error, GB_Result code, const char *message)
{
    if (error == NULL) return;
    gb_error_clear(error);
    error->code = code;
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static GB_Result require_debug(const GB_Debug *debug, GB_Error *error)
{
    if (debug == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "Debug pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!debug->initialized) {
        debug_error(error, GB_RESULT_BAD_STATE, "Debug subsystem is not initialized");
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static bool level_enabled(const GB_Debug *debug, GB_DebugLogLevel level)
{
    return debug != NULL && debug->output != NULL &&
           level != GB_DEBUG_LOG_NONE && level <= debug->log_level;
}

const char *gb_debug_log_level_name(GB_DebugLogLevel level)
{
    switch (level) {
    case GB_DEBUG_LOG_NONE:  return "NONE";
    case GB_DEBUG_LOG_ERROR: return "ERROR";
    case GB_DEBUG_LOG_WARN:  return "WARN";
    case GB_DEBUG_LOG_INFO:  return "INFO";
    case GB_DEBUG_LOG_DEBUG: return "DEBUG";
    case GB_DEBUG_LOG_TRACE: return "TRACE";
    default:                 return "UNKNOWN";
    }
}

void gb_debug_config_default(GB_DebugConfig *config)
{
    if (config == NULL) return;
    config->log_level = GB_DEBUG_LOG_NONE;
    config->trace_cpu = false;
}

GB_Result gb_debug_init(GB_Debug *debug, const GB_DebugConfig *config, GB_Error *error)
{
    gb_error_clear(error);
    if (debug == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "Debug pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    GB_DebugConfig local;
    gb_debug_config_default(&local);
    if (config != NULL) local = *config;

    memset(debug, 0, sizeof(*debug));
    debug->initialized = true;
    debug->log_level = local.log_level;
    debug->trace_cpu = local.trace_cpu;
    debug->output = stderr;
    return GB_RESULT_OK;
}

GB_Result gb_debug_destroy(GB_Debug *debug, GB_Error *error)
{
    gb_error_clear(error);
    if (debug == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "Debug pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    memset(debug, 0, sizeof(*debug));
    return GB_RESULT_OK;
}

GB_Result gb_debug_set_level(GB_Debug *debug, GB_DebugLogLevel level, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    if (level < GB_DEBUG_LOG_NONE || level > GB_DEBUG_LOG_TRACE) {
        debug_error(error, GB_RESULT_INVALID_ARGUMENT, "Invalid debug log level");
        return GB_RESULT_INVALID_ARGUMENT;
    }
    debug->log_level = level;
    return GB_RESULT_OK;
}

GB_Result gb_debug_set_trace_cpu(GB_Debug *debug, bool enabled, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    debug->trace_cpu = enabled;
    if (enabled && debug->log_level < GB_DEBUG_LOG_TRACE) {
        debug->log_level = GB_DEBUG_LOG_TRACE;
    }
    return GB_RESULT_OK;
}

GB_Result gb_debug_set_output(GB_Debug *debug, FILE *output, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    if (output == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "Debug output stream is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    debug->output = output;
    return GB_RESULT_OK;
}

GB_Result gb_debug_add_breakpoint(GB_Debug *debug, uint16_t address, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    if (gb_debug_has_breakpoint(debug, address)) return GB_RESULT_OK;
    if (debug->breakpoint_count >= GB_DEBUG_MAX_BREAKPOINTS) {
        debug_error(error, GB_RESULT_BAD_STATE, "Maximum number of execution breakpoints reached");
        return GB_RESULT_BAD_STATE;
    }
    debug->breakpoints[debug->breakpoint_count++] = address;
    return GB_RESULT_OK;
}

GB_Result gb_debug_remove_breakpoint(GB_Debug *debug, uint16_t address, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    for (size_t i = 0u; i < debug->breakpoint_count; ++i) {
        if (debug->breakpoints[i] == address) {
            memmove(&debug->breakpoints[i], &debug->breakpoints[i + 1u],
                    (debug->breakpoint_count - i - 1u) * sizeof(debug->breakpoints[0]));
            --debug->breakpoint_count;
            return GB_RESULT_OK;
        }
    }
    return GB_RESULT_OK;
}

GB_Result gb_debug_clear_breakpoints(GB_Debug *debug, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    debug->breakpoint_count = 0u;
    return GB_RESULT_OK;
}

bool gb_debug_has_breakpoint(const GB_Debug *debug, uint16_t address)
{
    if (debug == NULL || !debug->initialized) return false;
    for (size_t i = 0u; i < debug->breakpoint_count; ++i) {
        if (debug->breakpoints[i] == address) return true;
    }
    return false;
}

bool gb_debug_break_requested(const GB_Debug *debug)
{
    return debug != NULL && debug->initialized && debug->break_requested;
}

GB_DebugBreakReason gb_debug_break_reason(const GB_Debug *debug)
{
    return debug != NULL && debug->initialized
               ? debug->break_reason
               : GB_DEBUG_BREAK_NONE;
}

uint16_t gb_debug_break_pc(const GB_Debug *debug)
{
    return debug != NULL && debug->initialized ? debug->break_pc : 0u;
}

void gb_debug_clear_break(GB_Debug *debug)
{
    if (debug == NULL) return;
    debug->break_requested = false;
    debug->break_reason = GB_DEBUG_BREAK_NONE;
}

GB_Result gb_debug_before_cpu_step(GB_Debug *debug,
                                   const GB_CPU *cpu,
                                   GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    if (cpu == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "CPU pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (gb_debug_has_breakpoint(debug, cpu->r.pc)) {
        debug->break_requested = true;
        debug->break_reason = GB_DEBUG_BREAK_EXECUTION;
        debug->break_pc = cpu->r.pc;
        ++debug->counters.execution_breakpoints;
        return GB_RESULT_DEBUG_BREAK;
    }
    return GB_RESULT_OK;
}

GB_Result gb_debug_after_cpu_step(GB_Debug *debug,
                                  const GB_CPU *cpu,
                                  uint32_t cpu_t_cycles,
                                  uint32_t hardware_t_cycles,
                                  GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    if (cpu == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "CPU pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    ++debug->counters.cpu_steps;
    debug->counters.cpu_t_cycles += cpu_t_cycles;
    debug->counters.hardware_t_cycles += hardware_t_cycles;

    if (debug->trace_cpu && cpu_t_cycles != 0u) {
        if (level_enabled(debug, GB_DEBUG_LOG_TRACE)) {
            (void)fprintf(debug->output,
                          "[TRACE] PC=$%04X OP=$%02X AF=$%04X BC=$%04X DE=$%04X HL=$%04X SP=$%04X -> PC=$%04X +%u CPU-T +%u HW-T IME=%u HALT=%u STOP=%u\n",
                          cpu->current_instruction_pc,
                          cpu->current_opcode,
                          gb_cpu_get_af(cpu),
                          gb_cpu_get_bc(cpu),
                          gb_cpu_get_de(cpu),
                          gb_cpu_get_hl(cpu),
                          cpu->r.sp,
                          cpu->r.pc,
                          cpu_t_cycles,
                          hardware_t_cycles,
                          cpu->ime ? 1u : 0u,
                          cpu->halted ? 1u : 0u,
                          cpu->stopped ? 1u : 0u);
            (void)fflush(debug->output);
        }
    }
    return GB_RESULT_OK;
}

void gb_debug_record_frame(GB_Debug *debug)
{
    if (debug == NULL || !debug->initialized) return;
    ++debug->counters.frames;
}

GB_Result gb_debug_log(GB_Debug *debug,
                       GB_DebugLogLevel level,
                       const char *format,
                       GB_Error *error,
                       ...)
{
    gb_error_clear(error);
    GB_Result result = require_debug(debug, error);
    if (result != GB_RESULT_OK) return result;
    if (format == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "Debug format string is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!level_enabled(debug, level)) return GB_RESULT_OK;

    (void)fprintf(debug->output, "[%s] ", gb_debug_log_level_name(level));
    va_list args;
    va_start(args, error);
    (void)vfprintf(debug->output, format, args);
    va_end(args);
    (void)fputc('\n', debug->output);
    (void)fflush(debug->output);
    return GB_RESULT_OK;
}

GB_Result gb_debug_dump_cpu(FILE *output, const GB_CPU *cpu, GB_Error *error)
{
    gb_error_clear(error);
    if (output == NULL || cpu == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "CPU dump received a NULL argument");
        return GB_RESULT_NULL_ARGUMENT;
    }
    (void)fprintf(output,
                  "CPU: AF=$%04X BC=$%04X DE=$%04X HL=$%04X SP=$%04X PC=$%04X F=$%02X IME=%u EI_DELAY=%u HALT=%u STOP=%u HALT_BUG=%u FAULT=%u T=%llu\n",
                  gb_cpu_get_af(cpu), gb_cpu_get_bc(cpu), gb_cpu_get_de(cpu),
                  gb_cpu_get_hl(cpu), cpu->r.sp, cpu->r.pc, cpu->r.f,
                  cpu->ime ? 1u : 0u, cpu->ime_enable_delay,
                  cpu->halted ? 1u : 0u, cpu->stopped ? 1u : 0u,
                  cpu->halt_bug ? 1u : 0u, cpu->faulted ? 1u : 0u,
                  (unsigned long long)cpu->t_cycles);
    return GB_RESULT_OK;
}

GB_Result gb_debug_dump_cartridge(FILE *output, const GB_Cartridge *cartridge, GB_Error *error)
{
    gb_error_clear(error);
    if (output == NULL || cartridge == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "Cartridge dump received a NULL argument");
        return GB_RESULT_NULL_ARGUMENT;
    }
    (void)fprintf(output,
                  "CART: loaded=%u mapper=%s type=$%02X ROM=%zu bytes RAM=%zu bytes battery=%u RTC=%u CGB=%s title=%.16s\n",
                  cartridge->loaded ? 1u : 0u,
                  gb_cartridge_mapper_name(cartridge->mapper),
                  cartridge->cartridge_type_code,
                  cartridge->rom_size,
                  cartridge->ram_size,
                  cartridge->has_battery ? 1u : 0u,
                  cartridge->has_rtc ? 1u : 0u,
                  gb_cartridge_cgb_support_name(cartridge->cgb_support),
                  (const char *)cartridge->title);
    return GB_RESULT_OK;
}

GB_Result gb_debug_dump_ppu(FILE *output, const GB_PPU *ppu, GB_Error *error)
{
    gb_error_clear(error);
    if (output == NULL || ppu == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "PPU dump received a NULL argument");
        return GB_RESULT_NULL_ARGUMENT;
    }
    (void)fprintf(output,
                  "PPU: LCDC=$%02X STAT=$%02X LY=%u LYC=%u SCX=%u SCY=%u WX=%u WY=%u MODE=%u FRAME_READY=%u\n",
                  ppu->lcdc, gb_ppu_get_stat(ppu), ppu->ly, ppu->lyc,
                  ppu->scx, ppu->scy, ppu->wx, ppu->wy,
                  ppu->mode, ppu->frame_ready ? 1u : 0u);
    return GB_RESULT_OK;
}

GB_Result gb_debug_dump_memory(FILE *output,
                               const GB_Memory *memory,
                               uint16_t address,
                               size_t count,
                               GB_Error *error)
{
    gb_error_clear(error);
    if (output == NULL || memory == NULL) {
        debug_error(error, GB_RESULT_NULL_ARGUMENT, "Memory dump received a NULL argument");
        return GB_RESULT_NULL_ARGUMENT;
    }
    GB_CPU_BUS bus = gb_memory_cpu_bus((GB_Memory *)memory);
    if (bus.read8 == NULL) {
        debug_error(error, GB_RESULT_BAD_STATE, "Memory bus has no read callback");
        return GB_RESULT_BAD_STATE;
    }

    for (size_t i = 0u; i < count; ++i) {
        uint16_t current = (uint16_t)((uint32_t)address + (uint32_t)i);
        uint8_t value = 0u;
        GB_Result result = bus.read8(bus.user, current, &value, error);
        if (result != GB_RESULT_OK) return result;
        if ((i % 16u) == 0u) {
            if (i != 0u) (void)fputc('\n', output);
            (void)fprintf(output, "%04X: ", current);
        }
        (void)fprintf(output, "%02X ", value);
    }
    (void)fputc('\n', output);
    return GB_RESULT_OK;
}

const GB_DebugCounters *gb_debug_counters(const GB_Debug *debug)
{
    return (debug != NULL && debug->initialized) ? &debug->counters : NULL;
}
