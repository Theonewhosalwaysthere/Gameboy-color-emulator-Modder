#ifndef GB_SAVE_H
#define GB_SAVE_H

#include <stdbool.h>
#include <stddef.h>

#include "../cartridge/gb_cartridge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GB_SAVE_MAX_PATH 4096u

typedef struct GB_SaveRAM {
    bool initialized;
    bool enabled;
    bool loaded_from_disk;
    bool last_save_succeeded;
    GB_Cartridge *cartridge;
    char *path;
    size_t path_length;
} GB_SaveRAM;

GB_Result gb_save_ram_init(GB_SaveRAM *save, GB_Error *error);
GB_Result gb_save_ram_attach(GB_SaveRAM *save,
                             GB_Cartridge *cartridge,
                             const char *rom_path,
                             GB_Error *error);
GB_Result gb_save_ram_attach_path(GB_SaveRAM *save,
                                  GB_Cartridge *cartridge,
                                  const char *save_path,
                                  GB_Error *error);
GB_Result gb_save_ram_load(GB_SaveRAM *save, GB_Error *error);
GB_Result gb_save_ram_save(GB_SaveRAM *save, GB_Error *error);
GB_Result gb_save_ram_flush(GB_SaveRAM *save, GB_Error *error);
GB_Result gb_save_ram_detach(GB_SaveRAM *save, GB_Error *error);
GB_Result gb_save_ram_destroy(GB_SaveRAM *save, GB_Error *error);

bool gb_save_ram_is_enabled(const GB_SaveRAM *save);
bool gb_save_ram_is_dirty(const GB_SaveRAM *save);
const char *gb_save_ram_path(const GB_SaveRAM *save);

#ifdef __cplusplus
}
#endif

#endif /* GB_SAVE_H */
