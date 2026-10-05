#include <baro.h>
#include <stdio.h>
TEST("subtest stack behavior") {
    printf("begin\n");
    SUBTEST("1") {
        printf("1\n");
        SUBTEST("1.1") {
            printf("1.1\n");
        }
    }
    SUBTEST("2") {
        printf("2\n");
        SUBTEST("2.1") {
                printf("2.1\n");
        }
        SUBTEST("2.2") {
            printf("2.2\n");
            SUBTEST("2.2.1") {
                printf("2.2.1\n");
                SUBTEST("2.2.1.1") {
                    printf("2.2.1.1\n");
                }
                SUBTEST("2.2.1.2") {
                   printf("2.2.1.2\n");
                }
            }
        }
        SUBTEST("2.3") {
            printf("2.3\n");
        }
        SUBTEST("2.4") {
            printf("2.4\n");
        }
    }
    printf("\n");
}

TEST("subtest failures bubble up") {
    SUBTEST("a") {
        REQUIRE(1);
    }
    SUBTEST("b") {
        REQUIRE(0); // should fail
    }
    // will not be executed
    SUBTEST("c") {
        REQUIRE(0);
    }
}

TEST("subtests with identical descriptions + on the same line behave correctly") {
    SUBTEST("") { CHECK(0, "1"); } SUBTEST("") { CHECK(0, "2"); SUBTEST("") { CHECK(0, "3"); } SUBTEST("") { CHECK(0, "4"); } } SUBTEST("") { CHECK(0, "5"); }
}