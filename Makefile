# GCC/Clang-compatible Makefile. The default emulator is headless and does not
# require SDL3. Build the SDL3 frontend explicitly with: make sdl3

UNAME_S := $(shell uname -s 2>/dev/null)

ifeq ($(UNAME_S),)
  IS_WINDOWS := 1
else
  IS_WINDOWS := 0
endif

ifeq ($(IS_WINDOWS),1)
  EXE_EXT := .exe
  ifeq ($(origin CC),default)
    CC := gcc
  endif
  MKDIR_P = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
  RMDIR_RF = if exist "$(subst /,\,$1)" rmdir /S /Q "$(subst /,\,$1)"
else
  EXE_EXT :=
  ifeq ($(origin CC),default)
    CC := cc
  endif
  MKDIR_P = mkdir -p $1
  RMDIR_RF = rm -rf $1
endif

AR ?= ar

CSTD := -std=c11
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
CPPFLAGS := -Isrc/cpu -Isrc/memory -Isrc/cartridge -Isrc/timer -Isrc/interrupt -Isrc/ppu -Isrc/input -Isrc/dma -Isrc/cgb -Isrc/audio -Isrc/emulator -Isrc/platform -Isrc/save -Isrc/debug
CFLAGS ?= $(CSTD) $(WARNINGS) -O2 -g
LDFLAGS ?=
ifeq ($(IS_WINDOWS),1)
  MATH_LIB :=
else
  MATH_LIB := -lm
endif
LDLIBS ?= $(MATH_LIB)

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
LIB_DIR := $(BUILD_DIR)/lib
BIN_DIR := $(BUILD_DIR)/bin

SDL3_CFLAGS ?= $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL3_LIBS ?= $(shell pkg-config --libs sdl3 2>/dev/null)

ifeq ($(strip $(SDL3_CFLAGS)),)
  ifdef SDL3_DIR
    SDL3_CFLAGS := -I$(SDL3_DIR)/include
  endif
endif
ifeq ($(strip $(SDL3_LIBS)),)
  ifdef SDL3_DIR
    SDL3_LIBS := -L$(SDL3_DIR)/lib -lSDL3
  endif
endif

define CHECK_SDL3
$(if $(strip $(SDL3_CFLAGS)),,$(error SDL3 development files not found. Install SDL3, set SDL3_DIR, or set SDL3_CFLAGS/SDL3_LIBS manually))
$(if $(strip $(SDL3_LIBS)),,$(error SDL3 library flags not found. Install SDL3, set SDL3_DIR, or set SDL3_CFLAGS/SDL3_LIBS manually))
endef

CORE_SRC := \
	src/cpu/gb_cpu.c \
	src/cpu/gb_cpu_opcodes.c \
	src/memory/gb_memory.c \
	src/cartridge/gb_cartridge.c \
	src/timer/gb_timer.c \
	src/interrupt/gb_interrupt.c \
	src/ppu/gb_ppu.c \
	src/input/gb_input.c \
	src/dma/gb_dma.c \
	src/cgb/gb_cgb.c \
	src/audio/gb_audio.c \
	src/save/gb_save.c \
	src/debug/gb_debug.c \
	src/emulator/gb_emulator.c

TEST_SRC := \
	tests/gb_cpu_tests.c \
	tests/gb_memory_tests.c \
	tests/gb_cartridge_tests.c \
	tests/gb_timer_tests.c \
	tests/gb_interrupt_tests.c \
	tests/gb_ppu_tests.c \
	tests/gb_input_tests.c \
	tests/gb_dma_tests.c \
	tests/gb_cgb_tests.c \
	tests/gb_audio_tests.c \
	tests/gb_save_tests.c \
	tests/gb_debug_tests.c \
	tests/gb_emulator_tests.c

CORE_OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(CORE_SRC))
TEST_OBJ := $(patsubst tests/%.c,$(OBJ_DIR)/tests/%.o,$(TEST_SRC))

MAIN_OBJ := $(OBJ_DIR)/main.o
SDL3_MAIN_OBJ := $(OBJ_DIR)/main_sdl3.o
PLATFORM_OBJ := $(OBJ_DIR)/platform/gb_sdl3.o
SDL3_INPUT_OBJ := $(OBJ_DIR)/input/gb_input_sdl3.o
SDL3_AUDIO_OBJ := $(OBJ_DIR)/audio/gb_audio_sdl3.o

LIB := $(LIB_DIR)/libgbc_core.a
EMULATOR_BIN := $(BIN_DIR)/gbc_emulator$(EXE_EXT)
SDL3_EMULATOR_BIN := $(BIN_DIR)/gbc_emulator_sdl3$(EXE_EXT)

CPU_TEST_BIN := $(BIN_DIR)/gb_cpu_tests$(EXE_EXT)
MEMORY_TEST_BIN := $(BIN_DIR)/gb_memory_tests$(EXE_EXT)
CARTRIDGE_TEST_BIN := $(BIN_DIR)/gb_cartridge_tests$(EXE_EXT)
TIMER_TEST_BIN := $(BIN_DIR)/gb_timer_tests$(EXE_EXT)
INTERRUPT_TEST_BIN := $(BIN_DIR)/gb_interrupt_tests$(EXE_EXT)
PPU_TEST_BIN := $(BIN_DIR)/gb_ppu_tests$(EXE_EXT)
INPUT_TEST_BIN := $(BIN_DIR)/gb_input_tests$(EXE_EXT)
DMA_TEST_BIN := $(BIN_DIR)/gb_dma_tests$(EXE_EXT)
CGB_TEST_BIN := $(BIN_DIR)/gb_cgb_tests$(EXE_EXT)
AUDIO_TEST_BIN := $(BIN_DIR)/gb_audio_tests$(EXE_EXT)
SAVE_TEST_BIN := $(BIN_DIR)/gb_save_tests$(EXE_EXT)
DEBUG_TEST_BIN := $(BIN_DIR)/gb_debug_tests$(EXE_EXT)
EMULATOR_TEST_BIN := $(BIN_DIR)/gb_emulator_tests$(EXE_EXT)

TEST_BINS := \
	$(CPU_TEST_BIN) \
	$(MEMORY_TEST_BIN) \
	$(CARTRIDGE_TEST_BIN) \
	$(TIMER_TEST_BIN) \
	$(INTERRUPT_TEST_BIN) \
	$(PPU_TEST_BIN) \
	$(INPUT_TEST_BIN) \
	$(DMA_TEST_BIN) \
	$(CGB_TEST_BIN) \
	$(AUDIO_TEST_BIN) \
	$(SAVE_TEST_BIN) \
	$(DEBUG_TEST_BIN) \
	$(EMULATOR_TEST_BIN)

.PHONY: all core tests test verify emulator run sdl3 run-sdl3 \
	test-cpu test-memory test-cartridge test-mbc test-timer test-interrupt \
	test-ppu test-input test-dma test-cgb test-audio test-save test-debug test-emulator \
	sdl3-input sdl3-audio clean rebuild sanitize help

# Default build: no SDL3 required.
all: $(EMULATOR_BIN) tests

core: $(LIB)

tests: $(TEST_BINS)

test: tests
	$(CPU_TEST_BIN)
	$(MEMORY_TEST_BIN)
	$(CARTRIDGE_TEST_BIN)
	$(TIMER_TEST_BIN)
	$(INTERRUPT_TEST_BIN)
	$(PPU_TEST_BIN)
	$(INPUT_TEST_BIN)
	$(DMA_TEST_BIN)
	$(CGB_TEST_BIN)
	$(AUDIO_TEST_BIN)
	$(SAVE_TEST_BIN)
	$(DEBUG_TEST_BIN)
	$(EMULATOR_TEST_BIN)

verify:
	$(MAKE) test
	$(MAKE) sanitize

emulator: $(EMULATOR_BIN)

sdl3: $(SDL3_EMULATOR_BIN)

run: $(EMULATOR_BIN)
ifeq ($(IS_WINDOWS),1)
	@if "$(ROM)"=="" (echo Usage: make run ROM=path/to/game.gb ^[MODE=--cgb^|--dmg^] & exit /b 2)
else
	@if [ -z "$(ROM)" ]; then echo "Usage: make run ROM=path/to/game.gb [MODE=--cgb|--dmg]"; exit 2; fi
endif
	$(EMULATOR_BIN) $(MODE) "$(ROM)"

run-sdl3: $(SDL3_EMULATOR_BIN)
ifeq ($(IS_WINDOWS),1)
	@if "$(ROM)"=="" (echo Usage: make run-sdl3 ROM=path/to/game.gb ^[MODE=--cgb^|--dmg^] & exit /b 2)
else
	@if [ -z "$(ROM)" ]; then echo "Usage: make run-sdl3 ROM=path/to/game.gb [MODE=--cgb|--dmg]"; exit 2; fi
endif
	$(SDL3_EMULATOR_BIN) $(MODE) "$(ROM)"

sdl3-input: $(SDL3_INPUT_OBJ)
sdl3-audio: $(SDL3_AUDIO_OBJ)

$(LIB): $(CORE_OBJ) | $(LIB_DIR)
	$(AR) rcs $@ $^

$(EMULATOR_BIN): $(MAIN_OBJ) $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(MAIN_OBJ) $(LIB) $(LDLIBS)

$(SDL3_EMULATOR_BIN): $(SDL3_MAIN_OBJ) $(PLATFORM_OBJ) $(LIB) $(SDL3_INPUT_OBJ) $(SDL3_AUDIO_OBJ) | $(BIN_DIR)
	@$(CHECK_SDL3)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SDL3_MAIN_OBJ) $(PLATFORM_OBJ) $(SDL3_INPUT_OBJ) $(SDL3_AUDIO_OBJ) $(LIB) $(SDL3_LIBS) $(LDLIBS)

$(CPU_TEST_BIN): $(OBJ_DIR)/tests/gb_cpu_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(MEMORY_TEST_BIN): $(OBJ_DIR)/tests/gb_memory_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(CARTRIDGE_TEST_BIN): $(OBJ_DIR)/tests/gb_cartridge_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(TIMER_TEST_BIN): $(OBJ_DIR)/tests/gb_timer_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(INTERRUPT_TEST_BIN): $(OBJ_DIR)/tests/gb_interrupt_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(PPU_TEST_BIN): $(OBJ_DIR)/tests/gb_ppu_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(INPUT_TEST_BIN): $(OBJ_DIR)/tests/gb_input_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(DMA_TEST_BIN): $(OBJ_DIR)/tests/gb_dma_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(CGB_TEST_BIN): $(OBJ_DIR)/tests/gb_cgb_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(AUDIO_TEST_BIN): $(OBJ_DIR)/tests/gb_audio_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(SAVE_TEST_BIN): $(OBJ_DIR)/tests/gb_save_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(DEBUG_TEST_BIN): $(OBJ_DIR)/tests/gb_debug_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)
$(EMULATOR_TEST_BIN): $(OBJ_DIR)/tests/gb_emulator_tests.o $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

$(OBJ_DIR)/%.o: src/%.c
	$(call MKDIR_P,$(@D))
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/tests/%.o: tests/%.c
	$(call MKDIR_P,$(@D))
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(PLATFORM_OBJ): $(PLATFORM_SRC)
	@$(CHECK_SDL3)
	$(call MKDIR_P,$(@D))
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -MMD -MP -c $< -o $@

$(SDL3_MAIN_OBJ): src/main_sdl3.c
	@$(CHECK_SDL3)
	$(call MKDIR_P,$(@D))
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -MMD -MP -c $< -o $@

$(SDL3_INPUT_OBJ): src/input/gb_input_sdl3.c
	@$(CHECK_SDL3)
	$(call MKDIR_P,$(@D))
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -MMD -MP -c $< -o $@

$(SDL3_AUDIO_OBJ): src/audio/gb_audio_sdl3.c
	@$(CHECK_SDL3)
	$(call MKDIR_P,$(@D))
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL3_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR) $(LIB_DIR) $(BIN_DIR):
	$(call MKDIR_P,$@)

sanitize:
	$(MAKE) BUILD_DIR=build-sanitize clean
	$(MAKE) BUILD_DIR=build-sanitize CFLAGS="$(CSTD) $(WARNINGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined" test

rebuild:
	$(MAKE) clean
	$(MAKE) all

clean:
	$(call RMDIR_RF,$(BUILD_DIR))

help:
	@echo "GBC emulator build targets:"
	@echo "  make              Build headless emulator + tests (no SDL3)"
	@echo "  make run ROM=...  Run a ROM headlessly (ROM required)"
	@echo "  make sdl3         Build the SDL3 graphical emulator"
	@echo "  make run-sdl3 ROM=...  Run a ROM with SDL3"
	@echo "  make test         Run all core/integration tests"
	@echo "  make verify       Run tests + ASan/UBSan"
	@echo "  make sanitize     Run ASan/UBSan tests"
	@echo "  make clean        Remove the default build"
	@echo "SDL3: set SDL3_DIR or SDL3_CFLAGS/SDL3_LIBS when building sdl3"

-include $(CORE_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(SDL3_MAIN_OBJ:.o=.d) $(PLATFORM_OBJ:.o=.d) $(MAIN_OBJ:.o=.d) $(SDL3_INPUT_OBJ:.o=.d) $(SDL3_AUDIO_OBJ:.o=.d)
