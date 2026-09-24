#include "gb_save.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void save_error(GB_Error *error, GB_Result code, const char *message)
{
    if (error == NULL) return;
    gb_error_clear(error);
    error->code = code;
    if (message != NULL) {
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static GB_Result require_save(const GB_SaveRAM *save, GB_Error *error)
{
    if (save == NULL) {
        save_error(error, GB_RESULT_NULL_ARGUMENT, "Save RAM pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!save->initialized) {
        save_error(error, GB_RESULT_BAD_STATE, "Save RAM is not initialized");
        return GB_RESULT_BAD_STATE;
    }
    return GB_RESULT_OK;
}

static bool path_has_extension(const char *path, size_t length, size_t *dot_index)
{
    if (path == NULL || dot_index == NULL) return false;
    size_t slash = 0u;
    size_t backslash = 0u;
    for (size_t i = 0u; i < length; ++i) {
        if (path[i] == '/') slash = i + 1u;
        if (path[i] == '\\') backslash = i + 1u;
    }
    size_t base = slash > backslash ? slash : backslash;
    for (size_t i = length; i > base; --i) {
        char c = path[i - 1u];
        if (c == '.') {
            if (i - 1u == base) return false;
            *dot_index = i - 1u;
            return true;
        }
        if (c == '/' || c == '\\') break;
    }
    return false;
}

static GB_Result make_default_save_path(const char *rom_path, char **out_path, GB_Error *error)
{
    if (rom_path == NULL || out_path == NULL) {
        save_error(error, GB_RESULT_NULL_ARGUMENT, "ROM path/output is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }

    size_t length = strlen(rom_path);
    if (length == 0u) {
        save_error(error, GB_RESULT_INVALID_ARGUMENT, "ROM path is empty");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    size_t dot = length;
    if (!path_has_extension(rom_path, length, &dot)) {
        dot = length;
    }

    if (dot > (SIZE_MAX - 5u)) {
        save_error(error, GB_RESULT_ALLOCATION, "Save path length overflows size_t");
        return GB_RESULT_ALLOCATION;
    }

    size_t output_length = dot + 4u;
    char *path = (char *)malloc(output_length + 1u);
    if (path == NULL) {
        save_error(error, GB_RESULT_ALLOCATION, "Failed to allocate save path");
        return GB_RESULT_ALLOCATION;
    }

    memcpy(path, rom_path, dot);
    memcpy(path + dot, ".sav", 4u);
    path[output_length] = '\0';
    *out_path = path;
    return GB_RESULT_OK;
}

static GB_Result set_path(GB_SaveRAM *save, const char *path, GB_Error *error)
{
    if (path == NULL || path[0] == '\0') {
        save_error(error, GB_RESULT_INVALID_ARGUMENT, "Save path is empty");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    size_t length = strlen(path);
    if (length > GB_SAVE_MAX_PATH - 1u) {
        save_error(error, GB_RESULT_INVALID_ARGUMENT, "Save path is too long");
        return GB_RESULT_INVALID_ARGUMENT;
    }

    char *copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        save_error(error, GB_RESULT_ALLOCATION, "Failed to allocate save path");
        return GB_RESULT_ALLOCATION;
    }
    memcpy(copy, path, length + 1u);

    free(save->path);
    save->path = copy;
    save->path_length = length;
    return GB_RESULT_OK;
}

GB_Result gb_save_ram_init(GB_SaveRAM *save, GB_Error *error)
{
    gb_error_clear(error);
    if (save == NULL) {
        save_error(error, GB_RESULT_NULL_ARGUMENT, "Save RAM pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    memset(save, 0, sizeof(*save));
    save->initialized = true;
    save->last_save_succeeded = true;
    return GB_RESULT_OK;
}

GB_Result gb_save_ram_attach_path(GB_SaveRAM *save,
                                  GB_Cartridge *cartridge,
                                  const char *save_path,
                                  GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_save(save, error);
    if (result != GB_RESULT_OK) return result;
    if (cartridge == NULL) {
        save_error(error, GB_RESULT_NULL_ARGUMENT, "Cartridge pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!gb_cartridge_is_loaded(cartridge)) {
        save_error(error, GB_RESULT_BAD_STATE, "Cartridge is not loaded");
        return GB_RESULT_BAD_STATE;
    }

    result = set_path(save, save_path, error);
    if (result != GB_RESULT_OK) return result;

    save->cartridge = cartridge;
    save->enabled = cartridge->has_battery && cartridge->ram_size != 0u;
    save->loaded_from_disk = false;
    save->last_save_succeeded = true;
    return GB_RESULT_OK;
}

GB_Result gb_save_ram_attach(GB_SaveRAM *save,
                             GB_Cartridge *cartridge,
                             const char *rom_path,
                             GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_save(save, error);
    if (result != GB_RESULT_OK) return result;
    char *path = NULL;
    result = make_default_save_path(rom_path, &path, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_save_ram_attach_path(save, cartridge, path, error);
    free(path);
    return result;
}

GB_Result gb_save_ram_load(GB_SaveRAM *save, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_save(save, error);
    if (result != GB_RESULT_OK) return result;
    if (!save->enabled) return GB_RESULT_OK;
    if (save->path == NULL || save->cartridge == NULL) {
        save_error(error, GB_RESULT_BAD_STATE, "Save RAM has no cartridge/path attached");
        return GB_RESULT_BAD_STATE;
    }

    FILE *file = fopen(save->path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            if (save->cartridge->ram != NULL) {
                memset(save->cartridge->ram, 0xFF, save->cartridge->ram_size);
            }
            save->loaded_from_disk = false;
            save->last_save_succeeded = true;
            gb_cartridge_clear_ram_dirty(save->cartridge);
            return GB_RESULT_OK;
        }
        save_error(error, GB_RESULT_FILE_OPEN, "Failed to open cartridge save file");
        return GB_RESULT_FILE_OPEN;
    }

    if (save->cartridge->ram == NULL || save->cartridge->ram_size == 0u) {
        (void)fclose(file);
        save_error(error, GB_RESULT_BAD_STATE, "Cartridge reports save RAM but has no RAM buffer");
        return GB_RESULT_BAD_STATE;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        save_error(error, GB_RESULT_FILE_IO, "Failed to seek save file");
        return GB_RESULT_FILE_IO;
    }
    long size_long = ftell(file);
    if (size_long < 0L) {
        (void)fclose(file);
        save_error(error, GB_RESULT_FILE_IO, "Failed to determine save file size");
        return GB_RESULT_FILE_IO;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        save_error(error, GB_RESULT_FILE_IO, "Failed to rewind save file");
        return GB_RESULT_FILE_IO;
    }

    unsigned long size = (unsigned long)size_long;
    if (size > (unsigned long)save->cartridge->ram_size) {
        (void)fclose(file);
        save_error(error, GB_RESULT_FILE_IO, "Save file is larger than cartridge RAM");
        return GB_RESULT_FILE_IO;
    }

    memset(save->cartridge->ram, 0xFF, save->cartridge->ram_size);
    size_t wanted = (size_t)size;
    size_t read_count = fread(save->cartridge->ram, 1u, wanted, file);
    int close_result = fclose(file);
    if (read_count != wanted) {
        save_error(error, GB_RESULT_FILE_READ, "Failed to read complete save file");
        return GB_RESULT_FILE_READ;
    }
    if (close_result != 0) {
        save_error(error, GB_RESULT_FILE_IO, "Failed to close save file cleanly");
        return GB_RESULT_FILE_IO;
    }

    save->loaded_from_disk = true;
    save->last_save_succeeded = true;
    gb_cartridge_clear_ram_dirty(save->cartridge);
    return GB_RESULT_OK;
}

GB_Result gb_save_ram_save(GB_SaveRAM *save, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_save(save, error);
    if (result != GB_RESULT_OK) return result;
    if (!save->enabled) return GB_RESULT_OK;
    if (save->path == NULL || save->cartridge == NULL || save->cartridge->ram == NULL) {
        save_error(error, GB_RESULT_BAD_STATE, "Save RAM is not attached to a valid cartridge");
        return GB_RESULT_BAD_STATE;
    }

    size_t temp_length = save->path_length + 5u;
    char *temp_path = (char *)malloc(temp_length + 1u);
    if (temp_path == NULL) {
        save_error(error, GB_RESULT_ALLOCATION, "Failed to allocate temporary save path");
        save->last_save_succeeded = false;
        return GB_RESULT_ALLOCATION;
    }
    (void)snprintf(temp_path, temp_length + 1u, "%s.tmp", save->path);

    FILE *file = fopen(temp_path, "wb");
    if (file == NULL) {
        free(temp_path);
        save_error(error, GB_RESULT_FILE_OPEN, "Failed to open temporary save file");
        save->last_save_succeeded = false;
        return GB_RESULT_FILE_OPEN;
    }

    size_t written = fwrite(save->cartridge->ram, 1u, save->cartridge->ram_size, file);
    bool write_ok = written == save->cartridge->ram_size && fflush(file) == 0;
    int close_result = fclose(file);
    if (!write_ok || close_result != 0) {
        (void)remove(temp_path);
        free(temp_path);
        save_error(error, GB_RESULT_FILE_IO, "Failed to write cartridge save file");
        save->last_save_succeeded = false;
        return GB_RESULT_FILE_IO;
    }

    if (remove(save->path) != 0 && errno != ENOENT) {
        (void)remove(temp_path);
        free(temp_path);
        save_error(error, GB_RESULT_FILE_IO, "Failed to replace previous cartridge save file");
        save->last_save_succeeded = false;
        return GB_RESULT_FILE_IO;
    }
    if (rename(temp_path, save->path) != 0) {
        (void)remove(temp_path);
        free(temp_path);
        save_error(error, GB_RESULT_FILE_IO, "Failed to commit cartridge save file");
        save->last_save_succeeded = false;
        return GB_RESULT_FILE_IO;
    }

    free(temp_path);
    gb_cartridge_clear_ram_dirty(save->cartridge);
    save->last_save_succeeded = true;
    return GB_RESULT_OK;
}

GB_Result gb_save_ram_flush(GB_SaveRAM *save, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_save(save, error);
    if (result != GB_RESULT_OK) return result;
    if (!save->enabled || !gb_cartridge_ram_dirty(save->cartridge)) return GB_RESULT_OK;
    return gb_save_ram_save(save, error);
}

GB_Result gb_save_ram_detach(GB_SaveRAM *save, GB_Error *error)
{
    gb_error_clear(error);
    GB_Result result = require_save(save, error);
    if (result != GB_RESULT_OK) return result;
    result = gb_save_ram_flush(save, error);
    if (result != GB_RESULT_OK) return result;
    save->cartridge = NULL;
    save->enabled = false;
    save->loaded_from_disk = false;
    return GB_RESULT_OK;
}

GB_Result gb_save_ram_destroy(GB_SaveRAM *save, GB_Error *error)
{
    gb_error_clear(error);
    if (save == NULL) {
        save_error(error, GB_RESULT_NULL_ARGUMENT, "Save RAM pointer is NULL");
        return GB_RESULT_NULL_ARGUMENT;
    }
    if (!save->initialized) return GB_RESULT_OK;

    GB_Result result = GB_RESULT_OK;
    if (save->enabled && save->cartridge != NULL) {
        result = gb_save_ram_flush(save, error);
    }
    free(save->path);
    memset(save, 0, sizeof(*save));
    return result;
}

bool gb_save_ram_is_enabled(const GB_SaveRAM *save)
{
    return save != NULL && save->initialized && save->enabled;
}

bool gb_save_ram_is_dirty(const GB_SaveRAM *save)
{
    return gb_save_ram_is_enabled(save) && gb_cartridge_ram_dirty(save->cartridge);
}

const char *gb_save_ram_path(const GB_SaveRAM *save)
{
    return (save != NULL && save->initialized) ? save->path : NULL;
}
