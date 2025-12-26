#include <stdio.h>

#define ARENA_IMPLEMENTATION
#include "json.h"

int main() {
    Arena a = {0};
    arena_init(&a);

    // 1. Create the Root Object
    JsonValue *root = json_create_object(&a);

    // 2. Add Simple Fields
    json_add_string(&a, root, "event", "player_login");
    json_add_number(&a, root, "timestamp", 1678886400);
    
    // 3. Create Nested Object ("device")
    JsonValue *device = json_create_object(&a);
    json_add_string(&a, device, "os", "Linux");
    json_add_string(&a, device, "gpu", "NVIDIA RTX 4090");
    json_add_number(&a, device, "cores", 16);
    
    // Attach nested object to root
    json_add(&a, root, "device_info", device);

    // 4. Create Nested Array ("inventory")
    JsonValue *inventory = json_create_array(&a);
    json_append_string(&a, inventory, "sword");
    json_append_string(&a, inventory, "shield");
    json_append_string(&a, inventory, "potion");
    
    // Attach array to root
    json_add(&a, root, "inventory", inventory);

    // 5. Serialize to String
    char *output = json_to_string(&a, root, true); // true = Pretty Print
    
    printf("--- Constructed JSON Request ---\n");
    printf("%s\n", output);

    arena_free(&a);
    return 0;
}
