// Copyright (c) 2025 Wooseok Choi
// Licensed under the MIT License - see LICENSE file

#ifndef CHUNKSTREAM_SENDER_H_
#define CHUNKSTREAM_SENDER_H_

#include <string>
#include <asio.hpp>
#include "chunkstream/core/chunk_header.h"

namespace chunkstream {

struct SendingFrame {
  uint32_t id;
  std::mutex ref_count_lock;
  uint16_t ref_count = 0;
  std::vector<ChunkHeader> headers;
  std::vector< std::vector<uint8_t> > chunks;
};

class Sender {
public:
  Sender(const std::string& ip, const int port, const int mtu = 1500, 
         const size_t buffer_size = 10, const size_t max_data_size = 0);
  ~Sender();

  void Send(const uint8_t* data, const size_t size);

  // It will block thread
  void Start();
  void Stop();

private:
  void __Receive();
  void __HandlePacket(ChunkHeader header);

private: 
  std::atomic_bool running_ = false;
  std::unique_ptr<asio::ip::udp::socket> socket_;
  asio::ip::udp::endpoint remote_endpoint_;
  asio::io_context io_context_; // Must be ran if using async_send_to()
  asio::ip::udp::endpoint ENDPOINT;
  const int MTU;
  const int PAYLOAD;
  std::array<uint8_t, 65553> recv_buffer_;

  // Circular buffer for data.
  std::vector< std::unique_ptr<SendingFrame> > buffer_; 
  std::atomic_int buffer_index_;
  std::mutex buffering_mutex_;
  std::atomic<uint32_t> id_;
};

}

#endif