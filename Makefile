CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
LDFLAGS = -pthread

# 检查系统并添加libusb
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # 对于Mac系统
    LIBUSB_INC = $(shell pkg-config --cflags libusb-1.0)
    LIBUSB_LIB = $(shell pkg-config --libs libusb-1.0)
else
    # 对于Linux系统
    LIBUSB_INC = $(shell pkg-config --cflags libusb-1.0)
    LIBUSB_LIB = $(shell pkg-config --libs libusb-1.0)
endif

CXXFLAGS += $(LIBUSB_INC)
LDFLAGS += $(LIBUSB_LIB)

SRC_DIR = src
INCLUDE_DIR = include
BIN_DIR = bin
BUILD_DIR = build

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = $(BIN_DIR)/usbip

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
	rm -f $(SRC_DIR)/*.o 