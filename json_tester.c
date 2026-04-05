#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h> 

// Use the new single-file header implementation
#define ARENA_JSON_IMPLEMENTATION
#include "arena_json.h"

#include "cJSON.h"
#ifdef __cplusplus
#include "simdjson.h"
#endif

/* --- Helper Functions --- */

int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);
    if (len_suffix >  len_str) return 0;
    return strncmp(str + len_str - len_suffix, suffix, len_suffix) == 0;
}

/* Returns length via pointer argument */
char *read_file_to_arena(Arena *a, const char *filename, size_t *out_len) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (length < 0) { fclose(f); return NULL; }

    char *buffer = arena_alloc_array(a, char, length + 1);
    size_t read_len = fread(buffer, 1, length, f);
    buffer[read_len] = '\0';

    if (out_len) *out_len = read_len; // Save the explicit length

    fclose(f);
    return buffer;
}

/* --- Main Logic --- */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <directory_path>\n", argv[0]);
        return 1;
    }

    const char *dir_path = argv[1];
    DIR *d = opendir(dir_path);
    if (!d) {
        printf("Could not open directory: %s\n", dir_path);
        return 1;
    }

    struct dirent *dir;
    int total_files = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    int cjson_passed = 0;
    int cjson_failed = 0;
    int simd_passed = 0;
    int simd_failed = 0;

    printf("Running JSON Test Suite in '%s'...\n", dir_path);
    printf("--------------------------------------------------\n");
    printf("%-55s | %-8s | %-10s\n", "File", "Result", "Status");
    printf("--------------------------------------------------\n");

    // Initialize our dual arenas for testing
    Arena a = {0};
    Arena scratch = {0};
    arena_init(&a);
    arena_init(&scratch);

    while ((dir = readdir(d)) != NULL) {
      if (dir->d_name[0] == '.') continue;
      if (!ends_with(dir->d_name, ".json")) continue;

      total_files++;

      char prefix = dir->d_name[0]; 
      
      char full_path[1024];
      snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dir->d_name);

      // Instant cleanup for the next test file
      arena_reset(&a); 
      arena_reset(&scratch);
      
      size_t file_len = 0;
      char *json_data = read_file_to_arena(&a, full_path, &file_len);
      
      if (!json_data) {
          printf("%-55s | ERROR    | File Read Fail\n", dir->d_name);
          continue;
      }

      JsonError err = {0};

      // Pass both arenas to the high-performance parser
      JsonValue *root = json_parse(&a, &scratch, json_data, file_len, JSON_PARSE_STRICT, &err);
      
      int success = (root != NULL);

      // --- cJSON Parsing ---
      cJSON *cj = cJSON_ParseWithLength(json_data, file_len);
      int cj_success = (cj != NULL);
      if (cj) cJSON_Delete(cj);

      // --- simdjson Parsing ---
      int simd_success = 0;
#ifdef __cplusplus
      char *padded_data = arena_alloc_array(&scratch, char, file_len + simdjson::SIMDJSON_PADDING);
      if (padded_data) {
          memcpy(padded_data, json_data, file_len);
          memset(padded_data + file_len, 0, simdjson::SIMDJSON_PADDING);
          simdjson::dom::parser simd_parser;
          simdjson::dom::element simd_doc;
          auto simd_err = simd_parser.parse(padded_data, file_len, file_len + simdjson::SIMDJSON_PADDING).get(simd_doc);
          simd_success = (simd_err == simdjson::SUCCESS);
      }
#endif

      int test_passed = 0;
      int cj_test_passed = 0;
      int simd_test_passed = 0;
      const char *status_str = "";

      if (prefix == 'y') {
          test_passed = success; 
          cj_test_passed = cj_success;
          simd_test_passed = simd_success;
          status_str = test_passed ? "PASS" : "FAIL (Expected Success)";
      } else if (prefix == 'n') {
          test_passed = !success; 
          cj_test_passed = !cj_success;
          simd_test_passed = !simd_success;
          status_str = test_passed ? "PASS" : "FAIL (Expected Error)";
      } else {
          test_passed = 1; 
          cj_test_passed = 1;
          simd_test_passed = 1;
          status_str = success ? "INFO (Parsed)" : "INFO (Rejected)";
      }

      if (test_passed) passed_tests++;
      else failed_tests++;

      if (cj_test_passed) cjson_passed++;
      else cjson_failed++;

      if (simd_test_passed) simd_passed++;
      else simd_failed++;

      if (!test_passed || prefix == 'i') {
          if (!success) {
              // Rejected: Print the specific error message from the parser
              printf("%-55s | REJECTED | %s -> %s (Line %d:%d)\n", 
                     dir->d_name, 
                     status_str,
                     err.msg, err.line, err.col);
          } else {
              // Parsed successfully
              printf("%-55s | PARSED   | %s\n", 
                     dir->d_name, 
                     status_str);
          }
      }
    }

    closedir(d);
    
    // Final cleanup
    arena_free(&a);
    arena_free(&scratch);

    printf("--------------------------------------------------\n");
    printf("Summary: %d Files Processed\n", total_files);
    printf("Arena JSON: Passed: %3d | Failed: %3d\n", passed_tests, failed_tests);
    printf("cJSON:      Passed: %3d | Failed: %3d\n", cjson_passed, cjson_failed);
    printf("simdjson:   Passed: %3d | Failed: %3d\n", simd_passed, simd_failed);
    printf("--------------------------------------------------\n");

    return (failed_tests == 0) ? 0 : 1;
}