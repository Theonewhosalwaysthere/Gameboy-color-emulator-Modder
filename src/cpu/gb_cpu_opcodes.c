#include "gb_cpu_opcodes.h"

#include <stdio.h>

#define GB_FLAG_Z 0x80u
#define GB_FLAG_N 0x40u
#define GB_FLAG_H 0x20u
#define GB_FLAG_C 0x10u

#define GB_REG_HL 6u

static void op_error(GB_Error *error, GB_Result code, uint16_t pc, uint8_t opcode, const char *message)
{
    if (error == NULL) {
        return;
    }
    error->code = code;
    error->pc = pc;
    error->opcode = opcode;
    error->has_opcode = true;
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static uint16_t bc(const GB_CPU *cpu) { return gb_cpu_get_bc(cpu); }
static uint16_t de(const GB_CPU *cpu) { return gb_cpu_get_de(cpu); }
static uint16_t hl(const GB_CPU *cpu) { return gb_cpu_get_hl(cpu); }

static void set_bc(GB_CPU *cpu, uint16_t value) { gb_cpu_set_bc(cpu, value); }
static void set_de(GB_CPU *cpu, uint16_t value) { gb_cpu_set_de(cpu, value); }
static void set_hl(GB_CPU *cpu, uint16_t value) { gb_cpu_set_hl(cpu, value); }

static bool flag_z(const GB_CPU *cpu) { return (cpu->r.f & GB_FLAG_Z) != 0; }
static bool flag_c(const GB_CPU *cpu) { return (cpu->r.f & GB_FLAG_C) != 0; }

static void set_flag(GB_CPU *cpu, uint8_t mask, bool value)
{
    if (value) cpu->r.f |= mask;
    else cpu->r.f &= (uint8_t)~mask;
}

static void set_flags(GB_CPU *cpu, bool z, bool n, bool h, bool c)
{
    cpu->r.f = (uint8_t)((z ? GB_FLAG_Z : 0u) |
                         (n ? GB_FLAG_N : 0u) |
                         (h ? GB_FLAG_H : 0u) |
                         (c ? GB_FLAG_C : 0u));
}

static GB_Result read8_timed(GB_CPU *cpu, uint16_t address, uint8_t *value, GB_Error *error)
{
    if (value == NULL) {
        op_error(error, GB_RESULT_NULL_ARGUMENT, cpu->current_instruction_pc,
                 cpu->current_opcode, "NULL output pointer for memory read");
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
                (void)snprintf(error->message, sizeof(error->message), "Memory read failed at $%04X", address);
            }
        }
        return GB_RESULT_BUS_READ;
    }

    if (cpu->bus.tick != NULL) {
        GB_Error tick_error;
        gb_error_clear(&tick_error);
        result = cpu->bus.tick(cpu->bus.user, 4u, &tick_error);
        if (result != GB_RESULT_OK) {
            if (error != NULL) {
                *error = tick_error;
                error->code = GB_RESULT_BUS_TICK;
                error->pc = cpu->current_instruction_pc;
                error->opcode = cpu->current_opcode;
                error->has_opcode = true;
                if (error->message[0] == '\0') {
                    (void)snprintf(error->message, sizeof(error->message), "Bus tick failed after memory read");
                }
            }
            return GB_RESULT_BUS_TICK;
        }
    }
    cpu->t_cycles += 4u;
    return GB_RESULT_OK;
}

static GB_Result write8_timed(GB_CPU *cpu, uint16_t address, uint8_t value, GB_Error *error)
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
                (void)snprintf(error->message, sizeof(error->message), "Memory write failed at $%04X", address);
            }
        }
        return GB_RESULT_BUS_WRITE;
    }

    if (cpu->bus.tick != NULL) {
        GB_Error tick_error;
        gb_error_clear(&tick_error);
        result = cpu->bus.tick(cpu->bus.user, 4u, &tick_error);
        if (result != GB_RESULT_OK) {
            if (error != NULL) {
                *error = tick_error;
                error->code = GB_RESULT_BUS_TICK;
                error->pc = cpu->current_instruction_pc;
                error->opcode = cpu->current_opcode;
                error->has_opcode = true;
                if (error->message[0] == '\0') {
                    (void)snprintf(error->message, sizeof(error->message), "Bus tick failed after memory write");
                }
            }
            return GB_RESULT_BUS_TICK;
        }
    }
    cpu->t_cycles += 4u;
    return GB_RESULT_OK;
}

static GB_Result idle_m(GB_CPU *cpu, GB_Error *error)
{
    if (cpu->bus.tick != NULL) {
        GB_Error tick_error;
        gb_error_clear(&tick_error);
        GB_Result result = cpu->bus.tick(cpu->bus.user, 4u, &tick_error);
        if (result != GB_RESULT_OK) {
            if (error != NULL) {
                *error = tick_error;
                error->code = GB_RESULT_BUS_TICK;
                error->pc = cpu->current_instruction_pc;
                error->opcode = cpu->current_opcode;
                error->has_opcode = true;
            }
            return GB_RESULT_BUS_TICK;
        }
    }
    cpu->t_cycles += 4u;
    return GB_RESULT_OK;
}

static GB_Result fetch8(GB_CPU *cpu, uint8_t *value, GB_Error *error)
{
    GB_Result result = read8_timed(cpu, cpu->r.pc, value, error);
    if (result != GB_RESULT_OK) {
        return result;
    }
    cpu->r.pc = (uint16_t)(cpu->r.pc + 1u);
    return GB_RESULT_OK;
}

static GB_Result fetch16(GB_CPU *cpu, uint16_t *value, GB_Error *error)
{
    uint8_t lo8 = 0;
    uint8_t hi8 = 0;
    GB_Result result = fetch8(cpu, &lo8, error);
    if (result != GB_RESULT_OK) return result;
    result = fetch8(cpu, &hi8, error);
    if (result != GB_RESULT_OK) return result;
    *value = (uint16_t)(((uint16_t)hi8 << 8) | lo8);
    return GB_RESULT_OK;
}

static GB_Result read8_at_hl(GB_CPU *cpu, uint8_t *value, GB_Error *error)
{
    return read8_timed(cpu, hl(cpu), value, error);
}

static GB_Result write8_at_hl(GB_CPU *cpu, uint8_t value, GB_Error *error)
{
    return write8_timed(cpu, hl(cpu), value, error);
}

static GB_Result write16_at(GB_CPU *cpu, uint16_t address, uint16_t value, GB_Error *error)
{
    GB_Result result = write8_timed(cpu, address, (uint8_t)value, error);
    if (result != GB_RESULT_OK) return result;
    return write8_timed(cpu, (uint16_t)(address + 1u), (uint8_t)(value >> 8), error);
}

static GB_Result push16(GB_CPU *cpu, uint16_t value, GB_Error *error)
{
    cpu->r.sp = (uint16_t)(cpu->r.sp - 1u);
    GB_Result result = write8_timed(cpu, cpu->r.sp, (uint8_t)(value >> 8), error);
    if (result != GB_RESULT_OK) return result;
    cpu->r.sp = (uint16_t)(cpu->r.sp - 1u);
    return write8_timed(cpu, cpu->r.sp, (uint8_t)value, error);
}

static GB_Result pop16(GB_CPU *cpu, uint16_t *value, GB_Error *error)
{
    uint8_t lo8 = 0;
    uint8_t hi8 = 0;
    GB_Result result = read8_timed(cpu, cpu->r.sp, &lo8, error);
    if (result != GB_RESULT_OK) return result;
    cpu->r.sp = (uint16_t)(cpu->r.sp + 1u);
    result = read8_timed(cpu, cpu->r.sp, &hi8, error);
    if (result != GB_RESULT_OK) return result;
    cpu->r.sp = (uint16_t)(cpu->r.sp + 1u);
    *value = (uint16_t)(((uint16_t)hi8 << 8) | lo8);
    return GB_RESULT_OK;
}

static uint8_t reg8_plain_read(const GB_CPU *cpu, unsigned index)
{
    switch (index & 7u) {
    case 0: return cpu->r.b;
    case 1: return cpu->r.c;
    case 2: return cpu->r.d;
    case 3: return cpu->r.e;
    case 4: return cpu->r.h;
    case 5: return cpu->r.l;
    case 7: return cpu->r.a;
    default: return 0;
    }
}

static void reg8_plain_write(GB_CPU *cpu, unsigned index, uint8_t value)
{
    switch (index & 7u) {
    case 0: cpu->r.b = value; break;
    case 1: cpu->r.c = value; break;
    case 2: cpu->r.d = value; break;
    case 3: cpu->r.e = value; break;
    case 4: cpu->r.h = value; break;
    case 5: cpu->r.l = value; break;
    case 7: cpu->r.a = value; break;
    default: break;
    }
}

static GB_Result read_r8(GB_CPU *cpu, unsigned index, uint8_t *value, GB_Error *error)
{
    if ((index & 7u) == GB_REG_HL) {
        return read8_at_hl(cpu, value, error);
    }
    *value = reg8_plain_read(cpu, index);
    return GB_RESULT_OK;
}

static GB_Result write_r8(GB_CPU *cpu, unsigned index, uint8_t value, GB_Error *error)
{
    if ((index & 7u) == GB_REG_HL) {
        return write8_at_hl(cpu, value, error);
    }
    reg8_plain_write(cpu, index, value);
    return GB_RESULT_OK;
}

static uint16_t get_r16(unsigned index, const GB_CPU *cpu)
{
    switch (index & 3u) {
    case 0: return bc(cpu);
    case 1: return de(cpu);
    case 2: return hl(cpu);
    default: return cpu->r.sp;
    }
}

static void set_r16(unsigned index, GB_CPU *cpu, uint16_t value)
{
    switch (index & 3u) {
    case 0: set_bc(cpu, value); break;
    case 1: set_de(cpu, value); break;
    case 2: set_hl(cpu, value); break;
    default: cpu->r.sp = value; break;
    }
}

static uint16_t get_r16_stack(unsigned index, const GB_CPU *cpu)
{
    switch (index & 3u) {
    case 0: return bc(cpu);
    case 1: return de(cpu);
    case 2: return hl(cpu);
    default: return gb_cpu_get_af(cpu);
    }
}

static GB_Result set_r16_stack(unsigned index, GB_CPU *cpu, uint16_t value, GB_Error *error)
{
    switch (index & 3u) {
    case 0: set_bc(cpu, value); break;
    case 1: set_de(cpu, value); break;
    case 2: set_hl(cpu, value); break;
    default:
        gb_cpu_set_af(cpu, value);
        break;
    }
    (void)error;
    return GB_RESULT_OK;
}

static bool condition_true(const GB_CPU *cpu, unsigned cc)
{
    switch (cc & 3u) {
    case 0: return !flag_z(cpu); /* NZ */
    case 1: return flag_z(cpu);  /* Z */
    case 2: return !flag_c(cpu); /* NC */
    default: return flag_c(cpu); /* C */
    }
}

static uint8_t add8(GB_CPU *cpu, uint8_t lhs, uint8_t rhs, uint8_t carry)
{
    uint16_t sum = (uint16_t)((uint32_t)lhs + (uint32_t)rhs + (uint32_t)carry);
    uint8_t result = (uint8_t)sum;
    set_flags(cpu, result == 0,
              false,
              (((lhs & 0x0Fu) + (rhs & 0x0Fu) + carry) > 0x0Fu),
              sum > 0x00FFu);
    return result;
}

static uint8_t sub8(GB_CPU *cpu, uint8_t lhs, uint8_t rhs, uint8_t borrow)
{
    int result_i = (int)lhs - (int)rhs - (int)borrow;
    uint8_t result = (uint8_t)result_i;
    set_flags(cpu, result == 0,
              true,
              ((lhs & 0x0Fu) < ((rhs & 0x0Fu) + borrow)),
              (lhs < (uint16_t)rhs + borrow));
    return result;
}

static uint16_t add16_hl(GB_CPU *cpu, uint16_t lhs, uint16_t rhs)
{
    uint32_t sum = (uint32_t)lhs + (uint32_t)rhs;
    bool old_z = flag_z(cpu);
    cpu->r.f = (uint8_t)((old_z ? GB_FLAG_Z : 0u) |
                         (((lhs & 0x0FFFu) + (rhs & 0x0FFFu)) > 0x0FFFu ? GB_FLAG_H : 0u) |
                         (sum > 0xFFFFu ? GB_FLAG_C : 0u));
    return (uint16_t)sum;
}

static uint8_t inc8(GB_CPU *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)(value + 1u);
    set_flag(cpu, GB_FLAG_Z, result == 0);
    set_flag(cpu, GB_FLAG_N, false);
    set_flag(cpu, GB_FLAG_H, ((value & 0x0Fu) + 1u) > 0x0Fu);
    return result;
}

static uint8_t dec8(GB_CPU *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)(value - 1u);
    set_flag(cpu, GB_FLAG_Z, result == 0);
    set_flag(cpu, GB_FLAG_N, true);
    set_flag(cpu, GB_FLAG_H, (value & 0x0Fu) == 0);
    return result;
}

static uint8_t daa(GB_CPU *cpu)
{
    uint8_t a = cpu->r.a;
    uint8_t adjust = 0;
    bool carry = flag_c(cpu);

    if ((cpu->r.f & GB_FLAG_N) == 0) {
        if (carry || a > 0x99u) {
            adjust |= 0x60u;
            carry = true;
        }
        if ((cpu->r.f & GB_FLAG_H) != 0 || (a & 0x0Fu) > 0x09u) {
            adjust |= 0x06u;
        }
        a = (uint8_t)(a + adjust);
    } else {
        if (carry) adjust |= 0x60u;
        if ((cpu->r.f & GB_FLAG_H) != 0) adjust |= 0x06u;
        a = (uint8_t)(a - adjust);
    }

    cpu->r.a = a;
    set_flag(cpu, GB_FLAG_Z, a == 0);
    set_flag(cpu, GB_FLAG_H, false);
    set_flag(cpu, GB_FLAG_C, carry);
    return a;
}

static uint8_t rotate_left_circular(GB_CPU *cpu, uint8_t value, bool zero_flag)
{
    uint8_t carry = (uint8_t)(value >> 7);
    uint8_t result = (uint8_t)((value << 1) | carry);
    set_flags(cpu, zero_flag && result == 0, false, false, carry != 0);
    return result;
}

static uint8_t rotate_right_circular(GB_CPU *cpu, uint8_t value, bool zero_flag)
{
    uint8_t carry = (uint8_t)(value & 1u);
    uint8_t result = (uint8_t)((value >> 1) | (uint8_t)(carry << 7));
    set_flags(cpu, zero_flag && result == 0, false, false, carry != 0);
    return result;
}

static uint8_t rotate_left_through_carry(GB_CPU *cpu, uint8_t value, bool zero_flag)
{
    uint8_t old_c = flag_c(cpu) ? 1u : 0u;
    uint8_t carry = (uint8_t)(value >> 7);
    uint8_t result = (uint8_t)((value << 1) | old_c);
    set_flags(cpu, zero_flag && result == 0, false, false, carry != 0);
    return result;
}

static uint8_t rotate_right_through_carry(GB_CPU *cpu, uint8_t value, bool zero_flag)
{
    uint8_t old_c = flag_c(cpu) ? 0x80u : 0u;
    uint8_t carry = (uint8_t)(value & 1u);
    uint8_t result = (uint8_t)((value >> 1) | old_c);
    set_flags(cpu, zero_flag && result == 0, false, false, carry != 0);
    return result;
}

static uint8_t shift_left_arithmetic(GB_CPU *cpu, uint8_t value)
{
    uint8_t carry = (uint8_t)(value >> 7);
    uint8_t result = (uint8_t)(value << 1);
    set_flags(cpu, result == 0, false, false, carry != 0);
    return result;
}

static uint8_t shift_right_logical(GB_CPU *cpu, uint8_t value)
{
    uint8_t carry = (uint8_t)(value & 1u);
    uint8_t result = (uint8_t)(value >> 1);
    set_flags(cpu, result == 0, false, false, carry != 0);
    return result;
}

static uint8_t shift_right_arithmetic(GB_CPU *cpu, uint8_t value)
{
    uint8_t carry = (uint8_t)(value & 1u);
    uint8_t result = (uint8_t)((value >> 1) | (value & 0x80u));
    set_flags(cpu, result == 0, false, false, carry != 0);
    return result;
}

static uint8_t swap_nibbles(GB_CPU *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)((value << 4) | (value >> 4));
    set_flags(cpu, result == 0, false, false, false);
    return result;
}

static GB_Result execute_alu_a(GB_CPU *cpu, unsigned operation, uint8_t value, GB_Error *error)
{
    (void)error;
    switch (operation & 7u) {
    case 0: cpu->r.a = add8(cpu, cpu->r.a, value, 0); break; /* ADD */
    case 1: cpu->r.a = add8(cpu, cpu->r.a, value, flag_c(cpu) ? 1u : 0u); break; /* ADC */
    case 2: cpu->r.a = sub8(cpu, cpu->r.a, value, 0); break; /* SUB */
    case 3: cpu->r.a = sub8(cpu, cpu->r.a, value, flag_c(cpu) ? 1u : 0u); break; /* SBC */
    case 4: cpu->r.a = (uint8_t)(cpu->r.a & value); set_flags(cpu, cpu->r.a == 0, false, true, false); break;
    case 5: cpu->r.a = (uint8_t)(cpu->r.a ^ value); set_flags(cpu, cpu->r.a == 0, false, false, false); break;
    case 6: cpu->r.a = (uint8_t)(cpu->r.a | value); set_flags(cpu, cpu->r.a == 0, false, false, false); break;
    case 7: (void)sub8(cpu, cpu->r.a, value, 0); break; /* CP */
    default: break;
    }
    return GB_RESULT_OK;
}

static GB_Result execute_alu_a_source(GB_CPU *cpu, unsigned operation, unsigned source, uint32_t *t_cycles, GB_Error *error)
{
    uint8_t value = 0;
    GB_Result result = read_r8(cpu, source, &value, error);
    if (result != GB_RESULT_OK) return result;
    result = execute_alu_a(cpu, operation, value, error);
    if (result != GB_RESULT_OK) return result;
    if (t_cycles != NULL) {
        *t_cycles = ((source & 7u) == GB_REG_HL) ? 8u : 4u;
    }
    return GB_RESULT_OK;
}

static GB_Result execute_inc_r8(GB_CPU *cpu, unsigned index, uint32_t *t_cycles, GB_Error *error)
{
    uint8_t value = 0;
    GB_Result result = read_r8(cpu, index, &value, error);
    if (result != GB_RESULT_OK) return result;
    value = inc8(cpu, value);
    result = write_r8(cpu, index, value, error);
    if (result != GB_RESULT_OK) return result;
    if (t_cycles != NULL) {
        *t_cycles = ((index & 7u) == GB_REG_HL) ? 12u : 4u;
    }
    return GB_RESULT_OK;
}

static GB_Result execute_dec_r8(GB_CPU *cpu, unsigned index, uint32_t *t_cycles, GB_Error *error)
{
    uint8_t value = 0;
    GB_Result result = read_r8(cpu, index, &value, error);
    if (result != GB_RESULT_OK) return result;
    value = dec8(cpu, value);
    result = write_r8(cpu, index, value, error);
    if (result != GB_RESULT_OK) return result;
    if (t_cycles != NULL) {
        *t_cycles = ((index & 7u) == GB_REG_HL) ? 12u : 4u;
    }
    return GB_RESULT_OK;
}

static GB_Result execute_cb_rot_shift(GB_CPU *cpu, unsigned op, unsigned index, uint32_t *t_cycles, GB_Error *error)
{
    uint8_t value = 0;
    GB_Result result = read_r8(cpu, index, &value, error);
    if (result != GB_RESULT_OK) return result;

    switch (op & 7u) {
    case 0: value = rotate_left_circular(cpu, value, true); break;       /* RLC */
    case 1: value = rotate_right_circular(cpu, value, true); break;      /* RRC */
    case 2: value = rotate_left_through_carry(cpu, value, true); break;  /* RL */
    case 3: value = rotate_right_through_carry(cpu, value, true); break; /* RR */
    case 4: value = shift_left_arithmetic(cpu, value); break;             /* SLA */
    case 5: value = shift_right_arithmetic(cpu, value); break;            /* SRA */
    case 6: value = swap_nibbles(cpu, value); break;                      /* SWAP */
    case 7: value = shift_right_logical(cpu, value); break;               /* SRL */
    default: break;
    }

    if ((index & 7u) == GB_REG_HL) {
        result = write_r8(cpu, index, value, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 8u; /* read + write; prefix is counted by caller */
    } else {
        reg8_plain_write(cpu, index, value);
        /* The CB opcode fetch itself supplies the second M-cycle for register forms. */
        if (t_cycles != NULL) *t_cycles = 0u;
    }
    return GB_RESULT_OK;
}

static GB_Result execute_cb_bit(GB_CPU *cpu, unsigned bit, unsigned index, uint32_t *t_cycles, GB_Error *error)
{
    uint8_t value = 0;
    GB_Result result = read_r8(cpu, index, &value, error);
    if (result != GB_RESULT_OK) return result;

    set_flag(cpu, GB_FLAG_Z, (value & (uint8_t)(1u << (bit & 7u))) == 0);
    set_flag(cpu, GB_FLAG_N, false);
    set_flag(cpu, GB_FLAG_H, true);
    /* C is intentionally preserved. */

    /* BIT [HL] is one M-cycle after CB (the memory read); BIT r has one
       internal M-cycle after CB because register access has no bus cycle. */
    if ((index & 7u) == GB_REG_HL) {
        if (t_cycles != NULL) *t_cycles = 4u;
    } else {
        /* No extra bus cycle is needed: the CB opcode fetch is the second M-cycle. */
        if (t_cycles != NULL) *t_cycles = 0u;
    }
    return GB_RESULT_OK;
}

static GB_Result execute_cb_res_set(GB_CPU *cpu, bool set, unsigned bit, unsigned index, uint32_t *t_cycles, GB_Error *error)
{
    uint8_t value = 0;
    GB_Result result = read_r8(cpu, index, &value, error);
    if (result != GB_RESULT_OK) return result;

    uint8_t mask = (uint8_t)(1u << (bit & 7u));
    value = set ? (uint8_t)(value | mask) : (uint8_t)(value & (uint8_t)~mask);
    if ((index & 7u) == GB_REG_HL) {
        result = write_r8(cpu, index, value, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 8u; /* read + write */
    } else {
        reg8_plain_write(cpu, index, value);
        if (t_cycles != NULL) *t_cycles = 0u;
    }
    return GB_RESULT_OK;
}

bool gb_cpu_opcode_is_invalid(uint8_t opcode)
{
    switch (opcode) {
    case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4:
    case 0xEB: case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD:
        return true;
    default:
        return false;
    }
}

GB_Result gb_cpu_execute_cb_opcode(GB_CPU *cpu, uint8_t opcode, uint32_t *t_cycles, GB_Error *error)
{
    unsigned x = (unsigned)(opcode >> 6);
    unsigned y = (unsigned)((opcode >> 3) & 7u);
    unsigned z = (unsigned)(opcode & 7u);

    switch (x) {
    case 0: return execute_cb_rot_shift(cpu, y, z, t_cycles, error);
    case 1: return execute_cb_bit(cpu, y, z, t_cycles, error);
    case 2: return execute_cb_res_set(cpu, false, y, z, t_cycles, error);
    case 3: return execute_cb_res_set(cpu, true, y, z, t_cycles, error);
    default:
        op_error(error, GB_RESULT_INVALID_OPCODE, cpu->current_instruction_pc,
                 cpu->current_opcode, "Internal CB opcode decoder failure");
        return GB_RESULT_INVALID_OPCODE;
    }
}

static GB_Result execute_misc_alu_immediate(GB_CPU *cpu, unsigned operation, uint32_t *t_cycles, GB_Error *error)
{
    uint8_t value = 0;
    GB_Result result = fetch8(cpu, &value, error);
    if (result != GB_RESULT_OK) return result;
    result = execute_alu_a(cpu, operation, value, error);
    if (result != GB_RESULT_OK) return result;
    if (t_cycles != NULL) *t_cycles = 8u;
    return GB_RESULT_OK;
}

GB_Result gb_cpu_execute_opcode(GB_CPU *cpu, uint8_t opcode, uint32_t *t_cycles, GB_Error *error)
{
    if (t_cycles != NULL) *t_cycles = 0;

    if (gb_cpu_opcode_is_invalid(opcode)) {
        cpu->faulted = true;
        op_error(error, GB_RESULT_INVALID_OPCODE, cpu->current_instruction_pc, opcode,
                 "Invalid / undocumented Game Boy CPU opcode; CPU locked until reset");
        return GB_RESULT_INVALID_OPCODE;
    }

    /* The regular 0x40-0x7F load block has a single special opcode: HALT. */
    if (opcode >= 0x40u && opcode <= 0x7Fu) {
        if (opcode == 0x76u) {
            uint8_t pending = 0;
            if (cpu->bus.get_pending_interrupts != NULL) {
                GB_Error bus_error;
                gb_error_clear(&bus_error);
                GB_Result result = cpu->bus.get_pending_interrupts(cpu->bus.user, &pending, &bus_error);
                if (result != GB_RESULT_OK) {
                    if (error != NULL) {
                        *error = bus_error;
                        error->code = GB_RESULT_INTERRUPT_ERROR;
                        error->pc = cpu->current_instruction_pc;
                        error->opcode = opcode;
                        error->has_opcode = true;
                    }
                    return GB_RESULT_INTERRUPT_ERROR;
                }
            }

            if (!cpu->ime && (pending & 0x1Fu) != 0) {
                cpu->halt_bug = true;
                cpu->halted = false;
            } else {
                cpu->halted = true;
            }
            if (t_cycles != NULL) *t_cycles = 4u;
            return GB_RESULT_OK;
        }

        unsigned dst = (opcode >> 3) & 7u;
        unsigned src = opcode & 7u;
        uint8_t value = 0;
        GB_Result result = read_r8(cpu, src, &value, error);
        if (result != GB_RESULT_OK) return result;
        result = write_r8(cpu, dst, value, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = ((dst == GB_REG_HL || src == GB_REG_HL) ? 8u : 4u);
        return GB_RESULT_OK;
    }

    /* 0x80-0xBF: ALU A,r8. */
    if (opcode >= 0x80u && opcode <= 0xBFu) {
        unsigned operation = (opcode >> 3) & 7u;
        unsigned source = opcode & 7u;
        return execute_alu_a_source(cpu, operation, source, t_cycles, error);
    }

    /* 0xCB prefix. */
    if (opcode == 0xCBu) {
        uint8_t cb_opcode = 0;
        GB_Result result = fetch8(cpu, &cb_opcode, error);
        if (result != GB_RESULT_OK) return result;

        uint32_t cb_cycles = 0;
        result = gb_cpu_execute_cb_opcode(cpu, cb_opcode, &cb_cycles, error);
        if (result != GB_RESULT_OK) return result;
        /* cb_cycles excludes the prefix fetch (4 T-cycles). */
        if (t_cycles != NULL) *t_cycles = cb_cycles + 8u;
        return GB_RESULT_OK;
    }

    /* Fast path for the repeating INC/DEC/LD-r8 patterns. */
    if ((opcode & 0xC7u) == 0x04u) {
        unsigned index = (opcode >> 3) & 7u;
        return execute_inc_r8(cpu, index, t_cycles, error);
    }
    if ((opcode & 0xC7u) == 0x05u) {
        unsigned index = (opcode >> 3) & 7u;
        return execute_dec_r8(cpu, index, t_cycles, error);
    }
    if ((opcode & 0xC7u) == 0x06u) {
        unsigned index = (opcode >> 3) & 7u;
        uint8_t value = 0;
        GB_Result result = fetch8(cpu, &value, error);
        if (result != GB_RESULT_OK) return result;
        result = write_r8(cpu, index, value, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = (index == GB_REG_HL) ? 12u : 8u;
        return GB_RESULT_OK;
    }

    switch (opcode) {
    case 0x00: /* NOP */
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0x01: case 0x11: case 0x21: case 0x31: {
        unsigned index = (opcode >> 4) & 3u;
        uint16_t value = 0;
        GB_Result result = fetch16(cpu, &value, error);
        if (result != GB_RESULT_OK) return result;
        set_r16(index, cpu, value);
        if (t_cycles != NULL) *t_cycles = 12u;
        return GB_RESULT_OK;
    }

    case 0x02:
        { GB_Result result = write8_timed(cpu, bc(cpu), cpu->r.a, error); if (result != GB_RESULT_OK) return result; }
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;

    case 0x12:
        { GB_Result result = write8_timed(cpu, de(cpu), cpu->r.a, error); if (result != GB_RESULT_OK) return result; }
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;

    case 0x0A:
        { GB_Result result = read8_timed(cpu, bc(cpu), &cpu->r.a, error); if (result != GB_RESULT_OK) return result; }
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;

    case 0x1A:
        { GB_Result result = read8_timed(cpu, de(cpu), &cpu->r.a, error); if (result != GB_RESULT_OK) return result; }
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;

    case 0x03: case 0x13: case 0x23: case 0x33: {
        unsigned index = (opcode >> 4) & 3u;
        set_r16(index, cpu, (uint16_t)(get_r16(index, cpu) + 1u));
        GB_Result result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;
    }

    case 0x0B: case 0x1B: case 0x2B: case 0x3B: {
        unsigned index = (opcode >> 4) & 3u;
        set_r16(index, cpu, (uint16_t)(get_r16(index, cpu) - 1u));
        GB_Result result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;
    }

    case 0x07:
        cpu->r.a = rotate_left_circular(cpu, cpu->r.a, false);
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0x0F:
        cpu->r.a = rotate_right_circular(cpu, cpu->r.a, false);
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0x17:
        cpu->r.a = rotate_left_through_carry(cpu, cpu->r.a, false);
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0x1F:
        cpu->r.a = rotate_right_through_carry(cpu, cpu->r.a, false);
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0x08: {
        uint16_t address = 0;
        GB_Result result = fetch16(cpu, &address, error);
        if (result != GB_RESULT_OK) return result;
        result = write16_at(cpu, address, cpu->r.sp, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 20u;
        return GB_RESULT_OK;
    }

    case 0x09: case 0x19: case 0x29: case 0x39: {
        unsigned index = (opcode >> 4) & 3u;
        uint16_t old_hl = hl(cpu);
        uint16_t rhs = get_r16(index, cpu);
        uint16_t result_hl = add16_hl(cpu, old_hl, rhs);
        set_hl(cpu, result_hl);
        GB_Result result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;
    }

    case 0x10: {
        /* STOP is a one-M-cycle instruction in the timing tables. The following
           byte is part of the encoding and is ignored by the normal documented
           path; the CGB speed-switch / STOP quirks are owned by the later power
           control subsystem. */
        uint8_t operand = 0;
        GB_Result result = cpu->bus.read8(cpu->bus.user, cpu->r.pc, &operand, error);
        if (result != GB_RESULT_OK) {
            if (error != NULL) {
                error->code = GB_RESULT_BUS_READ;
                error->pc = cpu->current_instruction_pc;
                error->opcode = cpu->current_opcode;
                error->has_opcode = true;
                if (error->message[0] == '\0') {
                    (void)snprintf(error->message, sizeof(error->message), "STOP operand read failed at $%04X", cpu->r.pc);
                }
            }
            return GB_RESULT_BUS_READ;
        }
        cpu->stop_operand = operand;
        cpu->r.pc = (uint16_t)(cpu->r.pc + 1u);
        cpu->stopped = true;
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;
    }

    case 0x18: {
        uint8_t offset = 0;
        GB_Result result = fetch8(cpu, &offset, error);
        if (result != GB_RESULT_OK) return result;
        cpu->r.pc = (uint16_t)(cpu->r.pc + (int8_t)offset);
        result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 12u;
        return GB_RESULT_OK;
    }

    case 0x20: case 0x28: case 0x30: case 0x38: {
        unsigned cc_index = (opcode >> 3) & 3u;
        uint8_t offset = 0;
        GB_Result result = fetch8(cpu, &offset, error);
        if (result != GB_RESULT_OK) return result;
        if (condition_true(cpu, cc_index)) {
            cpu->r.pc = (uint16_t)(cpu->r.pc + (int8_t)offset);
            result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            if (t_cycles != NULL) *t_cycles = 12u;
        } else {
            if (t_cycles != NULL) *t_cycles = 8u;
        }
        return GB_RESULT_OK;
    }

    case 0x22: case 0x2A: case 0x32: case 0x3A: {
        bool load_a = (opcode & 0x08u) != 0;
        bool decrement = (opcode & 0x10u) != 0;
        uint16_t address = hl(cpu);
        GB_Result result;
        if (load_a) {
            result = read8_timed(cpu, address, &cpu->r.a, error);
        } else {
            result = write8_timed(cpu, address, cpu->r.a, error);
        }
        if (result != GB_RESULT_OK) return result;
        set_hl(cpu, (uint16_t)(decrement ? (address - 1u) : (address + 1u)));
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;
    }

    case 0x27:
        (void)daa(cpu);
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0x2F:
        cpu->r.a = (uint8_t)~cpu->r.a;
        set_flag(cpu, GB_FLAG_N, true);
        set_flag(cpu, GB_FLAG_H, true);
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0x34: {
        return execute_inc_r8(cpu, GB_REG_HL, t_cycles, error);
    }

    case 0x35: {
        return execute_dec_r8(cpu, GB_REG_HL, t_cycles, error);
    }

    case 0x36: {
        uint8_t value = 0;
        GB_Result result = fetch8(cpu, &value, error);
        if (result != GB_RESULT_OK) return result;
        result = write8_at_hl(cpu, value, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 12u;
        return GB_RESULT_OK;
    }

    case 0x37:
        set_flag(cpu, GB_FLAG_N, false);
        set_flag(cpu, GB_FLAG_H, false);
        set_flag(cpu, GB_FLAG_C, true);
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0x3F:
        set_flag(cpu, GB_FLAG_N, false);
        set_flag(cpu, GB_FLAG_H, false);
        set_flag(cpu, GB_FLAG_C, !flag_c(cpu));
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0xC0: case 0xC8: case 0xD0: case 0xD8: {
        unsigned cc_index = (opcode >> 3) & 3u;
        if (condition_true(cpu, cc_index)) {
            uint16_t address = 0;
            GB_Result result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            result = pop16(cpu, &address, error);
            if (result != GB_RESULT_OK) return result;
            cpu->r.pc = address;
            if (t_cycles != NULL) *t_cycles = 20u;
        } else {
            GB_Result result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            if (t_cycles != NULL) *t_cycles = 8u;
        }
        return GB_RESULT_OK;
    }

    case 0xC1: case 0xD1: case 0xE1: case 0xF1: {
        unsigned index = (opcode >> 4) & 3u;
        uint16_t value = 0;
        GB_Result result = pop16(cpu, &value, error);
        if (result != GB_RESULT_OK) return result;
        result = set_r16_stack(index, cpu, value, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 12u;
        return GB_RESULT_OK;
    }

    case 0xC2: case 0xCA: case 0xD2: case 0xDA: {
        unsigned cc_index = (opcode >> 3) & 3u;
        uint16_t address = 0;
        GB_Result result = fetch16(cpu, &address, error);
        if (result != GB_RESULT_OK) return result;
        if (condition_true(cpu, cc_index)) {
            cpu->r.pc = address;
            result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            if (t_cycles != NULL) *t_cycles = 16u;
        } else if (t_cycles != NULL) {
            *t_cycles = 12u;
        }
        return GB_RESULT_OK;
    }

    case 0xC3: {
        uint16_t address = 0;
        GB_Result result = fetch16(cpu, &address, error);
        if (result != GB_RESULT_OK) return result;
        result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        cpu->r.pc = address;
        if (t_cycles != NULL) *t_cycles = 16u;
        return GB_RESULT_OK;
    }

    case 0xCD: {
        uint16_t address = 0;
        GB_Result result = fetch16(cpu, &address, error);
        if (result != GB_RESULT_OK) return result;
        result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        result = push16(cpu, cpu->r.pc, error);
        if (result != GB_RESULT_OK) return result;
        cpu->r.pc = address;
        if (t_cycles != NULL) *t_cycles = 24u;
        return GB_RESULT_OK;
    }

    case 0xC4: case 0xCC: case 0xD4: case 0xDC: {
        unsigned cc_index = (opcode >> 3) & 3u;
        uint16_t address = 0;
        GB_Result result = fetch16(cpu, &address, error);
        if (result != GB_RESULT_OK) return result;
        if (condition_true(cpu, cc_index)) {
            result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            result = push16(cpu, cpu->r.pc, error);
            if (result != GB_RESULT_OK) return result;
            cpu->r.pc = address;
            if (t_cycles != NULL) *t_cycles = 24u;
        } else if (t_cycles != NULL) {
            *t_cycles = 12u;
        }
        return GB_RESULT_OK;
    }

    case 0xC5: case 0xD5: case 0xE5: case 0xF5: {
        unsigned index = (opcode >> 4) & 3u;
        GB_Result result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        result = push16(cpu, get_r16_stack(index, cpu), error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 16u;
        return GB_RESULT_OK;
    }

    case 0xC6: case 0xCE: case 0xD6: case 0xDE:
    case 0xE6: case 0xEE: case 0xF6: case 0xFE: {
        unsigned operation = (opcode >> 3) & 7u;
        return execute_misc_alu_immediate(cpu, operation, t_cycles, error);
    }

    case 0xC7: case 0xCF: case 0xD7: case 0xDF:
    case 0xE7: case 0xEF: case 0xF7: case 0xFF: {
        uint16_t vector = (uint16_t)(opcode & 0x38u);
        GB_Result result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        result = push16(cpu, cpu->r.pc, error);
        if (result != GB_RESULT_OK) return result;
        cpu->r.pc = vector;
        if (t_cycles != NULL) *t_cycles = 16u;
        return GB_RESULT_OK;
    }

    case 0xC9: case 0xD9: {
        uint16_t address = 0;
        GB_Result result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        result = pop16(cpu, &address, error);
        if (result != GB_RESULT_OK) return result;
        cpu->r.pc = address;
        if (opcode == 0xD9) {
            cpu->ime = true;
            cpu->ime_enable_delay = 0;
        }
        if (t_cycles != NULL) *t_cycles = 16u;
        return GB_RESULT_OK;
    }

    case 0xE0: case 0xF0: {
        uint8_t offset = 0;
        GB_Result result = fetch8(cpu, &offset, error);
        if (result != GB_RESULT_OK) return result;
        uint16_t address = (uint16_t)(0xFF00u + offset);
        if (opcode == 0xE0) {
            result = write8_timed(cpu, address, cpu->r.a, error);
        } else {
            result = read8_timed(cpu, address, &cpu->r.a, error);
        }
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 12u;
        return GB_RESULT_OK;
    }

    case 0xE2:
        { GB_Result result = write8_timed(cpu, (uint16_t)(0xFF00u + cpu->r.c), cpu->r.a, error); if (result != GB_RESULT_OK) return result; }
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;

    case 0xF2:
        { GB_Result result = read8_timed(cpu, (uint16_t)(0xFF00u + cpu->r.c), &cpu->r.a, error); if (result != GB_RESULT_OK) return result; }
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;

    case 0xE8: case 0xF8: {
        uint8_t raw = 0;
        GB_Result result = fetch8(cpu, &raw, error);
        if (result != GB_RESULT_OK) return result;
        uint16_t sp = cpu->r.sp;
        uint8_t low = (uint8_t)(sp & 0xFFu);
        uint8_t e8 = raw;
        bool half = ((low & 0x0Fu) + (e8 & 0x0Fu)) > 0x0Fu;
        bool carry = ((uint16_t)low + e8) > 0x00FFu;
        uint16_t result_value = (uint16_t)(sp + (int8_t)raw);
        set_flags(cpu, false, false, half, carry);
        if (opcode == 0xE8) {
            cpu->r.sp = result_value;
            result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            if (t_cycles != NULL) *t_cycles = 16u;
        } else {
            set_hl(cpu, result_value);
            result = idle_m(cpu, error);
            if (result != GB_RESULT_OK) return result;
            if (t_cycles != NULL) *t_cycles = 12u;
        }
        return GB_RESULT_OK;
    }

    case 0xE9:
        cpu->r.pc = hl(cpu);
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0xEA: case 0xFA: {
        uint16_t address = 0;
        GB_Result result = fetch16(cpu, &address, error);
        if (result != GB_RESULT_OK) return result;
        if (opcode == 0xEA) {
            result = write8_timed(cpu, address, cpu->r.a, error);
        } else {
            result = read8_timed(cpu, address, &cpu->r.a, error);
        }
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 16u;
        return GB_RESULT_OK;
    }

    case 0xF3:
        cpu->ime = false;
        cpu->ime_enable_delay = 0;
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0xFB:
        cpu->ime_enable_delay = 2u;
        if (t_cycles != NULL) *t_cycles = 4u;
        return GB_RESULT_OK;

    case 0xF9:
        cpu->r.sp = hl(cpu);
        GB_Result result = idle_m(cpu, error);
        if (result != GB_RESULT_OK) return result;
        if (t_cycles != NULL) *t_cycles = 8u;
        return GB_RESULT_OK;

    default:
        /* Explicitly cover the remaining fixed opcodes that are not pattern-encoded. */
        break;
    }

    /* Fixed LD A / LD r8 immediate family not caught above. */
    if ((opcode & 0xC7u) == 0xC6u) {
        unsigned operation = (opcode >> 3) & 7u;
        return execute_misc_alu_immediate(cpu, operation, t_cycles, error);
    }

    /* 0x3E is included by the generic LD r8,d8 pattern above. */

    /* Remaining control and load opcodes. */
    switch (opcode) {
    case 0x40: /* already handled by 0x40-0x7F */
    default:
        op_error(error, GB_RESULT_INVALID_OPCODE, cpu->current_instruction_pc, opcode,
                 "Opcode decoder reached an unhandled valid opcode");
        return GB_RESULT_INVALID_OPCODE;
    }
}
