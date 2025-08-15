#!/bin/bash

# Simple test script for the client-microphone example

echo "Whisper STT Client-Microphone Test"
echo "=================================="
echo

# Check if build directory exists
if [ ! -d "../../build" ]; then
    echo "Build directory not found. Please run 'make build' first."
    exit 1
fi

# Check if the client binary exists
CLIENT_BINARY="../../build/bin/whisper-client-microphone"
if [ ! -f "$CLIENT_BINARY" ]; then
    echo "Client binary not found at $CLIENT_BINARY"
    echo "Please build the project with SDL2 support enabled:"
    echo "  cmake -B build -DWHISPER_SDL2=ON"
    echo "  cmake --build build --config Release"
    exit 1
fi

# Check if server is running
echo "Checking if Whisper STT server is running..."
if curl -s "http://127.0.0.1:8080/inference" > /dev/null 2>&1; then
    echo "✓ Server is running at http://127.0.0.1:8080"
else
    echo "✗ Server is not running. Please start the server first:"
    echo "  ./build/bin/whisper-server -m models/ggml-base.en.bin"
    exit 1
fi

echo
echo "Starting client with default settings..."
echo "Press Ctrl+C to stop"
echo

# Run the client
exec "$CLIENT_BINARY" "$@"
