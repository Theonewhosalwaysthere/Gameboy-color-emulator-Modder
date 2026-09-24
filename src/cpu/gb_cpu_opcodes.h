#ifndef GB_CPU_OPCODES_H
#define GB_CPU_OPCODES_H

#include "gb_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

GB_Result gb_cpu_execute_opcode(GB_CPU *cpu, uint8_t opcode, uint32_t *t_cycles, GB_Error *error);
GB_Result gb_cpu_execute_cb_opcode(GB_CPU *cpu, uint8_t opcode, uint32_t *t_cycles, GB_Error *error);
bool gb_cpu_opcode_is_invalid(uint8_t opcode);

#ifdef __cplusplus
}
#endif

#endif /* GB_CPU_OPCODES_H */
