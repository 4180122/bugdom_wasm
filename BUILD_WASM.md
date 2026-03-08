# Building Bugdom for the web (WASM)

You can build Bugdom to run in a browser using **Emscripten** (WASM). This is not an officially supported platform; the following is a minimal path to get a browser build.

## Prerequisites

1. **Emscripten SDK (emsdk)**  
   Install and activate it:
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **Dependencies** — No submodules or manual cloning needed. `make wasm` fetches Pomme and SDL into `extern/` automatically.

## Build steps

From the Bugdom repo root, with `emsdk_env.sh` already sourced:

```bash
make wasm
```

This fetches dependencies (if missing), configures with Emscripten, and builds. Output is under `build-wasm/`: `Bugdom.js`, `Bugdom.wasm`, and `Bugdom.html`.

## Serve and run

Emscripten’s default output needs to be served over HTTP (file:// often won’t work). For example:

```bash
cd build-wasm
python3 -m http.server 8080
```

Or use `make serve-npx` for more reliable serving (fixes "Unexpected error while handling" with Emscripten 3.1.x and large .data files).

Then open `http://localhost:8080/Bugdom.html` (or the URL that loads `Bugdom.js`) in a modern browser.

## Data folder (game assets)

The CMake setup preloads the `Data` directory into the virtual filesystem so the game can find assets. The game is configured to look for data at `/Data` when built for Emscripten.

## Notes

- **Performance**: WebGL is GPU-accelerated. The build requests `powerPreference: high-performance` so the browser prefers a dedicated GPU when available. Bugdom uses fixed-function OpenGL emulated via LEGACY_GL_EMULATION, which adds CPU overhead; expect slower performance than native. Use `-DCMAKE_BUILD_TYPE=Release`. For tuning, experiment with **ASYNCIFY** or **PTHREADS** (extra link flags).
- **SDL3**: Building SDL3 from source for Emscripten is required; the macOS/Windows pre-built packages are native only.
- **Testing**: Prefer a recent Chrome, Firefox, or Safari for WebAssembly and WebGL 2.
