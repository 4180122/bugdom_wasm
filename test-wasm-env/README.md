# WASM Environment Test

Minimal project to verify your Emscripten setup: preload-file loading and legacy OpenGL emulation.

## Prerequisites

- Emscripten SDK active: `source /opt/emsdk/emsdk_env.sh` (or your emsdk path)
- Check version: `emcc --version`

## Build

**Basic test (preload + runtime):**
```bash
cd test-wasm-env
make build
```

**OpenGL test (legacy GL emulation - glBegin/glEnd, glEnable):**
```bash
make build-gl
```

## Serve & Test

```bash
make serve-npx
```

Then open:
- http://localhost:8080/index.html — basic test
- http://localhost:8080/gl_test.html — OpenGL test

## Expected Result

**Basic test:**
- Console: "Hello from Emscripten!"
- Console: "Read from preload: Hello from preloaded file!"
- No "Unexpected error while handling" for the .data file

**OpenGL test:**
- Colored triangle rendered (red/green/blue)
- Console: "GL test: context created, glActiveTexture OK, glEnable OK, drawing..."
- No "getCurTexUnit" or "Cannot read properties of null" errors

## If It Fails

- **"Unexpected error while handling"** → Use `make serve-npx` instead of Python
- **"Could not open /data/hello.txt"** → Preload path issue (should be data@/data)
- **"getCurTexUnit" / "Cannot read properties of null"** → Emscripten legacy GL emulation bug (same as Bugdom)
- **Other errors** → Check emcc version
