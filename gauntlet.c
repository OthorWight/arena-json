#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARENA_JSON_IMPLEMENTATION
#include "arena_json.h"
#include "cJSON.h"

#ifdef __cplusplus
#include "simdjson.h"
#include <string>
#endif

// --- Time Helper ---
double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// --- File Helper ---
char *read_file(const char *filename, size_t *len) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(*len + 1);
    if (!buf) return NULL;
    fread(buf, 1, *len, f);
    buf[*len] = 0;
    fclose(f);
    return buf;
}

#ifdef __cplusplus
char *pad_for_simdjson(const char *data, size_t len) {
    char *padded = (char*)malloc(len + simdjson::SIMDJSON_PADDING);
    memcpy(padded, data, len);
    memset(padded + len, 0, simdjson::SIMDJSON_PADDING);
    return padded;
}
#endif

// --- Memory Footprint Helpers ---
size_t get_arena_used_bytes(const Arena *a) {
    size_t used = 0;
    ArenaRegion *curr = a->begin;
    while (curr) {
        used += curr->count; 
        curr = curr->next;
    }
    return used;
}

size_t cjson_total_allocated = 0;
void *tracking_malloc(size_t size) {
    cjson_total_allocated += size;
    return malloc(size);
}
void tracking_free(void *ptr) {
    // Note: This tracker doesn't subtract on free for simplicity in 
    // peak-usage measurement during a single parse.
    free(ptr);
}

// --- DOM Traversal Helpers ---
void traverse_arena(JsonValue *v, double *sum, int *str_count) {
    if (!v) return;
    switch (v->type) {
        case JSON_NUMBER: *sum += v->as.number; break;
        case JSON_STRING: (*str_count)++; break;
        case JSON_ARRAY:
        case JSON_OBJECT:
            for (size_t i = 0; i < v->as.list.count; i++) {
                traverse_arena(&v->as.list.items[i].value, sum, str_count);
            }
            break;
        default: break;
    }
}

void traverse_cjson(cJSON *v, double *sum, int *str_count) {
    if (!v) return;
    if (cJSON_IsNumber(v)) {
        *sum += v->valuedouble;
    } else if (cJSON_IsString(v)) {
        (*str_count)++;
    }
    
    cJSON *child = v->child;
    while (child) {
        traverse_cjson(child, sum, str_count);
        child = child->next;
    }
}

#ifdef __cplusplus
void traverse_simdjson(simdjson::dom::element v, double *sum, int *str_count) {
    switch (v.type()) {
        case simdjson::dom::element_type::INT64:
        case simdjson::dom::element_type::UINT64:
        case simdjson::dom::element_type::DOUBLE:
            *sum += double(v); break;
        case simdjson::dom::element_type::STRING:
            (*str_count)++; break;
        case simdjson::dom::element_type::ARRAY:
            for (auto child : v.get_array()) {
                traverse_simdjson(child, sum, str_count);
            }
            break;
        case simdjson::dom::element_type::OBJECT:
            for (auto field : v.get_object()) {
                traverse_simdjson(field.value, sum, str_count);
            }
            break;
        default: break;
    }
}
#endif

// --- Printing Helper ---
void print_winner(const char *metric, const char *enemy_name, double arena_val, double enemy_val, int higher_is_better) {
    double ratio = higher_is_better ? (arena_val / enemy_val) : (enemy_val / arena_val);
    if (ratio > 1.0) {
        printf("    🏆 WINNER (%s): Arena JSON is %.1fx %s!\n\n", metric, ratio, higher_is_better ? "faster" : "more efficient");
    } else {
        printf("    🏆 WINNER (%s): %s is %.1fx %s!\n\n", metric, enemy_name, 1.0 / ratio, higher_is_better ? "faster" : "more efficient");
    }
}

int main() {
    Arena main_arena = {0};
    Arena scratch_arena = {0};
    arena_init(&main_arena);
    arena_init(&scratch_arena);

    cJSON_Hooks hooks = { tracking_malloc, tracking_free };
    cJSON_InitHooks(&hooks);

    printf("==================================================\n");
    printf("       ARENA JSON vs cJSON: THE GAUNTLET          \n");
    printf("==================================================\n\n");

    size_t catalog_len;
    char *catalog_data = read_file("citm_catalog.json", &catalog_len);
    if (!catalog_data) {
        printf("Error: Could not read citm_catalog.json. Ensure it's in the working directory.\n");
        return 1;
    }

#ifdef __cplusplus
    char *padded_catalog = pad_for_simdjson(catalog_data, catalog_len);
    simdjson::dom::parser simd_parser;
    simdjson::dom::element simd_root;
    auto err1 = simd_parser.parse(padded_catalog, catalog_len, catalog_len + simdjson::SIMDJSON_PADDING).get(simd_root);
    if (err1) { printf("simdjson error: %s\n", simdjson::error_message(err1)); }
#endif

    /* [1] MEMORY FOOTPRINT (CITM) */
    printf("[1] MEMORY FOOTPRINT TEST (citm_catalog.json)\n");
    arena_reset(&main_arena);
    arena_reset(&scratch_arena);
    JsonValue *arena_root = json_parse(&main_arena, &scratch_arena, catalog_data, catalog_len, JSON_PARSE_STRICT, NULL);
    size_t arena_ram = get_arena_used_bytes(&main_arena) + get_arena_used_bytes(&scratch_arena);
    
    cjson_total_allocated = 0;
    cJSON *cjson_root = cJSON_ParseWithLength(catalog_data, catalog_len);
    size_t cjson_ram = cjson_total_allocated;

    printf("    File Size:   %.2f MB\n", catalog_len / 1024.0 / 1024.0);
    printf("    Arena RAM:   %.2f MB (%.2fx file size)\n", arena_ram / 1024.0 / 1024.0, (double)arena_ram / catalog_len);
    printf("    cJSON RAM:   %.2f MB (%.2fx file size)\n", cjson_ram / 1024.0 / 1024.0, (double)cjson_ram / catalog_len);
#ifdef __cplusplus
    printf("    simdjson:    N/A (Preallocates tape based on capacity)\n");
#endif
    print_winner("Memory Efficiency", "cJSON", (double)arena_ram, (double)cjson_ram, 0);

    /* [2] DOM TRAVERSAL (CITM Cache Locality) */
    printf("[2] CITM DEEP TRAVERSAL (1,000 passes)\n");
    int citm_passes = 1000;
    double a_sum = 0, c_sum = 0;
    int a_strs = 0, c_strs = 0;
    
    double start = get_time();
    for (int i = 0; i < citm_passes; i++) traverse_arena(arena_root, &a_sum, &a_strs);
    double arena_citm_time = get_time() - start;
    
    start = get_time();
    for (int i = 0; i < citm_passes; i++) traverse_cjson(cjson_root, &c_sum, &c_strs);
    double cjson_citm_time = get_time() - start;

    printf("    Arena: %.4f s\n", arena_citm_time);
    printf("    cJSON: %.4f s\n", cjson_citm_time);

#ifdef __cplusplus
    double s_sum = 0;
    int s_strs = 0;
    start = get_time();
    for (int i = 0; i < citm_passes; i++) traverse_simdjson(simd_root, &s_sum, &s_strs);
    double simd_citm_time = get_time() - start;
    printf("    simdjs:%.4f s\n", simd_citm_time);
    print_winner("Traversal vs simdjson", "simdjson", 1.0/arena_citm_time, 1.0/simd_citm_time, 1);
#endif

    print_winner("Traversal Speed", "cJSON", 1.0/arena_citm_time, 1.0/cjson_citm_time, 1);

    /* [3] SERIALIZATION (CITM Write Speed) */
    printf("[3] CITM SERIALIZATION (100 iterations)\n");
    int write_iters = 100;
    start = get_time();
    for (int i = 0; i < write_iters; i++) {
        Arena temp_write = {0}; arena_init(&temp_write);
        char *out = json_to_string(&temp_write, arena_root, false, false, 0, false);
        (void)out; arena_free(&temp_write);
    }
    double arena_write_rate = (catalog_len * write_iters / 1024.0 / 1024.0) / (get_time() - start);
    
    start = get_time();
    for (int i = 0; i < write_iters; i++) {
        char *out = cJSON_PrintUnformatted(cjson_root);
        free(out);
    }
    double cjson_write_rate = (catalog_len * write_iters / 1024.0 / 1024.0) / (get_time() - start);

    printf("    Arena: %.2f MB/s\n", arena_write_rate);
    printf("    cJSON: %.2f MB/s\n", cjson_write_rate);

#ifdef __cplusplus
    start = get_time();
    for (int i = 0; i < write_iters; i++) {
        std::string out = simdjson::to_string(simd_root);
    }
    double simd_write_rate = (catalog_len * write_iters / 1024.0 / 1024.0) / (get_time() - start);
    printf("    simdjs:%.2f MB/s\n", simd_write_rate);
    print_winner("Serial vs simdjson", "simdjson", arena_write_rate, simd_write_rate, 1);
#endif

    print_winner("Serialization", "cJSON", arena_write_rate, cjson_write_rate, 1);

    /* [4] FLOATING POINT NIGHTMARE */
    printf("[4] FLOATING POINT NIGHTMARE (100k Floats)\n");
    int float_count = 100000;
    char *float_json = (char *)malloc(float_count * 22);
    strcpy(float_json, "[");
    for (int i = 0; i < float_count; i++) {
        char temp[32];
        snprintf(temp, sizeof(temp), "-1.%05de-%02d%s", rand()%99999, rand()%20, (i == float_count-1) ? "" : ",");
        strcat(float_json, temp);
    }
    strcat(float_json, "]");
    size_t f_len = strlen(float_json);

    start = get_time();
    for (int i = 0; i < 100; i++) {
        arena_reset(&main_arena); arena_reset(&scratch_arena);
        json_parse(&main_arena, &scratch_arena, float_json, f_len, JSON_PARSE_STRICT, NULL);
    }
    double arena_f_speed = (f_len * 100 / 1024.0 / 1024.0) / (get_time() - start);
    
    start = get_time();
    for (int i = 0; i < 100; i++) {
        cJSON *f_root = cJSON_ParseWithLength(float_json, f_len);
        cJSON_Delete(f_root);
    }
    double cjson_f_speed = (f_len * 100 / 1024.0 / 1024.0) / (get_time() - start);

    printf("    Arena: %.2f MB/s\n", arena_f_speed);
    printf("    cJSON: %.2f MB/s\n", cjson_f_speed);

#ifdef __cplusplus
    char *padded_float = pad_for_simdjson(float_json, f_len);
    simdjson::dom::parser simd_f_parser;
    start = get_time();
    for (int i = 0; i < 100; i++) {
        simdjson::dom::element f_doc;
        auto err = simd_f_parser.parse(padded_float, f_len, f_len + simdjson::SIMDJSON_PADDING).get(f_doc);
        if (err) { printf("simdjson error: %s\n", simdjson::error_message(err)); break; }
    }
    double simd_f_speed = (f_len * 100 / 1024.0 / 1024.0) / (get_time() - start);
    printf("    simdjs:%.2f MB/s\n", simd_f_speed);
    print_winner("Float vs simdjson", "simdjson", arena_f_speed, simd_f_speed, 1);
    free(padded_float);
#endif

    print_winner("Float Parsing", "cJSON", arena_f_speed, cjson_f_speed, 1);
    free(float_json);

    /* [5] MICRO-PAYLOAD LATENCY */
    printf("[5] MICRO-PAYLOAD LATENCY (500k iterations)\n");
    const char *tweet = "{\"id\":123,\"user\":\"ben\",\"text\":\"Test payload.\",\"active\":true}";
    size_t t_len = strlen(tweet);

    start = get_time();
    for (int i = 0; i < 500000; i++) {
        arena_reset(&main_arena); arena_reset(&scratch_arena);
        json_parse(&main_arena, &scratch_arena, tweet, t_len, JSON_PARSE_STRICT, NULL);
    }
    double a_ops = 500000 / (get_time() - start);

    start = get_time();
    for (int i = 0; i < 500000; i++) {
        cJSON *m = cJSON_ParseWithLength(tweet, t_len);
        cJSON_Delete(m);
    }
    double c_ops = 500000 / (get_time() - start);

    printf("    Arena: %.0f ops/s\n", a_ops);
    printf("    cJSON: %.0f ops/s\n", c_ops);

#ifdef __cplusplus
    char *padded_tweet = pad_for_simdjson(tweet, t_len);
    simdjson::dom::parser simd_m_parser;
    start = get_time();
    for (int i = 0; i < 500000; i++) {
        simdjson::dom::element m_doc;
        auto err = simd_m_parser.parse(padded_tweet, t_len, t_len + simdjson::SIMDJSON_PADDING).get(m_doc);
        if (err) { printf("simdjson error: %s\n", simdjson::error_message(err)); break; }
    }
    double s_ops = 500000 / (get_time() - start);
    printf("    simdjs:%.0f ops/s\n", s_ops);
    print_winner("Micro vs simdjson", "simdjson", a_ops, s_ops, 1);
    free(padded_tweet);
#endif

    print_winner("Micro-Payloads", "cJSON", a_ops, c_ops, 1);

    /* [6] COLD START OVERHEAD */
    printf("[6] COLD START OVERHEAD (Init/Free Cycle)\n");
    int cold_iters = 1000000;
    start = get_time();
    for (int i = 0; i < cold_iters; i++) {
        Arena a = {0}; arena_init(&a);
        void *p = arena_alloc(&a, 1); (void)p;
        arena_free(&a);
    }
    double a_cold = get_time() - start;

    start = get_time();
    for (int i = 0; i < cold_iters; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_Delete(item);
    }
    double c_cold = get_time() - start;

    printf("    Arena Init: %.2f ns/op\n", (a_cold/cold_iters)*1e9);
    printf("    cJSON Init: %.2f ns/op\n", (c_cold/cold_iters)*1e9);

#ifdef __cplusplus
    start = get_time();
    for (int i = 0; i < cold_iters; i++) {
        simdjson::dom::parser p;
    }
    double s_cold = get_time() - start;
    printf("    simd Init:  %.2f ns/op\n", (s_cold/cold_iters)*1e9);
    print_winner("Cold vs simdjson", "simdjson", 1.0/a_cold, 1.0/s_cold, 1);
#endif

    print_winner("Cold Start", "cJSON", 1.0/a_cold, 1.0/c_cold, 1);

    free(catalog_data);
    cJSON_Delete(cjson_root);
    arena_free(&main_arena);
    arena_free(&scratch_arena);

#ifdef __cplusplus
    free(padded_catalog);
#endif

    printf("Gauntlet Complete.\n");
    
    return 0;
}
