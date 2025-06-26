// Copyright (c) 2025 Wooseok Choi
// Licensed under the MIT License - see LICENSE file

#ifndef CHUNKSTREAM_RECEIVER_RECEIVING_FRAME_H_
#define CHUNKSTREAM_RECEIVER_RECEIVING_FRAME_H_

#include <asio.hpp>
#include "chunkstream/core/chunk_header.h"

namespace chunkstream {

class ReceivingFrame {
public:
  enum Status {
    ASSEMBLING, 
    DROPPED, 
    READY
  };
public:
  // @memory_pool requires its size as `total_chunks * chunk_size` 
  // @param send_assembled_callback `_1` for data ptr, `_2` for size of the data 
  ReceivingFrame(std::shared_ptr<asio::io_context> io_context, 
                const asio::ip::udp::endpoint sender_endpoint, 
                const uint32_t id, 
                const size_t total_chunks, 
                uint8_t* memory_pool,
                const size_t memory_pool_block_size,
                std::function<void(const ChunkHeader header, 
                                   const asio::ip::udp::endpoint endpoint)> request_resend_func,
                std::function<void(const uint32_t id, 
                                   uint8_t* data, 
                                   const size_t size)> send_assembled_callback, 
                std::function<void(const uint32_t id, uint8_t* data)> dropped_callback);

  bool IsChunkAdded(const uint16_t chunk_index);
  bool IsTimeout();

  // @data should be `recv_buffer_.data() + CHUNKHEADER_SIZE`
  void AddChunk(const ChunkHeader& header, uint8_t* data);
  int GetStatus();
  uint8_t* GetData();

private:
  void __RequestResend(const uint32_t id);

public: 
  const uint32_t ID;
  const size_t BLOCK_SIZE;
  const std::chrono::milliseconds INIT_CHUNK_TIMEOUT;
  const std::chrono::milliseconds FRAME_DROP_TIMEOUT; 
  const std::chrono::milliseconds RESEND_TIMEOUT;

private:
  asio::ip::udp::endpoint SENDER_ENDPOINT;
  std::shared_ptr<asio::io_context> io_context_;
  std::function<void(const ChunkHeader header, const asio::ip::udp::endpoint endpoint)> __RequestResendCallback;
  std::function<void(const uint32_t id, uint8_t* data, const size_t size)> __SendAssembledCallback;
  std::function<void(const uint32_t id, uint8_t* data)> __DroppedCallback;
  asio::steady_timer init_chunk_timer_;
  asio::steady_timer frame_drop_timer_;
  asio::steady_timer resend_timer_;
  std::vector<bool> chunk_bitmap_;
  std::mutex chunk_bitmap_mutex_;
  std::vector<ChunkHeader> chunk_headers_;
  uint8_t* data_;
  std::atomic_bool request_resend_ = false;
  std::atomic_bool request_timeout_ = false;
  std::atomic_int status_;
};

}

#endif