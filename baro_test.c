#include "baro.h"

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

TEST("Tag list") {
  struct baro__tag_list list;
  baro__tag_list_create(&list, 2);

  CHECK_FALSE(baro__tag_list_hash(&list));

  struct baro__tag a = {.desc="desc",.file_name="file",.line_num=123};
  baro__tag_list_push(&list, &a);

  uint64_t hash_1 = baro__tag_list_hash(&list);
  CHECK(hash_1);

  baro__tag_list_push(&list, &a);

  uint64_t hash_2 = baro__tag_list_hash(&list);
  CHECK(hash_2);
  CHECK_NE(hash_2, hash_1);

  struct baro__tag b;
  REQUIRE(baro__tag_list_pop(&list, &b));

  CHECK_STR_EQ(b.desc, "desc");
  CHECK_STR_EQ(b.file_name, "file");
  CHECK_EQ(b.line_num, 123);

  CHECK_EQ(baro__tag_list_hash(&list), hash_1);

  struct baro__tag c;
  REQUIRE(baro__tag_list_pop(&list, &c));

  CHECK_FALSE(baro__tag_list_hash(&list));

  baro__tag_list_destroy(&list);
}
