# Convenience Makefile for bit_fields
# Wraps CMake commands for easier building

BUILD_DIR := build
BUILD_DIR_ASAN := build-asan
BUILD_DIR_COV := build-coverage

CMAKE ?= cmake

# Auto-detect Ninja; override with CMAKE_GENERATOR= to force Unix Makefiles.
CMAKE_GENERATOR ?= $(shell command -v ninja >/dev/null 2>&1 && echo "-G Ninja")

# Prefer the gcov that matches the configured compiler, then fall back.
CXX ?= c++
GCOV_TOOL := $(shell $(CXX) -print-prog-name=gcov 2>/dev/null)
ifeq ($(strip $(GCOV_TOOL)),)
GCOV_TOOL := $(shell which gcov-14 2>/dev/null || which gcov 2>/dev/null || echo gcov)
endif

.PHONY: all build test clean rebuild
.PHONY: asan asan-test asan-clean
.PHONY: coverage coverage-test coverage-report coverage-clean
.PHONY: benchmark

# =============================================================================
# Standard build
# =============================================================================

# Default target: configure and build with tests enabled
all: build

build: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR)

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) -DBIT_FIELDS_BUILD_TESTS=ON -DBIT_FIELDS_BUILD_EXAMPLES=ON $(CMAKE_GENERATOR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	@rm -rf $(BUILD_DIR)

rebuild: clean all

# =============================================================================
# AddressSanitizer (ASAN) build
# =============================================================================

asan: $(BUILD_DIR_ASAN)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR_ASAN)

$(BUILD_DIR_ASAN)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR_ASAN) \
		-DBIT_FIELDS_BUILD_TESTS=ON \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
		$(CMAKE_GENERATOR)

asan-test: asan
	ctest --test-dir $(BUILD_DIR_ASAN) --output-on-failure

asan-clean:
	@rm -rf $(BUILD_DIR_ASAN)

# =============================================================================
# Code coverage build (gcov/lcov)
# =============================================================================

coverage: $(BUILD_DIR_COV)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR_COV)

$(BUILD_DIR_COV)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR_COV) \
		-DBIT_FIELDS_BUILD_TESTS=ON \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage -g -O0" \
		$(CMAKE_GENERATOR)

coverage-test: coverage
	ctest --test-dir $(BUILD_DIR_COV) --output-on-failure

coverage-report: coverage-test
	@echo "Generating coverage report..."
	@echo "Using gcov tool: $(GCOV_TOOL)"
	cd $(BUILD_DIR_COV) && lcov --capture --directory . --output-file coverage.info --gcov-tool $(GCOV_TOOL)
	cd $(BUILD_DIR_COV) && lcov --remove coverage.info '/usr/*' '*/third-party/*' --output-file coverage.info --ignore-errors unused
	cd $(BUILD_DIR_COV) && genhtml coverage.info --output-directory coverage-html --legend --show-details --highlight --ignore-errors source
	@if [ -f $(BUILD_DIR_COV)/coverage-html/index.html ]; then \
	  echo "Coverage report generated at $(BUILD_DIR_COV)/coverage-html/index.html"; \
	else \
	  echo "[ERROR] genhtml did not produce index.html"; exit 1; \
	fi

coverage-clean:
	@rm -rf $(BUILD_DIR_COV)

# =============================================================================
# Benchmark
# =============================================================================

benchmark: build
	@./$(BUILD_DIR)/tests/bench_bitstream

# =============================================================================
# Clean all builds
# =============================================================================

clean-all: clean asan-clean coverage-clean
