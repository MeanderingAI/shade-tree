# Distributed LXC Management System Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread
INCLUDES = -Iinclude

# Directories
SRCDIR = src
INCDIR = include
OBJDIR = obj
BINDIR = bin
CONFIGDIR = config
EXAMPLEDIR = examples

# Source files
COMMON_SOURCES = $(SRCDIR)/yaml_parser.c $(SRCDIR)/lxc_manager.c $(SRCDIR)/network.c
COORDINATOR_SOURCES = $(SRCDIR)/coordinator.c $(COMMON_SOURCES)
WORKER_SOURCES = $(SRCDIR)/worker.c $(COMMON_SOURCES)

# Object files
COMMON_OBJECTS = $(OBJDIR)/yaml_parser.o $(OBJDIR)/lxc_manager.o $(OBJDIR)/network.o
COORDINATOR_OBJECTS = $(OBJDIR)/coordinator.o $(COMMON_OBJECTS)
WORKER_OBJECTS = $(OBJDIR)/worker.o $(COMMON_OBJECTS)

# Binaries
COORDINATOR_BIN = $(BINDIR)/coordinator
WORKER_BIN = $(BINDIR)/worker

# Default target
all: directories $(COORDINATOR_BIN) $(WORKER_BIN)

# Create directories
directories:
	@mkdir -p $(OBJDIR)
	@mkdir -p $(BINDIR)

# Build coordinator
$(COORDINATOR_BIN): $(COORDINATOR_OBJECTS)
	$(CC) $(COORDINATOR_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Coordinator built successfully"

# Build worker
$(WORKER_BIN): $(WORKER_OBJECTS)
	$(CC) $(WORKER_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Worker built successfully"

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Install targets
install: all
	@echo "Installing distributed LXC system..."
	sudo cp $(COORDINATOR_BIN) /usr/local/bin/dlxc-coordinator
	sudo cp $(WORKER_BIN) /usr/local/bin/dlxc-worker
	sudo mkdir -p /etc/distributed-lxc
	sudo cp $(CONFIGDIR)/* /etc/distributed-lxc/ 2>/dev/null || true
	sudo mkdir -p /var/log/distributed-lxc
	@echo "Installation complete"

# Uninstall
uninstall:
	sudo rm -f /usr/local/bin/dlxc-coordinator
	sudo rm -f /usr/local/bin/dlxc-worker
	sudo rm -rf /etc/distributed-lxc
	sudo rm -rf /var/log/distributed-lxc
	@echo "Uninstallation complete"

# Clean build artifacts
clean:
	rm -rf $(OBJDIR)
	rm -rf $(BINDIR)
	@echo "Clean complete"

# Clean and rebuild
rebuild: clean all

# Development targets
debug: CFLAGS += -g -DDEBUG
debug: all

release: CFLAGS += -O2 -DNDEBUG
release: all

# Test targets
test: all
	@echo "Running basic functionality tests..."
	@./tests/run_tests.sh

# Package creation
package: release
	@echo "Creating distribution package..."
	@mkdir -p dist/distributed-lxc
	@cp -r $(BINDIR) dist/distributed-lxc/
	@cp -r $(CONFIGDIR) dist/distributed-lxc/
	@cp -r $(EXAMPLEDIR) dist/distributed-lxc/
	@cp README.md dist/distributed-lxc/
	@cp LICENSE dist/distributed-lxc/ 2>/dev/null || true
	@cd dist && tar -czf distributed-lxc.tar.gz distributed-lxc/
	@echo "Package created: dist/distributed-lxc.tar.gz"

# Documentation
docs:
	@echo "Generating documentation..."
	@doxygen Doxyfile 2>/dev/null || echo "Doxygen not found, skipping documentation generation"

# Check dependencies
check-deps:
	@echo "Checking system dependencies..."
	@which lxc >/dev/null 2>&1 || (echo "ERROR: LXC not found. Please install LXC." && exit 1)
	@which gcc >/dev/null 2>&1 || (echo "ERROR: GCC not found. Please install GCC." && exit 1)
	@echo "All dependencies satisfied"

# Show help
help:
	@echo "Distributed LXC Management System Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all        - Build all binaries (default)"
	@echo "  coordinator - Build only coordinator"
	@echo "  worker     - Build only worker"
	@echo "  debug      - Build with debug symbols"
	@echo "  release    - Build optimized release version"
	@echo "  install    - Install system-wide"
	@echo "  uninstall  - Remove system installation"
	@echo "  clean      - Remove build artifacts"
	@echo "  rebuild    - Clean and build all"
	@echo "  test       - Run tests"
	@echo "  package    - Create distribution package"
	@echo "  docs       - Generate documentation"
	@echo "  check-deps - Check system dependencies"
	@echo "  help       - Show this help"

# Individual binary targets
coordinator: directories $(COORDINATOR_BIN)
worker: directories $(WORKER_BIN)

# Dependencies
$(OBJDIR)/coordinator.o: $(SRCDIR)/coordinator.c $(INCDIR)/distributed_lxc.h $(INCDIR)/yaml_parser.h $(INCDIR)/lxc_manager.h
$(OBJDIR)/worker.o: $(SRCDIR)/worker.c $(INCDIR)/distributed_lxc.h $(INCDIR)/yaml_parser.h $(INCDIR)/lxc_manager.h
$(OBJDIR)/yaml_parser.o: $(SRCDIR)/yaml_parser.c $(INCDIR)/yaml_parser.h $(INCDIR)/distributed_lxc.h
$(OBJDIR)/lxc_manager.o: $(SRCDIR)/lxc_manager.c $(INCDIR)/lxc_manager.h $(INCDIR)/distributed_lxc.h
$(OBJDIR)/network.o: $(SRCDIR)/network.c $(INCDIR)/distributed_lxc.h

.PHONY: all directories install uninstall clean rebuild debug release test package docs check-deps help coordinator worker