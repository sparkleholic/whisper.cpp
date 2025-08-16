#!/usr/bin/env python3
"""
Test WebSocket handshake calculation to verify against our C++ implementation
"""
import hashlib
import base64

def test_websocket_handshake():
    # Test key from a typical WebSocket client
    test_key = "dGhlIHNhbXBsZSBub25jZQ=="
    magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    
    # Correct WebSocket handshake calculation
    combined = test_key + magic_string
    print(f"Combined string: {combined}")
    
    # Calculate SHA1 hash
    sha1_hash = hashlib.sha1(combined.encode('utf-8')).digest()
    print(f"SHA1 hash (hex): {sha1_hash.hex()}")
    print(f"SHA1 hash (binary length): {len(sha1_hash)}")
    
    # Encode in Base64
    accept_key = base64.b64encode(sha1_hash).decode('utf-8')
    print(f"Accept key: {accept_key}")
    
    # Expected result should be: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
    expected = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    print(f"Expected: {expected}")
    print(f"Match: {accept_key == expected}")

if __name__ == "__main__":
    test_websocket_handshake()
