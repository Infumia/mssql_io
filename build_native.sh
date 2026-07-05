#!/bin/bash
# Build script for native libraries across platforms

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
BUILD_DIR="$SRC_DIR/build"

echo "🔨 Building MSSQL IO Native Library"
echo "===================================="

# Detect platform
PLATFORM=$(uname -s)
case "$PLATFORM" in
    Linux*)     PLATFORM_NAME="Linux";;
    Darwin*)    PLATFORM_NAME="macOS";;
    MINGW*|MSYS*|CYGWIN*) PLATFORM_NAME="Windows";;
    *)          PLATFORM_NAME="UNKNOWN";;
esac

echo "Platform: $PLATFORM_NAME"

# Check for FreeTDS
echo ""
echo "Checking for FreeTDS..."

if [ "$PLATFORM_NAME" = "Linux" ]; then
    if ! dpkg -l | grep -q freetds-dev; then
        echo "❌ FreeTDS not found. Install with:"
        echo "   sudo apt-get install freetds-dev"
        exit 1
    fi
    echo "✅ FreeTDS found"
elif [ "$PLATFORM_NAME" = "macOS" ]; then
    if ! brew list freetds &>/dev/null; then
        echo "❌ FreeTDS not found. Install with:"
        echo "   brew install freetds"
        exit 1
    fi
    echo "✅ FreeTDS found"
elif [ "$PLATFORM_NAME" = "Windows" ]; then
    echo "⚠️  On Windows, ensure FreeTDS is installed via vcpkg"
fi

# Create build directory
echo ""
echo "Creating build directory..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Run CMake
echo ""
echo "Running CMake..."
cmake ..

# Build
echo ""
echo "Building..."
cmake --build . --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

# Check output
echo ""
echo "Build complete!"
echo ""
echo "Output files:"
ls -lh libmssql_io.* 2>/dev/null || ls -lh mssql_io.* 2>/dev/null || echo "No library files found"

echo ""
echo "✅ Build successful!"
echo ""
echo "Next steps:"
echo "  1. Run tests: flutter test"
echo "  2. Run example: cd example && flutter run"

