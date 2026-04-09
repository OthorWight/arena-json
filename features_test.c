#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define ARENA_JSON_IMPLEMENTATION
#include "arena_json.h"
#include "cJSON.h"

#define TEST_ASSERT(cond, msg) \
    do { if (!(cond)) { printf("  ❌ FAIL: %s\n", msg); exit(1); } } while(0)

void test_builder_api(Arena *a) {
    printf("[1] TESTING BUILDER API & PARITY\n");

    // --- Arena JSON Builder ---
    JsonValue *a_root = json_create_object(a);
    json_object_add_string(a, a_root, "project", "Arena JSON");
    json_object_add_number(a, a_root, "version", 1.2);
    
    JsonValue *a_arr = json_create_array(a);
    json_array_append_number(a, a_arr, 10);
    json_array_append_number(a, a_arr, 20);
    
    json_object_add(a, a_root, "data", a_arr); 

    char *a_out = json_serialize(a, a_root, false, false, 0, false);

    // --- cJSON Builder ---
    cJSON *c_root = cJSON_CreateObject();
    cJSON_AddStringToObject(c_root, "project", "Arena JSON");
    cJSON_AddNumberToObject(c_root, "version", 1.2);
    
    cJSON *c_arr = cJSON_CreateArray();
    cJSON_AddItemToArray(c_arr, cJSON_CreateNumber(10));
    cJSON_AddItemToArray(c_arr, cJSON_CreateNumber(20));
    cJSON_AddItemToObject(c_root, "data", c_arr);

    char *c_out = cJSON_PrintUnformatted(c_root);

    TEST_ASSERT(strcmp(a_out, c_out) == 0, "Builder outputs do not match!");
    printf("  ✅ Builder parity confirmed.\n\n");

    free(c_out);
    cJSON_Delete(c_root);
}

void test_qol_features(Arena *a) {
    printf("[2] TESTING QOL GETTERS & CASE INSENSITIVITY\n");
    const char *src = "{\"User\": \"Benjamín\", \"Score\": 95}";
    JsonValue *root = json_parse(a, a, src, strlen(src), JSON_PARSE_STRICT, NULL);

    TEST_ASSERT(strcmp(json_object_get_string(root, "User", ""), "Benjamín") == 0, "json_object_get_string failed");
    
    JsonValue *found = json_object_get_case_insensitive(root, "user");
    TEST_ASSERT(found != NULL, "json_object_get_case_insensitive failed to find 'User' via 'user'");
    
    printf("  ✅ QoL features confirmed.\n\n");
}

void test_mutations(Arena *a) {
    printf("[3] TESTING MUTATIONS (REMOVE/CLONE/REPLACE/DETACH)\n");
    JsonValue *root = json_create_object(a);
    json_object_add_number(a, root, "a", 1);
    json_object_add_number(a, root, "b", 2);
    json_object_add_string(a, root, "status", "offline");

    // Test Remove
    json_object_remove(root, "b");
    TEST_ASSERT(json_object_get(root, "b") == NULL, "Key 'b' still exists after removal");

    // Test Replace
    JsonValue *new_status = json_create_string(a, "online");
    json_object_replace(a, root, "status", new_status);
    TEST_ASSERT(strcmp(json_object_get_string(root, "status", ""), "online") == 0, "Replace failed");

    // Test Detach
    JsonValue *detached = json_object_detach(a, root, "a");
    TEST_ASSERT(detached != NULL && detached->as.number == 1, "Detach value mismatch");
    TEST_ASSERT(json_object_get(root, "a") == NULL, "Detached key still in parent");

    // Test Clone
    JsonValue *cloned = json_clone(a, root);
    TEST_ASSERT(strcmp(json_object_get_string(cloned, "status", ""), "online") == 0, "Clone failed");
    
    printf("  ✅ All mutations and cloning confirmed.\n\n");
}

void test_iteration_macros(Arena *a) {
    printf("[4] TESTING ITERATION MACROS\n");
    JsonValue *obj = json_create_object(a);
    json_object_add_number(a, obj, "val1", 100);
    json_object_add_number(a, obj, "val2", 200);
    json_object_add_number(a, obj, "val3", 300);

    double sum = 0;
    JsonNode *entry;
    json_object_foreach(entry, obj) {
        if (entry->value.type == JSON_NUMBER) {
            sum += entry->value.as.number;
        }
    }

    TEST_ASSERT(sum == 600.0, "Object iteration failed to calculate correct sum");
    printf("  ✅ Object iteration confirmed (Sum: %.0f).\n\n", sum);
}

void test_error_reporting(Arena *a) {
    printf("[5] TESTING ERROR REPORTING\n");
    const char *bad_json = "{\"key\": [1, 2, ]}"; 
    JsonError err = {0};
    
    JsonValue *val = json_parse(a, a, bad_json, strlen(bad_json), JSON_PARSE_STRICT, &err);
    
    TEST_ASSERT(val == NULL, "Parser should have failed");
    printf("  ✅ Caught expected error at Line %d, Col %d: %s\n\n", err.line, err.col, err.msg);
}

// --- Helper for Round Trip ---
bool json_eq(JsonValue *v1, JsonValue *v2) {
    if (!v1 && !v2) return true;
    if (!v1 || !v2) return false;
    if (v1->type != v2->type) return false;
    switch (v1->type) {
        case JSON_NULL: return true;
        case JSON_BOOL: return v1->as.boolean == v2->as.boolean;
        case JSON_NUMBER: return v1->as.number == v2->as.number; 
        case JSON_STRING: return strcmp(v1->as.string, v2->as.string) == 0;
        case JSON_ARRAY:
        case JSON_OBJECT:
            if (v1->as.list.count != v2->as.list.count) return false;
            for (size_t i = 0; i < v1->as.list.count; i++) {
                if (v1->type == JSON_OBJECT) {
                    if (strcmp(v1->as.list.items[i].key, v2->as.list.items[i].key) != 0) return false;
                }
                if (!json_eq(&v1->as.list.items[i].value, &v2->as.list.items[i].value)) return false;
            }
            return true;
    }
    return false;
}

void test_round_trip(Arena *a) {
    printf("[6] TESTING ROUND-TRIP SERIALIZATION\n");
    const char *src = "{\"name\":\"test\",\"values\":[1, 2.5, null, true, false, \"\\u260E\"]}";
    JsonError err;
    JsonValue *root1 = json_parse(a, a, src, strlen(src), JSON_PARSE_STRICT, &err);
    TEST_ASSERT(root1 != NULL, "Initial parse failed");

    char *out1 = json_serialize(a, root1, false, false, 0, false);
    JsonValue *root2 = json_parse(a, a, out1, strlen(out1), JSON_PARSE_STRICT, &err);
    TEST_ASSERT(root2 != NULL, "Second parse failed");

    TEST_ASSERT(json_eq(root1, root2), "Round-trip DOMs are not equal");
    printf("  ✅ Round-trip confirmed.\n\n");
}

void test_dom_correctness(Arena *a) {
    printf("[7] TESTING DOM CORRECTNESS\n");
    
    // Surrogate pair decoding: G-clef character \uD834\uDD1E -> \xF0\x9D\x84\x9E
    const char *src_str = "[\"\\uD834\\uDD1E\"]";
    JsonValue *root_str = json_parse(a, a, src_str, strlen(src_str), JSON_PARSE_STRICT, NULL);
    TEST_ASSERT(root_str != NULL && root_str->type == JSON_ARRAY, "Parse array failed");
    TEST_ASSERT(root_str->as.list.count == 1, "Array count should be 1");
    JsonValue *str_val = &root_str->as.list.items[0].value;
    TEST_ASSERT(str_val->type == JSON_STRING, "Value should be string");
    TEST_ASSERT(strcmp(str_val->as.string, "\xF0\x9D\x84\x9E") == 0, "Surrogate pair decoded incorrectly");

    // Extreme numbers
    const char *src_num = "[1e-100, 123.456, -0.0]";
    JsonValue *root_num = json_parse(a, a, src_num, strlen(src_num), JSON_PARSE_STRICT, NULL);
    TEST_ASSERT(root_num != NULL, "Parse numbers failed");
    TEST_ASSERT(root_num->as.list.items[0].value.as.number == 1e-100, "1e-100 mismatch");
    TEST_ASSERT(root_num->as.list.items[1].value.as.number == 123.456, "123.456 mismatch");
    TEST_ASSERT(root_num->as.list.items[2].value.as.number == -0.0, "-0.0 mismatch");

    printf("  ✅ DOM Correctness confirmed.\n\n");
}

int main() {
    Arena a = {0};
    arena_init(&a);

    printf("==================================================\n");
    printf("       ARENA JSON: FULL VALIDATION SUITE          \n");
    printf("==================================================\n\n");

    test_builder_api(&a);
    arena_reset(&a);

    test_qol_features(&a);
    arena_reset(&a);

    test_mutations(&a);
    arena_reset(&a);

    test_iteration_macros(&a);
    arena_reset(&a);

    test_error_reporting(&a);
    arena_reset(&a);

    test_round_trip(&a);
    arena_reset(&a);

    test_dom_correctness(&a);

    arena_free(&a);
    printf("All Validation Tests Passed.\n");
    return 0;
}