#include "baro.h"

#ifndef BARO_SELF_TEST
#error These unit tests are for baro itself and should not be used externally
#endif

TEST("Tag list") {
  struct baro__tag_list list;
  baro__tag_list_create(&list, 2);

  CHECK(baro__tag_list_hash(&list) == 0);

  struct baro__tag a = {.desc="desc",.file_name="file",.line_num=123};
  baro__tag_list_push(&list, &a);

  uint64_t hash_1 = baro__tag_list_hash(&list);
  CHECK(hash_1 != 0);

  baro__tag_list_push(&list, &a);

  uint64_t hash_2 = baro__tag_list_hash(&list);
  CHECK(hash_2 != 0);
  CHECK(hash_2 != hash_1);

  struct baro__tag b;
  REQUIRE(baro__tag_list_pop(&list, &b));

  CHECK(!strcmp(b.desc, "desc"));
  CHECK(!strcmp(b.file_name, "file"));
  CHECK(b.line_num == 123);

  CHECK(baro__tag_list_hash(&list) == hash_1);

  struct baro__tag c;
  REQUIRE(baro__tag_list_pop(&list, &c));

  CHECK(baro__tag_list_hash(&list) == 0);

  baro__tag_list_destroy(&list);
}
