#include <stdint.h>
#include <stddef.h>

#define ARENA_JSON_IMPLEMENTATION
#include "arena_json.h"

// libFuzzer entry point
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    Arena main_arena;
    Arena scratch_arena;
    arena_init(&main_arena);
    arena_init(&scratch_arena);

    JsonError err;
    // Fuzz both strict and comment-allowed modes
    json_parse(&main_arena, &scratch_arena, (const char *)data, size, JSON_PARSE_STRICT, &err);
    json_parse(&main_arena, &scratch_arena, (const char *)data, size, JSON_PARSE_ALLOW_COMMENTS, &err);
    
    arena_free(&main_arena);
    arena_free(&scratch_arena);
    
    return 0;
}