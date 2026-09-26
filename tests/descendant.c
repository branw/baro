#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
int main(void) {
    const char *path = getenv("BARO_DESCENDANT_MARKER");
    if (!path) return 1;
    char started[1024];
    snprintf(started, sizeof(started), "%s.started", path);
    FILE *file = fopen(started, "wb");
    if (!file) return 2;
    fclose(file);
#ifdef _WIN32
    Sleep(2000);
#else
    struct timespec delay = {2, 0};
    nanosleep(&delay, NULL);
#endif
    file = fopen(path, "wb");
    if (!file) return 3;
    fclose(file);
    return 0;
}
