#ifndef GB_CPU_H
#define GB_CPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GB_Result {
    GB_RESULT_OK = 0,
    GB_RESULT_NULL_ARGUMENT,
    GB_RESULT_INVALID_ARGUMENT,
    GB_RESULT_BAD_STATE,
    GB_RESULT_NO_BUS,
    GB_RESULT_BUS_READ,
    GB_RESULT_BUS_WRITE,
    GB_RESULT_BUS_TICK,
    GB_RESULT_INTERRUPT_ERROR,
    GB_RESULT_PPU_ERROR,
    GB_RESULT_INPUT_ERROR,
    GB_RESULT_INVALID_OPCODE,
    GB_RESULT_ALLOCATION,
    GB_RESULT_FILE_OPEN,
    GB_RESULT_FILE_READ,
    GB_RESULT_FILE_IO,
    GB_RESULT_INVALID_ROM,
    GB_RESULT_ROM_SIZE,
    GB_RESULT_UNSUPPORTED_CARTRIDGE,
    GB_RESULT_UNSUPPORTED,
    GB_RESULT_DEBUG_BREAK
} GB_Result;

typedef struct GB_Error {
    GB_Result code;
    char message[256];
    uint16_t pc;
    uint8_t opcode;
    bool has_opcode;
} GB_Error;

void gb_error_clear(GB_Error *error);
void gb_error_set(GB_Error *error, GB_Result code, const char *message);

/*
 * The LR35902 has five externally visible interrupt request bits.
 * Bit 0 has the highest priority.
 */
enum {
    GB_INTERRUPT_VBLANK = 0x01u,
    GB_INTERRUPT_STAT   = 0x02u,
    GB_INTERRUPT_TIMER  = 0x04u,
    GB_INTERRUPT_SERIAL = 0x08u,
    GB_INTERRUPT_JOYPAD = 0x10u
};

typedef GB_Result (*GB_BusRead8)(void *user, uint16_t address, uint8_t *value, GB_Error *error);
typedef GB_Result (*GB_BusWrite8)(void *user, uint16_t address, uint8_t value, GB_Error *error);

/* Called once for each emulated machine cycle (M-cycle = 4 T-cycles). */
typedef GB_Result (*GB_BusTick)(void *user, uint32_t t_cycles, GB_Error *error);

/*
 * Return the currently serviceable interrupt requests as a 5-bit mask.
 * The memory/interrupt subsystem is responsible for combining IF and IE.
 */
typedef GB_Result (*GB_GetPendingInterrupts)(void *user, uint8_t *pending_mask, GB_Error *error);

/* Clear the interrupt request bit which the CPU accepted. */
typedef GB_Result (*GB_AcknowledgeInterrupt)(void *user, uint8_t interrupt_mask, GB_Error *error);

typedef struct GB_CPU_BUS {
    void *user;
    GB_BusRead8 read8;
    GB_BusWrite8 write8;
    GB_BusTick tick;
    GB_GetPendingInterrupts get_pending_interrupts;
    GB_AcknowledgeInterrupt acknowledge_interrupt;
} GB_CPU_BUS;

typedef enum GB_CPU_STARTUP {
    /* Deterministic CPU state for unit tests / cold-core bring-up. */
    GB_CPU_STARTUP_COLD = 0,

    /* State observed when the mainline DMG boot ROM hands control to the cartridge. */
    GB_CPU_STARTUP_DMG,

    /* State observed when the official CGB boot ROM hands control to a CGB-native cartridge. */
    GB_CPU_STARTUP_CGB,

    /* State observed when the official CGB boot ROM hands control to a DMG cartridge. */
    GB_CPU_STARTUP_CGB_DMG_COMPAT
} GB_CPU_STARTUP;

typedef struct GB_CPU_REGISTERS {
    uint8_t a;
    uint8_t f;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;
    uint16_t sp;
    uint16_t pc;
} GB_CPU_REGISTERS;

typedef struct GB_CPU {
    GB_CPU_REGISTERS r;
    GB_CPU_BUS bus;

    bool ime;
    uint8_t ime_enable_delay;
    bool halted;
    bool stopped;
    bool halt_bug;
    bool faulted;

    /* Byte following STOP, kept for diagnostics and future CGB speed-switch handling. */
    uint8_t stop_operand;

    uint8_t current_opcode;
    uint16_t current_instruction_pc;

    uint64_t t_cycles;
} GB_CPU;

GB_Result gb_cpu_init(GB_CPU *cpu, const GB_CPU_BUS *bus, GB_Error *error);
GB_Result gb_cpu_reset(GB_CPU *cpu, GB_CPU_STARTUP startup, GB_Error *error);

/* Execute exactly one CPU instruction, interrupt dispatch, or halted machine cycle. */
GB_Result gb_cpu_step(GB_CPU *cpu, uint32_t *t_cycles, GB_Error *error);

/* Used by the future input / CGB power-control subsystem to wake STOP mode. */
GB_Result gb_cpu_wake_from_stop(GB_CPU *cpu, GB_Error *error);

bool gb_cpu_is_halted(const GB_CPU *cpu);
bool gb_cpu_is_stopped(const GB_CPU *cpu);
bool gb_cpu_is_faulted(const GB_CPU *cpu);
bool gb_cpu_interrupts_enabled(const GB_CPU *cpu);

uint16_t gb_cpu_get_af(const GB_CPU *cpu);
uint16_t gb_cpu_get_bc(const GB_CPU *cpu);
uint16_t gb_cpu_get_de(const GB_CPU *cpu);
uint16_t gb_cpu_get_hl(const GB_CPU *cpu);

void gb_cpu_set_af(GB_CPU *cpu, uint16_t value);
void gb_cpu_set_bc(GB_CPU *cpu, uint16_t value);
void gb_cpu_set_de(GB_CPU *cpu, uint16_t value);
void gb_cpu_set_hl(GB_CPU *cpu, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* GB_CPU_H */
