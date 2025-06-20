#include "chunkstream/receiver/receiving_frame.h"

namespace chunkstream {

ReceivingFrame::ReceivingFrame(
  std::shared_ptr<asio::io_context> io_context, 
  const uint32_t id, 
  const size_t total_chunks, 
  uint8_t* memory_pool,
  const size_t memory_pool_block_size, 
  std::function<void(const ChunkHeader header)> request_resend_func,
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
  bool all_chunk_added = true;
  {
    std::lock_guard<std::mutex> lock(chunk_bitmap_mutex_);
    
    assert(header.chunk_index < chunk_bitmap_.size());

    chunk_bitmap_[header.chunk_index] = true;
    chunk_headers_[header.chunk_index] = header;

    // Check all chunks are added
    for (int i = chunk_bitmap_.size() - 1; i >= 0; i--) {
      if (!chunk_bitmap_[i]) {
        all_chunk_added = false;
        break;
      }
    }
    
  }

  std::memcpy(
    data_ + (header.chunk_index * BLOCK_SIZE),
    data, 
    header.chunk_size
  );

  if (all_chunk_added) {
    status_ = READY;
    frame_drop_timer_.cancel();
    request_resend_ = false;
    init_chunk_timer_.cancel();
    __SendAssembledCallback(ID, data_, header.total_size);
  } else {
    if (header.transmission_type == 0 && !request_resend_) { // type == INIT
      init_chunk_timer_.cancel();
      init_chunk_timer_.expires_after(INIT_CHUNK_TIMEOUT);
      init_chunk_timer_.async_wait([this, header](const asio::error_code& ec) {
        if (!ec) {
          request_resend_ = true;
          __RequestResend(header.id);
        }
      });
    } else { // type == RESEND
      // nothing
    }
  }
}

void ReceivingFrame::__RequestResend(const uint32_t id) {
  frame_drop_timer_.expires_after(FRAME_DROP_TIMEOUT);
  frame_drop_timer_.async_wait([this](const asio::error_code& ec) {
    if (!ec) {
      request_resend_ = false;
      request_timeout_ = true;
      status_ = DROPPED;
      __DroppedCallback(ID, data_);
    }
  });
  
  __PeriodicResend(id); // Recursively call
}

void ReceivingFrame::__PeriodicResend(const uint32_t id) {
  if (!request_resend_) return;
  
  {
    std::lock_guard<std::mutex> lock(chunk_bitmap_mutex_);

    for (int i = 0; i < chunk_bitmap_.size(); i++) {
      if (!chunk_bitmap_[i]) {
        ChunkHeader req_header;
        req_header.id = id;
        req_header.chunk_index = static_cast<uint16_t>(i);
        req_header.total_chunks = static_cast<uint16_t>(chunk_bitmap_.size());
        __RequestResendCallback(req_header);
      }
    }
  }
  
  resend_timer_.expires_after(RESEND_TIMEOUT);
  resend_timer_.async_wait([this, id](const asio::error_code& ec) {
    if (!ec) __PeriodicResend(id);
  });
}

}