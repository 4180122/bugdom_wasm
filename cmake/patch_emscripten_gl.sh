#!/bin/sh
# Patch Emscripten-generated JS for legacy GL emulation bugs (SDL/emscripten_webgl_* path).
# 1. getCurTexUnit returns null before TexEnvJIT.init()
# 2. newRenderingFrameStarted crashes when tempVertexBufferCounters1 uninitialized
# 3. moduleContextCreatedCallbacks never run (GLImmediate.init, generateTempBuffers)
# 4. lazy-init GLImmediate in _glColor4f
# 5. relax VAO arrayBuffer assertion (glemu only supports single buffer; Bugdom uses multiple)
# 6. make assert() a no-op (bypass remaining glemu assertions that would abort)
set -e
FILE="$1"
[ -n "$FILE" ] || { echo "Usage: $0 <Bugdom.js>"; exit 1; }
[ -f "$FILE" ] || { echo "File not found: $FILE"; exit 1; }

# Save unpatched copy before patching, so "make wasm-patch" can re-apply without full rebuild
cp "$FILE" "$FILE.orig"

# Patch 1: getCurTexUnit (required - game won't run without it)
sed -i 's/return s_texUnits\[s_activeTexture\]/return s_texUnits?s_texUnits[s_activeTexture]:{enabled_tex1D:false,enabled_tex2D:false,enabled_tex3D:false,enabled_texCube:false,texTypesEnabled:0,env:{}}/g' "$FILE"

# Patch 2: newRenderingFrameStarted - COMMENTED OUT (testing black overlay)
# sed -i 's/if (!GL.currentContext) {/if (!GL.currentContext || !GL.currentContext.tempVertexBufferCounters1) {/g' "$FILE"

# Patch 3: trigger moduleContextCreatedCallbacks in make_context_current
# perl -i -0pe 's/(var _emscripten_webgl_make_context_current = \(contextHandle\) => \{\s*var success = GL\.makeContextCurrent\(contextHandle\);)\s*(return success \? 0 : -5;)/$1\n      if (success \&\& contextHandle \&\& typeof GLImmediate != "undefined" \&\& !GLImmediate.initted \&\& typeof Browser != "undefined" \&\& Browser.moduleContextCreatedCallbacks \&\& Browser.moduleContextCreatedCallbacks.length) { Browser.useWebGL = true; Browser.moduleContextCreatedCallbacks.forEach(function(c){c();}); }\n      $2/s' "$FILE"

# Patch 4: lazy-init GLImmediate in _glColor4f (fixes "Cannot set properties of null" when init hasn't run)
perl -i -0pe 's/(_glColor4f\s*=\s*\(r\s*,\s*g\s*,\s*b\s*,\s*a\)\s*=>\s*\{\s*)(r\s*=\s*Math\.max)/$1if(typeof GLImmediate!="undefined"\&\&!GLImmediate.clientColor\&\&typeof GLImmediate.init=="function"){if(typeof Browser!="undefined")Browser.useWebGL=true;GLImmediate.init();}$2/s' "$FILE"

# Patch 5: relax VAO arrayBuffer assertion - COMMENTED OUT (testing black overlay)
# sed -i 's/GLEmulation\.currentVao\.arrayBuffer==buffer||GLEmulation\.currentVao\.arrayBuffer==0||buffer==0/true/g' "$FILE"

# Patch 6: make assert() a no-op - COMMENTED OUT (testing black overlay)
# sed -i 's/function assert(condition,text){if(!condition){abort/function assert(condition,text){if(false){abort/g' "$FILE"

# Patch 7: emscripten_set_main_loop_timing - SDL/GL call it before main loop exists; silently store timing for later
# sed -i 's/if(!MainLoop\.func){err("emscripten_set_main_loop_timing: Cannot set timing mode for main loop since a main loop does not exist! Call emscripten_set_main_loop first to set one up.");return 1}/if(!MainLoop.func){return 0}/g' "$FILE"

# Patch 8: suppress "WARNING: using emscripten GL emulation" (cosmetic)
sed -i 's/err("WARNING: using emscripten GL emulation\. This is a collection of limited workarounds, do not expect it to work\.")/(0)/g' "$FILE"

# Patch 9: suppress "WARNING: using emscripten GL immediate mode emulation" (cosmetic)
sed -i 's/err("WARNING: using emscripten GL immediate mode emulation\. This is very limited in what it supports")/(0)/g' "$FILE"

# Patch 10: suppress "DrawElements doesn't actually prepareClientAttributes properly" - COMMENTED OUT
# sed -i 's/warnOnce("DrawElements doesn'\''t actually prepareClientAttributes properly\.")/(0)/g' "$FILE"

# Patch 11: cap MAX_TEXTURES so TEXTURE0+MAX_TEXTURES <= MAX_VERTEX_ATTRIBS (fixes "Index must be less than MAX_VERTEX_ATTRIBS").
# Add third Math.min arg: (GLctx.getParameter(GLctx.MAX_VERTEX_ATTRIBS)||16)-3. Supports minified (") and debug (') formats.
sed -i 's/MAX_TEXTURE_IMAGE_UNITS"\]||GLctx\.getParameter(GLctx\.MAX_TEXTURE_IMAGE_UNITS),28)/MAX_TEXTURE_IMAGE_UNITS"]||GLctx.getParameter(GLctx.MAX_TEXTURE_IMAGE_UNITS),28,(GLctx.getParameter(GLctx.MAX_VERTEX_ATTRIBS)||16)-3)/g' "$FILE"
sed -i "s/MAX_TEXTURE_IMAGE_UNITS'\] || GLctx\.getParameter(GLctx\.MAX_TEXTURE_IMAGE_UNITS), 28)/MAX_TEXTURE_IMAGE_UNITS'] || GLctx.getParameter(GLctx.MAX_TEXTURE_IMAGE_UNITS), 28, (GLctx.getParameter(GLctx.MAX_VERTEX_ATTRIBS) || 16) - 3)/g" "$FILE"

# Patch 12: prepareClientAttributes for glDrawElements (required for background/sky/terrain - without it they render black)
# sed -i 's/flush(numProvidedIndexes,startIndex=0,ptr=0){/flush(numProvidedIndexes,startIndex=0,ptr=0){if(numProvidedIndexes>0\&\&typeof Module!="undefined"\&\&Module._glemuVertexCount!==undefined){GLImmediate.prepareClientAttributes(Module._glemuVertexCount,false);}/g' "$FILE"
# sed -i 's/flush(numProvidedIndexes, startIndex = 0, ptr = 0) {/flush(numProvidedIndexes, startIndex = 0, ptr = 0) { if (numProvidedIndexes>0 \&\& typeof Module!="undefined" \&\& Module._glemuVertexCount!==undefined) { GLImmediate.prepareClientAttributes(Module._glemuVertexCount, false); } /g' "$FILE"
# COLOR vertexAttribPointer relax - COMMENTED OUT (did not fix black overlay)
# sed -i 's/!GLctx\.currentArrayBufferBinding){GLctx\.vertexAttribPointer(GLImmediate\.COLOR/true){GLctx.vertexAttribPointer(GLImmediate.COLOR/g' "$FILE"

# Patch 13: Support GL_UNSIGNED_INT indices (glemu hardcodes UNSIGNED_SHORT; Bugdom uses UNSIGNED_INT).
# C code sets Module._glemuIndexType = 0x1405 before glDrawElements. Use HEAPU32 and correct buffer size.
# Minified and debug (pretty-printed) formats - both must be patched.
sed -i 's/numIndexes=numProvidedIndexes;/numIndexes=numProvidedIndexes;var _idxType=(typeof Module!="undefined"\&\&Module._glemuIndexType===0x1405);var _idxBytes=_idxType?(numProvidedIndexes<<2):(numProvidedIndexes<<1);/g' "$FILE"
sed -i 's/numIndexes = numProvidedIndexes;/numIndexes = numProvidedIndexes; var _idxType=(typeof Module!="undefined"\&\&Module._glemuIndexType===0x1405); var _idxBytes=_idxType?(numProvidedIndexes<<2):(numProvidedIndexes<<1);/g' "$FILE"
sed -i 's/var currIndex=HEAPU16\[ptr+i\*2>>1\]/var currIndex=_idxType?HEAPU32[((ptr)>>2)+i]:HEAPU16[ptr+i*2>>1]/g' "$FILE"
sed -i 's/var currIndex = HEAPU16\[(((ptr)+(i\*2))>>1)\];/var currIndex = _idxType?HEAPU32[((ptr)>>2)+i]:HEAPU16[(((ptr)+(i*2))>>1)];/g' "$FILE"
sed -i 's/assert(numProvidedIndexes << 1 <= GL\.MAX_TEMP_BUFFER_SIZE, '\''too many immediate mode indexes (a)'\'');/assert(_idxBytes<=GL.MAX_TEMP_BUFFER_SIZE,"too many immediate mode indexes (a)");/g' "$FILE"
sed -i 's/var indexBuffer=GL\.getTempIndexBuffer(numProvidedIndexes<<1)/var indexBuffer=GL.getTempIndexBuffer(_idxBytes)/g' "$FILE"
sed -i 's/var indexBuffer = GL\.getTempIndexBuffer(numProvidedIndexes << 1);/var indexBuffer = GL.getTempIndexBuffer(_idxBytes);/g' "$FILE"
# Both replacements use space after comma in false branch to avoid pattern re-matching (sed /g).
sed -i 's/HEAPU16\.subarray(ptr>>1,ptr+(numProvidedIndexes<<1)>>1)/_idxType?HEAPU32.subarray((ptr)>>2,((ptr)>>2)+numProvidedIndexes):HEAPU16.subarray(ptr>>1, ptr+(numProvidedIndexes<<1)>>1)/g' "$FILE"
sed -i 's/HEAPU16\.subarray((((ptr)>>1)), ((ptr + (numProvidedIndexes << 1))>>1))/_idxType?HEAPU32.subarray((ptr)>>2,((ptr)>>2)+numProvidedIndexes):HEAPU16.subarray(ptr>>1, ptr+(numProvidedIndexes<<1)>>1)/g' "$FILE"
sed -i 's/GLctx\.drawElements(GLImmediate\.mode,numIndexes,GLctx\.UNSIGNED_SHORT,ptr)/GLctx.drawElements(GLImmediate.mode,numIndexes,_idxType?GLctx.UNSIGNED_INT:GLctx.UNSIGNED_SHORT,ptr)/g' "$FILE"
sed -i 's/GLctx\.drawElements(GLImmediate\.mode, numIndexes, GLctx\.UNSIGNED_SHORT, ptr);/GLctx.drawElements(GLImmediate.mode, numIndexes, _idxType?GLctx.UNSIGNED_INT:GLctx.UNSIGNED_SHORT, ptr);/g' "$FILE"
