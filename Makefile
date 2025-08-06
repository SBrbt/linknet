# Makefile for LinkNet TUN Bridge

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS = -lssl -lcrypto
TARGET = tun_bridge
SRCDIR = src
OBJDIR = obj

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

# Default target
all: $(TARGET)

# Create object directory
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Compile source files to object files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Link object files to create executable
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Clean build artifacts
clean:
	rm -rf $(OBJDIR) $(TARGET)

# Check dependencies
deps:
	@echo "Checking system dependencies..."
	@which g++ > /dev/null || (echo "Error: g++ not found. Install with: sudo apt install g++" && exit 1)
	@pkg-config --exists openssl || (echo "Error: OpenSSL development libraries not found. Install with: sudo apt install libssl-dev" && exit 1)
	@[ -c /dev/net/tun ] || (echo "Error: /dev/net/tun not found. TUN/TAP support required." && exit 1)
	@echo "All dependencies satisfied."

.PHONY: all clean deps
