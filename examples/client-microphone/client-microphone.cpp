// Real-time microphone audio streaming client for Whisper STT server
//
// This example continuously captures audio from the microphone and sends it
// to a running Whisper STT server for transcription.
//

#include "common-sdl.h"
#include "common.h"
#include "common-whisper.h"

#define HTTPLIB_OPENSSL_SUPPORT
#include "../server/httplib.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <csignal>
#include <atomic>
#include <regex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET INVALID_SOCKET
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR SOCKET_ERROR
#endif
#define closesocket closesocket
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif
#define closesocket close
typedef int socket_t;
#endif

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived shutdown signal. Stopping..." << std::endl;
        g_running = false;
    }
}

// WAV file format structures
struct WavHeader {
    char chunk_id[4];        // "RIFF"
    uint32_t chunk_size;     // File size - 8
    char format[4];          // "WAVE"
    char subchunk1_id[4];    // "fmt "
    uint32_t subchunk1_size; // 16 for PCM
    uint16_t audio_format;   // 1 for PCM
    uint16_t num_channels;   // 1 for mono
    uint32_t sample_rate;    // Sample rate
    uint32_t byte_rate;      // Sample rate * channels * bits_per_sample / 8
    uint16_t block_align;    // Channels * bits_per_sample / 8
    uint16_t bits_per_sample; // 16 for 16-bit
    char subchunk2_id[4];    // "data"
    uint32_t subchunk2_size; // Data size
};

// Simple Base64 encoder
class Base64 {
public:
    static std::string encode(const std::vector<uint8_t>& data) {
        static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        
        for (size_t i = 0; i < data.size(); i += 3) {
            uint32_t a = data[i];
            uint32_t b = (i + 1 < data.size()) ? data[i + 1] : 0;
            uint32_t c = (i + 2 < data.size()) ? data[i + 2] : 0;
            
            uint32_t bitmap = (a << 16) | (b << 8) | c;
            
            encoded += charset[(bitmap >> 18) & 0x3F];
            encoded += charset[(bitmap >> 12) & 0x3F];
            encoded += (i + 1 < data.size()) ? charset[(bitmap >> 6) & 0x3F] : '=';
            encoded += (i + 2 < data.size()) ? charset[bitmap & 0x3F] : '=';
        }
        
        return encoded;
    }
};

// Simple WebSocket client for real-time streaming
class WebSocketClient {
private:
    socket_t socket_;
    bool connected_;
    std::string host_;
    int port_;
    
public:
    WebSocketClient() : socket_(INVALID_SOCKET), connected_(false) {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }
    
    ~WebSocketClient() {
        disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    bool connect(const std::string& url) {
        // Parse WebSocket URL (ws://host:port)
        std::regex url_regex(R"(ws://([^:]+):(\d+))");
        std::smatch matches;
        
        if (!std::regex_match(url, matches, url_regex)) {
            std::cerr << "Invalid WebSocket URL format: " << url << std::endl;
            return false;
        }
        
        host_ = matches[1].str();
        port_ = std::stoi(matches[2].str());
        
        // Create socket
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ == INVALID_SOCKET) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        // Resolve host
        struct hostent* he = gethostbyname(host_.c_str());
        if (!he) {
            std::cerr << "Failed to resolve host: " << host_ << std::endl;
            closesocket(socket_);
            return false;
        }
        
        // Connect to server
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        
        if (::connect(socket_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << "Failed to connect to " << host_ << ":" << port_;
#ifndef _WIN32
            std::cerr << " - Error: " << strerror(errno);
#endif
            std::cerr << std::endl;
            closesocket(socket_);
            return false;
        }
        
        std::cout << "TCP connection established to " << host_ << ":" << port_ << std::endl;
        
        // Perform WebSocket handshake
        std::cout << "Performing WebSocket handshake..." << std::endl;
        if (!perform_handshake()) {
            std::cerr << "WebSocket handshake failed" << std::endl;
            closesocket(socket_);
            return false;
        }
        
        std::cout << "WebSocket handshake successful" << std::endl;
        
        // Set socket to non-blocking mode for receive operations
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(socket_, FIONBIO, &mode);
#else
        int flags = fcntl(socket_, F_GETFL, 0);
        fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
#endif
        
        connected_ = true;
        return true;
    }
    
    void disconnect() {
        if (connected_ && socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            connected_ = false;
            socket_ = INVALID_SOCKET;
        }
    }
    
    bool send_text(const std::string& message) {
        if (!connected_) return false;
        
        std::vector<uint8_t> frame;
        
        // WebSocket frame format for text message
        frame.push_back(0x81); // FIN=1, opcode=1 (text)
        
        size_t len = message.length();
        if (len < 126) {
            frame.push_back(0x80 | len); // MASK=1, payload length
        } else if (len < 65536) {
            frame.push_back(0x80 | 126); // MASK=1, extended payload length
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        } else {
            // Handle larger payloads
            frame.push_back(0x80 | 127); // MASK=1, 64-bit extended payload length
            for (int i = 7; i >= 0; --i) {
                frame.push_back((len >> (i * 8)) & 0xFF);
            }
        }
        
        // Masking key (simple, not cryptographically secure)
        uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
        frame.insert(frame.end(), mask, mask + 4);
        
        // Masked payload
        for (size_t i = 0; i < len; ++i) {
            frame.push_back(message[i] ^ mask[i % 4]);
        }
        
        ssize_t sent = send(socket_, reinterpret_cast<const char*>(frame.data()), frame.size(), 0);
        if (sent != static_cast<ssize_t>(frame.size())) {
            std::cerr << "[Debug] WebSocket send failed. Expected " << frame.size() 
                      << " bytes, sent " << sent << " bytes" << std::endl;
            if (sent == -1) {
#ifdef _WIN32
                std::cerr << "[Debug] Send error: " << WSAGetLastError() << std::endl;
#else
                std::cerr << "[Debug] Send error: " << strerror(errno) << std::endl;
#endif
                connected_ = false;
            }
            return false;
        }
        return true;
    }
    
    std::string receive_text() {
        if (!connected_) return "";
        
        char buffer[4096];
        ssize_t received = recv(socket_, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            if (received == 0) {
                std::cerr << "[Debug] Connection closed by server" << std::endl;
                connected_ = false;
            } else {
#ifdef _WIN32
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    std::cerr << "[Debug] recv() error: " << error << std::endl;
                    connected_ = false;
                }
#else
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "[Debug] recv() error: " << strerror(errno) << std::endl;
                    connected_ = false;
                }
#endif
            }
            return "";
        }
        
        std::cerr << "[Debug] received " << received << " bytes" << std::endl;
        
        // Simple frame parsing (assumes single frame, text message)
        if (received < 2) return "";
        
        uint8_t* data = reinterpret_cast<uint8_t*>(buffer);
        bool fin = (data[0] & 0x80) != 0;
        uint8_t opcode = data[0] & 0x0F;
        
        // Handle close frame
        if (opcode == 8) {
            std::cerr << "[Debug] Received WebSocket close frame" << std::endl;
            connected_ = false;
            return "";
        }
        
        // Handle ping frame
        if (opcode == 9) {
            std::cerr << "[Debug] Received WebSocket ping frame" << std::endl;
            // Should send pong, but for simplicity we'll just ignore
            return "";
        }
        
        // Handle pong frame
        if (opcode == 10) {
            std::cerr << "[Debug] Received WebSocket pong frame" << std::endl;
            return "";
        }
        
        if (opcode != 1) {
            std::cerr << "[Debug] Received non-text frame with opcode: " << static_cast<int>(opcode) << std::endl;
            return ""; // Not a text frame
        }
        
        bool masked = (data[1] & 0x80) != 0;
        uint64_t payload_len = data[1] & 0x7F;
        
        size_t header_len = 2;
        if (payload_len == 126) {
            if (received < 4) return "";
            payload_len = (data[2] << 8) | data[3];
            header_len = 4;
        } else if (payload_len == 127) {
            if (received < 10) return "";
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[2 + i];
            }
            header_len = 10;
        }
        
        if (masked) header_len += 4; // Mask key
        
        if (static_cast<size_t>(received) < header_len + payload_len) {
            std::cerr << "[Debug] Incomplete frame received. Got " << received 
                      << " bytes, need " << (header_len + payload_len) << std::endl;
            return "";
        }
        
        std::string message;
        size_t payload_start = header_len;
        
        if (masked) {
            uint8_t* mask = data + header_len - 4;
            for (size_t i = 0; i < payload_len; ++i) {
                message += static_cast<char>(data[payload_start + i] ^ mask[i % 4]);
            }
        } else {
            message.assign(reinterpret_cast<char*>(data + payload_start), payload_len);
        }
        
        return message;
    }
    
    bool is_connected() const { return connected_; }
    
private:
    bool perform_handshake() {
        // Send WebSocket handshake request
        std::string request = 
            "GET / HTTP/1.1\r\n"
            "Host: " + host_ + ":" + std::to_string(port_) + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";
        
        if (send(socket_, request.c_str(), request.length(), 0) != static_cast<int>(request.length())) {
            std::cerr << "Failed to send handshake request" << std::endl;
            return false;
        }
        
        // Receive handshake response
        char buffer[1024];
        ssize_t received = recv(socket_, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            std::cerr << "Failed to receive handshake response" << std::endl;
            return false;
        }
        
        buffer[received] = '\0';
        std::string response(buffer);
        
        // Check for successful handshake
        if (response.find("HTTP/1.1 101") == std::string::npos) {
            std::cerr << "WebSocket handshake failed" << std::endl;
            std::cerr << "Response: " << response << std::endl;
            return false;
        }
        
        return true;
    }
};

// Command-line parameters
struct client_params {
    std::string server_url = "http://127.0.0.1:8080";
    std::string inference_path = "/inference";
    std::string ws_url = "ws://127.0.0.1:8081";  // WebSocket URL
    bool use_websocket = false;  // Use WebSocket streaming instead of HTTP
    int32_t chunk_duration_ms = 3000;  // 3 seconds
    int32_t capture_id = -1;
    int32_t sample_rate = 16000;
    float vad_threshold = 0.01f;  // Much lower default threshold
    bool verbose = false;
    bool help = false;
    bool save_audio = false;  // Save audio chunks to files
    std::string audio_output_dir = "./audio_chunks";  // Directory to save audio files
};

void print_usage(int argc, char ** argv, const client_params & params) {
    fprintf(stdout, "\n");
    fprintf(stdout, "usage: %s [options]\n", argv[0]);
    fprintf(stdout, "\n");
    fprintf(stdout, "options:\n");
    fprintf(stdout, "  -h,        --help                    show this help message and exit\n");
    fprintf(stdout, "  -s URL,    --server URL              [%s] STT server URL\n", params.server_url.c_str());
    fprintf(stdout, "  -w URL,    --websocket URL           [%s] WebSocket server URL\n", params.ws_url.c_str());
    fprintf(stdout, "  --ws,      --use-websocket           [%s] use WebSocket streaming (default: HTTP)\n", params.use_websocket ? "true" : "false");
    fprintf(stdout, "  -d N,      --duration N              [%d] audio chunk duration in milliseconds\n", params.chunk_duration_ms);
    fprintf(stdout, "  -c N,      --capture-id N            [%d] capture device id (-1 for default)\n", params.capture_id);
    fprintf(stdout, "  -t N,      --threshold N             [%.3f] voice activity detection threshold\n", params.vad_threshold);
    fprintf(stdout, "  -v,        --verbose                 [%s] verbose output\n", params.verbose ? "true" : "false");
    fprintf(stdout, "  -r N,      --sample-rate N           [%d] audio sample rate\n", params.sample_rate);
    fprintf(stdout, "  --save-audio                         [%s] save audio chunks to files for debugging\n", params.save_audio ? "true" : "false");
    fprintf(stdout, "  --audio-dir DIR                      [%s] directory to save audio files\n", params.audio_output_dir.c_str());
    fprintf(stdout, "\n");
}

bool parse_params(int argc, char ** argv, client_params & params) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            params.help = true;
            return true;
        } else if (arg == "-s" || arg == "--server") {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing server URL\n");
                return false;
            }
            params.server_url = argv[i];
        } else if (arg == "-w" || arg == "--websocket") {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing WebSocket URL\n");
                return false;
            }
            params.ws_url = argv[i];
        } else if (arg == "--ws" || arg == "--use-websocket") {
            params.use_websocket = true;
        } else if (arg == "-d" || arg == "--duration") {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing duration value\n");
                return false;
            }
            params.chunk_duration_ms = std::atoi(argv[i]);
        } else if (arg == "-c" || arg == "--capture-id") {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing capture id\n");
                return false;
            }
            params.capture_id = std::atoi(argv[i]);
        } else if (arg == "-t" || arg == "--threshold") {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing threshold value\n");
                return false;
            }
            params.vad_threshold = std::atof(argv[i]);
        } else if (arg == "-r" || arg == "--sample-rate") {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing sample rate\n");
                return false;
            }
            params.sample_rate = std::atoi(argv[i]);
        } else if (arg == "-v" || arg == "--verbose") {
            params.verbose = true;
        } else if (arg == "--save-audio") {
            params.save_audio = true;
        } else if (arg == "--audio-dir") {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing audio directory\n");
                return false;
            }
            params.audio_output_dir = argv[i];
        } else {
            fprintf(stderr, "Error: unknown argument '%s'\n", arg.c_str());
            return false;
        }
    }

    return true;
}

// Convert float audio data to WAV format in memory
std::vector<uint8_t> create_wav_data(const std::vector<float>& audio_data, int sample_rate) {
    const int bits_per_sample = 16;
    const int num_channels = 1;
    const int data_size = audio_data.size() * sizeof(int16_t);
    
    WavHeader header;
    memcpy(header.chunk_id, "RIFF", 4);
    header.chunk_size = 36 + data_size;
    memcpy(header.format, "WAVE", 4);
    memcpy(header.subchunk1_id, "fmt ", 4);
    header.subchunk1_size = 16;
    header.audio_format = 1;  // PCM
    header.num_channels = num_channels;
    header.sample_rate = sample_rate;
    header.byte_rate = sample_rate * num_channels * bits_per_sample / 8;
    header.block_align = num_channels * bits_per_sample / 8;
    header.bits_per_sample = bits_per_sample;
    memcpy(header.subchunk2_id, "data", 4);
    header.subchunk2_size = data_size;

    std::vector<uint8_t> wav_data;
    wav_data.resize(sizeof(WavHeader) + data_size);
    
    // Copy header
    memcpy(wav_data.data(), &header, sizeof(WavHeader));
    
    // Convert and copy audio data
    int16_t* data_ptr = reinterpret_cast<int16_t*>(wav_data.data() + sizeof(WavHeader));
    for (size_t i = 0; i < audio_data.size(); ++i) {
        // Clamp to [-1, 1] and convert to 16-bit PCM
        float sample = std::max(-1.0f, std::min(1.0f, audio_data[i]));
        data_ptr[i] = static_cast<int16_t>(sample * 32767.0f);
    }
    
    return wav_data;
}

// Simple voice activity detection
bool detect_voice_activity(const std::vector<float>& audio_data, float threshold, bool verbose = false) {
    if (audio_data.empty()) return false;
    
    // Calculate RMS energy
    float sum_squares = 0.0f;
    for (float sample : audio_data) {
        sum_squares += sample * sample;
    }
    float rms = std::sqrt(sum_squares / audio_data.size());
    
    // Also calculate peak amplitude for better detection
    float peak = 0.0f;
    for (float sample : audio_data) {
        peak = std::max(peak, std::abs(sample));
    }
    
    if (verbose) {
        std::cout << "Audio analysis - RMS: " << std::fixed << std::setprecision(4) << rms 
                  << ", Peak: " << peak << ", Threshold: " << threshold << std::endl;
    }
    
    // Use either RMS or peak detection - whichever is more sensitive
    return (rms > threshold) || (peak > threshold * 2.0f);
}

// Get current timestamp string
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// Save audio chunk to WAV file
bool save_audio_chunk(const std::vector<float>& audio_data, int sample_rate, 
                     const std::string& filename) {
    try {
        auto wav_data = create_wav_data(audio_data, sample_rate);
        
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file for writing: " << filename << std::endl;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(wav_data.data()), wav_data.size());
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving audio file: " << e.what() << std::endl;
        return false;
    }
}

// Create directory if it doesn't exist
bool create_directory(const std::string& path) {
    // Simple directory creation using system call
    std::string command = "mkdir -p \"" + path + "\"";
    return system(command.c_str()) == 0;
}

// Send audio to server and get transcription
std::string send_audio_to_server(const std::vector<uint8_t>& wav_data, 
                                const client_params& params) {
    try {
        // Parse server URL
        std::string host = "127.0.0.1";
        int port = 8080;
        std::string protocol = "http";
        
        // Simple URL parsing
        size_t protocol_pos = params.server_url.find("://");
        if (protocol_pos != std::string::npos) {
            protocol = params.server_url.substr(0, protocol_pos);
            std::string remaining = params.server_url.substr(protocol_pos + 3);
            
            size_t port_pos = remaining.find(':');
            if (port_pos != std::string::npos) {
                host = remaining.substr(0, port_pos);
                std::string port_str = remaining.substr(port_pos + 1);
                size_t path_pos = port_str.find('/');
                if (path_pos != std::string::npos) {
                    port_str = port_str.substr(0, path_pos);
                }
                port = std::atoi(port_str.c_str());
            } else {
                size_t path_pos = remaining.find('/');
                if (path_pos != std::string::npos) {
                    host = remaining.substr(0, path_pos);
                } else {
                    host = remaining;
                }
            }
        }

        if (params.verbose) {
            std::cout << "Connecting to " << host << ":" << port << std::endl;
        }

        httplib::Client client(host, port);
        client.set_connection_timeout(5, 0); // 5 seconds
        client.set_read_timeout(30, 0);      // 30 seconds

        // Prepare multipart form data
        httplib::MultipartFormDataItems items = {
            {"response_format", "json", "", ""},
        };

        // Add the audio file
        std::string wav_content(wav_data.begin(), wav_data.end());
        items.push_back({"file", wav_content, "audio.wav", "audio/wav"});

        auto result = client.Post(params.inference_path, items);
        
        if (!result) {
            return "Error: Failed to connect to server";
        }
        
        if (result->status != 200) {
            return "Error: Server responded with status " + std::to_string(result->status);
        }

        // Parse JSON response to extract text
        std::string response = result->body;
        
        // Simple JSON parsing to extract "text" field
        size_t text_pos = response.find("\"text\"");
        if (text_pos != std::string::npos) {
            size_t colon_pos = response.find(":", text_pos);
            if (colon_pos != std::string::npos) {
                size_t quote_start = response.find("\"", colon_pos);
                if (quote_start != std::string::npos) {
                    quote_start++; // Skip the opening quote
                    size_t quote_end = quote_start;
                    
                    // Find the closing quote, handling escaped quotes
                    while (quote_end < response.length()) {
                        if (response[quote_end] == '\"' && 
                            (quote_end == 0 || response[quote_end - 1] != '\\')) {
                            break;
                        }
                        quote_end++;
                    }
                    
                    if (quote_end < response.length()) {
                        return response.substr(quote_start, quote_end - quote_start);
                    }
                }
            }
        }
        
        return "Error: Could not parse transcription from response";
        
    } catch (const std::exception& e) {
        return "Error: " + std::string(e.what());
    }
}

// WebSocket streaming client for real-time transcription
class WebSocketStreamer {
private:
    WebSocketClient client_;
    std::string ws_url_;
    bool verbose_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> session_active_{false};
    std::thread message_thread_;
    
public:
    WebSocketStreamer(const std::string& ws_url, bool verbose) 
        : ws_url_(ws_url), verbose_(verbose) {}
    
    ~WebSocketStreamer() {
        disconnect();
    }
    
    bool connect() {
        if (verbose_) {
            std::cout << "Connecting to WebSocket server: " << ws_url_ << std::endl;
        }
        
        if (!client_.connect(ws_url_)) {
            return false;
        }
        
        connected_ = true;
        
        // Start message receiving thread
        message_thread_ = std::thread([this]() {
            while (connected_ && client_.is_connected()) {
                std::string message = client_.receive_text();
                if (!message.empty()) {
                    if (verbose_) {
                        std::cout << "[Debug] Received message: " << message << std::endl;
                    }
                    handle_message(message);
                } else {
                    // Check if the client is still connected
                    if (!client_.is_connected()) {
                        std::cerr << "[Debug] WebSocket client disconnected" << std::endl;
                        connected_ = false;
                        session_active_ = false;
                        break;
                    }
                    
                    if (verbose_) {
                        // Reduce verbosity of empty message logs
                        static int empty_count = 0;
                        if (++empty_count % 100 == 0) {
                            std::cout << "[Debug] No messages for " << empty_count << " attempts" << std::endl;
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (verbose_) {
                std::cout << "[Debug] Message thread exiting - connected: " << connected_ 
                          << ", client connected: " << client_.is_connected() << std::endl;
            }
        });
        
        // Wait for session start message from server
        if (verbose_) {
            std::cout << "[Debug] Waiting for session start message..." << std::endl;
        }
        auto start_time = std::chrono::steady_clock::now();
        while (!session_active_ && 
               std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (verbose_) {
            std::cout << "[Debug] Session active: " << session_active_ << std::endl;
        }
        
        return session_active_;
    }
    
    void disconnect() {
        connected_ = false;
        session_active_ = false;
        
        if (message_thread_.joinable()) {
            message_thread_.join();
        }
        
        client_.disconnect();
    }
    
    bool send_audio_chunk(const std::vector<uint8_t>& wav_data) {
        if (!connected_ || !session_active_ || !client_.is_connected()) {
            if (!client_.is_connected()) {
                std::cerr << "[Debug] WebSocket client is no longer connected" << std::endl;
                connected_ = false;
                session_active_ = false;
            }
            return false;
        }
        
        if (verbose_) {
            std::cout << "[Debug] Sending audio chunk of " << wav_data.size() << " bytes" << std::endl;
        }
        
        // Start audio processing
        std::string start_msg = R"({"type": "audio.start"})";
        if (!client_.send_text(start_msg)) {
            std::cerr << "[Debug] Failed to send audio.start message" << std::endl;
            return false;
        }
        
        // Give the server time to process the start message
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Encode audio data as base64
        std::string base64_audio = Base64::encode(wav_data);
        
        if (verbose_) {
            std::cout << "[Debug] Encoded audio data to " << base64_audio.length() << " base64 characters" << std::endl;
        }
        
        // Send audio data
        std::string audio_msg = R"({"type": "audio.data", "data": ")" + base64_audio + R"("})";
        if (!client_.send_text(audio_msg)) {
            std::cerr << "[Debug] Failed to send audio.data message" << std::endl;
            return false;
        }
        
        // Give the server time to process the audio data
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // End audio processing
        std::string end_msg = R"({"type": "audio.end"})";
        if (!client_.send_text(end_msg)) {
            std::cerr << "[Debug] Failed to send audio.end message" << std::endl;
            return false;
        }
        
        return true;
    }
    
    bool is_connected() const {
        return connected_ && session_active_ && client_.is_connected();
    }
    
private:
    void handle_message(const std::string& message) {
        if (verbose_) {
            std::cout << "[WebSocket] Received: " << message << std::endl;
        }
        
        // Simple JSON parsing for message type
        size_t type_pos = message.find("\"type\"");
        if (type_pos == std::string::npos) return;
        
        size_t colon_pos = message.find(":", type_pos);
        if (colon_pos == std::string::npos) return;
        
        size_t quote_start = message.find("\"", colon_pos);
        if (quote_start == std::string::npos) return;
        quote_start++;
        
        size_t quote_end = message.find("\"", quote_start);
        if (quote_end == std::string::npos) return;
        
        std::string msg_type = message.substr(quote_start, quote_end - quote_start);
        
        if (msg_type == "session.start") {
            session_active_ = true;
            if (verbose_) {
                std::cout << "[WebSocket] Session started" << std::endl;
            }
        } else if (msg_type == "audio.started") {
            if (verbose_) {
                std::cout << "[WebSocket] Audio processing started" << std::endl;
            }
        } else if (msg_type == "audio.received") {
            if (verbose_) {
                std::cout << "[WebSocket] Audio data received by server" << std::endl;
            }
        } else if (msg_type == "audio.ended") {
            if (verbose_) {
                std::cout << "[WebSocket] Audio processing ended" << std::endl;
            }
        } else if (msg_type == "transcription.segment") {
            // Extract transcription text
            size_t text_pos = message.find("\"text\"");
            if (text_pos != std::string::npos) {
                size_t text_colon = message.find(":", text_pos);
                if (text_colon != std::string::npos) {
                    size_t text_quote_start = message.find("\"", text_colon);
                    if (text_quote_start != std::string::npos) {
                        text_quote_start++;
                        size_t text_quote_end = text_quote_start;
                        
                        // Find the end quote, handling escaped quotes
                        while (text_quote_end < message.length()) {
                            if (message[text_quote_end] == '\"' && 
                                (text_quote_end == 0 || message[text_quote_end - 1] != '\\')) {
                                break;
                            }
                            text_quote_end++;
                        }
                        
                        if (text_quote_end < message.length()) {
                            std::string transcription = message.substr(text_quote_start, text_quote_end - text_quote_start);
                            if (!transcription.empty()) {
                                std::cout << "[" << get_timestamp() << "] Real-time: \"" << transcription << "\"" << std::endl;
                            }
                        }
                    }
                }
            }
        }
    }
};

int main(int argc, char ** argv) {
    client_params params;

    if (!parse_params(argc, argv, params)) {
        print_usage(argc, argv, params);
        return 1;
    }

    if (params.help) {
        print_usage(argc, argv, params);
        return 0;
    }

    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "Whisper STT Client - Microphone Stream" << std::endl;
    std::cout << "Server: " << params.server_url << std::endl;
    if (params.use_websocket) {
        std::cout << "WebSocket: " << params.ws_url << " (streaming mode)" << std::endl;
    }
    std::cout << "Chunk duration: " << params.chunk_duration_ms << "ms" << std::endl;
    std::cout << "Sample rate: " << params.sample_rate << "Hz" << std::endl;
    std::cout << "VAD threshold: " << params.vad_threshold << std::endl;
    if (params.save_audio) {
        std::cout << "Audio saving: ENABLED -> " << params.audio_output_dir << std::endl;
        // Create output directory if it doesn't exist
        if (!create_directory(params.audio_output_dir)) {
            std::cerr << "Warning: Could not create audio output directory: " << params.audio_output_dir << std::endl;
        }
    }
    std::cout << std::endl;

    // Initialize audio capture
    std::cout << "Initializing audio capture..." << std::endl;
    
    audio_async audio(params.chunk_duration_ms);
    if (!audio.init(params.capture_id, params.sample_rate)) {
        fprintf(stderr, "Error: failed to initialize audio capture\n");
        return 1;
    }

    if (!audio.resume()) {
        fprintf(stderr, "Error: failed to start audio capture\n");
        return 1;
    }

    std::cout << "Audio device opened successfully" << std::endl;
    
    // Initialize WebSocket streaming if enabled
    std::unique_ptr<WebSocketStreamer> ws_streamer;
    if (params.use_websocket) {
        ws_streamer = std::make_unique<WebSocketStreamer>(params.ws_url, params.verbose);
        if (!ws_streamer->connect()) {
            std::cerr << "Error: failed to connect to WebSocket server" << std::endl;
            return 1;
        }
        std::cout << "WebSocket connection established" << std::endl;
    }
    
    std::cout << "Listening... (Press Ctrl+C to stop)" << std::endl;
    std::cout << std::endl;

    std::vector<float> audio_buffer;
    std::vector<float> accumulation_buffer;  // Buffer to accumulate fresh audio
    auto last_check_time = std::chrono::steady_clock::now();
    const int check_interval_ms = 100;  // Check every 100ms for new audio
    size_t last_audio_len = 0;  // Track how much audio we've processed
    int chunk_counter = 0;  // Counter for saved audio files
    
    while (g_running) {
        auto current_time = std::chrono::steady_clock::now();
        auto time_since_last_check = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_check_time).count();
        
        if (time_since_last_check >= check_interval_ms) {
            // Get all available audio data
            audio.get(params.chunk_duration_ms * 2, audio_buffer);  // Get more than needed to ensure we have fresh data
            
            if (!audio_buffer.empty() && audio_buffer.size() > last_audio_len) {
                // Extract only the new audio samples since last check
                std::vector<float> new_audio_samples;
                if (last_audio_len < audio_buffer.size()) {
                    new_audio_samples.assign(
                        audio_buffer.begin() + last_audio_len, 
                        audio_buffer.end()
                    );
                }
                
                if (!new_audio_samples.empty()) {
                    // Append new audio to accumulation buffer
                    accumulation_buffer.insert(accumulation_buffer.end(), 
                                              new_audio_samples.begin(), new_audio_samples.end());
                    
                    if (params.verbose) {
                        std::cout << "[" << get_timestamp() << "] Collected " << new_audio_samples.size() 
                                  << " new samples, total accumulated: " << accumulation_buffer.size() << std::endl;
                    }
                }
                
                // Update the last processed length
                last_audio_len = audio_buffer.size();
                
                // If we have enough audio data for a chunk, check if we should send
                int samples_needed = (params.sample_rate * params.chunk_duration_ms) / 1000;
                if (accumulation_buffer.size() >= samples_needed) {
                    // Take exactly the number of samples needed for this chunk
                    std::vector<float> chunk_to_send(accumulation_buffer.begin(), 
                                                   accumulation_buffer.begin() + samples_needed);
                    
                    // Check voice activity for this specific chunk
                    bool has_voice = detect_voice_activity(chunk_to_send, params.vad_threshold, params.verbose);
                    
                    // Save audio chunk if requested (regardless of voice activity)
                    if (params.save_audio) {
                        chunk_counter++;
                        std::string filename = params.audio_output_dir + "/chunk_" + 
                                             std::to_string(chunk_counter) + "_" + 
                                             (has_voice ? "voice" : "silence") + ".wav";
                        
                        if (save_audio_chunk(chunk_to_send, params.sample_rate, filename)) {
                            std::cout << "[" << get_timestamp() << "] Saved audio chunk: " << filename << std::endl;
                        } else {
                            std::cout << "[" << get_timestamp() << "] Failed to save audio chunk: " << filename << std::endl;
                        }
                    }
                    
                    if (has_voice) {
                        std::cout << "[" << get_timestamp() << "] Sending audio chunk (" 
                                  << (params.chunk_duration_ms / 1000.0f) << "s)..." << std::endl;

                        // Convert to WAV format
                        auto wav_data = create_wav_data(chunk_to_send, params.sample_rate);
                        
                        if (params.use_websocket && ws_streamer) {
                            // Check if WebSocket connection is still alive
                            if (!ws_streamer->is_connected()) {
                                std::cout << "[" << get_timestamp() << "] WebSocket disconnected, attempting to reconnect..." << std::endl;
                                ws_streamer->disconnect();
                                if (ws_streamer->connect()) {
                                    std::cout << "[" << get_timestamp() << "] WebSocket reconnection successful" << std::endl;
                                } else {
                                    std::cout << "[" << get_timestamp() << "] WebSocket reconnection failed, falling back to HTTP" << std::endl;
                                }
                            }
                            
                            // Send via WebSocket for real-time streaming
                            if (ws_streamer->is_connected()) {
                                if (!ws_streamer->send_audio_chunk(wav_data)) {
                                    std::cout << "[" << get_timestamp() << "] Error: Failed to send audio chunk via WebSocket" << std::endl;
                                }
                                // Note: Transcription will be received asynchronously in the WebSocket message handler
                            } else {
                                // Fallback to HTTP if WebSocket is not available
                                std::cout << "[" << get_timestamp() << "] Falling back to HTTP for this chunk..." << std::endl;
                                std::string transcription = send_audio_to_server(wav_data, params);
                                
                                if (!transcription.empty() && transcription.find("Error:") != 0) {
                                    std::cout << "[" << get_timestamp() << "] Transcription: \"" 
                                              << transcription << "\"" << std::endl;
                                } else {
                                    std::cout << "[" << get_timestamp() << "] " << transcription << std::endl;
                                }
                            }
                        } else {
                            // Send via HTTP
                            std::string transcription = send_audio_to_server(wav_data, params);
                            
                            if (!transcription.empty() && transcription.find("Error:") != 0) {
                                std::cout << "[" << get_timestamp() << "] Transcription: \"" 
                                          << transcription << "\"" << std::endl;
                            } else {
                                std::cout << "[" << get_timestamp() << "] " << transcription << std::endl;
                            }
                        }
                        
                        std::cout << std::endl;
                    } else {
                        if (params.verbose) {
                            std::cout << "[" << get_timestamp() << "] No voice activity in chunk, skipping" << std::endl;
                        }
                    }
                    
                    // Remove the processed samples from accumulation buffer
                    accumulation_buffer.erase(accumulation_buffer.begin(), 
                                            accumulation_buffer.begin() + samples_needed);
                    
                    // Reset the audio length tracking since we've consumed data
                    last_audio_len = 0;
                    audio.clear();  // Clear the circular buffer to start fresh
                }
            }
            
            last_check_time = current_time;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    audio.pause();
    std::cout << "Audio capture stopped." << std::endl;
    
    return 0;
}
