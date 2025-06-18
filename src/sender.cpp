#include "sender.h"

namespace chunkstream {

template <class T> const T& min (const T& a, const T& b) {
  return !(b<a)?a:b;
}

Sender::Sender(const std::string& ip, const int port, const int mtu, const size_t buffer_size, const size_t max_data_size)
  : socket_(io_context_, asio::ip::udp::v4()), 
    ENDPOINT(asio::ip::address::from_string(ip), port), 
    MTU(mtu), 
    PAYLOAD(MTU - 20 - 8 - CHUNKHEADER_SIZE), // mtu - IP header - UDP header - Chunk header
    buffer_index_(0), 
    id_(0)
  {
    if (max_data_size > 0) {
      const int total_chunks = (max_data_size + PAYLOAD - 1) / PAYLOAD;
      buffer_.resize(
        buffer_size, 
        std::vector< std::vector<uint8_t> >(
          total_chunks, std::vector<uint8_t>(CHUNKHEADER_SIZE + PAYLOAD)
        )
      );
    }
  }

void Sender::Send(const uint8_t* data, const size_t size) {
  ChunkHeader header;

  auto& buffer = buffer_[buffer_index_++];
  if (buffer_index_ >= buffer_.size()) buffer_index_ = 0;

  header.id = id_++;
  header.total_size = static_cast<uint32_t>(size);
  header.total_chunks = static_cast<uint16_t>((header.total_size + PAYLOAD - 1) / PAYLOAD);
  header.transmission_type = 0; // INIT
  
  if (buffer.size() < header.total_chunks) {
    buffer.resize(
      header.total_chunks, std::vector<uint8_t>(CHUNKHEADER_SIZE + PAYLOAD)
    );
  }

  for (int i = 0; i < header.total_chunks; i++) {
    header.chunk_index = static_cast<uint16_t>(i);

    const int remaining = header.total_size - (i * PAYLOAD);
    header.chunk_size = static_cast<uint32_t>(min(PAYLOAD, remaining));

    uint8_t* packet = buffer[header.chunk_index].data();
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

}