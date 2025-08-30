# Hunter Flipper Makefile
# Provides convenient targets for building and testing the game

# Path to ufbt virtual environment
UFBT = ~/ufbt-env/bin/ufbt

.PHONY: all build clean launch debug help install test test-clean

# Default target
all: build

# Build the game
build:
	$(UFBT)

# Clean build artifacts
clean:
	$(UFBT) clean

# Build and launch on connected Flipper Zero
launch:
	$(UFBT) launch

# Build debug version
debug:
	$(UFBT) COMPACT=0

# Install to connected Flipper Zero without launching
install:
	$(UFBT) install

# Show build information
info:
	$(UFBT) info

# Format code (if available)
format:
	$(UFBT) format

# Lint code (if available) 
lint:
	$(UFBT) lint

# Test suite - build and run all tests
test: test-build
	@echo "Running Hunter-Flipper Test Suite..."
	@echo "===================================="
	@echo ""
	
	@echo "=== Unit Tests ==="
	@echo "Building and running unit tests..."
	@gcc -o adhoc test/unit/test_raycaster.c raycaster.c chunk_manager.c terrain.c mock_furi.c -Itest -lm && ./adhoc
	@gcc -o adhoc test/unit/test_terrain.c terrain.c mock_furi.c -Itest -lm && ./adhoc
	@echo ""
	
	@echo "=== Integration Tests ==="
	@echo "Building and running integration tests..."
	@gcc -o adhoc test_progressive_ping.c -lm && ./adhoc
	@gcc -o adhoc test_standalone.c -lm && ./adhoc  
	@gcc -o adhoc test/integration/test_game_pipeline_simple.c -Itest -lm && ./adhoc
	@echo ""
	
	@echo "=== Visual Tests ==="
	@echo "Building and running visual tests..."
	@gcc -o adhoc test/visual/test_ascii_render.c raycaster.c chunk_manager.c terrain.c sonar_chart.c mock_furi.c -Itest -lm && ./adhoc
	@echo ""
	
	@echo "=== Full Game Integration Test ==="
	@echo "Building and running complete game pipeline test..."
	@gcc -o adhoc test_full_game_ping.c game.c chunk_manager.c raycaster.c sonar_chart.c terrain.c mock_furi.c -Itest -lm && ./adhoc
	@echo ""
	
	@echo "=== Test Suite Summary ==="
	@gcc -o adhoc test_runner.c -Itest && ./adhoc
	@echo ""
	@echo "🎉 Test suite completed!"

# Build all test binaries (without running)
test-build:
	@echo "Building all test binaries..."
	@gcc -o test/unit/test_raycaster test/unit/test_raycaster.c raycaster.c chunk_manager.c terrain.c mock_furi.c -Itest -lm
	@gcc -o test/unit/test_terrain test/unit/test_terrain.c terrain.c mock_furi.c -Itest -lm
	@gcc -o test_progressive_ping test_progressive_ping.c -lm
	@gcc -o test_standalone test_standalone.c -lm
	@gcc -o test/integration/test_game_pipeline_simple test/integration/test_game_pipeline_simple.c -Itest -lm
	@gcc -o test/visual/test_ascii_render test/visual/test_ascii_render.c raycaster.c chunk_manager.c terrain.c sonar_chart.c mock_furi.c -Itest -lm
	@gcc -o test_runner test_runner.c -Itest
	@echo "All test binaries built successfully."

# Clean test binaries
test-clean:
	@echo "Cleaning test binaries..."
	@rm -f adhoc
	@rm -f test_* 
	@rm -f test/unit/test_*
	@rm -f test/integration/test_*
	@rm -f test/visual/test_*
	@rm -f test/system/test_*
	@echo "Test binaries cleaned."

# Show help
help:
	@echo "Hunter Flipper Build Targets:"
	@echo "  build   - Build the game (default)"
	@echo "  clean   - Clean build artifacts"
	@echo "  launch  - Build and launch on connected Flipper Zero"
	@echo "  debug   - Build debug version with symbols"
	@echo "  install - Install to Flipper Zero without launching"
	@echo "  info    - Show build information"
	@echo "  format  - Format source code"
	@echo "  lint    - Lint source code"
	@echo "  test    - Build and run complete test suite"
	@echo "  test-build - Build all test binaries"
	@echo "  test-clean - Clean test binaries"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - ufbt installed in ~/ufbt-env/"
	@echo "  - Flipper Zero connected via USB (for launch/install)"
	@echo ""
	@echo "Examples:"
	@echo "  make build"
	@echo "  make launch"
	@echo "  make clean"