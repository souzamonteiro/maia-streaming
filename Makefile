CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread -Iinclude
LDFLAGS ?= -lssl -lcrypto -pthread

BUILD_DIR ?= build
SRC_DIR ?= src
TESTS_DIR ?= tests

SRCS := $(wildcard $(SRC_DIR)/core/*.cpp) \
        $(wildcard $(SRC_DIR)/auth/*.cpp) \
        $(wildcard $(SRC_DIR)/storage/*.cpp) \
        $(wildcard $(SRC_DIR)/vod/*.cpp) \
        $(wildcard $(SRC_DIR)/media/*.cpp) \
        $(wildcard $(SRC_DIR)/hls/*.cpp) \
        $(wildcard $(SRC_DIR)/webrtc/*.cpp) \
        $(wildcard $(SRC_DIR)/metrics/*.cpp) \
        $(wildcard $(SRC_DIR)/http/*.cpp)

OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))

TARGET := $(BUILD_DIR)/maia-streaming

TEST_SRCS := $(wildcard $(TESTS_DIR)/*.cpp)
TEST_BINS := $(patsubst $(TESTS_DIR)/%.cpp, $(BUILD_DIR)/%, $(TEST_SRCS))

.PHONY: all build test clean run dirs

all: build

build: dirs $(TARGET) $(TEST_BINS)

dirs:
	@mkdir -p $(BUILD_DIR)/core $(BUILD_DIR)/auth $(BUILD_DIR)/storage \
	          $(BUILD_DIR)/vod $(BUILD_DIR)/media $(BUILD_DIR)/hls \
	          $(BUILD_DIR)/webrtc $(BUILD_DIR)/metrics $(BUILD_DIR)/http

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(BUILD_DIR)/main.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Tests compilation
$(BUILD_DIR)/range_test: $(TESTS_DIR)/range_test.cpp $(BUILD_DIR)/vod/range.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/token_test: $(TESTS_DIR)/token_test.cpp $(BUILD_DIR)/auth/token.o $(BUILD_DIR)/core/utils.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/storage_test: $(TESTS_DIR)/storage_test.cpp $(BUILD_DIR)/storage/storage.o $(BUILD_DIR)/core/utils.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/asset_test: $(TESTS_DIR)/asset_test.cpp $(BUILD_DIR)/media/asset.o $(BUILD_DIR)/storage/storage.o $(BUILD_DIR)/core/utils.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/sfu_test: $(TESTS_DIR)/sfu_test.cpp $(BUILD_DIR)/webrtc/sfu.o $(BUILD_DIR)/media/recording.o $(BUILD_DIR)/media/asset.o $(BUILD_DIR)/media/job.o $(BUILD_DIR)/storage/storage.o $(BUILD_DIR)/core/utils.o $(BUILD_DIR)/core/thread_pool.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/integration_test: $(TESTS_DIR)/integration_test.cpp $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

test: $(TEST_BINS)
	@echo "=========================================="
	@echo "   Running Maia Streaming Test Suite      "
	@echo "=========================================="
	@for t in $(TEST_BINS); do \
		echo "-> Running $$t ..."; \
		$$t || exit 1; \
	done
	@echo "All tests passed successfully!"

run: build
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
