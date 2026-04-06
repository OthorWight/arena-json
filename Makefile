CC = gcc
CFLAGS = -O3 -flto -Wall -Wextra -std=c99 -msse4.2 -mbmi
CXX = g++
CXXFLAGS = -O3 -flto -Wall -Wextra -Wno-missing-field-initializers -std=c++17 -msse4.2 -mbmi

# --- Fuzzing & Coverage Config ---
FUZZ_CC = clang
FUZZ_CFLAGS = -g -O1 -fsanitize=fuzzer,address,undefined -I.
# Coverage-specific flags
COV_FLAGS = -fprofile-instr-generate -fcoverage-mapping

# Default target: Build ALL examples
all: config_manager api_client builder

# --- Help ---
help:
	@echo "Available targets:"
	@echo "  make all        - Build all examples (default)"
	@echo "  make test       - Run the validation suite and parser tests"
	@echo "  make benchmark  - Run the performance benchmarks"
	@echo "  make gauntlet   - Run the gauntlet stress tests"
	@echo "  make fuzz       - Start the fuzzer"
	@echo "  make coverage   - Generate HTML code coverage report from corpus"
	@echo "  make clean      - Remove all compiled binaries and temporary files"

# --- Examples ---
config_manager: example_config_manager.c arena_json.h
	$(CC) $(CFLAGS) example_config_manager.c -o config_manager -lm

api_client: example_api_client.c arena_json.h
	$(CC) $(CFLAGS) example_api_client.c -o api_client -lm

builder: example_builder.c arena_json.h
	$(CC) $(CFLAGS) example_builder.c -o builder -lm

# --- Testing ---
test: json_tester features_test
	./json_tester test_parsing/
	./features_test

features_test: features_test.c arena_json.h cJSON.c cJSON.h
	$(CC) $(CFLAGS) features_test.c cJSON.c -o features_test -lm

json_tester: json_tester.c arena_json.h cJSON.c cJSON.h simdjson.h simdjson.cpp
	$(CXX) $(CXXFLAGS) json_tester.c cJSON.c simdjson.cpp -o json_tester -lm

# --- Benchmarking ---
benchmark: benchmark_bin
	./benchmark_bin

benchmark_bin: benchmark.c cJSON.c cJSON.h citm_catalog.json arena_json.h simdjson.h simdjson.cpp
	$(CXX) $(CXXFLAGS) benchmark.c cJSON.c simdjson.cpp -o benchmark_bin -lm

# --- The Gauntlet ---
gauntlet: gauntlet_bin
	./gauntlet_bin

gauntlet_bin: gauntlet.c cJSON.c cJSON.h citm_catalog.json arena_json.h simdjson.h simdjson.cpp
	$(CXX) $(CXXFLAGS) gauntlet.c cJSON.c simdjson.cpp -o gauntlet_bin -lm

# --- Fuzzing ---
fuzzer: fuzzer.c arena_json.h
	$(FUZZ_CC) $(FUZZ_CFLAGS) fuzzer.c -o fuzzer

fuzz: fuzzer
	@echo "Starting fuzzer... (Press Ctrl+C to stop)"
	mkdir -p corpus
	./fuzzer -dict=json.dict corpus/ corpus_errors/

# --- Coverage Analysis ---
fuzzer_cov: fuzzer.c arena_json.h
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(COV_FLAGS) fuzzer.c -o fuzzer_cov

coverage: fuzzer_cov
	@echo "Running corpus through instrumented binary..."
	LLVM_PROFILE_FILE="fuzzer.profraw" ./fuzzer_cov -runs=0 corpus/
	llvm-profdata merge -sparse fuzzer.profraw -o fuzzer.profdata
	@echo "Generating report..."
	llvm-cov show ./fuzzer_cov -instr-profile=fuzzer.profdata -format=html -output-dir=coverage_report
	@echo "Report generated in coverage_report/index.html"

# Downloads
cJSON.c:
	wget -q https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
cJSON.h:
	wget -q https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h

citm_catalog.json:
	wget -q https://raw.githubusercontent.com/miloyip/nativejson-benchmark/master/data/citm_catalog.json

simdjson.h:
	wget -q https://raw.githubusercontent.com/simdjson/simdjson/master/singleheader/simdjson.h
simdjson.cpp:
	wget -q https://raw.githubusercontent.com/simdjson/simdjson/master/singleheader/simdjson.cpp

# Cleanup
clean:
	rm -rf benchmark_bin gauntlet_bin json_tester features_test fuzzer fuzzer_cov \
	      config_manager api_client builder \
	      cJSON.c cJSON.h citm_catalog.json settings.json \
	      simdjson.h simdjson.cpp \
	      perf.data perf.data.old *.o *.profraw *.profdata coverage_report
