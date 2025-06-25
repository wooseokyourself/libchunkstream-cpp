#include "chunkstream/receiver/receiving_frame.h"
#include <iostream>

namespace chunkstream {

ReceivingFrame::ReceivingFrame(
  std::shared_ptr<asio::io_context> io_context, 
  const asio::ip::udp::endpoint sender_endpoint, 
  const uint32_t id, 
  const size_t total_chunks, 
  uint8_t* memory_pool,
  const size_t memory_pool_block_size, 
  std::function<void(const ChunkHeader header, 
                     const asio::ip::udp::endpoint endpoint)> request_resend_func,
  std::function<void(const uint32_t id, 
                     uint8_t* data, 
                     const size_t size)> send_assembled_callback, 
  std::function<void(const uint32_t id, uint8_t* data)> dropped_callback)
: ID(id), 
  io_context_(io_context), 
  __RequestResendCallback(request_resend_func), 
  __SendAssembledCallback(send_assembled_callback), 
  __DroppedCallback(dropped_callback), 
  init_chunk_timer_(*io_context_), 
  frame_drop_timer_(*io_context_), 
  resend_timer_(*io_context_), 
  INIT_CHUNK_TIMEOUT(20), 
  FRAME_DROP_TIMEOUT(100), 
  RESEND_TIMEOUT(20), 
  BLOCK_SIZE(memory_pool_block_size), 
  status_(ASSEMBLING) {
  
  assert(memory_pool);
  SENDER_ENDPOINT = sender_endpoint;
  chunk_bitmap_.resize(total_chunks, false);
  chunk_headers_.resize(total_chunks);
  data_ = memory_pool;
}

bool ReceivingFrame::IsChunkAdded(const uint16_t chunk_index) {
  return chunk_bitmap_[chunk_index];
}

bool ReceivingFrame::IsTimeout() {
  return request_timeout_;
}

// @data should be `recv_buffer_.data() + CHUNKHEADER_SIZE`
void ReceivingFrame::AddChunk(const ChunkHeader& header, uint8_t* data) {
  //std::cout << "ReceivingFrame::AddChunk 0" << std::endl;
  bool all_chunk_added = true;
  {
    std::lock_guard<std::mutex> lock(chunk_bitmap_mutex_);
    //std::cout << "ReceivingFrame::AddChunk 0-1" << std::endl;
    
    assert(header.chunk_index < chunk_bitmap_.size());
    //std::cout << "ReceivingFrame::AddChunk 0-2" << std::endl;

    chunk_bitmap_[header.chunk_index] = true;
    //std::cout << "ReceivingFrame::AddChunk 0-3" << std::endl;
    chunk_headers_[header.chunk_index] = header;
    //std::cout << "ReceivingFrame::AddChunk 0-4" << std::endl;

    // Check all chunks are added
    for (int i = chunk_bitmap_.size() - 1; i >= 0; i--) {
      if (!chunk_bitmap_[i]) {
        all_chunk_added = false;
        break;
      }
    }
    
  }
  //std::cout << "ReceivingFrame::AddChunk 1" << std::endl;
  assert(data != nullptr);
  assert(data_ != nullptr);
  assert((data_ + (header.chunk_index * BLOCK_SIZE)) != nullptr);
  assert((data + header.chunk_size - 1) != nullptr);
  assert((data_ + (header.chunk_index * BLOCK_SIZE) + header.chunk_size - 1) != nullptr);
  //std::cout << "ReceivingFrame::AddChunk 1-1, header.chunk_size=" << header.chunk_size << std::endl;
  std::memcpy(
    data_ + (header.chunk_index * BLOCK_SIZE),
    data, 
    header.chunk_size
  );
  //std::cout << "ReceivingFrame::AddChunk 2" << std::endl;

  if (all_chunk_added) {
    status_ = READY;
    frame_drop_timer_.cancel();
    request_resend_ = false;
    init_chunk_timer_.cancel();
    __SendAssembledCallback(ID, data_, header.total_size);
  } else {
    if (header.transmission_type == 0 && !request_resend_) { // type == INIT
      //std::cout << "ReceivingFrame::AddChunk 3-1-1" << std::endl;
      init_chunk_timer_.cancel();
      //std::cout << "ReceivingFrame::AddChunk 3-1-2" << std::endl;
      init_chunk_timer_.expires_after(INIT_CHUNK_TIMEOUT);
      //std::cout << "ReceivingFrame::AddChunk 3-1-3" << std::endl;
      init_chunk_timer_.async_wait([this, header](const std::error_code& error) {
        if (error) {
          if (error.value() != 995) { // 995 is "cancellation"
            std::cerr << "INIT_CHUNK_TIMEOUT error(" << error << "): " << error.message() << std::endl;
          }
          return;
        }
        request_resend_ = true;

        // Start frame-drop timer
        frame_drop_timer_.expires_after(FRAME_DROP_TIMEOUT);
        frame_drop_timer_.async_wait([this, id = header.id](const std::error_code& ec) {
          if (!ec) {
            request_resend_ = false;
            request_timeout_ = true;
            status_ = DROPPED;
            __DroppedCallback(ID, data_);
          }
        });

        // Start resend requesting
        __RequestResend(header.id); // Recursively call
      });
      //std::cout << "ReceivingFrame::AddChunk 3-1-4" << std::endl;
    } else { // type == RESEND
      // nothing
      //std::cout << "ReceivingFrame::AddChunk 3-2-1" << std::endl;
    }
  }
}

int ReceivingFrame::GetStatus() {
  return status_;
}

uint8_t* ReceivingFrame::GetData() {
  return data_;
}

void ReceivingFrame::__RequestResend(const uint32_t id) {
  if (!request_resend_) return;
  
  {
    std::lock_guard<std::mutex> lock(chunk_bitmap_mutex_);

    for (int i = 0; i < chunk_bitmap_.size(); i++) {
      if (!chunk_bitmap_[i]) {
        ChunkHeader req_header;
        req_header.id = id;
        req_header.chunk_index = static_cast<uint16_t>(i);
        req_header.total_chunks = static_cast<uint16_t>(chunk_bitmap_.size());
        __RequestResendCallback(req_header, SENDER_ENDPOINT);
      }
    }
  }
  
  resend_timer_.expires_after(RESEND_TIMEOUT);
  resend_timer_.async_wait([this, id](const std::error_code& error) {
    if (error) {
      if (error.value() != 995) { // 995 is "cancellation"
        std::cerr << "RESEND_TIMEOUT error(" << error << "): " << error.message() << std::endl;
      }
      return;
    }
    __RequestResend(id);
  });
}

}