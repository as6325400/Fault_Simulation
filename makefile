CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic -O2 -fopenmp
NVCC := nvcc
NVCCFLAGS := -std=c++20 -O2 -Xcompiler "-fopenmp -Wall -Wextra -pedantic"
SRC_DIR := src
CPPFLAGS := -I$(SRC_DIR)
BUILD_DIR := build
BIN_DIR := bin
GEN_DIR := generator
TARGET := $(BIN_DIR)/main
GPU_TARGET := $(BIN_DIR)/main_gpu
GENERATOR_BIN := $(GEN_DIR)/pattern

# Allow invoking `make SOMEFLAG` to compile with -DSOMEFLAG automatically.
DEFAULT_GOALS := all clean
EXTRA_GOALS := $(filter-out $(DEFAULT_GOALS),$(MAKECMDGOALS))
CPPFLAGS += $(addprefix -D,$(EXTRA_GOALS))

ALL_SRCS := $(shell find $(SRC_DIR) -name '*.cpp')
CUDA_SRCS := $(shell find $(SRC_DIR) -name '*.cu')
FAULT_SIM_SRC := $(SRC_DIR)/main.cpp
GPU_FAULT_SIM_SRC := $(SRC_DIR)/main.cu
GENERATOR_SRC := $(SRC_DIR)/generator_main.cpp
LIB_SRCS := $(filter-out $(FAULT_SIM_SRC) $(GPU_FAULT_SIM_SRC) $(GENERATOR_SRC), $(ALL_SRCS))

LIB_OBJS := $(LIB_SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
FAULT_SIM_OBJ := $(FAULT_SIM_SRC:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
GPU_FAULT_SIM_OBJ := $(GPU_FAULT_SIM_SRC:$(SRC_DIR)/%.cu=$(BUILD_DIR)/%.cu.o)
GENERATOR_OBJ := $(GENERATOR_SRC:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
CUDA_OBJS := $(filter-out $(GPU_FAULT_SIM_OBJ), $(CUDA_SRCS:$(SRC_DIR)/%.cu=$(BUILD_DIR)/%.cu.o))

.PHONY: all clean $(EXTRA_GOALS)
.DEFAULT_GOAL := all

all: $(TARGET) $(GPU_TARGET) $(GENERATOR_BIN)

# Any extra goal simply builds the binaries with the corresponding -D flag(s).
$(EXTRA_GOALS): all

$(TARGET): $(LIB_OBJS) $(FAULT_SIM_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(GPU_TARGET): $(LIB_OBJS) $(CUDA_OBJS) $(GPU_FAULT_SIM_OBJ)
	@mkdir -p $(dir $@)
	$(NVCC) $(CPPFLAGS) $(NVCCFLAGS) $^ -o $@

$(GENERATOR_BIN): $(LIB_OBJS) $(GENERATOR_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cu.o: $(SRC_DIR)/%.cu
	@mkdir -p $(dir $@)
	$(NVCC) $(CPPFLAGS) $(NVCCFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(GPU_TARGET) $(GENERATOR_BIN)
