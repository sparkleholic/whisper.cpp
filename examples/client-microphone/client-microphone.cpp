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

// Command-line parameters
struct client_params {
    std::string server_url = "http://127.0.0.1:8080";
    std::string inference_path = "/inference";
    int32_t chunk_duration_ms = 3000;  // 3 seconds
    int32_t capture_id = -1;
    int32_t sample_rate = 16000;
    float vad_threshold = 0.6f;
    bool verbose = false;
    bool help = false;
};

void print_usage(int argc, char ** argv, const client_params & params) {
    fprintf(stdout, "\n");
    fprintf(stdout, "usage: %s [options]\n", argv[0]);
    fprintf(stdout, "\n");
    fprintf(stdout, "options:\n");
    fprintf(stdout, "  -h,        --help                    show this help message and exit\n");
    fprintf(stdout, "  -s URL,    --server URL              [%s] STT server URL\n", params.server_url.c_str());
    fprintf(stdout, "  -d N,      --duration N              [%d] audio chunk duration in milliseconds\n", params.chunk_duration_ms);
    fprintf(stdout, "  -c N,      --capture-id N            [%d] capture device id (-1 for default)\n", params.capture_id);
    fprintf(stdout, "  -t N,      --threshold N             [%.1f] voice activity detection threshold\n", params.vad_threshold);
    fprintf(stdout, "  -v,        --verbose                 [%s] verbose output\n", params.verbose ? "true" : "false");
    fprintf(stdout, "  -r N,      --sample-rate N           [%d] audio sample rate\n", params.sample_rate);
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
bool detect_voice_activity(const std::vector<float>& audio_data, float threshold) {
    if (audio_data.empty()) return false;
    
    // Calculate RMS energy
    float sum_squares = 0.0f;
    for (float sample : audio_data) {
        sum_squares += sample * sample;
    }
    float rms = std::sqrt(sum_squares / audio_data.size());
    
    return rms > threshold;
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
    std::cout << "Chunk duration: " << params.chunk_duration_ms << "ms" << std::endl;
    std::cout << "Sample rate: " << params.sample_rate << "Hz" << std::endl;
    std::cout << "VAD threshold: " << params.vad_threshold << std::endl;
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
    std::cout << "Listening... (Press Ctrl+C to stop)" << std::endl;
    std::cout << std::endl;

    std::vector<float> audio_buffer;
    
    while (g_running) {
        // Get audio data
        audio.get(params.chunk_duration_ms, audio_buffer);
        
        if (audio_buffer.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Simple voice activity detection
        if (!detect_voice_activity(audio_buffer, params.vad_threshold)) {
            if (params.verbose) {
                std::cout << "[" << get_timestamp() << "] No voice activity detected" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::cout << "[" << get_timestamp() << "] Sending audio chunk (" 
                  << (params.chunk_duration_ms / 1000.0f) << "s)..." << std::endl;

        // Convert to WAV format
        auto wav_data = create_wav_data(audio_buffer, params.sample_rate);
        
        // Send to server
        std::string transcription = send_audio_to_server(wav_data, params);
        
        if (!transcription.empty() && transcription.find("Error:") != 0) {
            std::cout << "[" << get_timestamp() << "] Transcription: \"" 
                      << transcription << "\"" << std::endl;
        } else {
            std::cout << "[" << get_timestamp() << "] " << transcription << std::endl;
        }
        
        std::cout << std::endl;
        
        // Small delay to prevent overwhelming the server
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    audio.pause();
    std::cout << "Audio capture stopped." << std::endl;
    
    return 0;
}
