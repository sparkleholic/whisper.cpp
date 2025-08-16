#!/usr/bin/env python3
"""
Simple WebSocket test client for whisper.cpp server
"""
import asyncio
import websockets
import json
import sys

async def test_websocket():
    uri = "ws://127.0.0.1:8081"
    
    try:
        print("🔌 Connecting to whisper WebSocket server...")
        async with websockets.connect(uri) as websocket:
            print("✅ Connected successfully!")
            
            # Listen for welcome message
            response = await websocket.recv()
            msg = json.loads(response)
            print(f"📥 Received: {msg}")
            
            if msg.get('type') == 'session.start':
                print(f"🎯 Session started: {msg.get('session_id')}")
                
                # Send audio.start
                print("📤 Sending audio.start...")
                await websocket.send(json.dumps({"type": "audio.start"}))
                
                # Wait for response
                response = await websocket.recv()
                msg = json.loads(response)
                print(f"📥 Received: {msg}")
                
                if msg.get('type') == 'audio.started':
                    print("🎵 Audio processing started")
                    
                    # Send audio.end
                    print("📤 Sending audio.end...")
                    await websocket.send(json.dumps({"type": "audio.end"}))
                    
                    # Wait for response
                    response = await websocket.recv()
                    msg = json.loads(response)
                    print(f"📥 Received: {msg}")
                    
                    if msg.get('type') == 'audio.ended':
                        print("✅ Audio processing completed")
                        print("🎉 WebSocket test successful!")
                        return True
            
            return False
            
    except Exception as e:
        print(f"❌ WebSocket error: {e}")
        return False

if __name__ == "__main__":
    result = asyncio.run(test_websocket())
    sys.exit(0 if result else 1)
