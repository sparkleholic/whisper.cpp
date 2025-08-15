# whisper.cpp/examples/client-microphone

Continuous microphone audio streaming client for Whisper STT server.

This example captures audio from the microphone in real-time and continuously sends it to a running Whisper STT server for transcription.

## Requirements

- SDL2 library for audio capture (install with `brew install sdl2` on macOS)
- A running Whisper STT server (from examples/server)
- A Whisper model file

## Quick Start

1. **Build the project with SDL2 support:**
   ```bash
   # Using Makefile (recommended)
   make client-microphone
   
   # Or using cmake directly
   cmake -B build -DWHISPER_SDL2=ON
   cmake --build build --config Release
   ```

2. **Download a model (if not already done):**
   ```bash
   bash ./models/download-ggml-model.sh base.en
   ```

3. **Start the Whisper STT server:**
   ```bash
   ./build/bin/whisper-server -m models/ggml-base.en.bin
   ```

4. **Run the client (in another terminal):**
   ```bash
   ./build/bin/whisper-client-microphone
   ```

## Usage

```
usage: ./build/bin/whisper-client-microphone [options]

options:
  -h,        --help                    show this help message and exit
  -s URL,    --server URL              [http://127.0.0.1:8080] STT server URL
  -d N,      --duration N              [3000] audio chunk duration in milliseconds
  -c N,      --capture-id N            [-1  ] capture device id (-1 for default)
  -t N,      --threshold N             [0.6 ] voice activity detection threshold
  -v,        --verbose                 [false] verbose output
  -r N,      --sample-rate N           [16000] audio sample rate
```

## Example Commands

```bash
# Basic usage with default settings (3-second chunks)
./build/bin/whisper-client-microphone

# Shorter chunks for faster response
./build/bin/whisper-client-microphone -d 2000

# More sensitive voice detection (lower threshold)
./build/bin/whisper-client-microphone -t 0.4

# Verbose output to see what's happening
./build/bin/whisper-client-microphone -v

# Connect to remote server
./build/bin/whisper-client-microphone -s http://192.168.1.100:8080

# Use specific audio device (check system settings for device IDs)
./build/bin/whisper-client-microphone -c 1

# Combination: 2-second chunks, verbose, lower threshold
./build/bin/whisper-client-microphone -d 2000 -v -t 0.4
```

## How it works

1. **Audio Capture**: Uses SDL2 to capture audio from the system's default microphone
2. **Voice Activity Detection**: Applies basic energy-based VAD to avoid sending silent audio
3. **Audio Processing**: Converts captured audio to 16-bit PCM WAV format in memory
4. **HTTP Transmission**: Sends audio as multipart/form-data to the STT server's `/inference` endpoint
5. **Response Parsing**: Extracts transcription text from JSON response and displays it

## Example output

```
Whisper STT Client - Microphone Stream
Server: http://127.0.0.1:8080
Chunk duration: 3000ms
Sample rate: 16000Hz
VAD threshold: 0.6

Initializing audio capture...
Audio device opened successfully
Listening... (Press Ctrl+C to stop)

[2025-08-16 14:30:12.123] Sending audio chunk (3.0s)...
[2025-08-16 14:30:13.456] Transcription: "Hello, this is a test of the whisper speech recognition system."

[2025-08-16 14:30:15.789] Sending audio chunk (3.0s)...
[2025-08-16 14:30:16.234] Transcription: "The weather today is quite nice and sunny."

[2025-08-16 14:30:18.567] No voice activity detected
[2025-08-16 14:30:20.891] Sending audio chunk (3.0s)...
[2025-08-16 14:30:21.345] Transcription: "This works really well for real-time transcription."
```

## Troubleshooting

### Build Issues

- **SDL2 not found**: Install SDL2 development libraries
  - macOS: `brew install sdl2`
  - Ubuntu: `sudo apt-get install libsdl2-dev`
  - Windows: Download from [SDL2 releases](https://github.com/libsdl-org/SDL/releases)

- **Linker errors**: Make sure you're building with SDL2 support:
  ```bash
  cmake -B build -DWHISPER_SDL2=ON
  ```

### Runtime Issues

- **"Failed to initialize audio capture"**: Check microphone permissions
  - macOS: System Settings > Privacy & Security > Microphone
  - Ensure the terminal/application has microphone access

- **"Failed to connect to server"**: Make sure the STT server is running
  ```bash
  curl http://127.0.0.1:8080/inference  # Should not return "connection refused"
  ```

- **No transcriptions**: Adjust the voice activity threshold
  ```bash
  ./build/bin/whisper-client-microphone -t 0.3 -v  # Lower threshold, verbose output
  ```

- **Audio device issues**: Try specifying a different capture device
  ```bash
  ./build/bin/whisper-client-microphone -c 0  # Try device 0, 1, 2, etc.
  ```

## Performance Tips

- **Faster Response**: Use shorter chunks (`-d 1500`) for quicker transcription
- **Better Accuracy**: Use longer chunks (`-d 5000`) for more context
- **Resource Usage**: The client itself is lightweight; the server does the heavy processing
- **Network**: For remote servers, consider network latency in chunk duration selection

## Integration

This client can be easily integrated into other applications:

- The HTTP protocol is simple multipart form data
- Audio format is standard 16-bit PCM WAV
- Response is JSON with a "text" field
- Can be modified to save audio to files instead of streaming
- Voice activity detection can be customized or replaced

## Files

- `client-microphone.cpp` - Main implementation
- `CMakeLists.txt` - Build configuration
- `README.md` - This documentation
- `demo.sh` - Interactive demonstration script
- `test.sh` - Simple test script
