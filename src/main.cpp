#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <random>
#include <atomic>
#include <iomanip>
#include <cstring>

#include "chunkstream/sender.h"
#include "chunkstream/receiver.h"

using namespace chunkstream;

// Test configuration
constexpr int TEST_PORT = 12345;
constexpr int TEST_MTU = 1500;
constexpr size_t TEST_BUFFER_SIZE = 10;
constexpr size_t MAX_DATA_SIZE = 1024 * 1024; // 1MB
const std::string TEST_IP = "127.0.0.1";

// Test statistics
std::atomic<size_t> total_sent{0};
std::atomic<size_t> total_received{0};
std::atomic<size_t> total_bytes_sent{0};
std::atomic<size_t> total_bytes_received{0};
std::atomic<bool> test_running{true};

// Generate test data
std::vector<uint8_t> GenerateTestData(size_t size) {
    std::vector<uint8_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    // uint8_t 대신 int 사용하고 범위 지정
    std::uniform_int_distribution<int> dis(0, 255);
    
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(dis(gen));
    }
    
    return data;
}

// Verify data integrity
bool VerifyData(const std::vector<uint8_t>& original, const std::vector<uint8_t>& received) {
    if (original.size() != received.size()) {
        return false;
    }
    
    return std::memcmp(original.data(), received.data(), original.size()) == 0;
}

// Receiver callback function
void OnDataReceived(const std::vector<uint8_t>& data, std::function<void()> release) {
    total_received++;
    total_bytes_received += data.size();
    
    std::cout << "Received frame #" << total_received 
              << ", size: " << data.size() << " bytes" << std::endl;
    
    // Release the buffer
    release();
}

// Statistics printer thread
void PrintStatistics() {
    auto start_time = std::chrono::steady_clock::now();
    
    while (test_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        
        if (elapsed > 0) {
            double sent_fps = static_cast<double>(total_sent) / elapsed;
            double recv_fps = static_cast<double>(total_received) / elapsed;
            double sent_mbps = static_cast<double>(total_bytes_sent) / (elapsed * 1024 * 1024);
            double recv_mbps = static_cast<double>(total_bytes_received) / (elapsed * 1024 * 1024);
            
            std::cout << "\n=== Statistics (Elapsed: " << elapsed << "s) ===" << std::endl;
            std::cout << "Sent:     " << total_sent << " frames (" 
                      << std::fixed << std::setprecision(2) << sent_fps << " fps, "
                      << sent_mbps << " MB/s)" << std::endl;
            std::cout << "Received: " << total_received << " frames (" 
                      << recv_fps << " fps, " << recv_mbps << " MB/s)" << std::endl;
            std::cout << "Loss:     " << (total_sent - total_received) << " frames" << std::endl;
            std::cout << "========================================\n" << std::endl;
        }
    }
}

// Sender test thread
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
        
        // Generate and send test data
        std::vector<std::vector<uint8_t>> test_datasets;
        
        // Different sizes of test data
        std::vector<size_t> test_sizes = {
            100,           // Small packet
            1024,          // 1KB
            4096,          // 4KB
            16384,         // 16KB
            65536,         // 64KB
            MAX_DATA_SIZE  // 1MB
        };
        
        // Generate test datasets
        for (size_t size : test_sizes) {
            test_datasets.push_back(GenerateTestData(size));
        }
        
        std::cout << "Generated " << test_datasets.size() << " test datasets" << std::endl;
        
        // Send data continuously
        size_t dataset_index = 0;
        while (test_running) {
            const auto& data = test_datasets[dataset_index % test_datasets.size()];
            
            sender.Send(data.data(), data.size());
            
            total_sent++;
            total_bytes_sent += data.size();
            
            std::cout << "Sent frame #" << total_sent 
                      << ", size: " << data.size() << " bytes" << std::endl;
            
            dataset_index++;
            
            // Send every 100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        sender.Stop();
        sender_thread.join();
        
    } catch (const std::exception& e) {
        std::cerr << "Sender error: " << e.what() << std::endl;
    }
}

// Receiver test thread
void ReceiverTest() {
    try {
        std::cout << "Starting receiver on port " << TEST_PORT << std::endl;
        
        Receiver receiver(
            TEST_PORT,
            OnDataReceived,
            TEST_MTU,
            TEST_BUFFER_SIZE,
            MAX_DATA_SIZE,
            TEST_IP,
            TEST_PORT
        );
        
        // Start receiver (this will block)
        receiver.Start();
        
    } catch (const std::exception& e) {
        std::cerr << "Receiver error: " << e.what() << std::endl;
    }
}

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [sender|receiver|both]" << std::endl;
    std::cout << "  sender   - Run as sender only" << std::endl;
    std::cout << "  receiver - Run as receiver only" << std::endl;
    std::cout << "  both     - Run both sender and receiver (default)" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "ChunkStream Test Application" << std::endl;
    std::cout << "============================" << std::endl;
    
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
    std::cout << "  Max data size: " << MAX_DATA_SIZE << " bytes" << std::endl;
    std::cout << std::endl;
    
    // Start statistics thread
    std::thread stats_thread(PrintStatistics);
    
    try {
        if (mode == "sender") {
            SenderTest();
        } else if (mode == "receiver") {
            ReceiverTest();
        } else if (mode == "both") {
            // Start receiver in separate thread
            std::thread receiver_thread(ReceiverTest);
            
            // Give receiver time to start
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Start sender in main thread (or separate thread)
            std::thread sender_thread(SenderTest);
            
            // Wait for user input to stop
            std::cout << "Press Enter to stop the test..." << std::endl;
            std::cin.get();
            
            test_running = false;
            
            // Wait for threads to finish
            sender_thread.join();
            receiver_thread.join();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        test_running = false;
    }
    
    test_running = false;
    stats_thread.join();
    
    std::cout << "\nFinal Statistics:" << std::endl;
    std::cout << "Total sent: " << total_sent << " frames (" 
              << total_bytes_sent << " bytes)" << std::endl;
    std::cout << "Total received: " << total_received << " frames (" 
              << total_bytes_received << " bytes)" << std::endl;
    std::cout << "Frame loss: " << (total_sent - total_received) << " frames" << std::endl;
    
    if (total_sent > 0) {
        double loss_rate = static_cast<double>(total_sent - total_received) / total_sent * 100;
        std::cout << "Loss rate: " << std::fixed << std::setprecision(2) 
                  << loss_rate << "%" << std::endl;
    }
    
    return 0;
}