#ifndef CHUNKSTREAM_SENDER_H_
#define CHUNKSTREAM_SENDER_H_

#include <string>
#include <asio.hpp>
#include "chunkstream/core/chunk_header.h"

namespace chunkstream {

class Sender {
public:
  Sender(const std::string& ip, const int port, const int mtu = 1500, const size_t buffer_size = 10, const size_t max_data_size = 0);

  void Send(const uint8_t* data, const size_t size);

private: 
  asio::ip::udp::socket socket_;
  asio::io_context io_context_; // Must be ran if using async_send_to()
  const asio::ip::udp::endpoint ENDPOINT;
  const int MTU;
  const int PAYLOAD;

  // Circular buffer for data.
  // A data(i) -> buffer_[i]
  // Chunks(j) of a data(i) -> buffer_[i][j]
  // <`buffer_index_`, [`chunk_index`].size() == `MTU-28`>
  std::vector< std::vector< std::vector<uint8_t> > > buffer_; 
  std::atomic_int buffer_index_;
  std::atomic<uint32_t> id_;
};

}

#endif