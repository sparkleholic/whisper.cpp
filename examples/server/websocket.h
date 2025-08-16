/*
 * Simple WebSocket server implementation for whisper.cpp
 * Based on RFC 6455 WebSocket Protocol
 */

#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <memory>
#include <cstring>
#include <cassert>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
typedef int SOCKET;
#ifndef INVALID_SOCKET
const int INVALID_SOCKET = -1;
#endif
#ifndef SOCKET_ERROR
const int SOCKET_ERROR = -1;
#endif
#endif

namespace websocket {

// Simple SHA1 implementation (for WebSocket handshake)
class SHA1 {
public:
    static std::string hash(const std::string& input) {
        // Simple implementation - for production use a proper crypto library
        uint32_t h0 = 0x67452301;
        uint32_t h1 = 0xEFCDAB89;
        uint32_t h2 = 0x98BADCFE;
        uint32_t h3 = 0x10325476;
        uint32_t h4 = 0xC3D2E1F0;
        
        std::vector<uint8_t> message(input.begin(), input.end());
        uint64_t original_length = message.size() * 8;
        
        // Padding
        message.push_back(0x80);
        while ((message.size() % 64) != 56) {
            message.push_back(0x00);
        }
        
        // Append length
        for (int i = 7; i >= 0; --i) {
            message.push_back((original_length >> (i * 8)) & 0xFF);
        }
        
        // Process chunks
        for (size_t i = 0; i < message.size(); i += 64) {
            uint32_t w[80];
            
            // Break chunk into sixteen 32-bit words
            for (int j = 0; j < 16; ++j) {
                w[j] = (message[i + j * 4] << 24) |
                       (message[i + j * 4 + 1] << 16) |
                       (message[i + j * 4 + 2] << 8) |
                       (message[i + j * 4 + 3]);
            }
            
            // Extend the sixteen 32-bit words into eighty 32-bit words
            for (int j = 16; j < 80; ++j) {
                w[j] = leftrotate(w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16], 1);
            }
            
            uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
            
            for (int j = 0; j < 80; ++j) {
                uint32_t f, k;
                if (j < 20) {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999;
                } else if (j < 40) {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                } else if (j < 60) {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                } else {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }
                
                uint32_t temp = leftrotate(a, 5) + f + e + k + w[j];
                e = d;
                d = c;
                c = leftrotate(b, 30);
                b = a;
                a = temp;
            }
            
            h0 += a;
            h1 += b;
            h2 += c;
            h3 += d;
            h4 += e;
        }
        
        // Produce the final hash value
        std::ostringstream result;
        result << std::hex << std::setfill('0')
               << std::setw(8) << h0
               << std::setw(8) << h1
               << std::setw(8) << h2
               << std::setw(8) << h3
               << std::setw(8) << h4;
        
        return hex_to_binary(result.str());
    }
    
private:
    static uint32_t leftrotate(uint32_t value, int shift) {
        return (value << shift) | (value >> (32 - shift));
    }
    
    static std::string hex_to_binary(const std::string& hex) {
        std::string binary;
        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string byte = hex.substr(i, 2);
            unsigned char chr = static_cast<unsigned char>(std::strtol(byte.c_str(), nullptr, 16));
            binary.push_back(static_cast<char>(chr));
        }
        return binary;
    }
};

// Simple Base64 encoder
class Base64 {
public:
    static std::string encode(const std::string& input) {
        static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        
        for (size_t i = 0; i < input.length(); i += 3) {
            uint32_t a = static_cast<unsigned char>(input[i]);
            uint32_t b = (i + 1 < input.length()) ? static_cast<unsigned char>(input[i + 1]) : 0;
            uint32_t c = (i + 2 < input.length()) ? static_cast<unsigned char>(input[i + 2]) : 0;
            
            uint32_t bitmap = (a << 16) | (b << 8) | c;
            
            encoded += charset[(bitmap >> 18) & 0x3F];
            encoded += charset[(bitmap >> 12) & 0x3F];
            encoded += (i + 1 < input.length()) ? charset[(bitmap >> 6) & 0x3F] : '=';
            encoded += (i + 2 < input.length()) ? charset[bitmap & 0x3F] : '=';
        }
        
        return encoded;
    }
    
    static std::vector<uint8_t> decode(const std::string& input) {
        static const int decode_table[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
        };
        
        std::vector<uint8_t> decoded;
        std::string clean_input;
        
        // Remove whitespace and padding
        for (char c : input) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                clean_input += c;
            }
        }
        
        // Remove padding
        while (!clean_input.empty() && clean_input.back() == '=') {
            clean_input.pop_back();
        }
        
        for (size_t i = 0; i < clean_input.length(); i += 4) {
            uint32_t a = (i < clean_input.length()) ? decode_table[static_cast<unsigned char>(clean_input[i])] : 0;
            uint32_t b = (i + 1 < clean_input.length()) ? decode_table[static_cast<unsigned char>(clean_input[i + 1])] : 0;
            uint32_t c = (i + 2 < clean_input.length()) ? decode_table[static_cast<unsigned char>(clean_input[i + 2])] : 0;
            uint32_t d = (i + 3 < clean_input.length()) ? decode_table[static_cast<unsigned char>(clean_input[i + 3])] : 0;
            
            if (a == -1 || b == -1 || (i + 2 < clean_input.length() && c == -1) || (i + 3 < clean_input.length() && d == -1)) {
                // Invalid base64 character
                continue;
            }
            
            uint32_t bitmap = (a << 18) | (b << 12) | (c << 6) | d;
            
            decoded.push_back((bitmap >> 16) & 0xFF);
            if (i + 2 < clean_input.length()) {
                decoded.push_back((bitmap >> 8) & 0xFF);
            }
            if (i + 3 < clean_input.length()) {
                decoded.push_back(bitmap & 0xFF);
            }
        }
        
        return decoded;
    }
};

// WebSocket frame opcodes
enum class OpCode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

// WebSocket frame structure
struct Frame {
    OpCode opcode;
    bool fin;
    std::vector<uint8_t> payload;
};

class Connection {
public:
    Connection(SOCKET socket) : socket_(socket), connected_(true) {}
    
    ~Connection() {
        close();
    }
    
    void close() {
        if (connected_.exchange(false)) {
#ifdef _WIN32
            closesocket(socket_);
#else
            ::close(socket_);
#endif
        }
    }
    
    bool is_connected() const {
        return connected_.load();
    }
    
    bool send_text(const std::string& message) {
        return send_frame(OpCode::TEXT, reinterpret_cast<const uint8_t*>(message.data()), message.size());
    }
    
    bool send_binary(const std::vector<uint8_t>& data) {
        return send_frame(OpCode::BINARY, data.data(), data.size());
    }
    
    bool send_ping() {
        return send_frame(OpCode::PING, nullptr, 0);
    }
    
    bool send_pong() {
        return send_frame(OpCode::PONG, nullptr, 0);
    }
    
    SOCKET get_socket() const { return socket_; }
    
private:
    SOCKET socket_;
    std::atomic<bool> connected_;
    
    bool send_frame(OpCode opcode, const uint8_t* data, size_t len) {
        if (!connected_.load()) return false;
        
        std::vector<uint8_t> frame;
        
        // First byte: FIN (1) + RSV (000) + Opcode (4 bits)
        frame.push_back(0x80 | static_cast<uint8_t>(opcode));
        
        // Payload length
        if (len < 126) {
            frame.push_back(static_cast<uint8_t>(len));
        } else if (len < 65536) {
            frame.push_back(126);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((len >> (i * 8)) & 0xFF);
            }
        }
        
        // Payload
        if (data && len > 0) {
            frame.insert(frame.end(), data, data + len);
        }
        
        ssize_t sent = send(socket_, reinterpret_cast<const char*>(frame.data()), frame.size(), 0);
        return sent == static_cast<ssize_t>(frame.size());
    }
};

class Server {
public:
    using MessageHandler = std::function<void(std::shared_ptr<Connection>, const std::string&)>;
    using BinaryHandler = std::function<void(std::shared_ptr<Connection>, const std::vector<uint8_t>&)>;
    using ConnectHandler = std::function<void(std::shared_ptr<Connection>)>;
    using DisconnectHandler = std::function<void(std::shared_ptr<Connection>)>;
    
    Server() : running_(false) {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }
    
    ~Server() {
        stop();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    bool listen(int port, const std::string& host = "0.0.0.0") {
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == INVALID_SOCKET) {
            return false;
        }
        
        int opt = 1;
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
        
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        addr.sin_port = htons(port);
        
        if (bind(server_socket_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            return false;
        }
        
        if (::listen(server_socket_, 10) == SOCKET_ERROR) {
            return false;
        }
        
        running_ = true;
        accept_thread_ = std::thread(&Server::accept_loop, this);
        
        return true;
    }
    
    void stop() {
        running_ = false;
        
        if (server_socket_ != INVALID_SOCKET) {
#ifdef _WIN32
            closesocket(server_socket_);
#else
            ::close(server_socket_);
#endif
            server_socket_ = INVALID_SOCKET;
        }
        
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
        
        // Close all connections
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& conn : connections_) {
            conn->close();
        }
        connections_.clear();
    }
    
    void set_message_handler(MessageHandler handler) {
        message_handler_ = handler;
    }
    
    void set_binary_handler(BinaryHandler handler) {
        binary_handler_ = handler;
    }
    
    void set_connect_handler(ConnectHandler handler) {
        connect_handler_ = handler;
    }
    
    void set_disconnect_handler(DisconnectHandler handler) {
        disconnect_handler_ = handler;
    }
    
private:
    SOCKET server_socket_ = INVALID_SOCKET;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    
    std::vector<std::shared_ptr<Connection>> connections_;
    std::mutex connections_mutex_;
    
    MessageHandler message_handler_;
    BinaryHandler binary_handler_;
    ConnectHandler connect_handler_;
    DisconnectHandler disconnect_handler_;
    
    void accept_loop() {
        while (running_) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            SOCKET client_socket = accept(server_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_socket == INVALID_SOCKET) {
                if (running_) {
                    continue;
                }
                break;
            }
            
            // Handle WebSocket handshake
            if (perform_handshake(client_socket)) {
                auto connection = std::make_shared<Connection>(client_socket);
                
                {
                    std::lock_guard<std::mutex> lock(connections_mutex_);
                    connections_.push_back(connection);
                }
                
                if (connect_handler_) {
                    connect_handler_(connection);
                }
                
                // Start handling this connection in a new thread
                std::thread([this, connection]() {
                    handle_connection(connection);
                }).detach();
            } else {
#ifdef _WIN32
                closesocket(client_socket);
#else
                ::close(client_socket);
#endif
            }
        }
    }
    
    bool perform_handshake(SOCKET client_socket) {
        // Read HTTP request
        char buffer[4096];
        ssize_t received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            return false;
        }
        buffer[received] = '\0';
        
        std::string request(buffer);
        
        // Parse WebSocket key
        size_t key_pos = request.find("Sec-WebSocket-Key: ");
        if (key_pos == std::string::npos) {
            return false;
        }
        
        key_pos += 19; // Length of "Sec-WebSocket-Key: "
        size_t key_end = request.find("\r\n", key_pos);
        if (key_end == std::string::npos) {
            return false;
        }
        
        std::string key = request.substr(key_pos, key_end - key_pos);
        
        // Generate accept key
        std::string accept_key = generate_accept_key(key);
        
        // Send handshake response
        std::string response = 
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n"
            "\r\n";
        
        ssize_t sent = send(client_socket, response.c_str(), response.length(), 0);
        return sent == static_cast<ssize_t>(response.length());
    }
    
    std::string generate_accept_key(const std::string& key) {
        std::string combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string hash = SHA1::hash(combined);
        return Base64::encode(hash);
    }
    
    void handle_connection(std::shared_ptr<Connection> connection) {
        char buffer[4096];
        
        while (connection->is_connected() && running_) {
            ssize_t received = recv(connection->get_socket(), buffer, sizeof(buffer), 0);
            if (received <= 0) {
                break;
            }
            
            // Parse WebSocket frame(s)
            size_t offset = 0;
            while (offset < static_cast<size_t>(received)) {
                Frame frame;
                size_t frame_size = parse_frame(reinterpret_cast<uint8_t*>(buffer + offset), received - offset, frame);
                if (frame_size == 0) {
                    break; // Incomplete frame
                }
                
                offset += frame_size;
                
                // Handle frame based on opcode
                switch (frame.opcode) {
                    case OpCode::TEXT:
                        if (message_handler_) {
                            std::string message(frame.payload.begin(), frame.payload.end());
                            message_handler_(connection, message);
                        }
                        break;
                    case OpCode::BINARY:
                        if (binary_handler_) {
                            binary_handler_(connection, frame.payload);
                        }
                        break;
                    case OpCode::PING:
                        connection->send_pong();
                        break;
                    case OpCode::CLOSE:
                        connection->close();
                        return;
                    default:
                        break;
                }
            }
        }
        
        // Connection closed
        if (disconnect_handler_) {
            disconnect_handler_(connection);
        }
        
        // Remove from connections list
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(
            std::remove(connections_.begin(), connections_.end(), connection),
            connections_.end()
        );
    }
    
    size_t parse_frame(uint8_t* data, size_t len, Frame& frame) {
        if (len < 2) return 0; // Need at least 2 bytes
        
        frame.fin = (data[0] & 0x80) != 0;
        frame.opcode = static_cast<OpCode>(data[0] & 0x0F);
        
        bool masked = (data[1] & 0x80) != 0;
        uint64_t payload_len = data[1] & 0x7F;
        
        size_t header_len = 2;
        
        // Extended payload length
        if (payload_len == 126) {
            if (len < 4) return 0;
            payload_len = (data[2] << 8) | data[3];
            header_len = 4;
        } else if (payload_len == 127) {
            if (len < 10) return 0;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[2 + i];
            }
            header_len = 10;
        }
        
        // Masking key
        uint8_t mask[4] = {0};
        if (masked) {
            if (len < header_len + 4) return 0;
            memcpy(mask, data + header_len, 4);
            header_len += 4;
        }
        
        // Check if we have the complete payload
        if (len < header_len + payload_len) return 0;
        
        // Extract payload
        frame.payload.resize(payload_len);
        for (uint64_t i = 0; i < payload_len; ++i) {
            frame.payload[i] = data[header_len + i];
            if (masked) {
                frame.payload[i] ^= mask[i % 4];
            }
        }
        
        return header_len + payload_len;
    }
};

} // namespace websocket

#endif // WEBSOCKET_H
