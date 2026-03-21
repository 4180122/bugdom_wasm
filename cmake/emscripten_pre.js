// Emscripten pre-js: browser-level setup before WASM runtime starts.
(function() {
  if (typeof Module === 'undefined') Module = {};

  // Prevent browser from consuming Escape so the game can use it for pause/UI cancel
  document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') {
      e.preventDefault();
    }
  }, true);
})();
