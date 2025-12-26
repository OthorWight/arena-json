# **arena-json**

A high-performance, strictly compliant, memory-safe JSON parser for C.

## **Why use this?**

Most C JSON parsers (like cJSON) use malloc for every single node. This causes memory fragmentation and makes cleanup slow (O(N) tree traversal).

**arena-json** uses an Arena allocator:

1. **Fast Allocation:** Objects are allocated sequentially in a pre-allocated block.  
2. **Instant Cleanup:** Freeing the entire JSON tree is O(1) (just resetting a pointer).  
3. **Strict:** Passes the extensive "JSON Parsing Test Suite".  
4. **Features:** Supports strict RFC 8259 JSON.

## **Performance**

Benchmarked against cJSON (industry standard) on citm\_catalog.json (1.7MB):

| Library | Speed | Notes |
| :---- | :---- | :---- |
| **arena-json** | **\~1000 MB/s** | SIMD-optimized strings, fast-path integers |
| cJSON | \~740 MB/s | Standard malloc-based parsing |

*(Tests run on a standard Linux laptop)*

## **Compliance & Safety**

arena-json is strictly RFC 8259 compliant. It rejects invalid JSON that other libraries (like cJSON) might accidentally accept.

**Validation Results:**

| Test Suite | Status | Details |
| :---- | :---- | :---- |
| **JSONTestSuite** | **Pass (318/318)** | Passed all strict parsing and transform tests. |
| **AFL++ Fuzzing** | **Pass** | Survived **25 Million+** executions with 0 crashes. |

*Note: cJSON fails 33/318 tests in the JSONTestSuite (permissive parsing of invalid numbers/strings).*

## **Usage**

### **1\. Parsing**

```C
\#define ARENA\_IMPLEMENTATION  
\#include "json.h"

int main() {  
    Arena a \= {0};  
    arena\_init(\&a);

    const char \*json \= "{\\"key\\": 123}";  
      
    // Parse (NULL for last arg ignores errors)  
    JsonValue \*root \= json\_parse(\&a, json, strlen(json), NULL);

    if (root) {  
        double val \= json\_get(root, "key")-\>as.number;  
        printf("Value: %g\\n", val);  
    }

    // Free everything instantly  
    arena\_free(\&a);  
    return 0;  
}
```

### **2\. Error Handling**

```C
JsonError err \= {0};  
JsonValue \*root \= json\_parse(\&a, bad\_json, len, \&err);

if (\!root) {  
    printf("Error: %s at line %d, col %d\\n", err.msg, err.line, err.col);  
}
```

## **Examples**

The repository includes several examples demonstrating real-world usage:

* **config\_manager**: A full settings manager that loads, modifies, and saves JSON configuration files.  
* **api\_client**: Shows how to iterate through lists of objects (like a REST API response).  
* **builder**: Demonstrates how to construct complex JSON objects from scratch using the Builder API.

To build and run them:

```Bash
make all  
./config\_manager  
./api\_client  
./builder
```

## **Build & Test**

```Bash
\# Run the test suite (expects test\_parsing/ folder)  
make test

\# Run the benchmark against cJSON (auto-downloads dependencies)  
make benchmark
```

## **Origin & Disclaimer**

I built this parser to experiment with Arena allocation and modern parser optimizations.

* **AI-Assisted:** This library was architected by a human and implemented with the assistance of an AI LLM.  
* **Verified:** The code has been validated against the industry-standard [JSON Parsing Test Suite](https://github.com/nst/JSONTestSuite), fuzz-tested with AFL++, and checks for memory leaks and overflows.  
* **Audit Recommended:** While robust and compliant, this is a new library. If you intend to use this in a security-critical environment (e.g., a public-facing web server), I recommend performing your own security audit.

## **License**

MIT License.
