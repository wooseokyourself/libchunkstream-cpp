#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <random>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <iomanip>

// chunkstream 헤더들 (실제 경로에 맞게 수정 필요)
#include "chunkstream/sender.h"
#include "chunkstream/receiver.h"

class ChunkStreamTester {
private:
    std::atomic<bool> test_completed_{false};
    std::atomic<int> received_count_{0};
    std::vector<std::vector<uint8_t>> sent_data_;
    std::vector<std::vector<uint8_t>> received_data_;
    std::mutex data_mutex_;
    std::condition_variable test_cv_;
    
    // 테스트 설정
    const std::string TEST_IP = "127.0.0.1";
    const int TEST_PORT = 12345;
    const int MTU = 1500;
    const size_t BUFFER_SIZE = 10;
    const size_t MAX_DATA_SIZE = 5 * 1024 * 1024; // 5MB

public:
    void RunTest() {
        std::cout << "=== ChunkStream UDP 대용량 데이터 스트리밍 테스트 ===" << std::endl;
        std::cout << "테스트 설정:" << std::endl;
        std::cout << "- IP: " << TEST_IP << std::endl;
        std::cout << "- Port: " << TEST_PORT << std::endl;
        std::cout << "- MTU: " << MTU << std::endl;
        std::cout << "- Buffer Size: " << BUFFER_SIZE << std::endl;
        std::cout << "- Max Data Size: " << MAX_DATA_SIZE / 1024 / 1024 << "MB" << std::endl;
        std::cout << std::endl;

        // 테스트 데이터 생성
        GenerateTestData();
        
        // Receiver 스레드 시작
        std::thread receiver_thread(&ChunkStreamTester::RunReceiver, this);
        
        // Receiver가 준비될 시간을 줌
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Sender 스레드 시작
        std::thread sender_thread(&ChunkStreamTester::RunSender, this);
        
        // 테스트 완료 대기
        std::unique_lock<std::mutex> lock(data_mutex_);
        test_cv_.wait(lock, [this] { return test_completed_.load(); });
        
        // 스레드 종료 대기
        sender_thread.join();
        receiver_thread.join();
        
        // 결과 검증
        VerifyResults();
    }

private:
    void GenerateTestData() {
        std::cout << "테스트 데이터 생성 중..." << std::endl;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dis(0, 255);
        
        // 다양한 크기의 데이터 생성
        std::vector<size_t> data_sizes = {
            1024,           // 1KB
            10 * 1024,      // 10KB
            100 * 1024,     // 100KB
            1024 * 1024,    // 1MB
            2 * 1024 * 1024 // 2MB
        };
        
        sent_data_.clear();
        for (size_t size : data_sizes) {
            std::vector<uint8_t> data(size);
            for (size_t i = 0; i < size; ++i) {
                data[i] = dis(gen);
            }
            sent_data_.push_back(std::move(data));
        }
        
        std::cout << "생성된 테스트 데이터: " << sent_data_.size() << "개" << std::endl;
        for (size_t i = 0; i < sent_data_.size(); ++i) {
            std::cout << "  데이터 " << (i+1) << ": " << sent_data_[i].size() << " bytes" << std::endl;
        }
        std::cout << std::endl;
    }
    
    void RunReceiver() {
        std::cout << "Receiver 시작..." << std::endl;
        
        try {
            chunkstream::Receiver receiver(
                TEST_PORT,
                [this](const std::vector<uint8_t>& data) {
                    this->OnDataReceived(data);
                },
                BUFFER_SIZE,
                MAX_DATA_SIZE
            );
            
            std::cout << "Receiver 준비 완료 (포트: " << TEST_PORT << ")" << std::endl;
            receiver.Start(); // 블로킹 호출
            
        } catch (const std::exception& e) {
            std::cerr << "Receiver 오류: " << e.what() << std::endl;
        }
        
        std::cout << "Receiver 종료" << std::endl;
    }
    
    void RunSender() {
        std::cout << "Sender 시작..." << std::endl;
        
        try {
            chunkstream::Sender sender(TEST_IP, TEST_PORT, MTU, BUFFER_SIZE, MAX_DATA_SIZE);
            
            std::cout << "데이터 전송 시작..." << std::endl;
            auto start_time = std::chrono::high_resolution_clock::now();
            
            for (size_t i = 0; i < sent_data_.size(); ++i) {
                const auto& data = sent_data_[i];
                std::cout << "데이터 " << (i+1) << " 전송 중... (" 
                         << data.size() << " bytes)" << std::endl;
                
                sender.Send(data.data(), data.size());
                
                // 각 데이터 전송 간 짧은 지연
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time
            );
            
            std::cout << "모든 데이터 전송 완료 (소요시간: " 
                     << duration.count() << "ms)" << std::endl;
            
            // 수신 완료 대기
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
        } catch (const std::exception& e) {
            std::cerr << "Sender 오류: " << e.what() << std::endl;
        }
        
        std::cout << "Sender 종료" << std::endl;
    }
    
    void OnDataReceived(const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        
        int count = received_count_.fetch_add(1) + 1;
        std::cout << "데이터 수신 완료 " << count << "/" << sent_data_.size() 
                 << " (" << data.size() << " bytes)" << std::endl;
        
        received_data_.push_back(data);
        
        // 모든 데이터 수신 완료 확인
        if (received_data_.size() >= sent_data_.size()) {
            test_completed_ = true;
            test_cv_.notify_one();
        }
    }
    
    void VerifyResults() {
        std::cout << std::endl << "=== 결과 검증 ===" << std::endl;
        
        std::cout << "전송된 데이터: " << sent_data_.size() << "개" << std::endl;
        std::cout << "수신된 데이터: " << received_data_.size() << "개" << std::endl;
        
        if (sent_data_.size() != received_data_.size()) {
            std::cout << "❌ 데이터 개수 불일치!" << std::endl;
            return;
        }
        
        // 데이터 무결성 검증
        bool all_match = true;
        size_t total_bytes = 0;
        
        for (size_t i = 0; i < sent_data_.size(); ++i) {
            const auto& sent = sent_data_[i];
            bool found_match = false;
            
            // 수신된 데이터 중에서 일치하는 것을 찾음 (순서가 바뀔 수 있음)
            for (size_t j = 0; j < received_data_.size(); ++j) {
                const auto& received = received_data_[j];
                
                if (sent.size() == received.size() && 
                    std::memcmp(sent.data(), received.data(), sent.size()) == 0) {
                    found_match = true;
                    total_bytes += sent.size();
                    std::cout << "✅ 데이터 " << (i+1) << " 검증 성공 (" 
                             << sent.size() << " bytes)" << std::endl;
                    break;
                }
            }
            
            if (!found_match) {
                std::cout << "❌ 데이터 " << (i+1) << " 검증 실패!" << std::endl;
                all_match = false;
            }
        }
        
        std::cout << std::endl;
        if (all_match) {
            std::cout << "🎉 모든 데이터 검증 성공!" << std::endl;
            std::cout << "총 전송량: " << total_bytes << " bytes (" 
                     << std::fixed << std::setprecision(2) 
                     << static_cast<double>(total_bytes) / 1024 / 1024 << " MB)" << std::endl;
        } else {
            std::cout << "❌ 데이터 검증 실패!" << std::endl;
        }
        
        // 상세 통계
        PrintDetailedStats();
    }
    
    void PrintDetailedStats() {
        std::cout << std::endl << "=== 상세 통계 ===" << std::endl;
        
        size_t min_size = SIZE_MAX, max_size = 0, total_size = 0;
        for (const auto& data : sent_data_) {
            min_size = std::min(min_size, data.size());
            max_size = std::max(max_size, data.size());
            total_size += data.size();
        }
        
        std::cout << "최소 데이터 크기: " << min_size << " bytes" << std::endl;
        std::cout << "최대 데이터 크기: " << max_size << " bytes" << std::endl;
        std::cout << "평균 데이터 크기: " << total_size / sent_data_.size() << " bytes" << std::endl;
        std::cout << "총 데이터 크기: " << total_size << " bytes" << std::endl;
        
        // 청크 계산
        const int payload_size = MTU - 20 - 8 - chunkstream::CHUNKHEADER_SIZE;
        size_t total_chunks = 0;
        for (const auto& data : sent_data_) {
            total_chunks += (data.size() + payload_size - 1) / payload_size;
        }
        std::cout << "총 청크 개수: " << total_chunks << "개" << std::endl;
        std::cout << "청크당 페이로드: " << payload_size << " bytes" << std::endl;
    }
};

// ThreadPool 구현이 필요한 경우를 위한 간단한 구현
// (실제로는 thread_pool.cpp 파일이 있어야 함)
namespace chunkstream {

ThreadPool::ThreadPool(size_t threads) : stop_(false) {
    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex_);
                    this->condition_.wait(lock, [this] { return this->stop_ || !this->tasks_.empty(); });
                    if (this->stop_ && this->tasks_.empty()) return;
                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }
                task();
                --active_tasks_;
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (std::thread &worker: workers_) {
        worker.join();
    }
}

size_t ThreadPool::GetActiveTasksCount() const {
    return active_tasks_.load();
}

size_t ThreadPool::GetPendingTasksCount() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

}

int main() {
    try {
        ChunkStreamTester tester;
        tester.RunTest();
        
        std::cout << std::endl << "테스트 완료. 아무 키나 누르세요..." << std::endl;
        std::cin.get();
        
    } catch (const std::exception& e) {
        std::cerr << "테스트 실행 중 오류: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}