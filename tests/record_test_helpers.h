/* Test-owned values and allocation ledger; never used by the AML runtime. */
#ifndef AML_RECORD_TEST_HELPERS_H
#define AML_RECORD_TEST_HELPERS_H
#include <stdint.h>

#if defined(AML_RECORD_ALLOC_WRAP) || defined(AML_CHECKPOINT_ALLOC_WRAP)
static int allocation_budget = -1, tracked_count;
static void* tracked[8192];
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
static void track(void* p) {
    if (p && allocation_budget >= 0) { CHECK(tracked_count < 8192); tracked[tracked_count++] = p; }
}
static void untrack(void* p) {
    for (int i = 0; p && i < tracked_count; i++)
        if (tracked[i] == p) { tracked[i] = tracked[--tracked_count]; return; }
}
static int refuse(void) {
    if (allocation_budget == 0) return 1;
    if (allocation_budget > 0) allocation_budget--;
    return 0;
}
void* __wrap_malloc(size_t n) { if (refuse()) return NULL; void* p = __real_malloc(n); track(p); return p; }
void* __wrap_calloc(size_t n, size_t s) { if (refuse()) return NULL; void* p = __real_calloc(n, s); track(p); return p; }
void* __wrap_realloc(void* old, size_t n) {
    if (refuse()) return NULL;
    void* p = __real_realloc(old, n); if (p) { untrack(old); track(p); } return p;
}
void __wrap_free(void* p) { untrack(p); __real_free(p); }
#endif

static uint32_t bits_of(float value) { uint32_t bits; memcpy(&bits, &value, 4); return bits; }
static float float_of(uint32_t bits) { float value; memcpy(&value, &bits, 4); return value; }
static void release_leaf(AML_Var* value) {
    if (value->type == AML_TYPE_ARRAY) am_array_free(value->array);
    if (value->type == AML_TYPE_STRING) am_string_free(value->string);
    if (value->type == AML_TYPE_LIST) am_list_free(value->list);
    if (value->type == AML_TYPE_MAP) am_map_free(value->map);
    memset(value, 0, sizeof(*value));
}
static int put(AM_Record* record, const char* name, const AML_Var* value) {
    AM_String* key = am_string_new(name); CHECK(key);
    int rc = am_record_set(record, key, value); am_string_free(key); return rc;
}
static const AML_Var* leaf(const AM_Record* record, const char* name) {
    AM_String* key = am_string_new(name); CHECK(key);
    const AML_Var* value = am_record_get(record, key); am_string_free(key); return value;
}
static const uint32_t sample_bits[] = {
    UINT32_C(0x3f800000), UINT32_C(0x00000001), UINT32_C(0x7f800000),
    UINT32_C(0xff800000), UINT32_C(0x7fc12345), UINT32_C(0x80000000)
};
static AM_Record* make_sample(void) {
    AM_Record* record = am_record_new(); CHECK(record);
    AML_Var value = {0}; value.type = AML_TYPE_FLOAT; value.value = float_of(0x80000000);
    CHECK(put(record, "scalar", &value) == 0);
    value.type = AML_TYPE_STRING; value.string = am_string_new("rain\nשלום"); CHECK(value.string);
    CHECK(put(record, "string", &value) == 0); release_leaf(&value);
    value.type = AML_TYPE_ARRAY; value.array = am_array_new(6); CHECK(value.array);
    value.array->rows = 2; value.array->cols = 3;
    for (int i = 0; i < 6; i++) value.array->data[i] = float_of(sample_bits[i]);
    CHECK(put(record, "array", &value) == 0); release_leaf(&value);
    value.type = AML_TYPE_LIST; value.list = am_list_new(); CHECK(value.list);
    const char* strings[] = {"", "é", "x\n"};
    for (int i = 0; i < 3; i++) {
        AM_String* item = am_string_new(strings[i]); CHECK(item);
        CHECK(am_list_push(value.list, item) >= 0); am_string_free(item);
    }
    CHECK(put(record, "list", &value) == 0); release_leaf(&value);
    value.type = AML_TYPE_MAP; value.map = am_map_new(); CHECK(value.map);
    const char* keys[] = {"first", "", "é"};
    const uint32_t numbers[] = {0x3f800000, 0x80000000, 0xc0500000};
    for (int i = 0; i < 3; i++) {
        AM_String* key = am_string_new(keys[i]); CHECK(key);
        CHECK(am_map_set(value.map, key, float_of(numbers[i])) == 0); am_string_free(key);
    }
    CHECK(put(record, "map", &value) == 0); release_leaf(&value);
    return record;
}
static void check_sample(const AM_Record* record) {
    const char* names[] = {"scalar", "string", "array", "list", "map"};
    AM_List* keys = am_record_keys(record); CHECK(keys && keys->len == 5);
    for (int i = 0; i < 5; i++) CHECK(!strcmp(keys->items[i]->data, names[i]));
    am_list_free(keys);
    const AML_Var* value = leaf(record, "scalar"); CHECK(value && value->type == AML_TYPE_FLOAT);
    CHECK(bits_of(value->value) == 0x80000000);
    value = leaf(record, "string"); CHECK(value && value->type == AML_TYPE_STRING);
    CHECK(!strcmp(value->string->data, "rain\nשלום"));
    value = leaf(record, "array"); CHECK(value && value->type == AML_TYPE_ARRAY);
    CHECK(value->array->len == 6 && value->array->rows == 2 && value->array->cols == 3);
    for (int i = 0; i < 6; i++) CHECK(bits_of(value->array->data[i]) == sample_bits[i]);
    value = leaf(record, "list"); CHECK(value && value->type == AML_TYPE_LIST && value->list->len == 3);
    CHECK(!strcmp(value->list->items[0]->data, "") && !strcmp(value->list->items[1]->data, "é") &&
          !strcmp(value->list->items[2]->data, "x\n"));
    value = leaf(record, "map"); CHECK(value && value->type == AML_TYPE_MAP && value->map->len == 3);
    CHECK(!strcmp(value->map->entries[0].key->data, "first") && bits_of(value->map->entries[0].value) == 0x3f800000);
    CHECK(!strcmp(value->map->entries[1].key->data, "") && bits_of(value->map->entries[1].value) == 0x80000000);
    CHECK(!strcmp(value->map->entries[2].key->data, "é") && bits_of(value->map->entries[2].value) == 0xc0500000);
}
#endif
