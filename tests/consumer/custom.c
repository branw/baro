#include "baro.h"
#include <stdio.h>
int main(int argc, char *argv[]) {
    if (baro_is_child(argc, argv) || baro_is_discovery(argc, argv)) return baro_run(argc, argv);
    /* Parent-only application setup must not corrupt the JSON inventory. */
    puts("Parent-only setup");
    return baro_run(argc, argv);
}
