#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ARENA_IMPLEMENTATION
#include "json.h"   // Your Parser
#include "cJSON.h"  // The Rival

// Helper to read file
char *read_file(const char *filename, size_t *len) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(*len + 1);
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

    printf("Benchmarking on %zu KB file...\n", len / 1024);
    int iterations = 1000;

    // --- ROUND 1: cJSON ---
    double start = get_time();
    for (int i = 0; i < iterations; i++) {
        cJSON *root = cJSON_ParseWithLength(data, len);
        cJSON_Delete(root);
    }
    double cjson_time = get_time() - start;
    printf("cJSON:   %.4f seconds (Score: %.0f MB/s)\n", cjson_time, (len * iterations / 1024.0 / 1024.0) / cjson_time);

    // --- ROUND 2: Your Parser ---
    Arena a = {0};
    arena_init(&a);
    
    start = get_time();
    for (int i = 0; i < iterations; i++) {
        arena_reset(&a); // Instant cleanup!
        // We use STRICT mode for fair comparison, assuming cJSON is strict
        JsonValue *root = json_parse(&a, data, len, NULL); 
        (void)root;
    }
    double my_time = get_time() - start;
    arena_free(&a);
    
    printf("My Lib:  %.4f seconds (Score: %.0f MB/s)\n", my_time, (len * iterations / 1024.0 / 1024.0) / my_time);

    // --- Verdict ---
    if (my_time < cjson_time) {
        printf("\n🏆 VICTORY! You are %.1fx faster than cJSON.\n", cjson_time / my_time);
    } else {
        printf("\nResult: cJSON is still %.1fx faster. Time to optimize!\n", my_time / cjson_time);
    }

    free(data);
    return 0;
}
