/* Minimal OpenGL test - verifies legacy GL emulation (glEnable, glActiveTexture, glBegin/glEnd)
 * Uses the same API as Bugdom to reproduce getCurTexUnit crash if present. */
#include <stdio.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GL/gl.h>

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;
static int frame_count = 0;
static int setup_done = 0;

static void main_loop(void)
{
    /* Defer matrix setup to first frame - GLImmediate.init() runs from
     * moduleContextCreatedCallbacks, which fires after main() returns. */
    if (!setup_done) {
        glEnable(GL_DEPTH_TEST);
        glViewport(0, 0, 640, 480);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1, 1, -1, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        setup_done = 1;
    }

    glClearColor(0.2f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Draw a simple colored triangle - legacy immediate mode */
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0);
    glVertex3f( 0.0f,  0.5f, 0);
    glColor3f(0, 1, 0);
    glVertex3f(-0.5f, -0.5f, 0);
    glColor3f(0, 0, 1);
    glVertex3f( 0.5f, -0.5f, 0);
    glEnd();

    frame_count++;
    if (frame_count == 60) {
        printf("GL test OK: 60 frames rendered (legacy GL emulation working)\n");
        emscripten_cancel_main_loop();
    }
}

int main(void)
{
    printf("OpenGL test starting...\n");

    EmscriptenWebGLContextAttributes attrs = {
        .alpha = 0,
        .depth = 1,
        .stencil = 0,
        .antialias = 0,
        .majorVersion = 2,
        .minorVersion = 0,
    };
    emscripten_webgl_init_context_attributes(&attrs);

    ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        printf("Failed to create WebGL context: %d\n", (int)ctx);
        return 1;
    }
    if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS) {
        printf("Failed to make context current\n");
        return 1;
    }

    /* CRITICAL: Must call glActiveTexture before any glEnable.
     * Otherwise getCurTexUnit() returns null and crashes. */
    glActiveTexture(GL_TEXTURE0);
    printf("glActiveTexture(GL_TEXTURE0) OK\n");

    /* This triggers getCurTexUnit - will crash if TexEnvJIT not initialized */
    glEnable(GL_NORMALIZE);
    printf("glEnable(GL_NORMALIZE) OK\n");

    /* Matrix setup (glLoadIdentity, etc.) deferred to first frame - GLImmediate.init()
     * runs from moduleContextCreatedCallbacks after main() returns. */
    printf("Starting render loop...\n");
    emscripten_set_main_loop(main_loop, 0, 1);

    return 0;
}
