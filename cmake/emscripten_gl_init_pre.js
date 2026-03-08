// Force Emscripten legacy GL emulation to initialize before first glEnable.
// The TexEnvJIT (s_texUnits) must be populated or getCurTexUnit returns null.
// Note: preRun cannot call WASM exports (runtime not ready). The C code must
// call glActiveTexture(GL_TEXTURE0) as the very first GL call in main().
(function() {
  if (typeof Module === 'undefined') Module = {};
  // Reserved for future use (e.g. Module.preinitializedWebGLContext)

  // Prevent browser from consuming Escape so the game can use it for pause/UI cancel
  document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') {
      e.preventDefault();
    }
  }, true);
})();
