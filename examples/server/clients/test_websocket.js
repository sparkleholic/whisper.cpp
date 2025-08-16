#!/usr/bin/env node

const WebSocket = require('ws');

// Test WebSocket connection to whisper server
const ws = new WebSocket('ws://127.0.0.1:8081');

ws.on('open', function open() {
    console.log('✅ Connected to whisper WebSocket server');
    
    // Test session start
    console.log('📤 Sending audio.start message...');
    ws.send(JSON.stringify({
        type: 'audio.start'
    }));
});

ws.on('message', function message(data) {
    try {
        const msg = JSON.parse(data.toString());
        console.log('📥 Received message:', msg);
        
        if (msg.type === 'session.start') {
            console.log(`🎯 Session started: ${msg.session_id}`);
        } else if (msg.type === 'audio.started') {
            console.log('🎵 Audio processing started');
            
            // Send audio end to complete the test
            setTimeout(() => {
                console.log('📤 Sending audio.end message...');
                ws.send(JSON.stringify({
                    type: 'audio.end'
                }));
            }, 1000);
        } else if (msg.type === 'audio.ended') {
            console.log('✅ Audio processing completed');
            console.log('🎉 WebSocket test successful!');
            ws.close();
        }
    } catch (e) {
        console.log('📥 Raw message:', data.toString());
    }
});

ws.on('error', function error(err) {
    console.error('❌ WebSocket error:', err.message);
});

ws.on('close', function close() {
    console.log('🔌 WebSocket connection closed');
});

// Timeout after 10 seconds
setTimeout(() => {
    console.log('⏰ Test timeout');
    process.exit(1);
}, 10000);
