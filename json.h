#ifndef JSON_H
#define JSON_H

#include "arena.h"
#include <stdbool.h>
#include <stddef.h> // for size_t

/* --- Error Reporting --- */
typedef struct {
    char msg[128]; // Error message
    int line;      // Line number (1-based)
    int col;       // Column number (1-based)
    size_t offset; // Raw byte offset from start
} JsonError;

/* --- Types --- */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;
typedef struct JsonNode JsonNode;

struct JsonValue {
    JsonType type;
    union {
        bool boolean;
        double number;
        char *string;
        struct { JsonNode *head; } list; 
    } as;
};

struct JsonNode {
    char *key;
    JsonValue *value;
    JsonNode *next;
};

/* --- API --- */

// UPDATED: Now accepts an optional JsonError pointer.
// If 'err' is provided and parsing fails, it will be filled with details.
JsonValue *json_parse(Arena *a, const char *input, size_t len, JsonError *err);

JsonValue *json_get(JsonValue *obj, const char *key);
JsonValue *json_at(JsonValue *arr, int index);
void json_print(JsonValue *v, int indent);
char *json_to_string(Arena *a, JsonValue *v, bool pretty);

/* --- Builder API --- */
JsonValue *json_create_null(Arena *a);
JsonValue *json_create_bool(Arena *a, bool b);
JsonValue *json_create_number(Arena *a, double num);
JsonValue *json_create_string(Arena *a, const char *str);
JsonValue *json_create_array(Arena *a);
JsonValue *json_create_object(Arena *a);

void json_add(Arena *a, JsonValue *obj, const char *key, JsonValue *val);
void json_add_string(Arena *a, JsonValue *obj, const char *key, const char *val);
void json_add_number(Arena *a, JsonValue *obj, const char *key, double val);
void json_add_bool(Arena *a, JsonValue *obj, const char *key, bool val);
void json_add_null(Arena *a, JsonValue *obj, const char *key);

void json_append(Arena *a, JsonValue *arr, JsonValue *val);
void json_append_string(Arena *a, JsonValue *arr, const char *val);
void json_append_number(Arena *a, JsonValue *arr, double val);
void json_append_bool(Arena *a, JsonValue *arr, bool val);
void json_append_null(Arena *a, JsonValue *arr);

#endif
