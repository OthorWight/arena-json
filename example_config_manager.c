#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define the implementation macro for the single-file header
#define ARENA_JSON_IMPLEMENTATION
#include "arena_json.h"

#define CONFIG_FILE "settings.json"

/* --- File Helpers --- */

// Read an entire file into the main arena
char *read_file(Arena *a, const char *filename, size_t *out_len) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = arena_alloc_array(a, char, length + 1);
    if (buffer) {
        fread(buffer, 1, length, f);
        buffer[length] = '\0';
        *out_len = length;
    }
    fclose(f);
    return buffer;
}

// Write a null-terminated string to a file
void write_file(const char *filename, const char *data) {
    FILE *f = fopen(filename, "w");
    if (f) {
        fprintf(f, "%s", data);
        fclose(f);
    }
}

/* --- Logic Helpers --- */

// Helper to update a number if it exists, or add it if missing.
void set_or_update_number(Arena *a, JsonValue *obj, const char *key, double val) {
    JsonValue *existing = json_object_get(obj, key);
    if (existing && existing->type == JSON_NUMBER) {
        existing->as.number = val; // Direct in-place update
    } else {
        json_object_add_number(a, obj, key, val); // Create a new key-value pair
    }
}

/* --- Main Application --- */

int main() {
    Arena a = {0};
    Arena scratch = {0};
    arena_init(&a);
    arena_init(&scratch);

    size_t len = 0;
    char *json_source = read_file(&a, CONFIG_FILE, &len);
    JsonValue *root = NULL;

    if (json_source) {
        // --- CASE 1: File Exists ---
        printf("[*] Loading existing settings...\n");
        
        JsonError err;
        // Parse the existing configuration
        root = json_parse(&a, &scratch, json_source, len, JSON_PARSE_STRICT, &err);
        
        if (!root) {
            printf("[!] Error parsing config: %s (Line %d:%d)\n", err.msg, err.line, err.col);
            arena_free(&a);
            arena_free(&scratch);
            return 1;
        }
    } else {
        // --- CASE 2: Create Defaults ---
        printf("[*] No config found. Creating defaults...\n");
        root = json_create_object(&a);
        
        // Add basic settings
        json_object_add_string(&a, root, "app_name", "Arena App");
        json_object_add_string(&a, root, "theme", "Dark");
        json_object_add_bool(&a, root, "fullscreen", false);
        json_object_add_number(&a, root, "volume", 85.5);
        json_object_add_number(&a, root, "launch_count", 0);

        // Add a nested object
        JsonValue *network = json_create_object(&a);
        json_object_add_string(&a, network, "host", "localhost");
        json_object_add_number(&a, network, "port", 8080);
        json_object_add(&a, root, "network", network);
    }

    // --- Modify Data (Business Logic) ---
    
    // 1. Read values
    JsonValue *count_val = json_object_get(root, "launch_count");
    double count = (count_val && count_val->type == JSON_NUMBER) ? count_val->as.number : 0;
    
    JsonValue *name_val = json_object_get(root, "app_name");
    const char *name = (name_val && name_val->type == JSON_STRING) ? name_val->as.string : "Unknown";

    printf("    App Name: %s\n", name);
    printf("    Old Launch Count: %.0f\n", count);

    // 2. Update values
    count++;
    set_or_update_number(&a, root, "launch_count", count);
    
    printf("    New Launch Count: %.0f\n", count);

    // --- Save Back to Disk ---
    // Serialize the JSON DOM to a string allocated in the arena
    char *output = json_serialize(&a, root, true, false, 4, false);
    
    write_file(CONFIG_FILE, output);
    printf("[*] Settings saved to %s\n", CONFIG_FILE);

    // --- Cleanup ---
    // Freeing the arenas cleans up the file buffer, JSON DOM, scratch memory, and output string instantly.
    arena_free(&a); 
    arena_free(&scratch);
    return 0;
}