#!/bin/bash

# Demo script showing how to use the whisper client-microphone example

echo "Whisper Client-Microphone Demo"
echo "============================="
echo
echo "This demo shows how to use the real-time microphone client"
echo "to send audio continuously to a Whisper STT server."
echo

# Check if we're in the right directory
if [ ! -f "build/bin/whisper-client-microphone" ]; then
    echo "Error: Please run this script from the whisper.cpp root directory"
    echo "Current directory: $(pwd)"
    exit 1
fi

echo "Step 1: Make sure you have built the project with SDL2 support"
echo "   cmake -B build -DWHISPER_SDL2=ON"
echo "   cmake --build build --config Release"
echo

echo "Step 2: Download a model (if not already done)"
echo "   bash ./models/download-ggml-model.sh base.en"
echo

echo "Step 3: Start the Whisper STT server in another terminal:"
echo "   ./build/bin/whisper-server -m models/ggml-base.en.bin"
echo

echo "Step 4: Once the server is running, start the client:"
echo "   ./build/bin/whisper-client-microphone"
echo

echo "Available options:"
echo "   -s URL     : Server URL (default: http://127.0.0.1:8080)"
echo "   -d N       : Audio chunk duration in ms (default: 3000)"
echo "   -t N       : Voice activity threshold (default: 0.6)"
echo "   -v         : Verbose output"
echo "   -c N       : Capture device ID (-1 for default)"
echo "   -r N       : Sample rate (default: 16000)"
echo

echo "Example commands:"
echo
echo "# Basic usage (3-second chunks, default server)"
echo "./build/bin/whisper-client-microphone"
echo
echo "# Shorter chunks with verbose output"
echo "./build/bin/whisper-client-microphone -d 2000 -v"
echo
echo "# Connect to different server with lower VAD threshold"
echo "./build/bin/whisper-client-microphone -s http://192.168.1.100:8080 -t 0.4"
echo
echo "# List available audio devices (use SDL2 tool or check system settings)"
echo "# Then use specific device: -c 1"
echo

echo "Press any key to continue with a quick test..."
read -n 1 -s

echo
echo "Testing if server is reachable..."
if curl -s "http://127.0.0.1:8080" > /dev/null 2>&1; then
    echo "✓ Server appears to be running"
    echo "You can now run: ./build/bin/whisper-client-microphone"
else
    echo "✗ Server is not running. Start it first with:"
    echo "  ./build/bin/whisper-server -m models/ggml-base.en.bin"
fi

echo
echo "Demo complete!"
