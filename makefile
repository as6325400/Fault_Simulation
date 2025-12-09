CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic -O2
SRC_DIR := src
CPPFLAGS := -I$(SRC_DIR)
BUILD_DIR := build
BIN_DIR := bin
GEN_DIR := generator
TARGET := $(BIN_DIR)/main
GENERATOR_BIN := $(GEN_DIR)/pattern

# Allow invoking `make SOMEFLAG` to compile with -DSOMEFLAG automatically.
DEFAULT_GOALS := all clean
EXTRA_GOALS := $(filter-out $(DEFAULT_GOALS),$(MAKECMDGOALS))
CPPFLAGS += $(addprefix -D,$(EXTRA_GOALS))

ALL_SRCS := $(shell find $(SRC_DIR) -name '*.cpp')
FAULT_SIM_SRC := $(SRC_DIR)/main.cpp
GENERATOR_SRC := $(SRC_DIR)/generator_main.cpp
LIB_SRCS := $(filter-out $(FAULT_SIM_SRC) $(GENERATOR_SRC), $(ALL_SRCS))

LIB_OBJS := $(LIB_SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
FAULT_SIM_OBJ := $(FAULT_SIM_SRC:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
GENERATOR_OBJ := $(GENERATOR_SRC:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean $(EXTRA_GOALS)
.DEFAULT_GOAL := all

all: $(TARGET) $(GENERATOR_BIN)

# Any extra goal simply builds the binaries with the corresponding -D flag(s).
$(EXTRA_GOALS): all

$(TARGET): $(LIB_OBJS) $(FAULT_SIM_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(GENERATOR_BIN): $(LIB_OBJS) $(GENERATOR_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(GENERATOR_BIN)
