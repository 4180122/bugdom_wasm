# Minimal Emscripten test - verifies env (preload, server, etc.)
# Prereq: source emsdk_env.sh

.PHONY: all clean build build-gl serve serve-npx

all: build

build:
	emcc hello.c -o hello.js \
		--preload-file data@/data \
		-s EXIT_RUNTIME=0 \
		-s ERROR_ON_UNDEFINED_SYMBOLS=0
	@echo "Build done. Output: hello.js, hello.wasm, hello.data"

# OpenGL test - uses legacy GL emulation (glBegin/glEnd, glEnable)
# Tests the getCurTexUnit path that crashes in Bugdom
# Post-build: patch getCurTexUnit to handle null s_texUnits (Emscripten bug workaround)
build-gl:
	emcc gl_test.c -o gl_test.js \
		-s LEGACY_GL_EMULATION=1 \
		-s GL_FFP_ONLY=1 \
		-s USE_WEBGL2=1 \
		-s GL_UNSAFE_OPTS=0 \
		-s EXIT_RUNTIME=0 \
		-lGL-emu-webgl2-getprocaddr
	sed -i 's/return s_texUnits\[s_activeTexture\]/return s_texUnits?s_texUnits[s_activeTexture]:{enabled_tex1D:false,enabled_tex2D:false,enabled_tex3D:false,enabled_texCube:false,texTypesEnabled:0,env:{}}/g' gl_test.js
	sed -i 's/if (!GL.currentContext) {/if (!GL.currentContext || !GL.currentContext.tempVertexBufferCounters1) {/g' gl_test.js
	@# Trigger moduleContextCreatedCallbacks when using emscripten_webgl_make_context_current
	@# (Browser.createContext path never runs, so GLImmediate.init and generateTempBuffers never run)
	perl -i -0pe 's/(var _emscripten_webgl_make_context_current = \(contextHandle\) => \{\s*var success = GL\.makeContextCurrent\(contextHandle\);)\s*(return success \? 0 : -5;)/$$1\n      if (success \&\& contextHandle \&\& typeof GLImmediate != "undefined" \&\& !GLImmediate.initted \&\& typeof Browser != "undefined" \&\& Browser.moduleContextCreatedCallbacks \&\& Browser.moduleContextCreatedCallbacks.length) { Browser.useWebGL = true; Browser.moduleContextCreatedCallbacks.forEach(function(c){c();}); }\n      $$2/s' gl_test.js
	@echo "GL build done. Output: gl_test.js, gl_test.wasm. Open gl_test.html"

clean:
	rm -f hello.js hello.wasm hello.data gl_test.js gl_test.wasm
	@echo "Cleaned."

# Python server (may fail with large .data on Emscripten 3.1.x)
serve:
	python3 -m http.server 8080
	@echo "Open http://localhost:8080/index.html"

# npx serve - more reliable for .data loading
serve-npx:
	npx --yes serve -l 8080
	@echo "Open http://localhost:8080/index.html"
