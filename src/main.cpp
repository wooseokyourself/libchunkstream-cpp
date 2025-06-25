#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601  // Windows 7
#endif
#endif

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <random>
#include <atomic>
#include <iomanip>
#include <cstring>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <algorithm>
#include <numeric>

#include "chunkstream/sender.h"
#include "chunkstream/receiver.h"

using namespace chunkstream;

// Test configuration
constexpr int TEST_PORT = 56343;
constexpr int TEST_MTU = 1500;
constexpr size_t TEST_BUFFER_SIZE = 100;
constexpr size_t MAX_DATA_SIZE = 2464 * 2064; // 5.09MB
const std::string TEST_IP = "127.0.0.1";

// Data integrity verification structures
struct DataFrameInfo {
    uint32_t frame_id;
    std::vector<uint8_t> original_data;
    std::chrono::steady_clock::time_point send_time;
    size_t checksum;
};

struct ReceivedFrameInfo {
    uint32_t frame_id;
    std::vector<uint8_t> received_data;
    std::chrono::steady_clock::time_point receive_time;
    size_t checksum;
    bool is_valid;
};

// Global data verification storage
std::unordered_map<uint32_t, DataFrameInfo> sent_frames;
std::unordered_map<uint32_t, ReceivedFrameInfo> received_frames;
std::mutex verification_mutex;

// Sender statistics
struct SenderStats {
    std::atomic<size_t> frames_sent{0};
    std::atomic<size_t> bytes_sent{0};
    std::atomic<double> average_fps{0};
    std::atomic<double> average_mbps{0};
    std::chrono::steady_clock::time_point start_time;
};

// Receiver statistics
struct ReceiverStats {
    std::atomic<size_t> frames_received{0};
    std::atomic<size_t> bytes_received{0};
    std::atomic<size_t> frames_dropped{0};
    std::atomic<size_t> frames_valid{0};
    std::atomic<size_t> frames_corrupted{0};
    std::atomic<double> average_fps{0};
    std::atomic<double> average_mbps{0};
    std::atomic<double> drop_rate{0};
    std::atomic<double> corruption_rate{0};
    std::chrono::steady_clock::time_point start_time;
};

// Global statistics
SenderStats sender_stats;
ReceiverStats receiver_stats;
std::atomic<bool> test_running{true};
std::atomic<uint32_t> global_frame_id{0};

// ANSI escape codes for console control
namespace Console {
    const std::string CLEAR_LINE = "\033[2K\r";
    std::string CURSOR_UP(int n) { return "\033[" + std::to_string(n) + "A"; }
    std::string CURSOR_DOWN(int n) { return "\033[" + std::to_string(n) + "B"; }
    const std::string SAVE_POSITION = "\033[s";
    const std::string RESTORE_POSITION = "\033[u";
    const std::string GREEN = "\033[32m";
    const std::string RED = "\033[31m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
}

// Simple checksum calculation for data verification
size_t CalculateChecksum(const std::vector<uint8_t>& data) {
    size_t checksum = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        checksum ^= static_cast<size_t>(data[i]) << (i % 8);
        checksum = (checksum << 1) | (checksum >> (sizeof(size_t) * 8 - 1)); // Rotate left
    }
    return checksum;
}

// Generate deterministic test data with frame ID embedded
std::vector<uint8_t> GenerateTestData(size_t size, uint32_t frame_id) {
    std::vector<uint8_t> data(size);
    
    // Use frame_id as seed for reproducible data generation
    std::mt19937 gen(frame_id);
    std::uniform_int_distribution<int> dis(0, 255);
    
    // Embed frame_id at the beginning of data for verification
    if (size >= sizeof(uint32_t)) {
        std::memcpy(data.data(), &frame_id, sizeof(uint32_t));
    }
    
    // Fill rest with deterministic random data
    for (size_t i = sizeof(uint32_t); i < size; ++i) {
        data[i] = static_cast<uint8_t>(dis(gen));
    }
    
    return data;
}

// Verify received data integrity
bool VerifyDataIntegrity(const std::vector<uint8_t>& received_data, uint32_t expected_frame_id) {
    if (received_data.size() < sizeof(uint32_t)) {
        std::cerr << "[VERIFY] Data too small: " << received_data.size() << " bytes" << std::endl;
        return false;
    }
    
    // Extract frame_id from received data
    uint32_t embedded_frame_id;
    std::memcpy(&embedded_frame_id, received_data.data(), sizeof(uint32_t));
    
    //std::cout << "[VERIFY] Expected ID: " << expected_frame_id << ", Embedded ID: " << embedded_frame_id << std::endl;
    
    if (embedded_frame_id != expected_frame_id) {
        std::cerr << "[VERIFY] Frame ID mismatch! Expected: " << expected_frame_id 
                  << ", Got: " << embedded_frame_id << std::endl;
        return false;
    }
    
    // Generate expected data and compare
    std::vector<uint8_t> expected_data = GenerateTestData(received_data.size(), expected_frame_id);
    
    if (received_data.size() != expected_data.size()) {
        std::cerr << "[VERIFY] Size mismatch! Expected: " << expected_data.size() 
                  << ", Got: " << received_data.size() << std::endl;
        return false;
    }
    
    // Compare byte by byte (sample first 100 bytes for debug)
    size_t mismatch_count = 0;
    for (size_t i = 0; i < std::min(received_data.size(), static_cast<size_t>(100)); ++i) {
        if (received_data[i] != expected_data[i]) {
            if (mismatch_count < 5) { // Only print first 5 mismatches
                std::cerr << "[VERIFY] Data mismatch at byte " << i << "! Expected: " 
                          << static_cast<int>(expected_data[i]) 
                          << ", Got: " << static_cast<int>(received_data[i]) << std::endl;
            }
            mismatch_count++;
        }
    }
    
    if (mismatch_count > 0) {
        std::cerr << "[VERIFY] Total mismatches in first 100 bytes: " << mismatch_count << std::endl;
        return false;
    }
    
    //std::cout << "[VERIFY] Frame " << expected_frame_id << " verified successfully" << std::endl;
    return true;
}

// Enhanced receiver callback function with data verification
void OnDataReceived(const std::vector<uint8_t>& data, std::function<void()> release) {
    auto receive_time = std::chrono::steady_clock::now();
    
    receiver_stats.frames_received++;
    receiver_stats.bytes_received += data.size();
    
    // Extract frame ID from received data for verification
    uint32_t frame_id = 0;
    if (data.size() >= sizeof(uint32_t)) {
        std::memcpy(&frame_id, data.data(), sizeof(uint32_t));
    }
    
    // Debug: Print frame ID for verification
    //std::cout << "[DEBUG] Received frame ID: " << frame_id << ", size: " << data.size() << std::endl;
    
    // Verify data integrity
    bool is_valid = VerifyDataIntegrity(data, frame_id);
    size_t checksum = CalculateChecksum(data);
    
    // Debug: Print verification result
    //std::cout << "[DEBUG] Frame " << frame_id << " verification: " << (is_valid ? "VALID" : "CORRUPTED") << std::endl;
    
    {
        std::lock_guard<std::mutex> lock(verification_mutex);
        
        ReceivedFrameInfo received_info;
        received_info.frame_id = frame_id;
        received_info.received_data = data;
        received_info.receive_time = receive_time;
        received_info.checksum = checksum;
        received_info.is_valid = is_valid;
        
        received_frames[frame_id] = std::move(received_info);
        
        if (is_valid) {
            receiver_stats.frames_valid++;
        } else {
            receiver_stats.frames_corrupted++;
            std::cerr << "Data corruption detected in frame " << frame_id << "!" << std::endl;
        }
    }
    
    // Release the buffer
    release();
}

// Enhanced sender statistics printer
void PrintSenderStats() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - sender_stats.start_time).count();
    
    if (elapsed_seconds > 0) {
        double fps = static_cast<double>(sender_stats.frames_sent) / elapsed_seconds;
        double mbps = static_cast<double>(sender_stats.bytes_sent) / (elapsed_seconds * 1024 * 1024);
        
        sender_stats.average_fps.store(fps);
        sender_stats.average_mbps.store(mbps);
        
        std::cout << Console::CLEAR_LINE
                  << Console::BLUE << Console::BOLD << "SENDER STATS: " << Console::RESET
                  << "Frames: " << sender_stats.frames_sent 
                  << " | Bytes: " << (sender_stats.bytes_sent / 1024) << " KB"
                  << " | " << std::fixed << std::setprecision(2) << fps << " fps"
                  << " | " << mbps << " MB/s";
        std::cout.flush();
    }
}

// Enhanced receiver statistics printer with integrity information
void PrintReceiverStats(const Receiver* receiver = nullptr) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - receiver_stats.start_time).count();
    
    if (elapsed_seconds > 0) {
        double fps = static_cast<double>(receiver_stats.frames_received) / elapsed_seconds;
        double mbps = static_cast<double>(receiver_stats.bytes_received) / (elapsed_seconds * 1024 * 1024);
        
        receiver_stats.average_fps.store(fps);
        receiver_stats.average_mbps.store(mbps);
        
        // Get dropped frames from receiver if available
        size_t frames_dropped = 0;
        if (receiver) {
            frames_dropped = receiver->GetDropCount();
            receiver_stats.frames_dropped.store(frames_dropped);
        } else {
            frames_dropped = receiver_stats.frames_dropped.load();
        }
        
        // Calculate drop rate
        double drop_rate = 0.0;
        if (receiver_stats.frames_received + frames_dropped > 0) {
            drop_rate = static_cast<double>(frames_dropped) / 
                        (receiver_stats.frames_received + frames_dropped) * 100.0;
            receiver_stats.drop_rate.store(drop_rate);
        }
        
        // Calculate corruption rate
        double corruption_rate = 0.0;
        if (receiver_stats.frames_received > 0) {
            corruption_rate = static_cast<double>(receiver_stats.frames_corrupted) / 
                             receiver_stats.frames_received * 100.0;
            receiver_stats.corruption_rate.store(corruption_rate);
        }
        
        std::cout << Console::CLEAR_LINE
                  << Console::GREEN << Console::BOLD << "RECEIVER STATS: " << Console::RESET
                  << "Frames: " << receiver_stats.frames_received 
                  << " | Valid: " << receiver_stats.frames_valid
                  << " | Corrupted: " << receiver_stats.frames_corrupted
                  << " | Bytes: " << (receiver_stats.bytes_received / 1024) << " KB"
                  << " | " << std::fixed << std::setprecision(2) << fps << " fps"
                  << " | " << mbps << " MB/s"
                  << " | " << Console::RED << "Dropped: " << frames_dropped 
                  << " (" << drop_rate << "%)"
                  << " | Corrupt: (" << corruption_rate << "%)" << Console::RESET;
        std::cout.flush();
    }
}

// Print detailed verification results
void PrintVerificationResults() {
    std::lock_guard<std::mutex> lock(verification_mutex);
    
    std::cout << "\n" << Console::BOLD << "=== DATA INTEGRITY VERIFICATION RESULTS ===" << Console::RESET << std::endl;
    
    size_t total_sent = sent_frames.size();
    size_t total_received = received_frames.size();
    size_t valid_frames = 0;
    size_t corrupted_frames = 0;
    size_t missing_frames = 0;
    
    // Check each sent frame
    for (const auto& [frame_id, sent_info] : sent_frames) {
        auto received_it = received_frames.find(frame_id);
        if (received_it != received_frames.end()) {
            if (received_it->second.is_valid) {
                valid_frames++;
            } else {
                corrupted_frames++;
            }
        } else {
            missing_frames++;
        }
    }
    
    // Check for unexpected frames (should not happen with our test)
    size_t unexpected_frames = 0;
    for (const auto& [frame_id, received_info] : received_frames) {
        if (sent_frames.find(frame_id) == sent_frames.end()) {
            unexpected_frames++;
        }
    }
    
    std::cout << Console::BLUE << "Total frames sent: " << Console::RESET << total_sent << std::endl;
    std::cout << Console::GREEN << "Valid frames received: " << Console::RESET << valid_frames << std::endl;
    std::cout << Console::RED << "Corrupted frames: " << Console::RESET << corrupted_frames << std::endl;
    std::cout << Console::YELLOW << "Missing frames: " << Console::RESET << missing_frames << std::endl;
    std::cout << Console::RED << "Unexpected frames: " << Console::RESET << unexpected_frames << std::endl;
    
    if (total_sent > 0) {
        double success_rate = static_cast<double>(valid_frames) / total_sent * 100.0;
        double loss_rate = static_cast<double>(missing_frames) / total_sent * 100.0;
        double corruption_rate = static_cast<double>(corrupted_frames) / total_sent * 100.0;
        
        std::cout << "\n" << Console::BOLD << "Success Rate: " << Console::RESET 
                  << Console::GREEN << std::fixed << std::setprecision(2) << success_rate << "%" << Console::RESET << std::endl;
        std::cout << Console::BOLD << "Loss Rate: " << Console::RESET 
                  << Console::RED << loss_rate << "%" << Console::RESET << std::endl;
        std::cout << Console::BOLD << "Corruption Rate: " << Console::RESET 
                  << Console::RED << corruption_rate << "%" << Console::RESET << std::endl;
        
        // Print some timing statistics if we have data
        if (valid_frames > 0) {
            std::vector<double> latencies;
            for (const auto& [frame_id, sent_info] : sent_frames) {
                auto received_it = received_frames.find(frame_id);
                if (received_it != received_frames.end() && received_it->second.is_valid) {
                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                        received_it->second.receive_time - sent_info.send_time).count() / 1000.0;
                    latencies.push_back(latency);
                }
            }
            
            if (!latencies.empty()) {
                std::sort(latencies.begin(), latencies.end());
                double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
                double min_latency = latencies.front();
                double max_latency = latencies.back();
                double median_latency = latencies[latencies.size() / 2];
                
                std::cout << "\n" << Console::BOLD << "Latency Statistics (ms):" << Console::RESET << std::endl;
                std::cout << "  Average: " << std::fixed << std::setprecision(2) << avg_latency << " ms" << std::endl;
                std::cout << "  Median: " << median_latency << " ms" << std::endl;
                std::cout << "  Min: " << min_latency << " ms" << std::endl;
                std::cout << "  Max: " << max_latency << " ms" << std::endl;
            }
        }
    }
    
    // Overall test result
    std::cout << "\n" << Console::BOLD;
    if (corrupted_frames == 0 && unexpected_frames == 0) {
        std::cout << Console::GREEN << "✓ DATA INTEGRITY TEST PASSED!" << Console::RESET << std::endl;
    } else {
        std::cout << Console::RED << "✗ DATA INTEGRITY TEST FAILED!" << Console::RESET << std::endl;
    }
    std::cout << Console::RESET;
}

// Combined mode test with both sender and receiver and data verification
void CombinedTest() {
    // Initialize start times
    sender_stats.start_time = std::chrono::steady_clock::now();
    receiver_stats.start_time = std::chrono::steady_clock::now();
    
    // Start receiver in separate thread
    std::thread receiver_thread([]() {
        try {
            Receiver receiver(
                TEST_PORT,
                OnDataReceived,
                TEST_MTU,
                TEST_BUFFER_SIZE,
                MAX_DATA_SIZE
            );
            
            // Stats update thread
            std::thread stats_thread([&receiver]() {
                while (test_running) {
                    std::cout << Console::RESTORE_POSITION;
                    PrintReceiverStats(&receiver);
                    std::cout << "\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            });
            
            receiver.Start();
            
            if (stats_thread.joinable()) {
                stats_thread.join();
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Receiver error: " << e.what() << std::endl;
        }
    });
    
    // Give receiver time to start
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Start sender in separate thread
    std::thread sender_thread([]() {
        try {
            Sender sender(TEST_IP, TEST_PORT, TEST_MTU, TEST_BUFFER_SIZE, MAX_DATA_SIZE);
            
            // Start sender
            std::thread sender_service_thread([&sender]() {
                sender.Start();
            });
            
            // Print header for sender stats
            std::cout << Console::SAVE_POSITION;
            PrintSenderStats();
            std::cout << "\n";
            PrintReceiverStats();
            std::cout << "\n";
            
            // Send data continuously with verification tracking
            while (test_running) {
                uint32_t frame_id = global_frame_id.fetch_add(1);
                std::vector<uint8_t> test_data = GenerateTestData(MAX_DATA_SIZE, frame_id);
                auto send_time = std::chrono::steady_clock::now();
                
                // Store sent frame info for verification
                {
                    std::lock_guard<std::mutex> lock(verification_mutex);
                    DataFrameInfo sent_info;
                    sent_info.frame_id = frame_id;
                    sent_info.original_data = test_data;
                    sent_info.send_time = send_time;
                    sent_info.checksum = CalculateChecksum(test_data);
                    sent_frames[frame_id] = std::move(sent_info);
                }
                
                sender.Send(test_data.data(), test_data.size());
                
                sender_stats.frames_sent++;
                sender_stats.bytes_sent += test_data.size();
                
                // Update sender statistics display
                std::cout << Console::RESTORE_POSITION;
                PrintSenderStats();
                std::cout << "\n";
                
                // Send every 100ms
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            sender.Stop();
            if (sender_service_thread.joinable()) {
                sender_service_thread.join();
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Sender error: " << e.what() << std::endl;
        }
    });
    
    // Wait for user input to stop
    std::cout << Console::YELLOW << "\n\nPress Enter to stop the test..." << Console::RESET << std::endl;
    std::cin.get();
    
    test_running = false;
    
    // Wait for threads to finish
    if (sender_thread.joinable()) {
        sender_thread.join();
    }
    
    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }
    
    // Print final statistics
    std::cout << "\n" << Console::BOLD << "Final Statistics:" << Console::RESET << std::endl;
    
    std::cout << Console::BLUE << "Sender:" << Console::RESET << std::endl;
    std::cout << "  Total frames sent: " << sender_stats.frames_sent << std::endl;
    std::cout << "  Total data sent: " << (sender_stats.bytes_sent / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "  Average rate: " << sender_stats.average_fps << " fps / " 
              << sender_stats.average_mbps << " MB/s" << std::endl;
    
    std::cout << Console::GREEN << "Receiver:" << Console::RESET << std::endl;
    std::cout << "  Total frames received: " << receiver_stats.frames_received << std::endl;
    std::cout << "  Valid frames: " << receiver_stats.frames_valid << std::endl;
    std::cout << "  Corrupted frames: " << receiver_stats.frames_corrupted << std::endl;
    std::cout << "  Total data received: " << (receiver_stats.bytes_received / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "  Frames dropped: " << receiver_stats.frames_dropped 
              << " (" << receiver_stats.drop_rate << "%)" << std::endl;
    std::cout << "  Average rate: " << receiver_stats.average_fps << " fps / " 
              << receiver_stats.average_mbps << " MB/s" << std::endl;
    
    if (sender_stats.frames_sent > 0) {
        double transmission_success = static_cast<double>(receiver_stats.frames_received) / sender_stats.frames_sent * 100.0;
        std::cout << "\n" << Console::YELLOW << "Transmission success rate: " << std::fixed 
                  << std::setprecision(2) << transmission_success << "%" << Console::RESET << std::endl;
    }
    
    // Print detailed verification results
    PrintVerificationResults();
}

// Sender test thread (unchanged)
void SenderTest() {
    try {
        std::cout << "Starting sender on " << TEST_IP << ":" << TEST_PORT << std::endl;
        
        Sender sender(TEST_IP, TEST_PORT, TEST_MTU, TEST_BUFFER_SIZE, MAX_DATA_SIZE);
        
        // Start sender in a separate thread
        std::thread sender_thread([&sender]() {
            sender.Start();
        });
        
        // Give some time for initialization
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Initialize start time for statistics
        sender_stats.start_time = std::chrono::steady_clock::now();
        
        // Print header
        std::cout << "\n" << Console::SAVE_POSITION;
        PrintSenderStats();
        std::cout << std::endl;
        
        // Send data continuously
        while (test_running) {
            uint32_t frame_id = global_frame_id.fetch_add(1);
            std::vector<uint8_t> data = GenerateTestData(MAX_DATA_SIZE, frame_id);
            
            sender.Send(data.data(), data.size());
            
            sender_stats.frames_sent++;
            sender_stats.bytes_sent += data.size();
            
            // Update statistics display
            std::cout << Console::RESTORE_POSITION;
            PrintSenderStats();
            
            // Send every 100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Final stats
        std::cout << "\nFinal Sender Statistics:" << std::endl;
        std::cout << "  Total frames sent: " << sender_stats.frames_sent << std::endl;
        std::cout << "  Total data sent: " << (sender_stats.bytes_sent / (1024.0 * 1024.0)) << " MB" << std::endl;
        std::cout << "  Average rate: " << sender_stats.average_fps << " fps / " 
                  << sender_stats.average_mbps << " MB/s" << std::endl;
        
        sender.Stop();
        sender_thread.join();
        
    } catch (const std::exception& e) {
        std::cerr << "Sender error: " << e.what() << std::endl;
    }
}

// Receiver test thread (with basic verification)
void ReceiverTest() {
    try {
        std::cout << "Starting receiver on port " << TEST_PORT << std::endl;
        
        Receiver receiver(
            TEST_PORT,
            OnDataReceived,
            TEST_MTU,
            TEST_BUFFER_SIZE,
            MAX_DATA_SIZE
        );
        
        // Initialize start time for statistics
        receiver_stats.start_time = std::chrono::steady_clock::now();
        
        // Print header
        std::cout << "\n" << Console::SAVE_POSITION;
        PrintReceiverStats(&receiver);
        std::cout << std::endl;
        
        // Stats update thread
        std::thread stats_thread([&receiver]() {
            while (test_running) {
                std::cout << Console::RESTORE_POSITION;
                PrintReceiverStats(&receiver);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
        
        // Start receiver (this will block)
        receiver.Start();
        
        // If we get here, the receiver has stopped
        test_running = false;
        if (stats_thread.joinable()) {
            stats_thread.join();
        }
        
        // Final stats
        std::cout << "\nFinal Receiver Statistics:" << std::endl;
        std::cout << "  Total frames received: " << receiver_stats.frames_received << std::endl;
        std::cout << "  Valid frames: " << receiver_stats.frames_valid << std::endl;
        std::cout << "  Corrupted frames: " << receiver_stats.frames_corrupted << std::endl;
        std::cout << "  Total data received: " << (receiver_stats.bytes_received / (1024.0 * 1024.0)) << " MB" << std::endl;
        std::cout << "  Frames dropped: " << receiver_stats.frames_dropped 
                  << " (" << receiver_stats.drop_rate << "%)" << std::endl;
        std::cout << "  Average rate: " << receiver_stats.average_fps << " fps / " 
                  << receiver_stats.average_mbps << " MB/s" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Receiver error: " << e.what() << std::endl;
    }
}

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [sender|receiver|both]" << std::endl;
    std::cout << "  sender   - Run as sender only" << std::endl;
    std::cout << "  receiver - Run as receiver only" << std::endl;
    std::cout << "  both     - Run both sender and receiver with data integrity verification (default)" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << Console::BOLD << "ChunkStream Data Integrity Test Application" << Console::RESET << std::endl;
    std::cout << "=============================================" << std::endl;
    
    std::string mode = "both";
    if (argc > 1) {
        mode = argv[1];
    }
    
    if (mode != "sender" && mode != "receiver" && mode != "both") {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::cout << "Test configuration:" << std::endl;
    std::cout << "  Mode: " << mode << std::endl;
    std::cout << "  IP: " << TEST_IP << std::endl;
    std::cout << "  Port: " << TEST_PORT << std::endl;
    std::cout << "  MTU: " << TEST_MTU << std::endl;
    std::cout << "  Buffer size: " << TEST_BUFFER_SIZE << std::endl;
    std::cout << "  Max data size: " << MAX_DATA_SIZE << " bytes (" 
              << (MAX_DATA_SIZE / (1024.0 * 1024.0)) << " MB)" << std::endl;
    
    if (mode == "both") {
        std::cout << "  " << Console::BOLD << "Data integrity verification: ENABLED" << Console::RESET << std::endl;
    }
    std::cout << std::endl;
    
    try {
        if (mode == "sender") {
            SenderTest();
        } else if (mode == "receiver") {
            ReceiverTest();
        } else if (mode == "both") {
            CombinedTest();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        test_running = false;
    }
    
    return 0;
}