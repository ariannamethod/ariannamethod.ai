/* Record ownership, typed leaves, independent snapshots and atomic replacement. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "FAIL record API line %d: %s (%s)\n", __LINE__, #c, am_get_error()); exit(1); } } while (0)
#include "record_test_helpers.h"

static void test_ownership(void) {
    AM_Record* record = make_sample(); check_sample(record);
    am_record_ref(record); am_record_free(record); check_sample(record);
    AM_Record* copy = am_record_clone(record); CHECK(copy && copy != record); check_sample(copy);
    const char* mutable_names[] = {"array", "list", "map"};
    for (int i = 0; i < 3; i++) {
        const AML_Var* a = leaf(record, mutable_names[i]); const AML_Var* b = leaf(copy, mutable_names[i]);
        CHECK(a != b);
        if (i == 0) CHECK(a->array != b->array && a->array->data != b->array->data);
        if (i == 1) CHECK(a->list != b->list && a->list->items != b->list->items);
        if (i == 2) CHECK(a->map != b->map && a->map->entries != b->map->entries);
    }
    leaf(copy, "array")->array->data[0] = 99;
    AM_String* added = am_string_new("copy"); CHECK(added);
    CHECK(am_list_push(leaf(copy, "list")->list, added) == 4);
    CHECK(am_map_set(leaf(copy, "map")->map, added, 99) == 0); am_string_free(added);
    check_sample(record); am_record_free(copy);
    AM_Record* destination = am_record_new(); CHECK(destination);
    AML_Var scalar = {0}; scalar.type = AML_TYPE_FLOAT; scalar.value = 17;
    CHECK(put(destination, "old", &scalar) == 0);
    CHECK(am_record_replace(destination, record) == 0); check_sample(destination);
    CHECK(leaf(destination, "old") == NULL);
    CHECK(am_record_replace(destination, destination) == 0); check_sample(destination);
    leaf(record, "array")->array->data[0] = 88; check_sample(destination);
    am_record_free(destination); am_record_free(record);
    am_record_ref(NULL); am_record_free(NULL);
}
static void test_types_and_limits(void) {
    AM_Record* record = am_record_new(); CHECK(record);
    AM_String* key = am_string_new("field"); CHECK(key);
    AML_Var value = {0}; value.type = AML_TYPE_FLOAT; value.value = float_of(0x7fc12345);
    CHECK(am_record_set(record, key, &value) == 0);
    CHECK(bits_of(am_record_get(record, key)->value) == 0x7fc12345);
    value.value = float_of(0x7f800000); CHECK(am_record_set(record, key, &value) == 0);
    CHECK(bits_of(am_record_get(record, key)->value) == 0x7f800000);
    value.value = float_of(0xff800000); CHECK(am_record_set(record, key, &value) == 0);
    CHECK(bits_of(am_record_get(record, key)->value) == 0xff800000);
    AM_String invalid_text = {.data = "\xc0", .byte_len = 1, .len = -1, .refcount = 1};
    CHECK(am_record_set(record, &invalid_text, &value) < 0);
    AML_Var invalid_leaf = {.type = AML_TYPE_STRING, .string = &invalid_text};
    CHECK(am_record_set(record, key, &invalid_leaf) < 0);
    CHECK(am_record_set(NULL, key, &value) < 0);
    CHECK(am_record_set(record, NULL, &value) < 0 && am_record_set(record, key, NULL) < 0);
    CHECK(am_record_clone(NULL) == NULL && am_record_keys(NULL) == NULL);
    CHECK(am_record_get(NULL, key) == NULL && am_record_get(record, NULL) == NULL);
    CHECK(am_record_replace(record, NULL) < 0 && am_record_replace(NULL, record) < 0);
    value.type = AML_TYPE_RECORD; value.record = record;
    CHECK(am_record_set(record, key, &value) < 0);
    value.type = AML_TYPE_TOKENIZER; value.tokenizer = NULL;
    CHECK(am_record_set(record, key, &value) < 0);
    value.type = 99; CHECK(am_record_set(record, key, &value) < 0);
    CHECK(bits_of(am_record_get(record, key)->value) == 0xff800000);
    am_string_free(key); am_record_free(record);
    record = am_record_new(); CHECK(record);
    memset(&value, 0, sizeof(value)); value.type = AML_TYPE_FLOAT;
    for (int i = 0; i < 256; i++) {
        char name[32]; snprintf(name, sizeof(name), "field_%03d", i); value.value = (float)i;
        CHECK(put(record, name, &value) == 0);
    }
    CHECK(put(record, "one-too-many", &value) < 0);
    AM_List* keys = am_record_keys(record); CHECK(keys && keys->len == 256); am_list_free(keys);
    value.value = 99; CHECK(put(record, "field_000", &value) == 0);
    CHECK(leaf(record, "field_000")->value == 99);
    am_record_free(record);
}
static void test_swap(void) {
    AM_Record* live = make_sample(); AM_Record* pending = am_record_new(); CHECK(pending);
    AML_Var scalar = {0}; scalar.type = AML_TYPE_FLOAT; scalar.value = 17;
    CHECK(put(pending, "old", &scalar) == 0); am_record_ref(live);
    AM_Record* alias = live;
#ifdef AML_RECORD_ALLOC_WRAP
    CHECK(tracked_count == 0); allocation_budget = 0;
#endif
    int rc = am_record_swap(live, pending);
    int self = am_record_swap(live, live);
#ifdef AML_RECORD_ALLOC_WRAP
    allocation_budget = -1; CHECK(tracked_count == 0);
#endif
    CHECK(rc == 0 && self == 0); check_sample(pending);
    CHECK(leaf(live, "old")->value == 17 && leaf(alias, "old")->value == 17);
    CHECK(am_record_swap(live, NULL) < 0 && am_record_swap(NULL, pending) < 0);
    am_record_free(alias); am_record_free(live); am_record_free(pending);
#ifdef AML_RECORD_ALLOC_WRAP
    am_init(); am_persistent_mode(1);
    CHECK(am_exec("live = record_new()\nrecord_set(live, 'v', 1)\n"
                  "pending = record_new()\nrecord_set(pending, 'v', 2)\n") == 0);
    void* program = am_program_open("record_swap(live, pending)\nsentinel = 1\n"); CHECK(program);
    allocation_budget = 0;
    int finished = am_program_step(program, 1);
    allocation_budget = -1; CHECK(finished == 0);
    while (!am_program_step(program, 1)) {}
    CHECK(am_program_close(program) == 0);
    CHECK(am_get_var_float("sentinel") == 1);
    CHECK(leaf(am_get_var_record("live"), "v")->value == 2);
    CHECK(leaf(am_get_var_record("pending"), "v")->value == 1);
    am_persistent_clear(); am_persistent_mode(0); CHECK(tracked_count == 0);
#endif
}
static void test_persistent(void) {
    am_init(); am_persistent_mode(1);
    AM_Record* record = make_sample(); CHECK(am_set_var_record("saved", record) == 0);
    const AM_Record* saved = am_get_var_record("saved"); CHECK(saved && saved != record); check_sample(saved);
    leaf(record, "array")->array->data[0] = 100;
    am_record_free(record); check_sample(saved);
    CHECK(am_set_var_record("saved", NULL) != 0); check_sample(am_get_var_record("saved"));
    CHECK(am_exec("saved = 1\n") == 0 && am_get_var_record("saved") == NULL);
    am_persistent_clear(); am_persistent_mode(0);
}
static void test_allocations(void) {
#ifdef AML_RECORD_ALLOC_WRAP
    AM_Record* source = make_sample();
    for (int operation = 0; operation < 3; operation++) {
        int success = 0;
        for (int budget = 0; budget < 128; budget++) {
            AM_Record* destination = am_record_new(); CHECK(destination);
            AML_Var old = {0}; old.type = AML_TYPE_FLOAT; old.value = 17;
            CHECK(put(destination, "old", &old) == 0);
            AM_String* key = am_string_new("new"); CHECK(key);
            const AML_Var* item = leaf(source, "list");
            CHECK(tracked_count == 0); allocation_budget = budget;
            AM_Record* cloned = NULL; int rc;
            if (operation == 0) { cloned = am_record_clone(source); rc = cloned ? 0 : -1; }
            else if (operation == 1) rc = am_record_replace(destination, source);
            else rc = am_record_set(destination, key, item);
            allocation_budget = -1;
            if (rc == 0) {
                success = 1;
                if (operation == 0) check_sample(cloned);
                else if (operation == 1) check_sample(destination);
                else CHECK(leaf(destination, "new")->list->len == 3);
            } else {
                CHECK(leaf(destination, "old")->value == 17);
                AM_List* keys = am_record_keys(destination); CHECK(keys && keys->len == 1); am_list_free(keys);
            }
            check_sample(source);
            am_record_free(cloned); am_record_free(destination); am_string_free(key);
            CHECK(tracked_count == 0);
            if (success) break;
        }
        CHECK(success);
    }
    am_record_free(source);
#endif
}
int main(void) {
    am_init(); test_ownership(); test_types_and_limits(); test_swap(); test_persistent(); test_allocations();
    printf("AML_RECORD_API_OK %d checks\n", checks); return 0;
}
