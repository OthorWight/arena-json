#include "json.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define MAX_JSON_DEPTH 1000

/* --- Parsing State --- */

typedef struct {
    const char *start;
    const char *curr;
    const char *end;
    int line;
    int col;
    JsonError *err;
} ParseState;

static void set_error(ParseState *s, const char *fmt, ...) {
    if (s->err) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(s->err->msg, sizeof(s->err->msg), fmt, args);
        va_end(args);
        s->err->line = s->line;
        s->err->col = s->col;
        s->err->offset = s->curr - s->start;
    }
}

/* --- Internal Helpers --- */

static void advance_fast(ParseState *s, int n) {
    s->curr += n;
    s->col += n;
}

static void advance(ParseState *s, int n) {
    for (int i = 0; i < n; i++) {
        if (s->curr >= s->end) break;
        if (*s->curr == '\n') {
            s->line++;
            s->col = 1;
        } else {
            s->col++;
        }
        s->curr++;
    }
}

static void skip_whitespace(ParseState *s) {
    // STRICT MODE: Only RFC 8259 whitespace allowed (Space, Tab, LineFeed, CR)
    while (s->curr < s->end) {
        char c = *s->curr;
        if (c == ' ' || c == '\t' || c == '\r') {
            s->curr++;
            s->col++;
        }
        else if (c == '\n') {
            s->curr++;
            s->line++;
            s->col = 1;
        }
        else {
            break; 
        }
    }
}

static JsonValue *make_value(Arena *a, JsonType type) {
    JsonValue *v = arena_alloc_struct(a, JsonValue);
    if (v) v->type = type;
    return v;
}

static bool parse_element(Arena *a, ParseState *s, JsonValue **out_val, int depth);

/* --- Robust String Parsing --- */

static bool parse_string(Arena *a, ParseState *s, char **out_str) {
    advance(s, 1); 
    
    const char *start_content = s->curr;
    const char *scan = s->curr;
    bool has_escapes = false;
    
    while (scan < s->end) {
        char c = *scan;
        if (c == '"') break;
        if (c == '\\') {
            has_escapes = true;
            scan++; 
            if (scan >= s->end) { set_error(s, "Unterminated escape"); return false; }
        }
        else if ((unsigned char)c < 0x20) {
            set_error(s, "Control character in string"); 
            return false;
        }
        scan++;
    }
    
    if (scan >= s->end) {
        set_error(s, "Unterminated string");
        return false;
    }

    size_t raw_len = scan - start_content;
    
    if (!has_escapes) {
        char *str = arena_alloc_array(a, char, raw_len + 1);
        if (!str) return false; 
        memcpy(str, start_content, raw_len);
        str[raw_len] = '\0';
        *out_str = str;
        advance_fast(s, (int)raw_len + 1); 
        return true;
    }

    char *str = arena_alloc_array(a, char, raw_len + 1);
    if (!str) return false; 
    char *out = str;
    const char *p = start_content;
    
    while (p < scan) {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"':  *out++ = '"';  break;
                case '\\': *out++ = '\\'; break;
                case '/':  *out++ = '/';  break;
                case 'b':  *out++ = '\b'; break;
                case 'f':  *out++ = '\f'; break;
                case 'n':  *out++ = '\n'; break;
                case 'r':  *out++ = '\r'; break;
                case 't':  *out++ = '\t'; break;
                case 'u': {
                    unsigned int codepoint = 0;
                    for (int i = 0; i < 4; i++) {
                        p++;
                        if (p >= scan) { set_error(s, "Invalid unicode escape"); return false; }
                        char c = *p;
                        int val = 0;
                        if      (c >= '0' && c <= '9') val = c - '0';
                        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
                        else { set_error(s, "Invalid unicode escape character"); return false; }
                        codepoint = (codepoint << 4) | val;
                    }
                    if (codepoint <= 0x7F) *out++ = (char)codepoint;
                    else if (codepoint <= 0x7FF) {
                        *out++ = (char)(0xC0 | (codepoint >> 6));
                        *out++ = (char)(0x80 | (codepoint & 0x3F));
                    } else if (codepoint <= 0xFFFF) {
                        *out++ = (char)(0xE0 | (codepoint >> 12));
                        *out++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        *out++ = (char)(0x80 | (codepoint & 0x3F));
                    }
                    break;
                }
                default: set_error(s, "Invalid escape sequence"); return false;
            }
        } else {
            *out++ = *p;
        }
        p++;
    }
    *out = '\0';
    *out_str = str;
    advance(s, (int)(scan - start_content) + 1); 
    return true;
}

static bool is_valid_json_number(const char *cursor, const char *end) {
    const char *p = cursor;
    if (p < end && *p == '-') p++;
    if (p >= end) return false;

    if (*p == '0') {
        p++;
        if (p < end && (*p == 'x' || *p == 'X')) return false; 
        if (p < end && isdigit((unsigned char)*p)) return false; 
    } else if (isdigit((unsigned char)*p)) {
        while (p < end && isdigit((unsigned char)*p)) p++;
    } else {
        return false;
    }

    if (p < end && *p == '.') {
        p++;
        if (p >= end || !isdigit((unsigned char)*p)) return false;
        while (p < end && isdigit((unsigned char)*p)) p++;
    }

    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        if (p >= end || !isdigit((unsigned char)*p)) return false;
        while (p < end && isdigit((unsigned char)*p)) p++;
    }
    return true;
}

static bool parse_number(Arena *a, ParseState *s, double *out_num) {
    (void)a;
    const char *p = s->curr;
    int sign = 1;
    
    if (p < s->end && *p == '-') {
        sign = -1;
        p++;
    }
    
    if (p < s->end && *p == '0') {
        goto USE_STRTOD;
    }
    
    const char *start_digits = p;
    double fast_val = 0;
    
    while (p < s->end && isdigit((unsigned char)*p)) {
        fast_val = fast_val * 10 + (*p - '0');
        p++;
    }
    
    if (p == s->end || (*p != '.' && *p != 'e' && *p != 'E')) {
        if (p == start_digits) goto USE_STRTOD; 
        *out_num = fast_val * sign;
        advance_fast(s, (int)(p - s->curr));
        return true;
    }

USE_STRTOD:
    if (!is_valid_json_number(s->curr, s->end)) {
        set_error(s, "Invalid number format");
        return false;
    }

    char *endptr;
    *out_num = strtod(s->curr, &endptr);
    int len = (int)(endptr - s->curr);
    advance(s, len);
    return true;
}

static bool parse_array(Arena *a, ParseState *s, JsonValue *arr, int depth) {
    if (depth > MAX_JSON_DEPTH) {
        set_error(s, "Maximum JSON depth exceeded");
        return false;
    }

    advance(s, 1); 
    skip_whitespace(s);
    
    if (s->curr < s->end && *s->curr == ']') {
        advance(s, 1);
        return true;
    }

    JsonNode **tail = &arr->as.list.head;
    while (s->curr < s->end) {
        JsonValue *elem;
        if (!parse_element(a, s, &elem, depth + 1)) return false;
        
        JsonNode *node = arena_alloc_struct(a, JsonNode);
        if (!node) return false; 
        
        node->value = elem;
        node->next = NULL;
        *tail = node;
        tail = &node->next;

        skip_whitespace(s);
        if (s->curr >= s->end) { set_error(s, "Unexpected end of input in array"); return false; }
        
        if (*s->curr == ']') {
            advance(s, 1);
            return true;
        }
        if (*s->curr == ',') {
            advance(s, 1);
            skip_whitespace(s);
            if (*s->curr == ']') {
                set_error(s, "Trailing comma in array");
                return false;
            }
        } else {
            set_error(s, "Expected ',' or ']'");
            return false;
        }
    }
    set_error(s, "Unclosed array");
    return false;
}

static bool parse_object(Arena *a, ParseState *s, JsonValue *obj, int depth) {
    if (depth > MAX_JSON_DEPTH) {
        set_error(s, "Maximum JSON depth exceeded");
        return false;
    }

    advance(s, 1); 
    skip_whitespace(s);

    if (s->curr < s->end && *s->curr == '}') {
        advance(s, 1);
        return true;
    }

    JsonNode **tail = &obj->as.list.head;
    while (s->curr < s->end) {
        if (*s->curr != '"') {
            set_error(s, "Expected string key");
            return false;
        }

        char *key;
        if (!parse_string(a, s, &key)) return false;

        skip_whitespace(s);
        if (s->curr >= s->end || *s->curr != ':') {
            set_error(s, "Expected ':' after key");
            return false;
        }
        advance(s, 1); 

        JsonValue *val;
        if (!parse_element(a, s, &val, depth + 1)) return false;

        JsonNode *node = arena_alloc_struct(a, JsonNode);
        if (!node) return false; 

        node->key = key;
        node->value = val;
        node->next = NULL;
        *tail = node;
        tail = &node->next;

        skip_whitespace(s);
        if (s->curr >= s->end) { set_error(s, "Unexpected end of input in object"); return false; }

        if (*s->curr == '}') {
            advance(s, 1);
            return true;
        }
        if (*s->curr == ',') {
            advance(s, 1);
            skip_whitespace(s);
            if (*s->curr == '}') {
                set_error(s, "Trailing comma in object");
                return false;
            }
        } else {
            set_error(s, "Expected ',' or '}'");
            return false;
        }
    }
    set_error(s, "Unclosed object");
    return false;
}

static bool parse_element(Arena *a, ParseState *s, JsonValue **out_val, int depth) {
    skip_whitespace(s);
    if (s->curr >= s->end) {
        set_error(s, "Unexpected end of input");
        return false;
    }

    char c = *s->curr;
    if (c == '"') {
        *out_val = make_value(a, JSON_STRING);
        if (!*out_val) return false; 
        return parse_string(a, s, &(*out_val)->as.string);
    }
    else if (c == '[') {
        *out_val = make_value(a, JSON_ARRAY);
        if (!*out_val) return false; 
        return parse_array(a, s, *out_val, depth);
    }
    else if (c == '{') {
        *out_val = make_value(a, JSON_OBJECT);
        if (!*out_val) return false; 
        return parse_object(a, s, *out_val, depth);
    }
    else if (isdigit((unsigned char)c) || c == '-') {
        *out_val = make_value(a, JSON_NUMBER);
        if (!*out_val) return false; 
        return parse_number(a, s, &(*out_val)->as.number);
    }
    else if (strncmp(s->curr, "true", 4) == 0) {
        *out_val = make_value(a, JSON_BOOL);
        if (!*out_val) return false; 
        (*out_val)->as.boolean = true;
        advance(s, 4);
        return true;
    }
    else if (strncmp(s->curr, "false", 5) == 0) {
        *out_val = make_value(a, JSON_BOOL);
        if (!*out_val) return false; 
        (*out_val)->as.boolean = false;
        advance(s, 5);
        return true;
    }
    else if (strncmp(s->curr, "null", 4) == 0) {
        *out_val = make_value(a, JSON_NULL);
        if (!*out_val) return false; 
        advance(s, 4);
        return true;
    }
    
    set_error(s, "Unexpected character '%c'", c);
    return false;
}

JsonValue *json_parse(Arena *a, const char *input, size_t len, JsonError *err) {
    if (!a) return NULL;        
    if (!input) return NULL;    
    if (len == 0) return NULL;  

    if (err) {
        memset(err, 0, sizeof(JsonError));
    }

    ParseState s = {0};
    s.start = input;
    s.curr = input;
    s.end = input + len;
    s.line = 1;
    s.col = 1;
    s.err = err;

    JsonValue *root;
    if (!parse_element(a, &s, &root, 0)) {
        return NULL;
    }
    
    skip_whitespace(&s);
    if (s.curr != s.end) {
        set_error(&s, "Unexpected garbage after JSON data");
        return NULL;
    }

    return root;
}

/* --- Helpers --- */

JsonValue *json_get(JsonValue *obj, const char *key) {
    if (!obj || !key) return NULL;
    if (obj->type != JSON_OBJECT) return NULL;
    
    JsonNode *curr = obj->as.list.head;
    while (curr) {
        if (strcmp(curr->key, key) == 0) return curr->value;
        curr = curr->next;
    }
    return NULL;
}

JsonValue *json_at(JsonValue *arr, int index) {
    if (!arr) return NULL;
    if (arr->type != JSON_ARRAY) return NULL;
    if (index < 0) return NULL;

    JsonNode *curr = arr->as.list.head;
    int i = 0;
    while (curr) {
        if (i == index) return curr->value;
        curr = curr->next;
        i++;
    }
    return NULL;
}

void json_print(JsonValue *v, int indent) {
    if (!v) return;
    for (int i=0; i<indent; i++) printf("  ");
    switch (v->type) {
        case JSON_NULL:   printf("null\n"); break;
        case JSON_BOOL:   printf("%s\n", v->as.boolean ? "true" : "false"); break;
        case JSON_NUMBER: printf("%g\n", v->as.number); break;
        case JSON_STRING: printf("\"%s\"\n", v->as.string); break;
        case JSON_ARRAY: {
            printf("[\n");
            JsonNode *curr = v->as.list.head;
            while (curr) {
                json_print(curr->value, indent + 1);
                curr = curr->next;
            }
            for (int i=0; i<indent; i++) printf("  ");
            printf("]\n");
            break;
        }
        case JSON_OBJECT: {
            printf("{\n");
            JsonNode *curr = v->as.list.head;
            while (curr) {
                for (int i=0; i<indent+1; i++) printf("  ");
                printf("\"%s\":\n", curr->key);
                json_print(curr->value, indent + 2);
                curr = curr->next;
            }
            for (int i=0; i<indent; i++) printf("  ");
            printf("}\n");
            break;
        }
    }
}

/* --- Serializer / Writer --- */

#include <math.h> 

static void w_str(char *buf, size_t *pos, const char *s) {
    size_t len = strlen(s);
    if (buf) memcpy(buf + *pos, s, len);
    *pos += len;
}

static void w_char(char *buf, size_t *pos, char c) {
    if (buf) buf[*pos] = c;
    (*pos)++;
}

static void w_escaped_string(char *buf, size_t *pos, const char *s) {
    w_char(buf, pos, '"');
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"')  w_str(buf, pos, "\\\"");
        else if (c == '\\') w_str(buf, pos, "\\\\");
        else if (c == '\b') w_str(buf, pos, "\\b");
        else if (c == '\f') w_str(buf, pos, "\\f");
        else if (c == '\n') w_str(buf, pos, "\\n");
        else if (c == '\r') w_str(buf, pos, "\\r");
        else if (c == '\t') w_str(buf, pos, "\\t");
        else if (c < 0x20) {
            char hex[7];
            sprintf(hex, "\\u00%02X", c);
            w_str(buf, pos, hex);
        } else {
            w_char(buf, pos, (char)c);
        }
        s++;
    }
    w_char(buf, pos, '"');
}

static void json_write_internal(JsonValue *v, char *buf, size_t *pos, int indent, bool pretty) {
    if (!v) return;

    switch (v->type) {
        case JSON_NULL: 
            w_str(buf, pos, "null"); 
            break;
        case JSON_BOOL: 
            w_str(buf, pos, v->as.boolean ? "true" : "false"); 
            break;
        case JSON_NUMBER: {
            char num_buf[64];
            if (!isfinite(v->as.number)) {
                w_str(buf, pos, "null");
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.17g", v->as.number);
                w_str(buf, pos, num_buf);
            }
            break;
        }
        case JSON_STRING: 
            w_escaped_string(buf, pos, v->as.string); 
            break;
        case JSON_ARRAY: {
            w_char(buf, pos, '[');
            if (v->as.list.head) {
                if (pretty) w_char(buf, pos, '\n');
                JsonNode *curr = v->as.list.head;
                while (curr) {
                    if (pretty) for (int i = 0; i < indent + 2; i++) w_char(buf, pos, ' ');
                    json_write_internal(curr->value, buf, pos, indent + (pretty ? 2 : 0), pretty);
                    if (curr->next) {
                        w_char(buf, pos, ',');
                        if (pretty) w_char(buf, pos, '\n');
                    }
                    curr = curr->next;
                }
                if (pretty) {
                    w_char(buf, pos, '\n');
                    for (int i = 0; i < indent; i++) w_char(buf, pos, ' ');
                }
            }
            w_char(buf, pos, ']');
            break;
        }
        case JSON_OBJECT: {
            w_char(buf, pos, '{');
            if (v->as.list.head) {
                if (pretty) w_char(buf, pos, '\n');
                JsonNode *curr = v->as.list.head;
                while (curr) {
                    if (pretty) for (int i = 0; i < indent + 2; i++) w_char(buf, pos, ' ');
                    w_escaped_string(buf, pos, curr->key);
                    w_str(buf, pos, pretty ? ": " : ":");
                    json_write_internal(curr->value, buf, pos, indent + (pretty ? 2 : 0), pretty);
                    if (curr->next) {
                        w_char(buf, pos, ',');
                        if (pretty) w_char(buf, pos, '\n');
                    }
                    curr = curr->next;
                }
                if (pretty) {
                    w_char(buf, pos, '\n');
                    for (int i = 0; i < indent; i++) w_char(buf, pos, ' ');
                }
            }
            w_char(buf, pos, '}');
            break;
        }
    }
}

char *json_to_string(Arena *a, JsonValue *v, bool pretty) {
    if (!a || !v) return NULL;

    size_t len = 0;
    json_write_internal(v, NULL, &len, 0, pretty);
    
    char *result = arena_alloc_array(a, char, len + 1);
    if (!result) return NULL;

    size_t pos = 0;
    json_write_internal(v, result, &pos, 0, pretty);
    result[pos] = '\0';
    return result;
}

/* --- Builder Implementation --- */

JsonValue *json_create_null(Arena *a) { 
    if (!a) return NULL; 
    return make_value(a, JSON_NULL); 
}
JsonValue *json_create_bool(Arena *a, bool b) { 
    if (!a) return NULL;
    JsonValue *v = make_value(a, JSON_BOOL); 
    if (v) v->as.boolean = b; 
    return v;
}
JsonValue *json_create_number(Arena *a, double num) {
    if (!a) return NULL;
    JsonValue *v = make_value(a, JSON_NUMBER); 
    if (v) v->as.number = num; 
    return v;
}
JsonValue *json_create_string(Arena *a, const char *str) {
    if (!a || !str) return NULL;
    JsonValue *v = make_value(a, JSON_STRING);
    if (!v) return NULL;
    size_t len = strlen(str);
    v->as.string = arena_alloc_array(a, char, len + 1);
    if (!v->as.string) return NULL;
    memcpy(v->as.string, str, len + 1);
    return v;
}
JsonValue *json_create_array(Arena *a) { 
    if (!a) return NULL;
    return make_value(a, JSON_ARRAY); 
}
JsonValue *json_create_object(Arena *a) { 
    if (!a) return NULL;
    return make_value(a, JSON_OBJECT); 
}

static void json_list_append(Arena *a, JsonValue *parent, const char *key, JsonValue *val) {
    if (!a || !parent || !val) return; 

    JsonNode *node = arena_alloc_struct(a, JsonNode);
    if (!node) return; 

    if (key) {
        size_t len = strlen(key);
        node->key = arena_alloc_array(a, char, len + 1);
        if (node->key) memcpy(node->key, key, len + 1);
    } else {
        node->key = NULL;
    }
    node->value = val;
    node->next = NULL;

    if (parent->as.list.head == NULL) {
        parent->as.list.head = node;
    } else {
        JsonNode *curr = parent->as.list.head;
        while (curr->next) curr = curr->next;
        curr->next = node;
    }
}

void json_add(Arena *a, JsonValue *obj, const char *key, JsonValue *val) {
    if (!obj || !key || !val) return;
    if (obj->type == JSON_OBJECT) json_list_append(a, obj, key, val);
}
void json_add_string(Arena *a, JsonValue *obj, const char *key, const char *val) {
    if (!val) return;
    json_add(a, obj, key, json_create_string(a, val));
}
void json_add_number(Arena *a, JsonValue *obj, const char *key, double val) {
    json_add(a, obj, key, json_create_number(a, val));
}
void json_add_bool(Arena *a, JsonValue *obj, const char *key, bool val) {
    json_add(a, obj, key, json_create_bool(a, val));
}
void json_add_null(Arena *a, JsonValue *obj, const char *key) {
    json_add(a, obj, key, json_create_null(a));
}

void json_append(Arena *a, JsonValue *arr, JsonValue *val) {
    if (!arr || !val) return;
    if (arr->type == JSON_ARRAY) json_list_append(a, arr, NULL, val);
}
void json_append_string(Arena *a, JsonValue *arr, const char *val) {
    if (!val) return;
    json_append(a, arr, json_create_string(a, val));
}
void json_append_number(Arena *a, JsonValue *arr, double val) {
    json_append(a, arr, json_create_number(a, val));
}
void json_append_bool(Arena *a, JsonValue *arr, bool val) {
    json_append(a, arr, json_create_bool(a, val));
}
void json_append_null(Arena *a, JsonValue *arr) {
    json_append(a, arr, json_create_null(a));
}
