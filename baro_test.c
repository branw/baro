// These tests reach the runtime's internals, so they compile it directly
// rather than linking it.
#include "baro.c"

#ifndef BARO_SELF_TEST
#error These unit tests are for baro itself and should not be used externally
#endif

TEST("Asserts") {
    CHECK(1 + 1);
    CHECK(1 + 1, "True");
    REQUIRE(1 + 1);
    REQUIRE(1 + 1, "True");

    CHECK_FALSE(1 - 1);
    CHECK_FALSE(1 - 1, "False");
    REQUIRE_FALSE(1 - 1);
    REQUIRE_FALSE(1 - 1, "False");

    CHECK_EQ(1 + 1, 2);
    CHECK_EQ(1 + 1, 2, "Equal");
    REQUIRE_EQ(1 + 1, 2);
    REQUIRE_EQ(1 + 1, 2, "Equal");

    CHECK_NE(1 + 1, 3);
    CHECK_NE(1 + 1, 3, "Not equal");
    REQUIRE_NE(1 + 1, 3);
    REQUIRE_NE(1 + 1, 3, "Not equal");

    CHECK_LT(2, 3);
    CHECK_LT(2, 3, "Less than");
    REQUIRE_LT(2, 3);
    REQUIRE_LT(2, 3, "Less than");

    CHECK_LE(3, 3);
    CHECK_LE(3, 3, "Less than or equal to");
    REQUIRE_LE(3, 3);
    REQUIRE_LE(3, 3, "Less than or equal to");

    CHECK_GT(3, 2);
    CHECK_GT(3, 2, "Greater than or equal to");
    REQUIRE_GT(3, 2);
    REQUIRE_GT(3, 2, "Greater than or equal to");

    CHECK_GE(3, 3);
    CHECK_GE(3, 3, "Greater than or equal to");
    REQUIRE_GE(3, 3);
    REQUIRE_GE(3, 3, "Greater than or equal to");

    CHECK_STR_EQ("bar", &"foobar"[3]);
    CHECK_STR_EQ("bar", &"foobar"[3], "Equal (case sensitive)");
    REQUIRE_STR_EQ("bar", &"foobar"[3]);
    REQUIRE_STR_EQ("bar", &"foobar"[3], "Equal (case sensitive)");

    CHECK_STR_NE("bar", "Bar");
    CHECK_STR_NE("bar", "Bar", "Not equal (case sensitive)");
    REQUIRE_STR_NE("bar", "Bar");
    REQUIRE_STR_NE("bar", "Bar", "Not equal (case sensitive)");

    CHECK_STR_ICASE_EQ("bar", "Bar");
    CHECK_STR_ICASE_EQ("bar", "Bar", "Equal (case insensitive)");
    REQUIRE_STR_ICASE_EQ("bar", "Bar");
    REQUIRE_STR_ICASE_EQ("bar", "Bar", "Equal (case insensitive)");

    CHECK_STR_ICASE_NE("bar", "foobar");
    CHECK_STR_ICASE_NE("bar", "foobar", "Not equal (case insensitive)");
    REQUIRE_STR_ICASE_NE("bar", "foobar");
    REQUIRE_STR_ICASE_NE("bar", "foobar", "Not equal (case insensitive)");
}

// The runtime maintains this hash incrementally; this is the definition it
// must agree with.
static uint64_t baro__tag_list_hash(struct baro__tag_list *list) {
    uint64_t hash = 0;
    for (size_t i = 0; i < list->size; i++) {
        hash ^= baro__tag_hash(list->tags[i], i);
    }
    return hash;
}

static inline void baro__tag_list_destroy(struct baro__tag_list *list) {
    if (list->tags) {
        free(list->tags);
    }
}

TEST("Tag list") {
    struct baro__tag_list list;
    baro__tag_list_create(&list, 2);

    CHECK_FALSE(baro__tag_list_hash(&list),"an empty list has a hash of 0");

    struct baro__tag a = {.desc = "desc 1", .file_path = "file 1", .line_num = 123};
    struct baro__tag b = {.desc = "desc 2", .file_path = "file 2", .line_num = 456};

    SUBTEST("Push and pop") {
        baro__tag_list_push(&list, &a);

        uint64_t hash_1 = baro__tag_list_hash(&list);
        CHECK(hash_1);

        baro__tag_list_push(&list, &b);

        uint64_t hash_2 = baro__tag_list_hash(&list);
        CHECK(hash_2);
        CHECK_NE(hash_2, hash_1);

        struct baro__tag c;
        REQUIRE(baro__tag_list_pop(&list, &c), "we can add");

        CHECK_STR_EQ(c.desc, "desc 2");
        CHECK_STR_EQ(c.file_path, "file 2");
        CHECK_EQ(c.line_num, 456);

        CHECK_EQ(baro__tag_list_hash(&list), hash_1);

        struct baro__tag d;
        REQUIRE(baro__tag_list_pop(&list, &d));

        CHECK_STR_EQ(d.desc, "desc 1");
        CHECK_STR_EQ(d.file_path, "file 1");
        CHECK_EQ(d.line_num, 123);

        CHECK_FALSE(baro__tag_list_hash(&list));

        struct baro__tag e;
        REQUIRE_FALSE(baro__tag_list_pop(&list, &e), "popping an empty list returns nothing");
    }

    SUBTEST("Hashing uses reference equality") {
        baro__tag_list_push(&list, &a);

        struct baro__tag_list list2;
        baro__tag_list_create(&list2, 2);

        struct baro__tag a_copy;
        memcpy(&a_copy, &a, sizeof(a));
        baro__tag_list_push(&list2, &a_copy);

        REQUIRE_NE(baro__tag_list_hash(&list), baro__tag_list_hash(&list2),
                   "lists with identical values at different addresses have different hashes");

        baro__tag_list_destroy(&list2);
    }

    baro__tag_list_destroy(&list);
}

TEST("Subtest hash is maintained incrementally") {
    CHECK_EQ(baro__c.subtest_hash, baro__tag_list_hash(&baro__c.subtest_stack));
    SUBTEST("outer") {
        CHECK_EQ(baro__c.subtest_hash, baro__tag_list_hash(&baro__c.subtest_stack));
        SUBTEST("inner") {
            REQUIRE_EQ(baro__c.subtest_stack.size, 2);
            CHECK_EQ(baro__c.subtest_hash, baro__tag_list_hash(&baro__c.subtest_stack));
        }
        CHECK_EQ(baro__c.subtest_hash, baro__tag_list_hash(&baro__c.subtest_stack));
    }
    CHECK_EQ(baro__c.subtest_hash, baro__tag_list_hash(&baro__c.subtest_stack));
}

#ifndef _WIN32
#include <sys/resource.h>
#endif
TEST("Capture does not leak descriptors") {
#ifndef _WIN32
    struct rlimit limit;
    REQUIRE_EQ(getrlimit(RLIMIT_NOFILE, &limit), 0);
    if (limit.rlim_cur > 64) limit.rlim_cur = 64;
    REQUIRE_EQ(setrlimit(RLIMIT_NOFILE, &limit), 0);
#endif
    for (int i = 0; i < 200; i++) {
        baro__redirect_output(&baro__c, 0);
        baro__redirect_output(&baro__c, 1);
        baro__redirect_output(&baro__c, 1); // Already capturing: do not duplicate again.
    }
    int fd = BARO__DUP(BARO__FILENO(stdout));
    REQUIRE_GE(fd, 0);
    BARO__CLOSE(fd);
}
