# GBC Emulator — Final Integrated C Implementation

The project root is `gbc_emulator/`.

This final integrated stage contains the CPU, memory/bus, cartridge loading, timer, interrupt controller, PPU/video, joypad input, OAM DMA, cartridge MBC hardware, CGB-specific hardware, Audio/APU, synchronized SDL3 host loop, persistent Save RAM, and the platform-independent diagnostics/debugging layer.

## Project structure

```text
gbc_emulator/
├── .vscode/
│   ├── c_cpp_properties.json
│   ├── launch.json
│   ├── settings.json
│   └── tasks.json
├── src/
│   ├── cpu/
│   ├── memory/
│   ├── cartridge/
│   ├── timer/
│   ├── interrupt/
│   ├── ppu/
│   ├── input/
│   ├── dma/
│   ├── cgb/
│   │   ├── gb_cgb.c
│   │   └── gb_cgb.h
│   ├── audio/
│   │   ├── gb_audio.c
│   │   ├── gb_audio.h
│   │   ├── gb_audio_sdl3.c
│   │   └── gb_audio_sdl3.h
│   ├── emulator/
│   │   ├── gb_emulator.c
│   │   └── gb_emulator.h
│   ├── platform/
│   │   ├── gb_sdl3.c
│   │   └── gb_sdl3.h
│   └── main.c
├── tests/
│   ├── gb_cpu_tests.c
│   ├── gb_memory_tests.c
│   ├── gb_cartridge_tests.c
│   ├── gb_timer_tests.c
│   ├── gb_interrupt_tests.c
│   ├── gb_ppu_tests.c
│   ├── gb_input_tests.c
│   ├── gb_dma_tests.c
│   ├── gb_cgb_tests.c
│   ├── gb_audio_tests.c
│   └── gb_emulator_tests.c
├── Makefile
└── README.md
```

## CPU

The CPU subsystem implements the LR35902 instruction set and CB-prefixed instructions, including register pairs, flags, stack operations, branches, calls/returns, bit operations, HALT, STOP, EI, DI, RETI, interrupt entry, and instruction timing.

The CPU does not include SDL or direct memory storage. It communicates through `GB_CPU_BUS` callbacks so memory and hardware can be replaced independently.

IME remains CPU-internal. It is controlled by `EI`, `DI`, `RETI`, and interrupt entry rather than by an I/O register.

## Memory / bus

`GB_Memory` implements the system address map and owns internal VRAM, WRAM, OAM, HRAM, and general memory routing. Cartridge ranges are delegated to the cartridge bus, while hardware registers can be delegated to device callbacks.

The memory map covers cartridge ROM at `$0000-$7FFF`, VRAM at `$8000-$9FFF`, cartridge RAM at `$A000-$BFFF`, WRAM at `$C000-$DFFF`, echo RAM at `$E000-$FDFF`, OAM at `$FE00-$FE9F`, unusable space at `$FEA0-$FEFF`, I/O at `$FF00-$FF7F`, HRAM at `$FF80-$FFFE`, and IE at `$FFFF`.

The interrupt controller owns `$FF0F` and `$FFFF`; memory routes those accesses to the controller instead of keeping a second IF/IE copy. The PPU, timer, input, cartridge, and DMA systems use the same device-routing mechanism rather than putting all hardware logic into `GB_Memory`.

## Cartridge / ROM loading

The cartridge subsystem owns loaded ROM data and parses the cartridge header. It validates ROM file size, Nintendo logo/header information, checksums, cartridge type, ROM/RAM sizing, CGB capability, and cartridge feature flags.

The cartridge address ranges are exposed through the memory bus. Mapper logic is now handled by the same cartridge object instead of being a separate ROM-only placeholder.

## MBC / cartridge hardware

The current mapper implementation supports the common MBC families below:

```text
MBC1
MBC2
MBC3
MBC5
```

### MBC1

Implemented behavior includes:

- RAM enable register
- lower five ROM-bank bits
- upper two ROM-bank bits
- ROM banking mode
- lower-bank switching in RAM-banking mode
- RAM-bank switching in RAM-banking mode
- ROM/RAM bank wrapping for the loaded cartridge size

### MBC2

Implemented behavior includes:

- address-bit-controlled RAM-enable / ROM-bank register writes
- 16 ROM banks
- internal 512 x 4-bit RAM
- mirrored RAM address range
- low-nibble RAM storage/read behavior
- battery-backed metadata from the cartridge header

### MBC3

Implemented behavior includes:

- RAM enable
- seven-bit ROM-bank selection
- RAM-bank selection
- RTC register selection
- RTC latch sequence (`00` then `01`)
- RTC seconds/minutes/hours
- nine-bit day counter
- halt flag
- carry flag
- emulated-cycle RTC advancement

The RTC is advanced through the cartridge bus tick callback. Persistent RTC save data is intentionally deferred to the later Save RAM stage.

### MBC5

Implemented behavior includes:

- RAM enable
- nine-bit ROM-bank selection
- four-bit RAM-bank selection
- rumble cartridges
- three-bit RAM bank selection when the rumble control bit is present
- observable rumble state through `gb_cartridge_rumble_active()`

MBC5 exposes a nine-bit ROM bank number, allowing cartridges much larger than the MBC1/MBC3 range.

### Unsupported mapper types

MMM01, MBC6, MBC7, Pocket Camera, TAMA5, HuC3, and HuC1 are identified from their cartridge type codes but are rejected with an explicit unsupported-cartridge error rather than silently behaving like another mapper.

## Timer

The timer owns `$FF04-$FF07`:

```text
FF04  DIV
FF05  TIMA
FF06  TMA
FF07  TAC
```

The internal divider is 16 bits and TIMA uses falling-edge detection on the selected divider bit for the four TAC frequencies:

```text
TAC=00 -> divider bit 9 -> 4096 Hz
TAC=01 -> divider bit 3 -> 262144 Hz
TAC=10 -> divider bit 5 -> 65536 Hz
TAC=11 -> divider bit 7 -> 16384 Hz
```

The timer handles DIV writes, TAC writes, timer edges, TIMA overflow, delayed TMA-to-TIMA reload, timer interrupt requests, TIMA writes during the reload window, and the CGB double-speed timer domain.

## Interrupt system

The interrupt subsystem owns IF/IE and provides the request/acknowledgement path between hardware devices and the CPU.

```text
Bit 0  VBlank  -> $0040
Bit 1  STAT    -> $0048
Bit 2  Timer   -> $0050
Bit 3  Serial  -> $0058
Bit 4  Joypad  -> $0060
```

Pending interrupts are calculated as `IF & IE`, while IME remains an internal CPU flag.

## PPU / video

The PPU owns the LCD/video registers and produces a renderer-independent 160x144 RGB555 framebuffer. It implements the LCD mode timing state machine, LY/LYC, STAT interrupts, VBlank, scrolling, window rendering, DMG palettes, CGB palettes/attributes, sprite selection, sprite limits, OAM/VRAM access restrictions, and the CGB palette registers.

The current implementation renders complete scanlines on Mode 3 entry rather than implementing the complete dot-by-dot pixel FIFO/fetcher. The timing and hardware-register interfaces are in place for a later accuracy pass.

## Input / joypad

The input core owns `$FF00` and implements the active-low Game Boy two-row joypad matrix, including action buttons, D-pad, both-row selection, selection-induced input edges, and Joypad interrupt requests.

The SDL3 adapter is separate from the hardware core. It translates SDL3 keyboard and gamepad events into `GB_Input` state without adding SDL dependencies to the emulator hardware implementation.

Default keyboard bindings are:

```text
Arrow keys       -> D-pad
Z                -> A
X                -> B
Right Shift      -> Select
Enter            -> Start
```

## DMA

The DMA subsystem owns `$FF46` and performs the Game Boy OAM DMA transfer into `$FE00-$FE9F`.

Writing an XX value to `$FF46` starts a transfer from `$XX00-$XX9F`. The implementation transfers exactly 160 bytes and advances through the existing memory/bus timing interface instead of copying the whole block immediately.

Normal-speed timing is 640 T-cycles total for the 160-byte transfer. In CGB double-speed mode the CPU clock is doubled and the same DMA transfer occupies 320 T-cycles.

During active DMA, the CPU's normal bus access is restricted by a DMA-aware controller. HRAM remains accessible; DMG mode blocks the other CPU memory ranges. CGB mode uses the source bus to determine the documented opposite-bus access cases for cartridge/WRAM sources. DMA's internal reads bypass the CPU access restriction and PPU CPU-access restrictions so the transfer can actually reach OAM.

The DMA subsystem supports restart by writing `$FF46` again and exposes its active state, source address, and transferred-byte count for diagnostics/tests.

DMA source pages are accepted only from `$0000-$DFFF` in the hardware source-page range. Requests using higher source-page values do not accidentally alias the memory map.

## Error handling

The subsystems use the shared `GB_Result` and `GB_Error` types. Functions validate pointers and state before using them and propagate detailed failures between subsystem boundaries.

The DMA and cartridge systems report allocation failures, invalid mapper/cartridge configuration, unsupported hardware, bad state, and bus/device failures rather than silently continuing.

## Tests

The project retains tests from every completed stage and adds dedicated DMA and MBC coverage.

Cartridge tests cover:

- ROM-only loading
- MBC1 ROM-bank switching
- MBC1 RAM-bank switching
- MBC2 ROM banking
- MBC2 four-bit RAM
- MBC3 ROM/RAM banking
- MBC3 RTC selection and latch sequence
- MBC3 RTC advancement through bus ticks
- MBC5 nine-bit ROM banking
- MBC5 RAM banks
- MBC5 rumble state
- Explicit unsupported-mapper failure

DMA tests cover:

- `$FF46` register access
- 160-byte OAM transfer
- source-page addressing
- startup delay
- normal-speed 640 T-cycle timing
- CGB double-speed 320 T-cycle timing
- CPU access restriction during DMA
- HRAM access during DMA
- DMA restart
- completion state
- raw memory/OAM path used by DMA

## Game Boy Color-specific hardware

The CGB subsystem (`src/cgb/`) coordinates the console hardware that is not part of the DMG core. CGB memory banking and CGB palettes/PPU attributes were already present in the memory/PPU stages; this stage adds the remaining central CGB controls:

- `KEY1` speed preparation and current-speed reporting
- CGB double-speed switching through `STOP`
- Synchronization of CGB speed state with the timer and OAM DMA
- General-purpose VRAM DMA
- HBlank VRAM DMA
- `HDMA1`-`HDMA5` register behavior
- CGB VRAM-bank-latched HDMA destination
- CPU stall accounting for HDMA
- CGB infrared port register state (`RP`)
- Infrared LED output state and input state hooks

CGB CPU double speed toggles between 4.194304 MHz and 8.388608 MHz CPU operation, while the LCD/PPU and CGB HDMA keep their normal hardware timing. The timer/divider and OAM DMA are updated together with the speed state. citeturn905390search11turn374490search4

CGB VRAM DMA transfers 16-byte blocks from cartridge ROM/RAM or WRAM into VRAM. General-purpose DMA completes all requested blocks as one CPU stall, while HBlank DMA transfers one 16-byte block per visible-line HBlank and pauses during VBlank. The current implementation exposes the CPU stall duration to the future emulator scheduler instead of incorrectly running CPU instructions during the DMA window. citeturn905390search5turn374490search7

The CGB IR register is implemented as hardware state without inventing a platform-specific IR device. A later platform/frontend layer can drive the input state through `gb_cgb_set_ir_input()`.

The following areas are intentionally deferred to their owning later stages: CGB PCM/audio output (`FF76-FF77`) belongs to the Audio subsystem; boot-ROM remapping/`KEY0` belongs to cartridge/boot integration; serial-link behavior remains outside this stage.

## Main emulator loop / synchronization

The main runtime is split between a platform-independent emulator core and an SDL3 host layer. `src/emulator/gb_emulator.c` owns ROM initialization, hardware construction, CPU stepping, CGB CPU-stall handling, frame completion, and the CPU/base-clock conversion. `src/platform/gb_sdl3.c` owns SDL3 windowing, event processing, presentation, audio pumping, and real-time pacing. `src/main.c` is only the command-line entry point.

The synchronized runtime works as follows:

1. SDL3 reports elapsed host time through its nanosecond timer.
2. Host elapsed time is converted into a budget of CPU T-cycles using the current CPU clock.
3. The emulator executes CPU instructions while budget remains. Every CPU machine cycle clocks the memory/device bus.
4. In CGB double-speed mode the CPU clock is 8.388608 MHz, while PPU/APU/base memory hardware remains on the 4.194304 MHz base clock. The CPU bus timing wrapper therefore converts CPU cycles to base hardware cycles.
5. CGB HDMA stalls consume hardware time without executing CPU instructions. The returned CPU-cycle cost is scaled to the active CPU clock.
6. A completed PPU frame is presented through SDL3.
7. Audio is pumped from the APU's platform-independent PCM ring buffer.
8. If the emulator falls behind, host catch-up is capped at three frames instead of allowing an unbounded spiral of catch-up work.
9. STOP produces no CPU work until an input event wakes the CPU, or the CGB speed-switch path consumes a prepared STOP.

This keeps the emulated hardware clocks independent from host frame rate while still producing a real-time presentation/audio loop. The PPU signals completed frames through `frame_ready`; the SDL3 platform does not invent a separate display clock.

## Build with Make

From the `gbc_emulator` root, a full build compiles the SDL3 executable and all tests:

```sh
make
```

Run the regression suite without requiring the SDL3 development package:

```sh
make test
```

Build only the emulator executable:

```sh
make emulator
```

Run a ROM:

```sh
make run ROM=path/to/game.gb
make run ROM=path/to/game.gbc MODE=--cgb
```

Individual subsystem tests:

```sh
make test-cpu
make test-memory
make test-cartridge
make test-mbc
make test-timer
make test-interrupt
make test-ppu
make test-input
make test-dma
make test-cgb
make test-audio
make test-emulator
```

Run AddressSanitizer + UndefinedBehaviorSanitizer:

```sh
make sanitize
```

Clean:

```sh
make clean
```

Use Clang instead of the default C compiler:

```sh
make CC=clang
```

`make`/`make emulator` require an installed SDL3 development package discoverable through `pkg-config`. `make test` and the hardware-core tests do not link against SDL3.

## Build with VS Code

Open the `gbc_emulator` directory itself as the VS Code workspace. The `.vscode` configuration calls the same Makefile used from the terminal, so editor builds do not use a separate compiler/build definition.

The workspace contains tasks for the complete regression suite, each hardware subsystem, sanitizer builds, the SDL3 executable, and running a ROM. It also contains debugger configurations for the emulator, DMG/CGB launch modes, and the emulator synchronization tests.

For the normal emulator build, use the `Build GBC emulator` task. To run a ROM through VS Code, use `Run ROM with Make` or one of the emulator debugger configurations.

## Audio / APU

The Audio Processing Unit is implemented in `src/audio/` as a platform-independent hardware core. The Game Boy has four hardware sound channels: two pulse channels, one programmable wave channel, and one noise channel, mixed into left/right outputs. The APU runs from the same master clock as the CPU/PPU, but its timing is independent of CGB CPU double-speed mode. citeturn692625search0turn692625search2

Implemented audio hardware:

- Channel 1 pulse generator
- Channel 1 frequency sweep
- Channel 2 pulse generator
- Channel 3 programmable wave channel
- 16-byte wave RAM at `$FF30-$FF3F`
- Channel 4 noise generator with 15-bit/7-bit LFSR modes
- Length counters
- Volume envelopes
- APU frame sequencer at 512 Hz
- 256 Hz length clocks
- 128 Hz channel 1 sweep clocks
- 64 Hz envelope clocks
- DAC enable behavior
- Trigger behavior
- `NR50` master volume
- `NR51` stereo routing
- `NR52` APU power/channel status
- CGB `PCM12`/`PCM34` digital channel output registers at `$FF76-$FF77`
- Fixed-point 48 kHz stereo PCM generation
- Fixed-size audio ring buffer
- Dropped-frame diagnostics when the PCM ring is full

The register layout follows the documented `NR10-NR52` sound-control registers, including the per-channel control registers, mixer registers, and CGB PCM readback. citeturn692625search1turn692625search3

The frame sequencer is connected to the existing timer's DIV-APU falling-edge path. Consequently, normal timer operation, DIV writes, and CGB speed changes can drive APU frame-sequencer clocks through the same hardware timing boundary instead of using an unrelated wall-clock timer. The APU itself is not doubled when the CGB CPU enters double-speed mode, matching the documented hardware behavior. citeturn692625search0turn692625search2

### SDL3 audio output

`src/audio/gb_audio_sdl3.c` is the platform adapter. It contains the only SDL3 audio dependency and consumes the core's generated stereo float32 PCM through `SDL_AudioStream`. It uses the SDL3 audio-stream API rather than SDL2's removed `SDL_QueueAudio`/callback conventions. `SDL_OpenAudioDeviceStream`, `SDL_ResumeAudioStreamDevice`, and `SDL_PutAudioStreamData` are current SDL3 APIs. citeturn373101search4turn373101search2turn968771search0turn968771search7

The project exposes:

```sh
make sdl3-audio
```

for compiling the SDL3 adapter. The current build environment used for validation does not contain the SDL3 development package, so this adapter was syntax-checked against the current SDL3 API signatures but could not be linked against a local SDL3 library here.

### Audio accuracy boundary

The four digital generators, timers, envelopes, sweep, mixer routing, power behavior, PCM generation, and APU timing are implemented. A few low-level analog and silicon-quirk behaviors are intentionally not treated as exact transistor-level models in this stage: the high-pass filter uses a stable digital approximation, and the most obscure active wave-RAM corruption/access rules are not yet modeled cycle-for-cycle on every Game Boy revision. The public hardware interfaces are structured so those details can be tightened without changing the SDL3 layer.

## Main runtime files

```text
src/emulator/gb_emulator.h  Public platform-independent emulator orchestration API
src/emulator/gb_emulator.c  Hardware construction, CPU stepping, frame control, CGB clock conversion
src/platform/gb_sdl3.h      SDL3 host-layer API
src/platform/gb_sdl3.c      Window, renderer, input events, audio pump, real-time scheduler
src/main.c                  ROM command-line parsing and process lifetime
```

The emulator core does not include SDL3. SDL3 appears only in `src/platform/`, `src/main.c`, and the existing SDL3 input/audio adapters.

## Save RAM

Battery-backed cartridge RAM now persists to a sidecar `.sav` file. For a normal ROM path such as `games/Pokemon.gbc`, the default save path is `games/Pokemon.sav`. The save layer is separate from the MBC implementation, so MBC1/MBC2/MBC3/MBC5 banking continues to own cartridge addressing while `src/save/` owns persistence.

Implemented save behavior:

- Detects battery-backed cartridges through the parsed cartridge type
- Creates the default `.sav` path from the ROM path
- Loads an existing save before emulation starts
- Initializes missing save RAM to `0xFF`
- Tracks RAM dirty state only when an emulated RAM byte actually changes
- Writes raw cartridge RAM without adding emulator-specific headers
- Uses a temporary file followed by replacement of the destination file
- Flushes dirty RAM when the emulator is explicitly saved, when a ROM is replaced, and during normal emulator shutdown
- Supports MBC2's 512-byte internal RAM size
- Does not require SDL3

The command-line option `--no-save` disables loading and writing battery-backed save RAM for that emulator session. ROMs loaded from an in-memory buffer do not get an implicit save filename; callers that need persistence for buffer-loaded ROMs can use `GB_SaveRAM` directly with an explicit path.

MBC3 RTC persistence remains separate from the RAM save file and is still deferred; the existing RTC state is preserved only for the running emulator instance at this stage.

## Debugging / diagnostics

`src/debug/` contains a platform-independent diagnostics subsystem. It is attached to the emulator core but does not affect normal execution when disabled.

Implemented diagnostics:

- Runtime log levels: ERROR, WARN, INFO, DEBUG, TRACE
- CPU instruction tracing
- Up to 64 execution breakpoints
- Breakpoint detection before instruction execution
- CPU step counters
- CPU T-cycle counters
- Base hardware T-cycle counters
- Completed-frame counters
- Breakpoint-hit counters
- CPU state dump
- Cartridge metadata dump
- PPU state dump
- Arbitrary memory-range dump through the real memory bus
- Configurable diagnostic output stream

The command-line frontend exposes the most useful controls:

```text
--debug
--trace
--breakpoint $ADDR
--no-save
```

Example:

```sh
./build/bin/gbc_emulator --trace --breakpoint 0150 game.gbc
```

When an execution breakpoint is reached, the SDL3 loop exits cleanly and the frontend reports the breakpoint address. With a breakpoint configured, it also prints the CPU, cartridge, PPU, and diagnostic-counter state.

The debugger remains a diagnostic layer rather than an interactive debugger UI; GDB/LLDB can still be used normally because the build keeps debug symbols and separates the hardware modules into normal C translation units.

The dedicated tests are:

```sh
make test-save
make test-debug
```

## Final integration

The final integration layer is now complete. All ordered subsystems are constructed and destroyed through `GB_Emulator`, and the runtime exposes one consistent path from ROM loading through CPU execution, hardware timing, frame presentation, audio output, persistent saves, and diagnostics.

The final runtime flow is:

```text
ROM path
  ↓
Cartridge header + MBC setup
  ↓
GB_Emulator hardware construction
  ├── CPU
  ├── Memory / bus
  ├── Interrupts
  ├── Timer
  ├── PPU
  ├── Input
  ├── DMA
  ├── CGB
  └── APU
  ↓
Save RAM load
  ↓
CPU-driven synchronized execution
  ├── peripheral/base-clock ticking
  ├── CGB speed-domain conversion
  ├── DMA/HDMA stalls
  └── debugger/breakpoint hooks
  ↓
SDL3 presentation + input + audio
  ↓
Save RAM flush + deterministic subsystem cleanup
```

The final integration tests cover not only isolated subsystems but also cross-subsystem paths: loading a cartridge into the complete emulator, executing a frame, switching CGB clock speed, persisting battery-backed RAM through the emulator API, and stopping execution through a debugger breakpoint.

`make test` runs the complete functional suite. `make sanitize` reruns the full suite with AddressSanitizer and UndefinedBehaviorSanitizer. `make verify` runs both validation passes. The VS Code workspace has a `Final integration verification` task that calls the same Make target.

### Platform/build integration

The Makefile supports Unix-like hosts and Windows GNU Make environments through OS-specific directory/cleanup recipes. SDL3 can be found through `pkg-config`, or supplied explicitly with `SDL3_PREFIX`, `SDL3_CFLAGS`, and `SDL3_LIBS` when the platform does not provide a `.pc` file.

The SDL3 layer remains isolated from the emulated hardware core. The renderer uses SDL3's current renderer/texture and logical-presentation APIs, including `SDL_CreateWindow`, `SDL_CreateRenderer`, `SDL_SetRenderLogicalPresentation`, `SDL_SetTextureScaleMode`, `SDL_RenderTexture`, and `SDL_RenderPresent`. citeturn458057search6turn458057search7turn458057search5turn458057search0turn458057search3

### Remaining scope

The final integration stage does not silently claim hardware that is still intentionally outside the implemented emulator: exact MBC3 RTC persistence format and a full interactive debugger UI remain optional future extensions. They do not block the completed hardware/runtime integration represented by this project stage.

## Development boundary

Implemented now:

1. CPU
2. Memory / bus
3. Cartridge / ROM loading
4. Timer
5. Interrupt system
6. PPU / video
7. Input / joypad
8. DMA
9. MBC / cartridge hardware
10. Game Boy Color-specific hardware
11. Audio / APU
12. Main emulator loop / synchronization
13. Save RAM
14. Debugging / diagnostics
15. Final integration

The project is now at the final integration stage requested by the project specification. fileciteturn0file0L62-L78
