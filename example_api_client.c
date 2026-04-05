#include <stdio.h>
#include <string.h>

// We define this here because the Makefile compiles this standalone file directly into an executable.
#define ARENA_JSON_IMPLEMENTATION
#include "arena_json.h"

// Mock JSON response from a server
const char *API_RESPONSE = 
    "[\n"
    "  {\"id\": 101, \"username\": \"jdoe\", \"role\": \"admin\", \"active\": true},\n"
    "  {\"id\": 102, \"username\": \"guest\", \"role\": \"visitor\", \"active\": false},\n"
    "  {\"id\": 103, \"username\": \"msmith\", \"role\": \"editor\", \"active\": true}\n"
    "]";

int main() {
    // 1. Initialize our dual arenas
    Arena a = {0};
    Arena scratch = {0};
    arena_init(&a);
    arena_init(&scratch);

    printf("Received %zu bytes from API.\n", strlen(API_RESPONSE));

    // 2. Parse (Requires main and scratch arenas)
    JsonValue *root = json_parse(&a, &scratch, API_RESPONSE, strlen(API_RESPONSE), JSON_PARSE_STRICT, NULL);

    if (!root || root->type != JSON_ARRAY) {
        fprintf(stderr, "Error: Expected JSON Array\n");
        return 1;
    }

    // 3. Iterate List
    printf("\nID    | Username   | Role       | Status\n");
    printf("------+------------+------------+--------\n");

    // We now use flat arrays instead of linked lists! 
    // This is blazing fast because the CPU cache loves contiguous memory.
    for (size_t i = 0; i < root->as.list.count; i++) {
        
        // The value is embedded directly inside the node, so we take its address
        JsonValue *user = &root->as.list.items[i].value;
        
        // Use helper functions to extract data safely
        // If a field is missing, it defaults to 0 or ""
        double id = 0;
        const char *name = "N/A";
        const char *role = "N/A";
        bool active = false;

        if (user->type == JSON_OBJECT) {
            JsonValue *v_id = json_get(user, "id");
            if (v_id && v_id->type == JSON_NUMBER) id = v_id->as.number;

            JsonValue *v_name = json_get(user, "username");
            if (v_name && v_name->type == JSON_STRING) name = v_name->as.string;

            JsonValue *v_role = json_get(user, "role");
            if (v_role && v_role->type == JSON_STRING) role = v_role->as.string;

            JsonValue *v_active = json_get(user, "active");
            if (v_active && v_active->type == JSON_BOOL) active = v_active->as.boolean;
        }

        printf("%-5.0f | %-10s | %-10s | %s\n", 
               id, name, role, active ? "Active" : "Inactive");
    }

    arena_free(&a);
    arena_free(&scratch);
    return 0;
}