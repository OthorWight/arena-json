# **Arena JSON**

A high-performance, **zero-fragmentation** JSON parser and generator for C99+, built on a linear Arena allocator.

Arena JSON was designed to solve the "pointer-chasing" performance bottlenecks and memory fragmentation issues found in traditional linked-list parsers. By using a contiguous memory layout, it achieves near-optimal CPU cache hits and significantly reduced memory overhead.

## **Performance: The Gauntlet**

Tested against **cJSON** (C) and **simdjson** (C++) using `citm_catalog.json` (1.65 MB) on a Laptop (SSE4.2/BMI enabled).

| Test | cJSON | Arena JSON | simdjson | Winner |
| :---- | :---- | :---- | :---- | :---- |
| **Parsing Speed** | 772 MB/s | 1315 MB/s | **4228 MB/s** | **simdjson (3.2x faster)** |
| **Memory Efficiency** | 2.70 MB | **2.70 MB** | N/A (Tape) | **Arena / cJSON (Tie)** |
| **Deep Traversal** | 0.0725 s | **0.0585 s** | 0.0990 s | **Arena (1.7x faster)** |
| **Float Parsing** | 125 MB/s | 605 MB/s | **1165 MB/s** | **simdjson (1.9x faster)** |
| **Serialization** | 1097 MB/s | 2515 MB/s | **2942 MB/s** | **simdjson (1.2x faster)** |
| **Micro-Payloads** | 5.2M ops/s | 9.2M ops/s | **17.6M ops/s**| **simdjson (1.9x faster)** |

*Note: `simdjson` is the state-of-the-art C++ parser and serves as the absolute "speed of light" benchmark. Arena JSON aims to be the fastest pure C DOM parser.*

## **Key Features**

* **Arena Allocation:** No individual free() calls. Drop the entire arena when the task is done.  
* **Dual-Arena Parsing:** Uses separate main and scratch arenas so temporary parsing memory is instantly reclaimed.
* **Cache-Friendly:** Nodes are stored in contiguous arrays, maximizing L1/L2 cache pre-fetching.  
* **SIMD Accelerated:** Fast-path parsing for strings and numbers using SSE4.2/BMI instructions.  
* **Lossless Comments:** Optionally parses, preserves, and serializes inline and block comments.
* **Case-Insensitive Lookups:** Built-in support for flexible configuration parsing.  
* **Zero-Allocation Builder:** Construct and serialize JSON payloads entirely within the arena.

## **Installation**

Simply include `arena_json.h` in your project. To create the implementation, define `ARENA_JSON_IMPLEMENTATION` in **one** C file.

```c
#define ARENA_JSON_IMPLEMENTATION  
#include "arena_json.h"
```

## **Quick Start**

### **Parsing and Access**

```c
#include <stdio.h>
#include <string.h>

Arena a = {0}, scratch = {0};  
arena_init(&a);
arena_init(&scratch);

const char *json = "{\"user\": \"Ben\", \"tags\": [\"C\", \"Linux\"]}";  
// Pass separate main and scratch arenas for optimal memory usage
JsonValue *root = json_parse(&a, &scratch, json, strlen(json), JSON_PARSE_STRICT, NULL);

// Type-safe QoL getters  
const char *name = json_object_get_string(root, "user", "Unknown");  
printf("Hello, %s!\n", name);
```

### **Iteration**

```c
JsonNode *entry;  
// Clean, macro-based iteration over objects
json_object_foreach(entry, root) {  
    printf("Key: %s\n", entry->key);  
}

// Arrays have their own clean iterator
JsonValue *tags = json_get(root, "tags");
json_array_foreach(entry, tags) {
    printf("Tag: %s\n", entry->value.as.string);
}
```

### **Mutation and Lookups** 

```c 
// Find keys safely, ignoring case 
JsonValue *val = json_get_case(root, "USER"); 

// Mutate existing DOM (modifies or inserts) 
json_replace_in_object(&a, root, "user", json_create_string(&a, "SuperBen")); 

// Remove a key completely 
json_remove_from_object(root, "tags"); 

// Clean up when fully done with the parsed DOM 
arena_free(&a); 
arena_free(&scratch); 
```

### **Building and Serialization**

```c
Arena b = {0};
arena_init(&b);

JsonValue *payload = json_create_object(&b);
json_add_string(&b, payload, "event", "startup");
json_add_number(&b, payload, "code", 200);

// Serialize to string: pretty print (true), no tabs (false), 4 spaces indent, keep comments (false)
char *out = json_to_string(&b, payload, true, false, 4, false);
printf("%s\n", out);

arena_free(&b);
```

### **Lossless Comments** 

Arena JSON can optionally parse, preserve, and serialize inline (//) and block (/* */) comments. 

```c 
Arena c = {0}, scratch2 = {0}; 
arena_init(&c); 
arena_init(&scratch2); 

const char *json_c = "{ /* server port */ \"port\": 8080 }"; 

// Use the JSON_PARSE_ALLOW_COMMENTS flag 
JsonValue *cfg = json_parse(&c, &scratch2, json_c, strlen(json_c), JSON_PARSE_ALLOW_COMMENTS, NULL); 

char *out_c = json_to_string(&c, cfg, true, false, 4, true); 
// true = keep comments 
printf("%s\n", out_c); 

arena_free(&c); 
arena_free(&scratch2);
```

## **Building**

The library is standard C99. For maximum performance (as seen in the benchmarks), compile with SSE4.2 support:

```bash
gcc -O3 -msse4.2 -mbmi your_code.c -o your_app -lm
```

## **License**

MIT License.