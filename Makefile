# Bugdom - Quick build targets
# Use: make <target>

.PHONY: all clean wasm wasm-clean wasm-debug wasm-debug-clean native native-clean deps submodule-setup help

# Use all CPU cores for parallel builds
JOBS := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Default: show help
all: help

#----------------------------------------------------------------------
# WASM (Emscripten) build
#----------------------------------------------------------------------
# Prereqs: source emsdk_env.sh
WASM_DIR := build-wasm
WASM_DEBUG_DIR := build-wasm-debug

POMME_URL := https://github.com/jorio/Pomme
SDL_URL := https://github.com/libsdl-org/SDL

# Fetch Pomme and SDL into extern/, apply patches. No submodules needed.
deps:
	@mkdir -p extern
	@if [ ! -f extern/Pomme/CMakeLists.txt ]; then \
		rm -rf extern/Pomme; \
		echo "Cloning Pomme..."; \
		git clone --depth 1 $(POMME_URL) extern/Pomme; \
		git apply --directory=extern/Pomme --check patches/pomme-wasm.patch 2>/dev/null && \
			git apply --directory=extern/Pomme patches/pomme-wasm.patch && \
			echo "Pomme WASM patch applied."; \
	elif [ -f patches/pomme-wasm.patch ]; then \
		git apply --directory=extern/Pomme --check patches/pomme-wasm.patch 2>/dev/null && \
			git apply --directory=extern/Pomme patches/pomme-wasm.patch && \
			echo "Pomme WASM patch applied." || true; \
	fi
	@if [ ! -f extern/SDL/CMakeLists.txt ]; then \
		rm -rf extern/SDL; \
		echo "Cloning SDL..."; \
		git clone --depth 1 $(SDL_URL) extern/SDL; \
		git apply --directory=extern/SDL --check patches/sdl-emscripten.patch 2>/dev/null && \
			git apply --directory=extern/SDL patches/sdl-emscripten.patch && \
			echo "SDL emscripten patch applied."; \
	elif [ -f patches/sdl-emscripten.patch ]; then \
		git apply --directory=extern/SDL --check patches/sdl-emscripten.patch 2>/dev/null && \
			git apply --directory=extern/SDL patches/sdl-emscripten.patch && \
			echo "SDL emscripten patch applied." || true; \
	fi

wasm-clean:
	rm -rf $(WASM_DIR)
	@echo "WASM build dir cleaned."

# Debug build type runs faster than Release with legacy GL emulation (JS minification/link-step).
# Even -O0 Release is slower; use Debug config for production WASM.
wasm-configure: deps wasm-clean
	emcmake cmake -S . -B $(WASM_DIR) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_SDL_FROM_SOURCE=ON
	@echo "WASM configured (Debug type for best runtime performance)."

# Configure with Data embedded in JS (avoids .data fetch; use if "Unexpected error while handling" persists)
wasm-configure-embed: deps wasm-clean
	emcmake cmake -S . -B $(WASM_DIR) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_SDL_FROM_SOURCE=ON \
		-DEMBED_DATA_FOR_WASM=ON
	@echo "WASM configured (Data embedded in JS)."

wasm-embed: wasm-configure-embed
	emmake cmake --build $(WASM_DIR) -j$(JOBS)
	@echo "WASM build done (Data embedded). Output: $(WASM_DIR)/Bugdom.{js,wasm,html} (no .data file)"

wasm: wasm-configure
	emmake cmake --build $(WASM_DIR) -j$(JOBS)
	@echo "WASM build done. Output: $(WASM_DIR)/Bugdom.{js,wasm,html,data}"

# Rebuild only (no clean) - use when you've already configured
wasm-rebuild:
	emmake cmake --build $(WASM_DIR) -j$(JOBS)

# Ensure submodules are initialized and patches applied (run after clone)
submodule-setup:
	git submodule update --init --recursive
	@if [ -f patches/pomme-wasm.patch ]; then \
		git -C extern/Pomme apply --check patches/pomme-wasm.patch 2>/dev/null && \
		git -C extern/Pomme apply patches/pomme-wasm.patch && \
		echo "Pomme WASM patch applied." || echo "Pomme patch skipped (changes already present or incompatible)."; \
	fi
	@if [ -f patches/sdl-emscripten.patch ]; then \
		git -C extern/SDL apply --check patches/sdl-emscripten.patch 2>/dev/null && \
		git -C extern/SDL apply patches/sdl-emscripten.patch && \
		echo "SDL emscripten patch applied." || echo "SDL patch skipped (changes already present or incompatible)."; \
	fi

# Re-apply glemu patches without full rebuild (run after editing cmake/patch_emscripten_gl.sh)
wasm-patch:
	@test -f $(WASM_DIR)/Bugdom.js.orig || (echo "Run 'make wasm' or 'make wasm-embed' first"; exit 1)
	cp $(WASM_DIR)/Bugdom.js.orig $(WASM_DIR)/Bugdom.js
	sh cmake/patch_emscripten_gl.sh $(WASM_DIR)/Bugdom.js
	@echo "$(WASM_DIR): patches reapplied."
	@if [ -f $(WASM_DEBUG_DIR)/Bugdom.js.orig ]; then \
		cp $(WASM_DEBUG_DIR)/Bugdom.js.orig $(WASM_DEBUG_DIR)/Bugdom.js; \
		sh cmake/patch_emscripten_gl.sh $(WASM_DEBUG_DIR)/Bugdom.js; \
		echo "$(WASM_DEBUG_DIR): patches reapplied."; \
	else \
		echo "$(WASM_DEBUG_DIR): skipped (no .orig; run 'make wasm-debug' first)."; \
	fi
	@echo "Refresh browser to test."

#----------------------------------------------------------------------
# WASM Debug build (pretty-printed JS for devtools)
#----------------------------------------------------------------------
wasm-debug-clean:
	rm -rf $(WASM_DEBUG_DIR)
	@echo "WASM debug build dir cleaned."

wasm-debug-configure: wasm-debug-clean
	emcmake cmake -S . -B $(WASM_DEBUG_DIR) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_SDL_FROM_SOURCE=ON
	@echo "WASM debug configured."

wasm-debug: wasm-debug-configure
	emmake cmake --build $(WASM_DEBUG_DIR) -j$(JOBS)
	@echo "WASM debug build done. Output: $(WASM_DEBUG_DIR)/Bugdom.{js,wasm,html}"

wasm-debug-rebuild:
	emmake cmake --build $(WASM_DEBUG_DIR) -j$(JOBS)

wasm-debug-patch:
	@test -f $(WASM_DEBUG_DIR)/Bugdom.js.orig || (echo "Run 'make wasm-debug' first"; exit 1)
	cp $(WASM_DEBUG_DIR)/Bugdom.js.orig $(WASM_DEBUG_DIR)/Bugdom.js
	sh cmake/patch_emscripten_gl.sh $(WASM_DEBUG_DIR)/Bugdom.js
	@echo "Debug patches reapplied. Refresh browser to test."

#----------------------------------------------------------------------
# Native Linux build
#----------------------------------------------------------------------
NATIVE_DIR := build

native-clean:
	rm -rf $(NATIVE_DIR)
	@echo "Native build dir cleaned."

native-configure: native-clean
	cmake -S . -B $(NATIVE_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo
	@echo "Native configured."

native: native-configure
	cmake --build $(NATIVE_DIR) -j$(JOBS)
	@echo "Native build done. Run: $(NATIVE_DIR)/Bugdom"

native-rebuild:
	cmake --build $(NATIVE_DIR) -j$(JOBS)

#----------------------------------------------------------------------
# Serve WASM (for testing in browser)
#----------------------------------------------------------------------
# Python's http.server can fail loading large .data files with Emscripten 3.1.x
# (streaming fetch error). Use serve-npx for more reliable serving.
serve:
	cd $(WASM_DIR) && python3 -m http.server 8080
	@echo "Open http://localhost:8080/Bugdom.html"

# Use npx serve - handles large .data files better (fixes Emscripten 3.1.x streaming errors)
serve-npx:
	cd $(WASM_DIR) && npx --yes serve -l 8080
	@echo "Open http://localhost:8080/Bugdom.html"

serve-debug:
	cd $(WASM_DEBUG_DIR) && npx --yes serve -l 8081
	@echo "Open http://localhost:8081/Bugdom.html"

#----------------------------------------------------------------------
help:
	@echo "Bugdom build targets:"
	@echo ""
	@echo "  make wasm          - Clean + configure + build WASM (Debug config for best perf)"
	@echo "  make wasm-embed    - Same but embed Data in JS (avoids .data fetch errors)"
	@echo "  make wasm-rebuild  - Rebuild WASM only (no clean)"
	@echo "  make wasm-patch    - Re-apply glemu patches (Release + Debug if built)"
	@echo "  make wasm-clean    - Remove build-wasm/"
	@echo ""
	@echo "  make wasm-debug    - Clean + configure + build WASM Debug (pretty-printed JS)"
	@echo "  make wasm-debug-rebuild  - Rebuild WASM Debug only"
	@echo "  make wasm-debug-patch    - Re-apply glemu patches on Debug build only"
	@echo "  make wasm-debug-clean    - Remove build-wasm-debug/"
	@echo ""
	@echo "  make native        - Clean + configure + build native Linux"
	@echo "  make native-rebuild - Rebuild native only"
	@echo "  make native-clean  - Remove build/"
	@echo ""
	@echo "  make serve         - Serve build-wasm/ on http://localhost:8080 (Python)"
	@echo "  make serve-npx     - Serve Release on :8080 (npx, handles .data better)"
	@echo "  make serve-debug   - Serve build-wasm-debug/ on http://localhost:8081"
	@echo ""
	@echo "  make deps          - Fetch Pomme + SDL into extern/, apply patches (auto-run by wasm)"
	@echo ""
	@echo "WASM prereqs: source emsdk_env.sh"
