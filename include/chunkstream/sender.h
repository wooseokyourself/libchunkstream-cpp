#ifndef CHUNKSTREAM_SENDER_H_
#define CHUNKSTREAM_SENDER_H_

#include <string>
#include <asio.hpp>
#include "chunkstream/core/chunk_header.h"
#include "chunkstream/core/thread_pool.h"

namespace chunkstream {

struct SendingFrame {
  uint32_t id;
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
  void __HandlePacket(const asio::error_code& error, std::size_t bytes_transferred);
  void __HandleResend(const uint32_t frame_id, const uint16_t chunk_index);

private: 
  std::atomic_bool running_ = false;
  asio::ip::udp::socket socket_;
  asio::ip::udp::endpoint remote_endpoint_;
  asio::io_context io_context_; // Must be ran if using async_send_to()
  const asio::ip::udp::endpoint ENDPOINT;
  const int MTU;
  const int PAYLOAD;
  std::array<uint8_t, 65553> recv_buffer_;

  // Circular buffer for data.
  // A data(i) -> buffer_[i]
  // Chunks(j) of a data(i) -> buffer_[i][j]
  // <`buffer_index_`, [`chunk_index`].size() == `MTU-28`>
  std::vector<SendingFrame> buffer_; 
  std::atomic_int buffer_index_;
  std::atomic<uint32_t> id_;
  
  std::shared_ptr<ThreadPool> threads_;
};

}

#endif