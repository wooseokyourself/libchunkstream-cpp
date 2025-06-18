#include "sender.h"

namespace chunkstream {

template <class T> const T& min (const T& a, const T& b) {
  return !(b<a)?a:b;
}

Sender::Sender(const std::string& ip, const int port, 
               const int mtu, const size_t buffer_size, const size_t max_data_size)
  : socket_(io_context_, asio::ip::udp::v4()), 
    ENDPOINT(asio::ip::address::from_string(ip), port), 
    MTU(mtu), 
    PAYLOAD(MTU - 20 - 8 - CHUNKHEADER_SIZE), // mtu - IP header - UDP header - Chunk header
    buffer_index_(0), 
    id_(0)
  {
    threads_ = std::make_shared<ThreadPool>(std::thread::hardware_concurrency());
    if (max_data_size > 0) {
      const int total_chunks = (max_data_size + PAYLOAD - 1) / PAYLOAD;
      buffer_.resize(buffer_size);
      for (int i = 0; i < buffer_.size(); i++) {
        buffer_[i].id = -1;
        buffer_[i].chunks.resize(total_chunks, std::vector<uint8_t>(CHUNKHEADER_SIZE + PAYLOAD));
      }
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
  
  if (buffer.chunks.size() < header.total_chunks) {
    buffer.chunks.resize(
      header.total_chunks, std::vector<uint8_t>(CHUNKHEADER_SIZE + PAYLOAD)
    );
  }

  buffer.id = header.id;

  for (int i = 0; i < header.total_chunks; i++) {
    header.chunk_index = static_cast<uint16_t>(i);

    const int remaining = header.total_size - (i * PAYLOAD);
    header.chunk_size = static_cast<uint32_t>(min(PAYLOAD, remaining));

    uint8_t* packet = buffer.chunks[header.chunk_index].data();
    std::memcpy(packet, &header, CHUNKHEADER_SIZE);
    std::memcpy(packet + CHUNKHEADER_SIZE, data + (i * PAYLOAD), header.chunk_size);
    socket_.send_to(
      asio::buffer(
        packet, static_cast<size_t>(CHUNKHEADER_SIZE + header.chunk_size)
      ), 
      ENDPOINT
    );
  }
}

void Sender::Start() {
  running_ = true;
  __Receive();
  io_context_.run();
}

void Sender::Stop() {
  running_ = false;
  io_context_.stop();
}

void Sender::__Receive() {
  socket_.async_receive_from(
    asio::buffer(recv_buffer_), remote_endpoint_,
    std::bind(&Sender::__HandlePacket, this,
      std::placeholders::_1,
      std::placeholders::_2
    )
  );
}

void Sender::__HandlePacket(const asio::error_code& error, std::size_t bytes_transferred) {
  if ((!error || error == asio::error::message_size) && bytes_transferred >= CHUNKHEADER_SIZE)
  {
    ChunkHeader header;
    std::memcpy(&header, recv_buffer_.data(), CHUNKHEADER_SIZE);

    header.id = ntohl(header.id);
    header.total_size = ntohl(header.total_size);
    header.total_chunks = ntohs(header.total_chunks);
    header.chunk_index = ntohs(header.chunk_index);
    header.chunk_size = ntohl(header.chunk_size);
    header.transmission_type = ntohs(header.transmission_type);

    // Change type flag to RESEND
    header.transmission_type = 1;

    threads_->Enqueue([this, header]() {
      SendingFrame* frame = nullptr;
      // TO DO: Find `req.id` frame by binary search
      for (int i = 0; i < buffer_.size(); i++) {
        if (buffer_[i].id == header.id) {
          frame = &buffer_[i];
          break;
        }
      }
      if (!frame) return;

      // Overwrite chunk header (for changed type to "RESEND")
      std::memcpy(frame->chunks[header.chunk_index].data(), &header, CHUNKHEADER_SIZE);
      socket_.send_to(
        asio::buffer(
          frame->chunks[header.chunk_index].data(), CHUNKHEADER_SIZE + header.chunk_size
        ), 
        ENDPOINT
      );
    });

    if (running_) __Receive();
  }
}

}