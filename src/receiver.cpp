#include "receiver.h"

namespace chunkstream {

Receiver::ReceivingFrame::ReceivingFrame(
  std::shared_ptr<asio::io_context> io_context, 
  const size_t total_chunks, 
  uint8_t* memory_pool,
  const size_t memory_pool_block_size,  
  std::function<void(const uint32_t frame_id, const uint16_t chunk_index)> request_resend_func, 
  std::function<void(uint8_t* data, const size_t size)> send_assembled_callback)
: io_context_(io_context), 
  request_resend_func_(request_resend_func), 
  send_assembled_callback_(send_assembled_callback), 
  init_chunk_timer_(*io_context_), 
  frame_drop_timer_(*io_context_), 
  resend_timer_(*io_context_), 
  INIT_CHUNK_TIMEOUT(20), 
  FRAME_DROP_TIMEOUT(100), 
  RESEND_TIMEOUT(20), 
  BLOCK_SIZE(memory_pool_block_size) {

  chunk_bitmap_.resize(total_chunks, false);
  data_ = memory_pool;
}

bool Receiver::ReceivingFrame::IsChunkAdded(const uint16_t chunk_index) {
  return chunk_bitmap_[chunk_index];
}

bool Receiver::ReceivingFrame::IsTimeout() {
  return request_timeout_;
}

// @data should be `recv_buffer_.data() + CHUNKHEADER_SIZE`
void Receiver::ReceivingFrame::AddChunk(const ChunkHeader& header, uint8_t* data) {
  std::memcpy(
    data_ + (header.chunk_index * BLOCK_SIZE),
    data, 
    header.chunk_size
  );
  chunk_bitmap_[header.chunk_index] = true;

  // Check all chunks are added
  bool all_chunk_added = true;
  for (int i = chunk_bitmap_.size() - 1; i >= 0; i--) {
    if (!chunk_bitmap_[i]) {
      all_chunk_added = false;
      break;
    }
  }
  if (all_chunk_added) {
    frame_drop_timer_.cancel();
    request_resend_ = false;
    init_chunk_timer_.cancel();
    send_assembled_callback_(data_, header.total_size);
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

void Receiver::ReceivingFrame::__RequestResend(const uint32_t id) {
  frame_drop_timer_.expires_after(FRAME_DROP_TIMEOUT);
  frame_drop_timer_.async_wait([this](const asio::error_code& ec) {
    if (!ec) {
      request_resend_ = false;
      request_timeout_ = true;
    }
  });
  
  __PeriodicResend(id); // Recursively call
}

void Receiver::ReceivingFrame::__PeriodicResend(const uint32_t id) {
  if (!request_resend_) return;
  
  for (int i = 0; i < chunk_bitmap_.size(); i++) {
    if (!chunk_bitmap_[i]) {
      request_resend_func_(id, i);
    }
  }
  
  resend_timer_.expires_after(RESEND_TIMEOUT);
  resend_timer_.async_wait([this, id](const asio::error_code& ec) {
    if (!ec) __PeriodicResend(id);
  });
}

Receiver::Receiver(const int port, 
                   std::function<void(const std::vector<uint8_t>&)> grab, 
                   const size_t buffer_size, 
                   const size_t max_data_size) 
: grabbed_(grab),
  socket_(*io_context_, asio::ip::udp::endpoint(asio::ip::udp::v4(), port)), 
  BUFFER_SIZE(buffer_size), 
  BLOCK_SIZE(max_data_size)
{
  threads_ = std::make_shared<ThreadPool>(std::thread::hardware_concurrency());
  data_memory_pool_.resize(BLOCK_SIZE * BUFFER_SIZE);
}

Receiver::~Receiver() {
  Stop();
}

void Receiver::Start() {
  running_ = true;
  __Receive();
  io_context_->run();
}

void Receiver::Stop() {
  running_ = false;
  io_context_->stop();
  dropped_count_ = 0;
  assembled_count_ = 0;
}

size_t Receiver::GetFrameCount() {
  return assembled_count_;
}

size_t Receiver::GetDropCount() {
  return dropped_count_;
}

void Receiver::__Receive() {
  socket_.async_receive_from(
    asio::buffer(recv_buffer_), remote_endpoint_,
    std::bind(&Receiver::__HandlePacket, this,
      std::placeholders::_1,
      std::placeholders::_2
    )
  );
}

void Receiver::__HandlePacket(const asio::error_code& error, std::size_t bytes_transferred) {
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

    threads_->Enqueue([this, header]() {
      if (buffer_.empty()
          || (buffer_.front().first < header.id && !buffer_.find(header.id))) {
        // Push new frame
        buffer_.push_back(
          header.id, 
          ReceivingFrame(
            io_context_, 
            header.total_chunks, 
            data_memory_pool_.data() + (BLOCK_SIZE * data_memory_pool_index_), 
            BLOCK_SIZE, 
            std::bind(&Receiver::__RequestResend, this, std::placeholders::_1, std::placeholders::_2), 
            std::bind(&Receiver::__FrameGrabbed, this, std::placeholders::_1, std::placeholders::_2)
          )
        );

        // Indexing memory pool
        data_memory_pool_index_++;
        if (data_memory_pool_index_ >= BUFFER_SIZE) data_memory_pool_index_ = 0;

        // Buffering
        while (buffer_.size() >= BUFFER_SIZE) {
          // const uint32_t& drop_frame_id = buffer_.front().first;
          // const ReceivingFrame& drop_frame = buffer_.front().second;
          // 여기서 drop_frame 에 대한 어떤 조취를 취해야 하지 않을까?
          buffer_.pop_front();
        }
      } else {
        ReceivingFrame* frame = buffer_.find(header.id);
        if (frame && !frame->IsTimeout() && !frame->IsChunkAdded(header.chunk_index)) {
          // Push chunk to the frame
          frame->AddChunk(header, recv_buffer_.data() + CHUNKHEADER_SIZE);
        } else {
          // drop packet
          dropped_count_++;
        }
      }
    });

    if (running_) __Receive();
  }
}

void Receiver::__RequestResend(const uint32_t frame_id, const uint16_t chunk_index) {

}

void Receiver::__FrameGrabbed(uint8_t* data, const size_t size) {
  if (grabbed_ && data && size > 0) {
    std::vector<uint8_t> buffer(data, data + size);
    assembled_count_++;
    grabbed_(std::move(buffer));
  }
}

}