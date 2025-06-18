#ifndef CHUNKSTREAM_RECEIVER_H_
#define CHUNKSTREAM_RECEIVER_H_

#include <asio.hpp>
#include <functional>
#include "chunkstream/core/chunk_header.h"
#include "chunkstream/core/thread_pool.h"
#include "chunkstream/core/ordered_hash_container.h"

namespace chunkstream {

class Receiver {

private: 
  class ReceivingFrame {
  public:
    // @memory_pool requires its size as `total_chunks * chunk_size` 
    // @param send_assembled_callback `_1` for data ptr, `_2` for size of the data 
    ReceivingFrame(std::shared_ptr<asio::io_context> io_context, 
                  const size_t total_chunks, 
                  uint8_t* memory_pool,
                  const size_t memory_pool_block_size,
                  std::function<void(const ChunkHeader header)> request_resend_func,
                  std::function<void(uint8_t* data, const size_t size)> send_assembled_callback);

    bool IsChunkAdded(const uint16_t chunk_index);
    bool IsTimeout();

    // @data should be `recv_buffer_.data() + CHUNKHEADER_SIZE`
    void AddChunk(const ChunkHeader& header, uint8_t* data);

  private:
    void __RequestResend(const uint32_t id);
    void __PeriodicResend(const uint32_t id);

  private:
    std::shared_ptr<asio::io_context> io_context_;
    std::function<void(const ChunkHeader header)> request_resend_func_;
    std::function<void(uint8_t* data, const size_t size)> send_assembled_callback_;
    asio::steady_timer init_chunk_timer_;
    asio::steady_timer frame_drop_timer_;
    asio::steady_timer resend_timer_;
    std::vector<std::atomic_bool> chunk_bitmap_;
    std::vector<ChunkHeader> chunk_headers_;
    const std::chrono::milliseconds INIT_CHUNK_TIMEOUT;
    const std::chrono::milliseconds FRAME_DROP_TIMEOUT; 
    const std::chrono::milliseconds RESEND_TIMEOUT;
    uint8_t* data_;
    const size_t BLOCK_SIZE;
    std::atomic_bool request_resend_ = false;
    std::atomic_bool request_timeout_ = false;
  };

public:
  Receiver(const int port, 
           std::function<void(const std::vector<uint8_t>&)> grab,
           const size_t buffer_size = 10, 
           const size_t max_data_size = 0, 
           std::string sender_ip = "localhost", 
           const int sender_port = 35241) ;
  ~Receiver();

  // It will block thread
  void Start();
  void Stop();
  size_t GetFrameCount();
  size_t GetDropCount();

private: 
  void __Receive();
  void __HandlePacket(const asio::error_code& error, std::size_t bytes_transferred);
  void __RequestResend(const ChunkHeader header);
  void __FrameGrabbed(uint8_t* data, const size_t size);

private: 
  const asio::ip::udp::endpoint SENDER_ENDPOINT;
  std::atomic_bool running_ = false;
  std::function<void(const std::vector<uint8_t>&)> grabbed_;
  asio::ip::udp::socket socket_;
  asio::ip::udp::endpoint remote_endpoint_;
  std::shared_ptr<asio::io_context> io_context_ = std::make_shared<asio::io_context>();
  std::array<uint8_t, 65553> recv_buffer_;

  // [ <-- max_data_size * buffer_size --> ]
  std::vector<uint8_t> data_memory_pool_;
  int data_memory_pool_index_;
  const size_t BLOCK_SIZE;

  std::vector<uint8_t> resend_request_memory_pool_;

  OrderedHashContainer<uint32_t, ReceivingFrame> buffer_;
  const size_t BUFFER_SIZE;

  std::shared_ptr<ThreadPool> threads_;

  std::atomic<size_t> assembled_count_ = 0;
  std::atomic<size_t> dropped_count_ = 0;
};

}

#endif