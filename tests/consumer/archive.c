#include "baro.h"
/* Referencing this symbol pulls the object (and its registrar) from the archive. */
void archive_anchor(void) {}
TEST("archive test") { CHECK(1); }
