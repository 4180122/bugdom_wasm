/* Minimal Emscripten test - verifies preload-file and basic runtime */
#include <stdio.h>
#include <emscripten.h>

int main(void)
{
    printf("Hello from Emscripten!\n");

#ifdef __EMSCRIPTEN__
    /* Try to read a preloaded file */
    FILE* f = fopen("/data/hello.txt", "r");
    if (f) {
        char buf[64];
        if (fgets(buf, sizeof(buf), f)) {
            printf("Read from preload: %s", buf);
        }
        fclose(f);
    } else {
        printf("Could not open /data/hello.txt\n");
    }
#endif

    printf("Test complete.\n");
    return 0;
}
