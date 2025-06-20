#include "chunkstream/sender.h"

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
    id_(0) {
  threads_ = std::make_shared<ThreadPool>(std::thread::hardware_concurrency());
  if (max_data_size > 0) {
    const int total_chunks = (max_data_size + PAYLOAD - 1) / PAYLOAD;
    buffer_.resize(buffer_size);
    for (int i = 0; i < buffer_.size(); i++) {
      buffer_[i] = std::make_unique<SendingFrame>();
      buffer_[i]->id = -1;
      buffer_[i]->chunks.resize(total_chunks, std::vector<uint8_t>(CHUNKHEADER_SIZE + PAYLOAD));
    }
  }
}

Sender::~Sender() {
  Stop();
}

void Sender::Send(const uint8_t* data, const size_t size) {
  ChunkHeader header;
  header.id = id_++;
  header.total_size = static_cast<uint32_t>(size);
  header.total_chunks = static_cast<uint16_t>((header.total_size + PAYLOAD - 1) / PAYLOAD);
  header.transmission_type = 0; // INIT

  SendingFrame* frame = nullptr;

  while (!frame) {
    // Find buffer whose `ref_count == 0`

    int idx = buffer_index_.fetch_add(1) % buffer_.size();
    
    std::lock_guard<std::mutex> lock(buffer_[idx]->ref_count_lock);
    if (buffer_[idx]->ref_count == 0) {
      frame = buffer_[idx].get();
      frame->id = header.id;
      frame->ref_count = header.total_chunks;
    }
  }
  
  if (frame->chunks.size() < header.total_chunks) {
    frame->chunks.resize(
      header.total_chunks, std::vector<uint8_t>(CHUNKHEADER_SIZE + PAYLOAD)
    );
  }

  for (int i = 0; i < header.total_chunks; i++) {
    header.chunk_index = static_cast<uint16_t>(i);
    const int remaining = header.total_size - (i * PAYLOAD);
    header.chunk_size = static_cast<uint32_t>(min(PAYLOAD, remaining));

    uint8_t* packet = frame->chunks[header.chunk_index].data();

    ChunkHeader n_header = HostToNetwork(header);

    std::memcpy(packet, &n_header, CHUNKHEADER_SIZE);
    std::memcpy(packet + CHUNKHEADER_SIZE, data + (i * PAYLOAD), header.chunk_size);
    {
      socket_.async_send_to(
        asio::buffer(
          packet, static_cast<size_t>(CHUNKHEADER_SIZE + header.chunk_size)
        ), 
        ENDPOINT, 
        [this, frame](const asio::error_code& error, std::size_t bytes_transferred) { 
          std::lock_guard<std::mutex> lock(frame->ref_count_lock);
          frame->ref_count--; 
        }
      );
    }
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
    [this](const asio::error_code& error, std::size_t bytes_transferred) {
      if ((!error || error == asio::error::message_size) 
          && bytes_transferred >= CHUNKHEADER_SIZE) {
        ChunkHeader header;
        std::memcpy(&header, recv_buffer_.data(), CHUNKHEADER_SIZE);
        NetworkToHost(&header);
        threads_->Enqueue([this, header] {
          __HandlePacket(header);
        });
      }
      if (running_) __Receive();
    }
  );
}

void Sender::__HandlePacket(ChunkHeader header) {
  SendingFrame* frame = nullptr;
  // TO DO: Optimize finding `req.id` frame; current is O(n)
  // `buffer_` requires circular indexing so cannot be `unordered_map`.
  for (int i = 0; i < buffer_.size(); i++) {
    std::lock_guard<std::mutex> lock(buffer_[i]->ref_count_lock);
    if (buffer_[i]->id == header.id && buffer_[i]->ref_count > 0) {
      frame = buffer_[i].get();
      frame->ref_count++;
      break;
    }
  }

  if (!frame) return;
  
  // Change type flag to RESEND
  header.transmission_type = 1;

  ChunkHeader n_header = HostToNetwork(header);

  // Overwrite chunk header (for changed type to "RESEND")
  std::memcpy(frame->chunks[header.chunk_index].data(), &n_header, CHUNKHEADER_SIZE);

  socket_.async_send_to(
    asio::buffer(
      frame->chunks[header.chunk_index].data(), CHUNKHEADER_SIZE + header.chunk_size
    ), 
    ENDPOINT, 
    [this, frame](const asio::error_code& error, std::size_t bytes_transferred) { 
      std::lock_guard<std::mutex> lock(frame->ref_count_lock);
      frame->ref_count--; 
    }
  );
}

}