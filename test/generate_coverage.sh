#!/bin/bash
# Generate code coverage reports for unit tests

set -e

# Get script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Change to project root
cd "$PROJECT_ROOT"

# Detect platform
OS="$(uname -s)"
if [[ "$OS" == "Darwin" ]]; then
    echo "⚠️  Warning: Code coverage has known issues on macOS"
    echo "   Using native environment instead of native_coverage"
    echo "   For full coverage support, use Linux or CI"
    echo ""
    TEST_ENV="native"
else
    echo "Running tests with coverage (using native_coverage environment)..."
    TEST_ENV="native_coverage"
fi

echo "Running tests with coverage..."
pio test -e "$TEST_ENV"

echo ""
echo "Generating coverage reports..."

# Find all .gcda files (check both native and native_coverage)
BUILD_DIR=".pio/build/$TEST_ENV"
GCDA_COUNT=$(find "$BUILD_DIR" -name "*.gcda" -type f 2>/dev/null | wc -l)
if [[ $GCDA_COUNT -eq 0 ]]; then
    echo "⚠️  No coverage data found in $BUILD_DIR"
    
    if [[ "$TEST_ENV" == "native" ]]; then
        echo "   Coverage instrumentation may not be enabled on this platform"
        echo "   Tests passed but coverage report cannot be generated"
        echo ""
        echo "   For full coverage support:"
        echo "   - Use Linux or WSL"
        echo "   - Or run in CI/GitHub Actions"
        exit 0
    else
        echo "   Make sure tests ran successfully with coverage flags"
        exit 1
    fi
fi

# Check if lcov is installed
if command -v lcov > /dev/null 2>&1; then
    echo "Generating lcov report..."
    
    lcov --capture \
         --directory "$BUILD_DIR" \
         --output-file coverage.info \
         --exclude '*/test/*' \
         --exclude '*/mock*' \
         --exclude '*/usr/*' \
         --exclude '*/.platformio/*' \
         --ignore-errors gcov,source
    
    if command -v genhtml > /dev/null 2>&1; then
        genhtml coverage.info --output-directory coverage_html --quiet
        echo ""
        echo "✅ Coverage report generated: coverage_html/index.html"
        
        # Show coverage summary
        echo ""
        echo "Coverage Summary:"
        lcov --summary coverage.info 2>&1 | grep -E "lines|functions" || true
        
        echo ""
        echo "   Open in browser: open coverage_html/index.html"
    else
        echo "⚠️  genhtml not found. Install with: brew install lcov (macOS) or apt install lcov (Linux)"
        echo ""
        echo "Coverage summary:"
        lcov --summary coverage.info 2>&1 | grep -E "lines|functions" || true
    fi
else
    echo "⚠️  lcov not found. Install with: brew install lcov (macOS) or apt install lcov (Linux)"
    echo ""
    echo "For now, you can view raw coverage data in .pio/build/native/"
    exit 1
fi

echo ""
echo "Coverage data location: $BUILD_DIR/"
echo ""
echo "Note: Coverage requires native_coverage environment (Linux/CI only)"
echo "      On macOS, coverage instrumentation may not work correctly"

