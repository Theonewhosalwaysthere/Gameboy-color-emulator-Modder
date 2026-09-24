#include "gb_cpu.h"
#include "gb_cpu_opcodes.h"

#include <stdio.h>
#include <string.h>

#define GB_CPU_M_CYCLE_TICKS 4u
#define GB_CPU_EI_DELAY_INSTRUCTIONS 2u

static void set_error_text(GB_Error *error, GB_Result code, const char *text)
{
    if (error == NULL) {
        return;
    }

    error->code = code;
    error->pc = 0;
    error->opcode = 0;
    error->has_opcode = false;
    if (text == NULL) {
        error->message[0] = '\0';
        return;
    }

    (void)snprintf(error->message, sizeof(error->message), "%s", text);
}

void gb_error_clear(GB_Error *error)
{
    if (error == NULL) {
        return;
    }

    error->code = GB_RESULT_OK;
    error->message[0] = '\0';
    error->pc = 0;
    error->opcode = 0;
    error->has_opcode = false;
}

void gb_error_set(GB_Error *error, GB_Result code, const char *message)
{
    set_error_text(error, code, message);
}

static void set_cpu_error(GB_Error *error, GB_Result code, const char *message,
                          uint16_t pc, bool has_opcode, uint8_t opcode)
{
    set_error_text(error, code, message);
    if (error != NULL) {
        error->pc = pc;
        error->has_opcode = has_opcode;
        error->opcode = opcode;
    }
}

static GB_Result validate_cpu(const GB_CPU *cpu, GB_Error *error)
{
    if (cpu == NULL) {
        set_cpu_error(error, GB_RESULT_NULL_ARGUMENT, "CPU pointer is NULL", 0, false, 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (cpu->bus.read8 == NULL || cpu->bus.write8 == NULL) {
        set_cpu_error(error, GB_RESULT_NO_BUS,
                      "CPU bus is incomplete: read8 and write8 callbacks are required",
                      cpu->r.pc, false, 0);
        return GB_RESULT_NO_BUS;
    }

    return GB_RESULT_OK;
}

static uint16_t get_pair(uint8_t hi, uint8_t lo)
{
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static void set_pair(uint8_t *hi, uint8_t *lo, uint16_t value)
{
    *hi = (uint8_t)(value >> 8);
    *lo = (uint8_t)value;
}

static void sanitize_flags(GB_CPU *cpu)
{
    cpu->r.f &= 0xF0u;
}

uint16_t gb_cpu_get_af(const GB_CPU *cpu)
{
    return (cpu == NULL) ? 0 : get_pair(cpu->r.a, (uint8_t)(cpu->r.f & 0xF0u));
}

uint16_t gb_cpu_get_bc(const GB_CPU *cpu)
{
    return (cpu == NULL) ? 0 : get_pair(cpu->r.b, cpu->r.c);
}

uint16_t gb_cpu_get_de(const GB_CPU *cpu)
{
    return (cpu == NULL) ? 0 : get_pair(cpu->r.d, cpu->r.e);
}

uint16_t gb_cpu_get_hl(const GB_CPU *cpu)
{
    return (cpu == NULL) ? 0 : get_pair(cpu->r.h, cpu->r.l);
}

void gb_cpu_set_af(GB_CPU *cpu, uint16_t value)
{
    if (cpu == NULL) {
        return;
    }
    set_pair(&cpu->r.a, &cpu->r.f, (uint16_t)(value & 0xFFF0u));
}

void gb_cpu_set_bc(GB_CPU *cpu, uint16_t value)
{
    if (cpu == NULL) {
        return;
    }
    set_pair(&cpu->r.b, &cpu->r.c, value);
}

void gb_cpu_set_de(GB_CPU *cpu, uint16_t value)
{
    if (cpu == NULL) {
        return;
    }
    set_pair(&cpu->r.d, &cpu->r.e, value);
}

void gb_cpu_set_hl(GB_CPU *cpu, uint16_t value)
{
    if (cpu == NULL) {
        return;
    }
    set_pair(&cpu->r.h, &cpu->r.l, value);
}

static void set_cold_state(GB_CPU *cpu)
{
    memset(&cpu->r, 0, sizeof(cpu->r));
    cpu->r.sp = 0;
    cpu->r.pc = 0;
    cpu->ime = false;
    cpu->ime_enable_delay = 0;
    cpu->halted = false;
    cpu->stopped = false;
    cpu->halt_bug = false;
    cpu->faulted = false;
    cpu->stop_operand = 0;
    cpu->current_opcode = 0;
    cpu->current_instruction_pc = 0;
    cpu->t_cycles = 0;
    sanitize_flags(cpu);
}

GB_Result gb_cpu_init(GB_CPU *cpu, const GB_CPU_BUS *bus, GB_Error *error)
{
    gb_error_clear(error);

    if (cpu == NULL || bus == NULL) {
        set_cpu_error(error, GB_RESULT_NULL_ARGUMENT, "CPU and bus pointers are required", 0, false, 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (bus->read8 == NULL || bus->write8 == NULL) {
        set_cpu_error(error, GB_RESULT_NO_BUS,
                      "CPU bus requires read8 and write8 callbacks", 0, false, 0);
        return GB_RESULT_NO_BUS;
    }

    memset(cpu, 0, sizeof(*cpu));
    cpu->bus = *bus;
    set_cold_state(cpu);
    cpu->bus = *bus;
    return GB_RESULT_OK;
}

GB_Result gb_cpu_reset(GB_CPU *cpu, GB_CPU_STARTUP startup, GB_Error *error)
{
    gb_error_clear(error);

    if (cpu == NULL) {
        set_cpu_error(error, GB_RESULT_NULL_ARGUMENT, "CPU pointer is NULL", 0, false, 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (cpu->bus.read8 == NULL || cpu->bus.write8 == NULL) {
        set_cpu_error(error, GB_RESULT_NO_BUS,
                      "Cannot reset CPU with an incomplete bus", 0, false, 0);
        return GB_RESULT_NO_BUS;
    }

    set_cold_state(cpu);

    switch (startup) {
    case GB_CPU_STARTUP_COLD:
        break;

    case GB_CPU_STARTUP_DMG:
        gb_cpu_set_af(cpu, 0x01B0u);
        gb_cpu_set_bc(cpu, 0x0013u);
        gb_cpu_set_de(cpu, 0x00D8u);
        gb_cpu_set_hl(cpu, 0x014Du);
        cpu->r.sp = 0xFFFEu;
        cpu->r.pc = 0x0100u;
        break;

    case GB_CPU_STARTUP_CGB:
        gb_cpu_set_af(cpu, 0x1180u);
        gb_cpu_set_bc(cpu, 0x0000u);
        gb_cpu_set_de(cpu, 0xFF56u);
        gb_cpu_set_hl(cpu, 0x000Du);
        cpu->r.sp = 0xFFFEu;
        cpu->r.pc = 0x0100u;
        break;

    case GB_CPU_STARTUP_CGB_DMG_COMPAT:
        gb_cpu_set_af(cpu, 0x1180u);
        gb_cpu_set_bc(cpu, 0x3C00u);
        gb_cpu_set_de(cpu, 0x0008u);
        gb_cpu_set_hl(cpu, 0x007Cu);
        cpu->r.sp = 0xFFFEu;
        cpu->r.pc = 0x0100u;
        break;

    default:
        set_cpu_error(error, GB_RESULT_INVALID_ARGUMENT,
                      "Unknown CPU startup configuration", 0, false, 0);
        return GB_RESULT_INVALID_ARGUMENT;
    }

    sanitize_flags(cpu);
    return GB_RESULT_OK;
}

static GB_Result cpu_tick(GB_CPU *cpu, uint32_t t_cycles, GB_Error *error)
{
    if (t_cycles == 0) {
        return GB_RESULT_OK;
    }

    if (cpu->bus.tick != NULL) {
        GB_Error bus_error;
        gb_error_clear(&bus_error);
        GB_Result result = cpu->bus.tick(cpu->bus.user, t_cycles, &bus_error);
        if (result != GB_RESULT_OK) {
            if (error != NULL) {
                *error = bus_error;
                if (error->message[0] == '\0') {
                    (void)snprintf(error->message, sizeof(error->message),
                                   "Bus tick failed while executing opcode $%02X", cpu->current_opcode);
                }
                error->code = GB_RESULT_BUS_TICK;
                error->pc = cpu->current_instruction_pc;
                error->opcode = cpu->current_opcode;
                error->has_opcode = true;
            }
            return GB_RESULT_BUS_TICK;
        }
    }

    cpu->t_cycles += t_cycles;
    return GB_RESULT_OK;
}

static GB_Result cpu_get_pending_interrupts(GB_CPU *cpu, uint8_t *pending, GB_Error *error)
{
    if (pending == NULL) {
        set_cpu_error(error, GB_RESULT_NULL_ARGUMENT,
                      "Pending-interrupt output pointer is NULL",
                      cpu->r.pc, false, 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    *pending = 0;
    if (cpu->bus.get_pending_interrupts == NULL) {
        return GB_RESULT_OK;
    }

    GB_Error bus_error;
    gb_error_clear(&bus_error);
    GB_Result result = cpu->bus.get_pending_interrupts(cpu->bus.user, pending, &bus_error);
    if (result != GB_RESULT_OK) {
        if (error != NULL) {
            *error = bus_error;
            if (error->message[0] == '\0') {
                (void)snprintf(error->message, sizeof(error->message),
                               "Interrupt pending-state query failed");
            }
            error->code = GB_RESULT_INTERRUPT_ERROR;
            error->pc = cpu->r.pc;
            error->opcode = cpu->current_opcode;
            error->has_opcode = true;
        }
        return GB_RESULT_INTERRUPT_ERROR;
    }

    *pending &= 0x1Fu;
    return GB_RESULT_OK;
}

static uint8_t highest_priority_interrupt(uint8_t pending)
{
    static const uint8_t bits[5] = {
        GB_INTERRUPT_VBLANK,
        GB_INTERRUPT_STAT,
        GB_INTERRUPT_TIMER,
        GB_INTERRUPT_SERIAL,
        GB_INTERRUPT_JOYPAD
    };

    for (size_t i = 0; i < 5; ++i) {
        if ((pending & bits[i]) != 0) {
            return bits[i];
        }
    }
    return 0;
}

static uint16_t interrupt_vector(uint8_t interrupt_mask)
{
    switch (interrupt_mask) {
    case GB_INTERRUPT_VBLANK: return 0x0040u;
    case GB_INTERRUPT_STAT:   return 0x0048u;
    case GB_INTERRUPT_TIMER:  return 0x0050u;
    case GB_INTERRUPT_SERIAL: return 0x0058u;
    case GB_INTERRUPT_JOYPAD: return 0x0060u;
    default:                  return 0x0000u;
    }
}

static GB_Result cpu_bus_write8(GB_CPU *cpu, uint16_t address, uint8_t value, GB_Error *error)
{
    GB_Error bus_error;
    gb_error_clear(&bus_error);
    GB_Result result = cpu->bus.write8(cpu->bus.user, address, value, &bus_error);
    if (result != GB_RESULT_OK) {
        if (error != NULL) {
            *error = bus_error;
            error->code = GB_RESULT_BUS_WRITE;
            error->pc = cpu->current_instruction_pc;
            error->opcode = cpu->current_opcode;
            error->has_opcode = true;
            if (error->message[0] == '\0') {
                (void)snprintf(error->message, sizeof(error->message),
                               "Bus write failed at $%04X", address);
            }
        }
        return GB_RESULT_BUS_WRITE;
    }

    return cpu_tick(cpu, GB_CPU_M_CYCLE_TICKS, error);
}

static GB_Result cpu_bus_read8(GB_CPU *cpu, uint16_t address, uint8_t *value, GB_Error *error)
{
    if (value == NULL) {
        set_cpu_error(error, GB_RESULT_NULL_ARGUMENT,
                      "Bus read output pointer is NULL",
                      cpu->current_instruction_pc, true, cpu->current_opcode);
        return GB_RESULT_NULL_ARGUMENT;
    }

    GB_Error bus_error;
    gb_error_clear(&bus_error);
    GB_Result result = cpu->bus.read8(cpu->bus.user, address, value, &bus_error);
    if (result != GB_RESULT_OK) {
        if (error != NULL) {
            *error = bus_error;
            error->code = GB_RESULT_BUS_READ;
            error->pc = cpu->current_instruction_pc;
            error->opcode = cpu->current_opcode;
            error->has_opcode = true;
            if (error->message[0] == '\0') {
                (void)snprintf(error->message, sizeof(error->message),
                               "Bus read failed at $%04X", address);
            }
        }
        return GB_RESULT_BUS_READ;
    }

    return cpu_tick(cpu, GB_CPU_M_CYCLE_TICKS, error);
}

/* One internal CPU machine cycle with no bus transaction. */
static GB_Result cpu_idle_mcycle(GB_CPU *cpu, GB_Error *error)
{
    return cpu_tick(cpu, GB_CPU_M_CYCLE_TICKS, error);
}

static GB_Result cpu_interrupt_service(GB_CPU *cpu, uint8_t pending, uint32_t *t_cycles, GB_Error *error)
{
    uint8_t interrupt_mask = highest_priority_interrupt(pending);
    if (interrupt_mask == 0) {
        set_cpu_error(error, GB_RESULT_BAD_STATE,
                      "Interrupt service requested without a valid pending interrupt",
                      cpu->r.pc, false, 0);
        return GB_RESULT_BAD_STATE;
    }

    if (cpu->bus.acknowledge_interrupt == NULL) {
        set_cpu_error(error, GB_RESULT_INTERRUPT_ERROR,
                      "Interrupt service requires an acknowledge_interrupt bus callback",
                      cpu->r.pc, false, 0);
        return GB_RESULT_INTERRUPT_ERROR;
    }

    GB_Error bus_error;
    gb_error_clear(&bus_error);
    GB_Result result = cpu->bus.acknowledge_interrupt(cpu->bus.user, interrupt_mask, &bus_error);
    if (result != GB_RESULT_OK) {
        if (error != NULL) {
            *error = bus_error;
            error->code = GB_RESULT_INTERRUPT_ERROR;
            error->pc = cpu->r.pc;
            error->has_opcode = false;
            if (error->message[0] == '\0') {
                (void)snprintf(error->message, sizeof(error->message),
                               "Failed to acknowledge interrupt $%02X", interrupt_mask);
            }
        }
        return GB_RESULT_INTERRUPT_ERROR;
    }

    cpu->ime = false;
    cpu->ime_enable_delay = 0;
    cpu->halted = false;

    /* Interrupt entry is five machine cycles. Two are internal/wait cycles. */
    result = cpu_idle_mcycle(cpu, error);
    if (result != GB_RESULT_OK) return result;
    result = cpu_idle_mcycle(cpu, error);
    if (result != GB_RESULT_OK) return result;

    /* Interrupt dispatch pushes PC exactly like a hardware CALL/RST stack write. */
    cpu->r.sp = (uint16_t)(cpu->r.sp - 1u);
    result = cpu_bus_write8(cpu, cpu->r.sp, (uint8_t)(cpu->r.pc >> 8), error);
    if (result != GB_RESULT_OK) return result;

    cpu->r.sp = (uint16_t)(cpu->r.sp - 1u);
    result = cpu_bus_write8(cpu, cpu->r.sp, (uint8_t)cpu->r.pc, error);
    if (result != GB_RESULT_OK) return result;

    result = cpu_idle_mcycle(cpu, error);
    if (result != GB_RESULT_OK) return result;

    cpu->r.pc = interrupt_vector(interrupt_mask);

    if (t_cycles != NULL) {
        *t_cycles = 20u;
    }
    return GB_RESULT_OK;
}

GB_Result gb_cpu_step(GB_CPU *cpu, uint32_t *t_cycles, GB_Error *error)
{
    gb_error_clear(error);
    if (t_cycles != NULL) {
        *t_cycles = 0;
    }

    GB_Result result = validate_cpu(cpu, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    sanitize_flags(cpu);

    if (cpu->faulted) {
        set_cpu_error(error, GB_RESULT_BAD_STATE,
                      "CPU is faulted after an invalid opcode; reset is required",
                      cpu->r.pc, true, cpu->current_opcode);
        return GB_RESULT_BAD_STATE;
    }

    if (cpu->stopped) {
        /* STOP is exited explicitly by a later input/speed-control subsystem. */
        return GB_RESULT_OK;
    }

    uint8_t pending = 0;
    result = cpu_get_pending_interrupts(cpu, &pending, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    if (cpu->halted) {
        if (pending == 0) {
            /* A halted CPU stops instruction execution, but the rest of the machine keeps clocking. */
            result = cpu_idle_mcycle(cpu, error);
            if (result == GB_RESULT_OK && t_cycles != NULL) {
                *t_cycles = 4u;
            }
            return result;
        }

        cpu->halted = false;
        if (cpu->ime) {
            return cpu_interrupt_service(cpu, pending, t_cycles, error);
        }
        /* IME=0: a pending interrupt only wakes HALT. Continue with the next instruction. */
    }

    if (cpu->ime && pending != 0) {
        return cpu_interrupt_service(cpu, pending, t_cycles, error);
    }

    cpu->current_instruction_pc = cpu->r.pc;
    cpu->current_opcode = 0;

    uint8_t opcode = 0;
    result = cpu_bus_read8(cpu, cpu->r.pc, &opcode, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    /* The HALT bug suppresses exactly one PC increment on the next opcode fetch. */
    if (cpu->halt_bug) {
        cpu->halt_bug = false;
    } else {
        cpu->r.pc = (uint16_t)(cpu->r.pc + 1u);
    }

    cpu->current_opcode = opcode;
    result = gb_cpu_execute_opcode(cpu, opcode, t_cycles, error);
    if (result != GB_RESULT_OK) {
        return result;
    }

    sanitize_flags(cpu);

    if (cpu->ime_enable_delay != 0) {
        cpu->ime_enable_delay = (uint8_t)(cpu->ime_enable_delay - 1u);
        if (cpu->ime_enable_delay == 0) {
            cpu->ime = true;
        }
    }

    return GB_RESULT_OK;
}

GB_Result gb_cpu_wake_from_stop(GB_CPU *cpu, GB_Error *error)
{
    gb_error_clear(error);
    if (cpu == NULL) {
        set_cpu_error(error, GB_RESULT_NULL_ARGUMENT, "CPU pointer is NULL", 0, false, 0);
        return GB_RESULT_NULL_ARGUMENT;
    }

    if (!cpu->stopped) {
        set_cpu_error(error, GB_RESULT_BAD_STATE, "CPU is not currently in STOP mode",
                      cpu->r.pc, false, 0);
        return GB_RESULT_BAD_STATE;
    }

    cpu->stopped = false;
    return GB_RESULT_OK;
}

bool gb_cpu_is_halted(const GB_CPU *cpu)
{
    return cpu != NULL && cpu->halted;
}

bool gb_cpu_is_stopped(const GB_CPU *cpu)
{
    return cpu != NULL && cpu->stopped;
}

bool gb_cpu_is_faulted(const GB_CPU *cpu)
{
    return cpu != NULL && cpu->faulted;
}

bool gb_cpu_interrupts_enabled(const GB_CPU *cpu)
{
    return cpu != NULL && cpu->ime;
}
