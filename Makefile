CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude
BUILD := build
CORE := src/kv/page_allocator.cpp src/scheduler/continuous_scheduler.cpp

.PHONY: all test run clean

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

test: $(BUILD)/test_page_allocator $(BUILD)/test_scheduler $(BUILD)/helios_trace_replay
	$(BUILD)/test_page_allocator
	$(BUILD)/test_scheduler
	$(BUILD)/helios_trace_replay bench/traces/smoke.csv

run: $(BUILD)/helios_sim
	$(BUILD)/helios_sim

clean:
	rm -f $(BUILD)/helios_sim $(BUILD)/helios_trace_replay $(BUILD)/test_page_allocator $(BUILD)/test_scheduler
