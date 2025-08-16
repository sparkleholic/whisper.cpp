# Whisper.cpp WebSocket Streaming API

This implementation adds WebSocket streaming support to the Whisper.cpp server, enabling real-time speech-to-text transcription.

## Features

- **Real-time streaming**: Process audio chunks as they arrive
- **Session management**: Each client can have independent transcription sessions
- **Configurable parameters**: Language, temperature, VAD settings per session
- **Word-level timestamps**: Detailed timing information for each word
- **Speaker diarization**: When enabled, identifies speakers in stereo audio
- **Compatible with OpenAI API pattern**: Similar message structure to OpenAI's real-time API

## Usage

### Starting the Server

```bash
# Default: HTTP on 8080, WebSocket on 8081
./server -m models/ggml-base.en.bin

# Custom ports
./server -m models/ggml-base.en.bin --port 3000 --ws-port 3001

# Disable WebSocket
./server -m models/ggml-base.en.bin --disable-websocket
```

### WebSocket Protocol

Connect to `ws://localhost:8081` (or your configured port).

#### 1. Create Session

```json
{
  "type": "session.create",
  "config": {
    "language": "en",
    "temperature": 0.0,
    "vad": false
  }
}
```

Response:
```json
{
  "type": "session.created",
  "session_id": "ws_1234567890abcdef"
}
```

#### 2. Send Audio Data

```json
{
  "type": "audio.append",
  "session_id": "ws_1234567890abcdef",
  "audio": "base64_encoded_audio_data"
}
```

#### 3. Commit Audio for Transcription

```json
{
  "type": "audio.commit",
  "session_id": "ws_1234567890abcdef"
}
```

#### 4. Receive Transcription Results

```json
{
  "type": "transcript",
  "session_id": "ws_1234567890abcdef",
  "segments": [
    {
      "id": 0,
      "text": "Hello, world!",
      "start": 0.0,
      "end": 1.5,
      "is_final": true,
      "words": [
        {
          "word": "Hello",
          "start": 0.0,
          "end": 0.5,
          "probability": 0.99
        },
        {
          "word": "world",
          "start": 0.7,
          "end": 1.2,
          "probability": 0.98
        }
      ]
    }
  ]
}
```

## Client Examples

### JavaScript/HTML Client

See `websocket_client.html` for a complete web-based client that demonstrates:
- WebSocket connection management
- Session creation
- File upload processing
- Microphone recording (HTTPS required)
- Real-time transcript display

### Python Client Example

```python
import websocket
import json
import base64

def on_message(ws, message):
    data = json.loads(message)
    print(f"Received: {data}")
    
    if data['type'] == 'session.created':
        session_id = data['session_id']
        
        # Send audio file
        with open('audio.wav', 'rb') as f:
            audio_data = base64.b64encode(f.read()).decode('utf-8')
        
        ws.send(json.dumps({
            'type': 'audio.append',
            'session_id': session_id,
            'audio': audio_data
        }))
        
        ws.send(json.dumps({
            'type': 'audio.commit',
            'session_id': session_id
        }))

def on_open(ws):
    print("Connected")
    ws.send(json.dumps({
        'type': 'session.create',
        'config': {
            'language': 'en',
            'temperature': 0.0
        }
    }))

ws = websocket.WebSocketApp("ws://localhost:8081",
                          on_open=on_open,
                          on_message=on_message)
ws.run_forever()
```

## Audio Format Requirements

Currently, the WebSocket implementation expects audio data to be:
- Sample rate: 16kHz
- Channels: 1 (mono) or 2 (stereo for diarization)
- Format: 32-bit float PCM
- Encoding: Base64 for transmission

Future versions may support automatic audio format conversion.

## Configuration Options

### Command Line Options

- `--ws-port PORT`: WebSocket port (default: 8081)
- `--disable-websocket`: Disable WebSocket server
- All existing whisper.cpp options apply to WebSocket sessions

### Session Configuration

- `language`: Language code ("en", "es", "fr", etc.) or "auto"
- `temperature`: Sampling temperature (0.0 - 1.0)
- `vad`: Enable Voice Activity Detection
- `no_speech_threshold`: Threshold for speech detection
- Additional whisper.cpp parameters can be added

## Performance Considerations

- Each WebSocket connection runs in its own thread
- Audio processing is queued and handled sequentially per session
- Memory usage scales with number of concurrent sessions
- Consider implementing session timeouts for production use

## Limitations

- Basic audio format support (no automatic conversion yet)
- No authentication/authorization
- Simple session management (no persistence)
- Limited error handling and recovery

## Future Enhancements

- [ ] Automatic audio format detection and conversion
- [ ] Chunked audio streaming for longer sessions
- [ ] Authentication and rate limiting
- [ ] Session persistence and recovery
- [ ] More sophisticated audio buffering
- [ ] Integration with existing ffmpeg converter
- [ ] Streaming VAD for real-time audio detection
