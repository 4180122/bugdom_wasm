# Patches for external dependencies

## pomme-wasm.patch

Applies to `extern/Pomme`. Contains fixes for WASM/Emscripten:

- **Files.cpp**: Emscripten prefs path, non-throwing `create_directories`
- **HostVolume.cpp**: Non-throwing `create_directory` (avoids exception-handling crash in WASM)
- **SoundManager.cpp**: Remove noisy sustain-loop warning

## sdl-emscripten.patch

Applies to `extern/SDL`. Contains WASM-specific WebGL tweaks:

- **SDL_emscriptenopengles.c**: `alpha=false` (avoids Color LCD compositing black overlay), `powerPreference=HIGH_PERFORMANCE`

### Apply (after submodule init)

```bash
git submodule update --init --recursive
git -C extern/Pomme apply --check patches/pomme-wasm.patch && git -C extern/Pomme apply patches/pomme-wasm.patch
git -C extern/SDL apply --check patches/sdl-emscripten.patch && git -C extern/SDL apply patches/sdl-emscripten.patch
```

Or run `make submodule-setup`.

### Revert

```bash
git -C extern/Pomme checkout -- src/Files/Files.cpp src/Files/HostVolume.cpp src/SoundMixer/SoundManager.cpp
git -C extern/SDL checkout -- src/video/emscripten/SDL_emscriptenopengles.c
```
