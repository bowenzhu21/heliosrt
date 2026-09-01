CXX ?= g++
PYTHON ?= python3
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude
SANITIZER_FLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -Iinclude \
	-fsanitize=address,undefined -fno-omit-frame-pointer
BUILD := build
CORE := src/kv/page_allocator.cpp src/scheduler/continuous_scheduler.cpp src/runtime/mock_executor.cpp

.PHONY: all test test-sanitize validate fingerprint run clean

all: $(BUILD)/helios_sim $(BUILD)/helios_trace_replay

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/helios_sim: $(CORE) apps/helios_sim.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/helios_trace_replay: $(CORE) apps/trace_replay.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_page_allocator: $(CORE) tests/test_page_allocator.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_scheduler: $(CORE) tests/test_scheduler.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/test_runtime: $(CORE) tests/test_runtime.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/test_page_allocator $(BUILD)/test_scheduler $(BUILD)/test_runtime $(BUILD)/helios_trace_replay
	$(BUILD)/test_page_allocator
	$(BUILD)/test_scheduler
	$(BUILD)/test_runtime
	$(BUILD)/helios_trace_replay bench/traces/smoke.csv
	$(PYTHON) scripts/validate_benchmark.py bench/examples/cpu-smoke.json
	$(PYTHON) -m unittest tests/test_artifact_manifest.py

test-sanitize: | $(BUILD)
	mkdir -p $(BUILD)/sanitize
	$(CXX) $(SANITIZER_FLAGS) $(CORE) tests/test_page_allocator.cpp -o $(BUILD)/sanitize/test_page_allocator
	$(CXX) $(SANITIZER_FLAGS) $(CORE) tests/test_scheduler.cpp -o $(BUILD)/sanitize/test_scheduler
	$(CXX) $(SANITIZER_FLAGS) $(CORE) tests/test_runtime.cpp -o $(BUILD)/sanitize/test_runtime
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/sanitize/test_page_allocator
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/sanitize/test_scheduler
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/sanitize/test_runtime

validate:
	$(PYTHON) scripts/validate_benchmark.py bench/examples/cpu-smoke.json

fingerprint: | $(BUILD)
	$(PYTHON) scripts/environment_fingerprint.py --output $(BUILD)/environment.json

run: $(BUILD)/helios_sim
	$(BUILD)/helios_sim

clean:
	rm -f $(BUILD)/helios_sim $(BUILD)/helios_trace_replay $(BUILD)/test_page_allocator $(BUILD)/test_scheduler $(BUILD)/test_runtime
