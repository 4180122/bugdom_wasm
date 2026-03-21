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

## Deploy to Render (Static Site)

Deploy the WASM build to [Render](https://render.com) as a **Static Site** (free tier):

1. **Trigger a build** — Push to `main`; the workflow `.github/workflows/build-and-push-prebuilt.yml` builds WASM and pushes artifacts to the `prebuilt` branch
2. **Create Static Site**: [dashboard.render.com](https://dashboard.render.com) → New → Static Site
3. **Connect** your GitHub repo (e.g. `bugdom_wasm`)
4. **Settings**:
   - **Branch**: `prebuilt`
   - **Build Command**: leave empty (or `echo "Pre-built"`)
   - **Publish Directory**: `.`
5. Click **Create Static Site** — Render serves the pre-built files from the branch
6. Your game is live at `https://<service-name>.onrender.com`

## Notes

- **Performance**: WebGL is GPU-accelerated. The build requests `powerPreference: high-performance` so the browser prefers a dedicated GPU when available. Bugdom uses a native WebGL2/GLES3 renderer (`gles3_rhi.c`). Use `-DCMAKE_BUILD_TYPE=Release` for production.
- **SDL3**: Building SDL3 from source for Emscripten is required; the macOS/Windows pre-built packages are native only.
- **Testing**: Prefer a recent Chrome, Firefox, or Safari for WebAssembly and WebGL 2.
