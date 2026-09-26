#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
int main(void) {
    const char *mode = getenv("BARO_PROBE_MODE");
    if (mode && !strcmp(mode, "fail")) return 17;
    if (mode && !strcmp(mode, "crash")) raise(SIGABRT);
    if (mode && !strcmp(mode, "hang")) {
#ifdef _WIN32
        Sleep(10000);
#else
        sleep(10);
#endif
        return 0;
    }
    if (!getenv("BARO_EMULATOR_CHECK") || strcmp(getenv("BARO_EMULATOR_CHECK"), "arg with spaces;and semicolon")) return 18;
    const char *path = getenv("BARO_PROBE_JSON");
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file) return 19;
    int byte;
    while ((byte = fgetc(file)) != EOF) putchar(byte);
    fclose(file);
    return 0;
}
