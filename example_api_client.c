#include <stdio.h>
#include <string.h>

#define ARENA_IMPLEMENTATION
#include "json.h"

// Mock JSON response from a server
const char *API_RESPONSE = 
    "[\n"
    "  {\"id\": 101, \"username\": \"jdoe\", \"role\": \"admin\", \"active\": true},\n"
    "  {\"id\": 102, \"username\": \"guest\", \"role\": \"visitor\", \"active\": false},\n"
    "  {\"id\": 103, \"username\": \"msmith\", \"role\": \"editor\", \"active\": true}\n"
    "]";

int main() {
    Arena a = {0};
    arena_init(&a);

    printf("Received %zu bytes from API.\n", strlen(API_RESPONSE));

    // 1. Parse
    JsonValue *root = json_parse(&a, API_RESPONSE, strlen(API_RESPONSE), NULL);

    if (!root || root->type != JSON_ARRAY) {
        fprintf(stderr, "Error: Expected JSON Array\n");
        return 1;
    }

    // 2. Iterate List
    printf("\nID    | Username   | Role       | Status\n");
    printf("------+------------+------------+--------\n");

    // The 'list' struct contains the head pointer
    JsonNode *curr = root->as.list.head;
    
    while (curr) {
        JsonValue *user = curr->value;
        
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

        // Move to next item
        curr = curr->next;
    }

    arena_free(&a);
    return 0;
}
