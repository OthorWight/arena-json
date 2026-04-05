#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Define the implementation for the new single-file header
#define ARENA_JSON_IMPLEMENTATION
#include "arena_json.h" // Your new unified Parser & Arena

#include "cJSON.h"      // The Rival

#ifdef __cplusplus
#include "simdjson.h"
#endif

// Helper to read file
char *read_file(const char *filename, size_t *len) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(*len + 1);
    fread(buf, 1, *len, f);
    buf[*len] = 0;
    fclose(f);
    return buf;
}

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main() {
    size_t len;
    char *data = read_file("citm_catalog.json", &len);
    if (!data) { printf("Error: Could not read citm_catalog.json\n"); return 1; }

    double target_time = 2.0; // Run each parser for 2 seconds
    printf("Benchmarking on %zu KB file for %.1f seconds each...\n", len / 1024, target_time);

    // --- ROUND 1: cJSON ---
    int cjson_iters = 0;
    double start = get_time();
    double current_time;
    while ((current_time = get_time()) - start < target_time) {
        cJSON *root = cJSON_ParseWithLength(data, len);
        cJSON_Delete(root);
        cjson_iters++;
    }
    double cjson_time = current_time - start;
    double cjson_score = 0;
    if (cjson_time > 0) {
        cjson_score = (len * cjson_iters / 1024.0 / 1024.0) / cjson_time;
        printf("cJSON:   %d iters in %.4f s (Score: %.0f MB/s)\n", cjson_iters, cjson_time, cjson_score);
    }

    // --- ROUND 2: Your Parser ---
    Arena a = {0};
    Arena scratch = {0};
    arena_init(&a);
    arena_init(&scratch);
    
    int my_iters = 0;
    start = get_time();
    while ((current_time = get_time()) - start < target_time) {
        arena_reset(&a); 
        arena_reset(&scratch); // Instant cleanup for both!
        
        JsonValue *root = json_parse(&a, &scratch, data, len, JSON_PARSE_STRICT, NULL); 
        (void)root;
        my_iters++;
    }
    double my_time = current_time - start;
    arena_free(&a);
    arena_free(&scratch);
    
    double my_score = (len * my_iters / 1024.0 / 1024.0) / my_time;
    printf("My Lib:  %d iters in %.4f s (Score: %.0f MB/s)\n", my_iters, my_time, my_score);

#ifdef __cplusplus
    // --- ROUND 3: simdjson (DOM) ---
    simdjson::dom::parser simd_parser;
    size_t padded_len = len + simdjson::SIMDJSON_PADDING;
    char *padded_data = (char *)malloc(padded_len);
    memcpy(padded_data, data, len);
    memset(padded_data + len, 0, simdjson::SIMDJSON_PADDING);

    int simd_iters = 0;
    start = get_time();
    while ((current_time = get_time()) - start < target_time) {
        simdjson::dom::element doc;
        auto error = simd_parser.parse(padded_data, len, padded_len).get(doc);
        if (error) { printf("simdjson error: %s\n", simdjson::error_message(error)); break; }
        simd_iters++;
    }
    double simd_time = current_time - start;
    double simd_score = 0;
    if (simd_time > 0) {
        simd_score = (len * simd_iters / 1024.0 / 1024.0) / simd_time;
        printf("simdjson:%d iters in %.4f s (Score: %.0f MB/s)\n", simd_iters, simd_time, simd_score);
    }
    free(padded_data);
#endif

    // --- Verdict ---
    if (cjson_score > 0) {
        if (my_score > cjson_score) {
            printf("\n🏆 VICTORY! You are %.1fx faster than cJSON.\n", my_score / cjson_score);
        } else {
            printf("\nResult: cJSON is still %.1fx faster. Time to optimize!\n", cjson_score / my_score);
        }
    }
    
#ifdef __cplusplus
    if (simd_score > 0) {
        if (my_score > simd_score) {
            printf("🏆 VICTORY! You are %.1fx faster than simdjson (DOM).\n", my_score / simd_score);
        } else {
            printf("Result: simdjson (DOM) is %.1fx faster. The final boss remains undefeated!\n", simd_score / my_score);
        }
    }
#endif

    free(data);
    return 0;
}
