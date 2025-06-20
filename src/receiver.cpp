#include "chunkstream/receiver.h"

namespace chunkstream {

Receiver::Receiver(const int port, 
                   std::function<void(const std::vector<uint8_t>&, std::function<void()>) > grab, 
                   const int mtu, 
                   const size_t buffer_size, 
                   const size_t max_data_size, 
                   std::string sender_ip, 
                   const int sender_port) 
: grabbed_(grab),
  socket_(*io_context_, asio::ip::udp::endpoint(asio::ip::udp::v4(), port)), 
  BUFFER_SIZE(buffer_size), 
  data_pool_(max_data_size, buffer_size), 
  raw_pool_(mtu - 20 - 8, 
            ((max_data_size + (mtu - 20 - 8 - CHUNKHEADER_SIZE) - 1) 
            / (mtu - 20 - 8 - CHUNKHEADER_SIZE)) * buffer_size),
  resend_pool_(CHUNKHEADER_SIZE, buffer_size), 
  SENDER_ENDPOINT(asio::ip::address::from_string(sender_ip), sender_port)
{
  threads_ = std::make_shared<ThreadPool>(std::thread::hardware_concurrency());
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
  uint8_t* recv_buf = raw_pool_.Acquire();

  socket_.async_receive_from(
    // asio::buffer(recv_buf, RAW_POOL_BLOCK_SIZE), 
    asio::buffer(recv_buf, raw_pool_.BLOCK_SIZE), 
    remote_endpoint_,
    [this, recv_buf](
      const asio::error_code& error, std::size_t bytes_transferred
    ) {
      if ((!error || error == asio::error::message_size) && bytes_transferred >= CHUNKHEADER_SIZE) {
        threads_->Enqueue([this, recv_buf]() {
          __HandlePacket(recv_buf);
        });
      }
    }
  );
  if (running_) __Receive();
}

void Receiver::__HandlePacket(uint8_t* recv_buf) {
  ChunkHeader header;

  std::memcpy(&header, recv_buf, CHUNKHEADER_SIZE);
  
  NetworkToHost(&header);

  if (assembling_queue_.empty()
      || (assembling_queue_.front().first < header.id && !assembling_queue_.find(header.id))) {
    
    // Buffering
    while (!dropped_queue_.empty()) {
      const std::pair<uint32_t, uint8_t*> dropped = dropped_queue_.front();
      dropped_queue_.pop();
      assembling_queue_.erase(dropped.first);
      data_pool_.Release(dropped.second);
    }

    uint8_t* data_pool_starting = data_pool_.Acquire();
    
    if (data_pool_starting) {
      std::lock_guard<std::mutex> lock(assembling_queue_push_mutex_);

      auto frame_ptr = std::make_unique<ReceivingFrame>(
        io_context_, 
        header.id, 
        header.total_chunks, 
        data_pool_starting, 
        data_pool_.BLOCK_SIZE,
        std::bind(&Receiver::__RequestResend, this, std::placeholders::_1), 
        std::bind(&Receiver::__FrameGrabbed, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
        [this](const uint32_t id, uint8_t* data) { // Dropped callback
          dropped_queue_.push({id, data});
          dropped_count_++;
        }
      );

      // Push chunk to the frame
      frame_ptr->AddChunk(header, recv_buf + CHUNKHEADER_SIZE);

      // Push new frame
      assembling_queue_.push_back(header.id, std::move(frame_ptr));
    } else {
      // Buffer is full, drop packet
    }
  } else {
    auto* frame_ptr_ptr = assembling_queue_.find(header.id);
    if (frame_ptr_ptr && *frame_ptr_ptr && !(*frame_ptr_ptr)->IsTimeout() && !(*frame_ptr_ptr)->IsChunkAdded(header.chunk_index)) {
      // Push chunk to the frame
      (*frame_ptr_ptr)->AddChunk(header, recv_buf + CHUNKHEADER_SIZE);
    } else {
      // drop packet
    }
  }
  raw_pool_.Release(recv_buf);
}

void Receiver::__RequestResend(const ChunkHeader header) {
  const ChunkHeader n_header = HostToNetwork(header);
  uint8_t* data = resend_pool_.Acquire();
  std::memcpy(data, &n_header, CHUNKHEADER_SIZE);
  socket_.async_send_to(
    asio::buffer(data, CHUNKHEADER_SIZE), 
    SENDER_ENDPOINT, 
    [this, data](const asio::error_code& error, std::size_t bytes_transferred) { resend_pool_.Release(data); }
  );
}

void Receiver::__FrameGrabbed(const uint32_t id, uint8_t* data, const size_t size) {
  if (!data || size <= 0) {
    return; // error condition
  }
  assembled_count_++;
  if (grabbed_) {
    std::vector<uint8_t> buffer(data, data + size);
    grabbed_(
      std::move(buffer), 
      [this, id, data]() { // Delegate responsibility for freeing buffers to the user 
        assembling_queue_.erase(id); // Release assembling_queue_
        data_pool_.Release(data);
      }
    );
  } else {
    assembling_queue_.erase(id);
    data_pool_.Release(data);
  }
}

}