/* Emscripten/WASM: compatibility stubs for desktop OpenGL features not in WebGL. */
#if defined(__EMSCRIPTEN__)
#include <GL/gl.h>

void glColorMaterial(GLenum face, GLenum mode)
{
	(void)face;
	(void)mode;
	/* No-op: Emscripten glemu doesn't provide this. Vertex colors are
	 * typically used via glColor* in the FFP; rendering may still work. */
}

/* WebGL only supports GL_GENERATE_MIPMAP_HINT and GL_FRAGMENT_SHADER_DERIVATIVE_HINT.
 * GL_FOG_HINT and others cause INVALID_ENUM. No-op for unsupported hints. */
void glHint(GLenum target, GLenum mode)
{
	(void)target;
	(void)mode;
	/* No-op: fog and other desktop hints are not in WebGL. */
}
#endif
