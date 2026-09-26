#include "baro.h"
int main(int argc, char *argv[]) {
    if (baro_is_child(argc, argv)) return baro_run(argc, argv);
    /* Parent-only application setup belongs here. */
    return baro_run(argc, argv);
}
