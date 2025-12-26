#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h> 

#define ARENA_IMPLEMENTATION
#include "arena.h"

#undef ARENA_IMPLEMENTATION
#include "json.c" 

/* --- Helper Functions --- */

int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);
    if (len_suffix >  len_str) return 0;
    return strncmp(str + len_str - len_suffix, suffix, len_suffix) == 0;
}

/* UPDATED: Returns length via pointer argument */
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

    printf("Running JSON Test Suite in '%s'...\n", dir_path);
    printf("--------------------------------------------------\n");
    printf("%-55s | %-8s | %-10s\n", "File", "Result", "Status");
    printf("--------------------------------------------------\n");

    Arena a = {0};
    arena_init(&a);

    while ((dir = readdir(d)) != NULL) {
      if (dir->d_name[0] == '.') continue;
      if (!ends_with(dir->d_name, ".json")) continue;

      total_files++;

      char prefix = dir->d_name[0]; 
      
      char full_path[1024];
      snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dir->d_name);

      arena_reset(&a); 
      
      size_t file_len = 0;
      char *json_data = read_file_to_arena(&a, full_path, &file_len);
      
      if (!json_data) {
          printf("%-55s | ERROR    | File Read Fail\n", dir->d_name);
          continue;
      }

      // UPDATED 1: Create the error struct
      JsonError err = {0};

      // UPDATED 2: Pass the address of 'err' to the parser
      JsonValue *root = json_parse(&a, json_data, file_len, &err);
      
      int success = (root != NULL);

      int test_passed = 0;
      const char *status_str = "";

      if (prefix == 'y') {
          test_passed = success; 
          status_str = test_passed ? "PASS" : "FAIL (Expected Success)";
      } else if (prefix == 'n') {
          test_passed = !success; 
          status_str = test_passed ? "PASS" : "FAIL (Expected Error)";
      } else {
          test_passed = 1; 
          status_str = success ? "INFO (Parsed)" : "INFO (Rejected)";
      }

      if (test_passed) passed_tests++;
      else failed_tests++;

      // UPDATED 3: Print error details if parsing failed
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
    arena_free(&a);

    printf("--------------------------------------------------\n");
    printf("Summary: %d Files Processed\n", total_files);
    printf("Passed:  %d\n", passed_tests);
    printf("Failed:  %d\n", failed_tests);
    printf("--------------------------------------------------\n");

    return (failed_tests == 0) ? 0 : 1;
}
