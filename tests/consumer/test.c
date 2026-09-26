#include <assert.h>
#include "baro.h"
#include <assert.h>
#ifdef close
#error Baro must not redefine close
#endif
#ifdef _Static_assert
#error Baro must not redefine _Static_assert
#endif
void archive_anchor(void);
TEST("consumer") { archive_anchor(); CHECK_EQ(2 + 2, 4); }
