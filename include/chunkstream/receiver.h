#ifndef CHUNKSTREAM_RECEIVER_H_
#define CHUNKSTREAM_RECEIVER_H_

#include <asio.hpp>
#include <functional>
#include "chunkstream/core/chunk_header.h"

namespace chunkstream {

class Receiver {
public:
  Receiver(const int port, std::function<void(const std::vector<uint8_t>&)> grab, const size_t buffer_size = 10, const size_t max_data_size = 0) 
  : socket_(io_context_, asio::ip::udp::endpoint(asio::ip::udp::v4(), port)) {
    io_context_.run();
  }

  void StartReceive() {
    socket_.async_receive_from(
        asio::buffer(recv_buffer_), remote_endpoint_,
        std::bind(&Receiver::__HandlePacket, this,
          std::placeholders::_1,
          std::placeholders::_2));
  }

private: 
  void __HandlePacket(const asio::error_code& error, std::size_t bytes_transferred) {
    if ((!error || error == asio::error::message_size) && bytes_transferred >= CHUNKHEADER_SIZE)
    {
      ChunkHeader header;
      std::memcpy(&header, recv_buffer_.data(), CHUNKHEADER_SIZE);

      header.buffer_index = ntohl(header.buffer_index);
      header.total_size = ntohl(header.total_size);
      header.total_chunks = ntohs(header.total_chunks);
      header.chunk_index = ntohs(header.chunk_index);
      header.chunk_size = ntohl(header.chunk_size);

      // 여기서 패킷 수신 및 분석?
      StartReceive();
    }
  }

private: 
  asio::ip::udp::socket socket_;
  asio::ip::udp::endpoint remote_endpoint_;
  asio::io_context io_context_; // Must be ran if using async_send_to()
  std::array<uint8_t, 65553> recv_buffer_;

  // std::atomic_int 

  // <`ChunkHeader::message_id`, [`chunk_index`].size() == `MTU-28`>
  std::vector< std::vector< std::vector<uint8_t> > > buffer_; 
};

}  // namespace chunkstream

#endif  // CHUNKSTREAM_CLIENT_H_