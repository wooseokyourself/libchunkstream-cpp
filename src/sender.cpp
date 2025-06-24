#include "chunkstream/sender.h"
#include <iostream>

namespace chunkstream {

template <class T> const T& min (const T& a, const T& b) {
  return !(b<a)?a:b;
}

Sender::Sender(const std::string& ip, const int port, 
               const int mtu, const size_t buffer_size, const size_t max_data_size)
  : MTU(mtu), 
    PAYLOAD(MTU - 20 - 8 - CHUNKHEADER_SIZE), // mtu - IP header - UDP header - Chunk header
    buffer_index_(0), 
    id_(0) {
  
  try {
    // Create the endpoint first to validate IP
    ENDPOINT = asio::ip::udp::endpoint(asio::ip::address::from_string(ip), port);
    
    // Initialize socket
    socket_ = std::make_unique<asio::ip::udp::socket>(
      io_context_, 
      asio::ip::udp::v4()
    );
    socket_->bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0)); // OS automatically allocates port
    
    threads_ = std::make_shared<ThreadPool>(std::thread::hardware_concurrency());
    
    if (max_data_size > 0) {
      const int total_chunks = (max_data_size + PAYLOAD - 1) / PAYLOAD;
      
      // Pre-allocate buffer
      buffer_.reserve(buffer_size);
      
      for (int i = 0; i < buffer_size; i++) {
        auto frame = std::make_unique<SendingFrame>();
        frame->id = -1;
        
        // Carefully allocate memory for chunks
        frame->chunks.reserve(total_chunks);
        for (int j = 0; j < total_chunks; j++) {
          frame->chunks.emplace_back(CHUNKHEADER_SIZE + PAYLOAD);
        }
        
        buffer_.push_back(std::move(frame));
      }
    }
    
    std::cout << "Sender Constructor completed successfully" << std::endl;
  }
  catch (const std::exception& e) {
    std::cerr << "Sender construction failed: " << e.what() << std::endl;
    throw; // Re-throw to notify caller
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
    
    //std::cout << "Send ChunkHeader(" << header.id << "): " << (header.chunk_index + 1) << " / " << header.total_chunks << std::endl;

    std::memcpy(packet, &n_header, CHUNKHEADER_SIZE);
    std::memcpy(packet + CHUNKHEADER_SIZE, data + (i * PAYLOAD), header.chunk_size);
    {
      // async
      socket_->async_send_to(
        asio::buffer(
          packet, CHUNKHEADER_SIZE + static_cast<size_t>(header.chunk_size)
        ), 
        ENDPOINT, 
        [this, frame](const std::error_code& error, std::size_t bytes_transferred) { 
          //std::cout << "Send.async_send_to Bytes transferred: " << bytes_transferred << ", msg=" << error.message() << std::endl;
          if (error) {
            std::cerr << "Send error(" << error << "): " << error.message() << std::endl;
          }
          std::lock_guard<std::mutex> lock(frame->ref_count_lock);
          frame->ref_count--; 
        }
      );
      /*
      // sync 
      try {
        size_t sent = socket_->send_to(
          asio::buffer(
            packet, CHUNKHEADER_SIZE + static_cast<size_t>(header.chunk_size)
          ), 
          ENDPOINT
        );
        std::cout << "Send succeed: " << sent << "bytes" << std::endl;
        std::lock_guard<std::mutex> lock(frame->ref_count_lock);
        frame->ref_count--;
      } catch (const std::error_code& error){
        std::cerr << "Send error(" << error << "): " << error.message() << std::endl;
      }
      */
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
  socket_->async_receive_from(
    asio::buffer(recv_buffer_), remote_endpoint_,
    [this](const std::error_code& error, std::size_t bytes_transferred) {
      if (error) {
        const int& error_code = error.value();
        if (error_code != 10054 && error_code != 10061) {
          std::cerr << "Receive error(" << error_code << "): " << error.message() << std::endl;
        }
      }
      if (!error && bytes_transferred >= CHUNKHEADER_SIZE) {
        ChunkHeader header;
        std::memcpy(&header, recv_buffer_.data(), CHUNKHEADER_SIZE);
        NetworkToHost(&header);
        try {
          __HandlePacket(header);
        } catch (const std::error_code& error) {
          std::cerr << "Handling packet error(" << error << "): " << error.message() << std::endl;
        }
      }
      if (running_) __Receive();
    }
  );
}

void Sender::__HandlePacket(ChunkHeader header) {
  SendingFrame* frame = nullptr;
  {
    // Binary search for rotated sorted array; O(log n)

    // TO DO: The case where there is a frame with id=-1 because the buffer is not full yet is not considered. Handle this case.

    int left = 0, right = buffer_.size() - 1;
    
    while (left <= right) {
      int mid = left + (right - left) / 2;
      
      if (buffer_[mid]->id == header.id) {
        if (buffer_[mid]->ref_count > 0) {
          frame = buffer_[mid].get();
          frame->ref_count++;
        }
        break;
      }
      
      // If left-side is ordered
      if (buffer_[left]->id <= buffer_[mid]->id) {
        if (header.id >= buffer_[left]->id && header.id < buffer_[mid]->id) {
          right = mid - 1;
        } else {
          left = mid + 1;
        }
      }
      // If right-side is ordered
      else {
        if (header.id > buffer_[mid]->id && header.id <= buffer_[right]->id) {
          left = mid + 1;
        } else {
          right = mid - 1;
        }
      }
    }
  }
  
  if (!frame) return;
  
  // Change type flag to RESEND
  header.transmission_type = 1;

  ChunkHeader n_header = HostToNetwork(header);

  // Overwrite chunk header (for changed type to "RESEND")
  std::memcpy(frame->chunks[header.chunk_index].data(), &n_header, CHUNKHEADER_SIZE);

  /*
  socket_->async_send_to(
    asio::buffer(
      frame->chunks[header.chunk_index].data(), CHUNKHEADER_SIZE + header.chunk_size
    ), 
    ENDPOINT, 
    [this, frame](const std::error_code& error, std::size_t bytes_transferred) { 
      std::lock_guard<std::mutex> lock(frame->ref_count_lock);
      frame->ref_count--; 
    }
  );
  */

  try {
    const size_t len = socket_->send_to(
      asio::buffer(frame->chunks[header.chunk_index].data(), CHUNKHEADER_SIZE + header.chunk_size), 
      ENDPOINT
    );
  } catch (const std::error_code& error) {
    std::cerr << "Resend error(" << error << "): " << error.message() << std::endl;
  }
  
  {
    std::lock_guard<std::mutex> lock(frame->ref_count_lock);
    frame->ref_count--; 
  }
}

}