#ifndef CHUNKSTREAM_SENDER_H_
#define CHUNKSTREAM_SENDER_H_

#include <string>
#include <asio.hpp>
#include "chunkstream/core/chunk_header.h"

namespace chunkstream {

template <class T> const T& min (const T& a, const T& b) {
  return !(b<a)?a:b;
}

class Sender {
public:
  Sender(const std::string& ip, const int port, const int mtu = 1500, const size_t buffer_size = 10, const size_t max_data_size = 0)
  : socket_(io_context_, asio::ip::udp::v4()), 
    ENDPOINT(asio::ip::address::from_string(ip), port), 
    MTU(mtu), 
    PAYLOAD(MTU - 20 - 8 - CHUNKHEADER_SIZE) // mtu - IP header - UDP header - Chunk header
{
  if (max_data_size > 0) {
    const int total_chunks = (max_data_size + PAYLOAD - 1) / PAYLOAD;
    buffer_.resize(
      buffer_size, 
      std::vector< std::vector<uint8_t> >(total_chunks, std::vector<uint8_t>(CHUNKHEADER_SIZE + PAYLOAD))
    );
  }
}

  void Send(const uint8_t* data, const size_t size) {
    ChunkHeader header;

    header.buffer_index = buffer_index_++;
    if (buffer_index_ >= buffer_.size()) buffer_index_ = 0;

    header.total_size = static_cast<uint32_t>(size);
    header.total_chunks = static_cast<uint16_t>((header.total_size + PAYLOAD - 1) / PAYLOAD);
    
    if (buffer_[header.buffer_index].size() < header.total_chunks) {
      buffer_[header.buffer_index].resize(
        header.total_chunks, std::vector<uint8_t>(CHUNKHEADER_SIZE + PAYLOAD)
      );
    }

    for (int i = 0; i < header.total_chunks; i++) {
      header.chunk_index = static_cast<uint16_t>(i);

      const int remaining = header.total_size - (i * PAYLOAD);
      header.chunk_size = static_cast<uint32_t>(min(PAYLOAD, remaining));

      uint8_t* packet = buffer_[header.buffer_index][header.chunk_index].data();
      memcpy(packet, &header, CHUNKHEADER_SIZE);
      memcpy(packet + CHUNKHEADER_SIZE, data + (i * PAYLOAD), header.chunk_size);
      socket_.send_to(
        asio::buffer(
          packet, static_cast<size_t>(CHUNKHEADER_SIZE + header.chunk_size)
        ), 
        ENDPOINT
      );
    }
  }

private: 
  asio::ip::udp::socket socket_;
  asio::io_context io_context_; // Must be ran if using async_send_to()
  const asio::ip::udp::endpoint ENDPOINT;
  const int MTU;
  const int PAYLOAD;

  std::atomic_int buffer_index_;

  // <`ChunkHeader::message_id`, [`chunk_index`].size() == `MTU-28`>
  std::vector< std::vector< std::vector<uint8_t> > > buffer_; 

};

}

#endif