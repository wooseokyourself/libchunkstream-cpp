// Copyright (c) 2025 Wooseok Choi
// Licensed under the MIT License - see LICENSE file

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
    
    if (max_data_size > 0) {
      const int total_chunks = (max_data_size + PAYLOAD - 1) / PAYLOAD;
      
      // Pre-allocate buffer
      buffer_.reserve(buffer_size);
      
      for (int i = 0; i < buffer_size; i++) {
        auto frame = std::make_unique<SendingFrame>();
        frame->id = -1;
        
        frame->chunks.reserve(total_chunks);
        for (int j = 0; j < total_chunks; j++) {
          frame->chunks.emplace_back(CHUNKHEADER_SIZE + PAYLOAD);
        }
        frame->headers.resize(frame->chunks.size());
        buffer_.push_back(std::move(frame));
      }
    }
    
    //std::cout << "Sender Constructor completed successfully" << std::endl;
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

    int idx;
    {
      std::lock_guard<std::mutex> lock(buffering_mutex_);
      idx = buffer_index_.fetch_add(1) % buffer_.size();
    }
    
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
    frame->headers.resize(frame->chunks.size());
  }

  for (int i = 0; i < header.total_chunks; i++) {
    header.chunk_index = static_cast<uint16_t>(i);
    const int remaining = header.total_size - (i * PAYLOAD);
    header.chunk_size = static_cast<uint32_t>(min(PAYLOAD, remaining));
    frame->headers[header.chunk_index] = header;
    uint8_t* packet = frame->chunks[header.chunk_index].data();

    ChunkHeader n_header = HostToNetwork(header);
    
    //std::cout << "Send(" << header.id << "): " << (header.chunk_index + 1) << " / " << header.total_chunks << std::endl;

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
  
  //std::cout << "Sent #" << header.id << ", size: " << size << std::endl;
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
      // std::cerr << "Receive error(" << error << "): " << error.message() << std::endl;
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
  std::lock_guard<std::mutex> lock(buffering_mutex_);

  SendingFrame* frame = nullptr;
  {
    // Binary search for rotated sorted array; O(log n)

    // TO DO: The case where there is a frame with id=-1 because the buffer is not full yet is not considered. Handle this case.

    int left = 0, right = buffer_.size() - 1;
    
    while (left <= right) {
      int mid = left + (right - left) / 2;
      
      if (buffer_[mid]->id == header.id) {
        // std::cout << "Found resend frame header_id=" << buffer_[mid]->id << ", ref_count=" << buffer_[mid]->ref_count << std::endl;
        // if (buffer_[mid]->ref_count > 0) {
          frame = buffer_[mid].get();
          std::lock_guard<std::mutex> lock(frame->ref_count_lock);
          frame->ref_count++;
        // }
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
  
  // Change other uninitialized data
  header.total_size = frame->headers[header.chunk_index].total_size;
  header.chunk_size = frame->headers[header.chunk_index].chunk_size;

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
    /*
    std::cout << "Resend(" << header.id << "): " << (header.chunk_index + 1) << " / " << header.total_chunks; 
    const size_t len = socket_->send_to(
      asio::buffer(frame->chunks[header.chunk_index].data(), CHUNKHEADER_SIZE + static_cast<size_t>(frame->headers[header.chunk_index].chunk_size)), 
      ENDPOINT
    );
    std::cout << " bytes:" << len << std::endl;
    */
   //std::cout << "Resent #" << header.id << "'s " << (header.chunk_index + 1) << " / " << header.total_chunks;
    
    /*
    // 🔍 디버깅 정보 추가
    std::cout << "\nDEBUG: chunk_size=" << frame->headers[header.chunk_index].chunk_size 
              << ", chunks.size()=" << frame->chunks.size()
              << ", chunk_index=" << (header.chunk_index + 1) << std::endl;
    
    // 🔍 버퍼 유효성 확인
    if (header.chunk_index >= frame->chunks.size()) {
        std::cerr << "ERROR: Invalid chunk_index!" << std::endl;
        return;
    }
    
    // 🔍 소켓 상태 확인
    if (!socket_->is_open()) {
        std::cerr << "ERROR: Socket is closed!" << std::endl;
        return;
    }
    */
    
    const size_t len = socket_->send_to(
        asio::buffer(frame->chunks[header.chunk_index].data(), 
                    CHUNKHEADER_SIZE + header.chunk_size), 
        ENDPOINT
    );
    
    //std::cout << " ~sent:" << len << std::endl;
  } catch (const std::error_code& error) {
    std::cerr << "Resend error(" << error << "): " << error.message() << std::endl;
  }
  
  {
    std::lock_guard<std::mutex> lock(frame->ref_count_lock);
    frame->ref_count--; 
  }
}

}