CC = gcc
CFLAGS = -O3 -flto -Wall -Wextra -std=c99

# Default target: Build the object file and ALL examples
all: json.o config_manager api_client builder

# Compile the library object (Partial object, expects Arena implementation elsewhere)
json.o: json.c json.h arena.h
	$(CC) $(CFLAGS) -c json.c -o json.o

# --- Examples ---
config_manager: example_config_manager.c json.o
	$(CC) $(CFLAGS) example_config_manager.c json.o -o config_manager

api_client: example_api_client.c json.o
	$(CC) $(CFLAGS) example_api_client.c json.o -o api_client

builder: example_builder.c json.o
	$(CC) $(CFLAGS) example_builder.c json.o -o builder

# --- Testing ---
test: json_tester test_parsing
	./json_tester test_parsing/

json_tester: json_tester.c json.c json.h arena.h
	$(CC) $(CFLAGS) json_tester.c -o json_tester

# Download JSON Test Suite if directory is missing
test_parsing:
	@echo "Downloading JSONTestSuite..."
	git clone --depth 1 https://github.com/nst/JSONTestSuite.git temp_suite
	mv temp_suite/test_parsing .
	rm -rf temp_suite

# --- Benchmarking ---
# Automatically downloads dependencies so you don't commit them
benchmark: benchmark_bin
	./benchmark_bin

benchmark_bin: benchmark.c json.c cJSON.c cJSON.h citm_catalog.json
	$(CC) $(CFLAGS) benchmark.c json.c cJSON.c -o benchmark_bin

# Download cJSON for comparison
cJSON.c:
	wget -q https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
cJSON.h:
	wget -q https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h

# Download benchmark data
citm_catalog.json:
	wget -q https://raw.githubusercontent.com/miloyip/nativejson-benchmark/master/data/citm_catalog.json

# Cleanup
clean:
	rm -f *.o benchmark_bin json_tester config_manager api_client builder cJSON.c cJSON.h citm_catalog.json settings.json
