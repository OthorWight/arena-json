#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define ARENA_JSON_IMPLEMENTATION
#include "arena_json.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;

    Arena main_arena;
    Arena scratch_arena;
    
    // Initializing with zero/null to test the arena's internal allocation logic
    memset(&main_arena, 0, sizeof(Arena));
    memset(&scratch_arena, 0, sizeof(Arena));
    
    // 1. Force Arena Block Growth
    // Exhaust the 1024-byte inline buffer immediately so the parser is forced 
    // to use arena_alloc_fallback and allocate new dynamic regions.
    arena_alloc(&main_arena, 1024);
    arena_alloc(&scratch_arena, 1024);

    // Test explicit arena init and auxiliary allocator features
    Arena test_arena;
    arena_init(&test_arena);
    void *z_ptr = arena_zalloc(&test_arena, 16);
    void *f_ptr = arena_alloc_fallback(&test_arena, 32);
    void *s_ptr = arena_alloc_struct(&test_arena, JsonValue);
    void *a_ptr = arena_alloc_array(&test_arena, JsonValue, 5);
    ArenaTemp temp = arena_temp_begin(&test_arena);
    void *t_ptr = arena_alloc(&test_arena, 8);
    (void)z_ptr; (void)f_ptr; (void)s_ptr; (void)a_ptr; (void)t_ptr;
    arena_temp_end(temp);
    arena_reset(&test_arena);

    // Test restoring an arena to a completely zeroed state
    Arena test_arena2;
    memset(&test_arena2, 0, sizeof(Arena));
    ArenaTemp temp2 = arena_temp_begin(&test_arena2);
    arena_alloc(&test_arena2, 16);
    arena_temp_end(temp2);
    arena_alloc(&test_arena2, 16); // Triggers `a->begin != NULL && a->end == NULL` fallback

    // Test ARENA_MAX_BLOCK_SIZE limit logic (runs once to avoid tanking iters/sec)
    static bool tested_max_block = false;
    if (!tested_max_block) {
        Arena max_arena;
        arena_init(&max_arena);
        arena_alloc(&max_arena, (ARENA_MAX_BLOCK_SIZE / 2) + 1024); // Block cap > 16MB
        arena_alloc(&max_arena, 16); // Doubling this capacity hits the 32MB cap branch
        arena_free(&max_arena);
        tested_max_block = true;
    }

    JsonError err;
    
    // 2. Trigger Key Cache (String Deduplication)
    // The parser only enables the key cache if the input length is > 4096.
    // We pad the input with spaces to guarantee the cache is active.
    size_t padded_size = size > 4097 ? size : 4097;
    char *padded_data = (char *)malloc(padded_size);
    memset(padded_data, ' ', padded_size);
    memcpy(padded_data, data, size);

    // Test Strict Parsing (no comments allowed)
    // This exercises the fast-path in `skip_whitespace`
    JsonError strict_err;
    JsonValue *strict_root = json_parse(&main_arena, &scratch_arena, padded_data, padded_size, 
                                 JSON_PARSE_STRICT, &strict_err);
    (void)strict_root;

    // 3. Test the Parser 
    // We use ALLOW_COMMENTS to hit the largest possible number of branches
    JsonValue *root = json_parse(&main_arena, &scratch_arena, padded_data, padded_size, 
                                 JSON_PARSE_ALLOW_COMMENTS, &err);
    free(padded_data);
    
    if (root) {
        // 2. Test Deep Cloning (Exercises recursive traversal)
        JsonValue *cloned = json_clone(&main_arena, root);

        // 3. Test Builders
        JsonValue *dummy_null = json_create_null(&main_arena);
        JsonValue *dummy_bool = json_create_bool(&main_arena, false);
        JsonValue *dummy_num  = json_create_number(&main_arena, 42.0);
        JsonValue *dummy_str  = json_create_string(&main_arena, "test");
        JsonValue *dummy_arr  = json_create_array(&main_arena);
        JsonValue *dummy_obj  = json_create_object(&main_arena);
        (void)dummy_num; (void)dummy_str; (void)dummy_arr;

        // 4. Test Mutation, Iterators & QoL APIs
        // These were 'red' in your report; this forces them to execute
        if (cloned->type == JSON_OBJECT) {
            json_object_add_bool(&main_arena, cloned, "fuzz_bool", true);
            json_object_add_number(&main_arena, cloned, "fuzz_num", 123.456);
            json_object_add_string(&main_arena, cloned, "fuzz_str", "string_val");
            json_object_add(&main_arena, cloned, "fuzz_obj", dummy_obj);

            // Getters
            (void)json_object_get(cloned, "fuzz_num");
            (void)json_object_get_number(cloned, "fuzz_num", 0.0);
            (void)json_object_get_string(cloned, "fuzz_str", "fallback");
            (void)json_object_get_bool(cloned, "fuzz_bool", false);
            (void)json_object_get_case_insensitive(cloned, "FUZZ_STR");

            // Iterator
            JsonNode *entry;
            json_object_foreach(entry, cloned) {
                (void)entry;
            }

            // Object mutations
            json_object_replace(&main_arena, cloned, "fuzz_str", dummy_null);
            JsonValue *detached = json_object_detach(&main_arena, cloned, "fuzz_obj");
            (void)detached;
            json_object_remove(cloned, "fuzz_bool");
        } else if (cloned->type == JSON_ARRAY) {
            json_array_append_number(&main_arena, cloned, 789.0);
            json_array_append_string(&main_arena, cloned, "fuzzed_string");
            json_array_append(&main_arena, cloned, dummy_bool);

            // Iterator
            JsonNode *entry;
            json_array_foreach(entry, cloned) {
                (void)entry;
            }
        }

        // 5. Test Serialization
        // Inject a comment to guarantee comment-handling logic is executed
        if (!cloned->pre_comment) {
            cloned->pre_comment = "// injected fuzzer comment";
        }
        
        // Test pretty printing
        char *serialized_pretty = json_serialize(&main_arena, cloned, true, false, 4, true);
        if (serialized_pretty) {
            size_t ser_len = strlen(serialized_pretty);
            JsonError rep_err;
            JsonValue *reparsed = json_parse(&main_arena, &scratch_arena, serialized_pretty, ser_len, JSON_PARSE_ALLOW_COMMENTS, &rep_err);
            (void)reparsed;
        }

        // Test minified printing with comments (this triggers `comment_requires_newline`)
        char *serialized_min = json_serialize(&main_arena, cloned, false, false, 0, true);
        if (serialized_min) {
            size_t ser_len = strlen(serialized_min);
            JsonError rep_err;
            JsonValue *reparsed = json_parse(&main_arena, &scratch_arena, serialized_min, ser_len, JSON_PARSE_ALLOW_COMMENTS, &rep_err);
            (void)reparsed;
        }
    }

    // 6. Cleanup
    // This tests the ArenaRegion 'next' pointer traversal and memory release
    arena_free(&main_arena);
    arena_free(&scratch_arena);
    arena_free(&test_arena);
    arena_free(&test_arena2);
    
    return 0;
}