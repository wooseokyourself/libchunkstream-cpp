// Copyright (c) 2025 Wooseok Choi
// Licensed under the MIT License - see LICENSE file

#ifndef CHUNKSTREAM_RECEIVER_H_
#define CHUNKSTREAM_RECEIVER_H_

#include <asio.hpp>
#include <functional>
#include "chunkstream/receiver/receiving_frame.h"
#include "chunkstream/core/chunk_header.h"
#include "chunkstream/core/ordered_hash_container.h"
#include "chunkstream/receiver/memory_pool.h"

namespace chunkstream {

class Receiver {
public:
  Receiver(const int port, 
           std::function<void(const std::vector<uint8_t>& data, std::function<void()> Release)> grab,
           const int mtu = 1500, 
           const size_t buffer_size = 10, 
           const size_t max_data_size = 0) ;
  ~Receiver();

  // It will block thread
  void Start();
  void Stop();
  void Flush();
  size_t GetFrameCount() const;
  size_t GetDropCount() const;

public:
  const size_t BUFFER_SIZE;
  const size_t MTU;
  const size_t PAYLOAD;

private: 
  void __Receive();
  void __HandlePacket(const asio::ip::udp::endpoint& sender_endpoint, uint8_t* recv_buf);
  void __RequestResend(const ChunkHeader header, const asio::ip::udp::endpoint endpoint);
  void __FrameGrabbed(const uint32_t id, uint8_t* data, const size_t size);

private: 
  std::atomic_bool running_ = false;
  std::function< void(const std::vector<uint8_t>&, std::function<void()>) > grabbed_;
  std::unique_ptr<asio::ip::udp::socket> socket_;
  asio::ip::udp::endpoint remote_endpoint_;
  std::shared_ptr<asio::io_context> io_context_ = std::make_shared<asio::io_context>();

  // [ <-- BLOCK_SIZE * BUFFER_SIZE --> ]
  // block: one data (assembled packets)
  MemoryPool data_pool_;

  // [ <-- PACKET_SIZE * EXPECTED_CHUNK_COUNT * BUFFER_SIZE --> ]
  // block: one packet
  MemoryPool raw_pool_;
  
  // [ <-- CHUNKHEADER_SIZE * BUFFER_SIZE --> ]
  // block: one chunk_header
  MemoryPool resend_pool_;

  std::queue< std::pair<uint32_t, uint8_t*> > dropped_queue_;

  OrderedHashContainer<uint32_t, std::shared_ptr<ReceivingFrame> > assembling_queue_;
  std::mutex assembling_queue_push_mutex_;

  std::atomic<size_t> assembled_count_ = 0;
  std::atomic<size_t> dropped_count_ = 0;
};

}

#endif