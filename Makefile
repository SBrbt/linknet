# Makefile for LinkNet Multi-threaded TUN Bridge

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS = -lssl -lcrypto
TARGET = linknet
SRCDIR = src
BUILD_DIR = build

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(BUILD_DIR)/%.o)

# Default target
all: $(TARGET)

# Create object directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile source files to object files
$(BUILD_DIR)/%.o: $(SRCDIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Link object files to create executable
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Check dependencies
deps:
	@echo "Checking system dependencies..."
	@which g++ > /dev/null || (echo "Error: g++ not found. Install with: sudo apt install g++" && exit 1)
	@pkg-config --exists openssl || (echo "Error: OpenSSL development libraries not found. Install with: sudo apt install libssl-dev" && exit 1)
	@[ -c /dev/net/tun ] || (echo "Error: /dev/net/tun not found. TUN/TAP support required." && exit 1)
	@echo "All dependencies satisfied."

# Generate random PSK
generate-psk:
	openssl rand -hex 32

# Test performance
test-performance:
	@echo "Running performance tests..."
	@chmod +x test_performance.sh
	@sudo ./test_performance.sh

# Interactive installation
install: $(TARGET)
	@echo "Starting interactive installation..."
	@sudo ./scripts/install.sh

# Uninstall service and binary
uninstall:
	@echo "Starting uninstall process..."
	@sudo ./scripts/uninstall.sh

# Clean build files
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean deps install uninstall generate-psk test-performance
