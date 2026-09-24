#ifndef GB_DEBUG_H
#define GB_DEBUG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../cartridge/gb_cartridge.h"
#include "../cpu/gb_cpu.h"
#include "../memory/gb_memory.h"
#include "../ppu/gb_ppu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_DEBUG_MAX_BREAKPOINTS 64u

typedef enum GB_DebugLogLevel {
    GB_DEBUG_LOG_NONE = 0,
    GB_DEBUG_LOG_ERROR,
    GB_DEBUG_LOG_WARN,
    GB_DEBUG_LOG_INFO,
    GB_DEBUG_LOG_DEBUG,
    GB_DEBUG_LOG_TRACE
} GB_DebugLogLevel;

typedef enum GB_DebugBreakReason {
    GB_DEBUG_BREAK_NONE = 0,
    GB_DEBUG_BREAK_EXECUTION
} GB_DebugBreakReason;

typedef struct GB_DebugCounters {
    uint64_t cpu_steps;
    uint64_t cpu_t_cycles;
    uint64_t hardware_t_cycles;
    uint64_t frames;
    uint64_t execution_breakpoints;
} GB_DebugCounters;

typedef struct GB_DebugConfig {
    GB_DebugLogLevel log_level;
    bool trace_cpu;
} GB_DebugConfig;

typedef struct GB_Debug {
    bool initialized;
    GB_DebugLogLevel log_level;
    bool trace_cpu;
    FILE *output;

    uint16_t breakpoints[GB_DEBUG_MAX_BREAKPOINTS];
    size_t breakpoint_count;
    bool break_requested;
    GB_DebugBreakReason break_reason;
    uint16_t break_pc;

    GB_DebugCounters counters;
} GB_Debug;

void gb_debug_config_default(GB_DebugConfig *config);
GB_Result gb_debug_init(GB_Debug *debug, const GB_DebugConfig *config, GB_Error *error);
GB_Result gb_debug_destroy(GB_Debug *debug, GB_Error *error);

GB_Result gb_debug_set_level(GB_Debug *debug, GB_DebugLogLevel level, GB_Error *error);
GB_Result gb_debug_set_trace_cpu(GB_Debug *debug, bool enabled, GB_Error *error);
GB_Result gb_debug_set_output(GB_Debug *debug, FILE *output, GB_Error *error);

GB_Result gb_debug_add_breakpoint(GB_Debug *debug, uint16_t address, GB_Error *error);
GB_Result gb_debug_remove_breakpoint(GB_Debug *debug, uint16_t address, GB_Error *error);
GB_Result gb_debug_clear_breakpoints(GB_Debug *debug, GB_Error *error);

bool gb_debug_has_breakpoint(const GB_Debug *debug, uint16_t address);
bool gb_debug_break_requested(const GB_Debug *debug);
GB_DebugBreakReason gb_debug_break_reason(const GB_Debug *debug);
uint16_t gb_debug_break_pc(const GB_Debug *debug);
void gb_debug_clear_break(GB_Debug *debug);

GB_Result gb_debug_before_cpu_step(GB_Debug *debug,
                                   const GB_CPU *cpu,
                                   GB_Error *error);
GB_Result gb_debug_after_cpu_step(GB_Debug *debug,
                                  const GB_CPU *cpu,
                                  uint32_t cpu_t_cycles,
                                  uint32_t hardware_t_cycles,
                                  GB_Error *error);
void gb_debug_record_frame(GB_Debug *debug);

GB_Result gb_debug_log(GB_Debug *debug,
                       GB_DebugLogLevel level,
                       const char *format,
                       GB_Error *error,
                       ...);

GB_Result gb_debug_dump_cpu(FILE *output, const GB_CPU *cpu, GB_Error *error);
GB_Result gb_debug_dump_cartridge(FILE *output, const GB_Cartridge *cartridge, GB_Error *error);
GB_Result gb_debug_dump_ppu(FILE *output, const GB_PPU *ppu, GB_Error *error);
GB_Result gb_debug_dump_memory(FILE *output,
                               const GB_Memory *memory,
                               uint16_t address,
                               size_t count,
                               GB_Error *error);

const GB_DebugCounters *gb_debug_counters(const GB_Debug *debug);
const char *gb_debug_log_level_name(GB_DebugLogLevel level);

#ifdef __cplusplus
}
#endif

#endif /* GB_DEBUG_H */
