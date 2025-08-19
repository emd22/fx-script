CXX := cc
CXXFLAGS := -std=c++20 -Wall -g -lc++

BUILD_DIR := build

SRC := FxScript.cpp Main.cpp
OBJ := $(SRC:%.cpp=$(BUILD_DIR)/%.o)
TARGET := fxscript

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -r $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)
