#include "chunkstream/receiver.h"
#include <iostream>

namespace chunkstream {

Receiver::Receiver(const int port, 
                   std::function<void(const std::vector<uint8_t>&, std::function<void()>) > grab, 
                   const int mtu, 
                   const size_t buffer_size, 
                   const size_t max_data_size) 
: grabbed_(grab),
  BUFFER_SIZE(buffer_size),
  MTU(mtu), 
  PAYLOAD(MTU - 20 - 8 - CHUNKHEADER_SIZE), 
  data_pool_(max_data_size, buffer_size), 
  raw_pool_(mtu - 20 - 8, 
            ((max_data_size + PAYLOAD - 1) / PAYLOAD) * buffer_size),
  resend_pool_(CHUNKHEADER_SIZE, buffer_size)
{
  try {
    socket_ = std::make_unique<asio::ip::udp::socket>(
      *io_context_, 
      asio::ip::udp::endpoint(asio::ip::udp::v4(), port)
    );
    threads_ = std::make_shared<ThreadPool>(std::thread::hardware_concurrency());
  } catch (const std::exception& e) {
    std::cerr << "Error initializing Receiver: " << e.what() << std::endl;
    throw;
  }
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
  if (!recv_buf) {
    std::cerr << "Receive error: Buffer overflow; bigger buffer_size is required" << std::endl;
    return;
  } 
  socket_->async_receive_from(
    asio::buffer(recv_buf, raw_pool_.BLOCK_SIZE), 
    remote_endpoint_,
    [this, recv_buf](
      const std::error_code& error, std::size_t bytes_transferred
    ) {
      if (error) {
        std::cerr << "Receive error(" << error << "): " << error.message() << std::endl;
      }
      if (!error && bytes_transferred >= CHUNKHEADER_SIZE) {
        // threads_->Enqueue([this, recv_buf]() { __HandlePacket(recv_buf); });
        try {
          __HandlePacket(remote_endpoint_, recv_buf);
        } catch (const std::error_code& error) {
          std::cerr << "Handling packet error(" << error << "): " << error.message() << std::endl;
        }
      }
      if (running_) __Receive();
    }
  );
  // if (running_) __Receive();
}

void Receiver::__HandlePacket(const asio::ip::udp::endpoint& sender_endpoint, uint8_t* recv_buf) {
  ChunkHeader header;

  std::memcpy(&header, recv_buf, CHUNKHEADER_SIZE);
  
  NetworkToHost(&header);

  std::cout << "Receive ChunkHeader(" << header.id << "): " << (header.chunk_index + 1) << " / " << header.total_chunks << " from " << sender_endpoint.address() << ":" << sender_endpoint.port();

  if (assembling_queue_.empty()
      || (/*assembling_queue_.front().first < header.id &&*/
         !assembling_queue_.find(header.id) && 
         header.transmission_type == 0)) {
    
    std::cout << " >> New" << std::endl;
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
        sender_endpoint, 
        header.id, 
        header.total_chunks, 
        data_pool_starting, 
        // data_pool_.BLOCK_SIZE,
        PAYLOAD, 
        std::bind(&Receiver::__RequestResend, this, std::placeholders::_1, std::placeholders::_2), 
        std::bind(&Receiver::__FrameGrabbed, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), 
        [this](const uint32_t id, uint8_t* data) { // Dropped callback
          std::cout << "Frame " << id << " dropped" << std::endl;
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
      std::cerr << "Receive error: Buffer overflow; bigger buffer_size is required" << std::endl;
    }
  } else {
    std::cout << " >";
    auto* frame_ptr_ptr = assembling_queue_.find(header.id);
    std::cout << ">";
    if (frame_ptr_ptr && *frame_ptr_ptr && !(*frame_ptr_ptr)->IsTimeout() && !(*frame_ptr_ptr)->IsChunkAdded(header.chunk_index)) {
      std::cout << "> ";
      // Push chunk to the frame
      (*frame_ptr_ptr)->AddChunk(header, recv_buf + CHUNKHEADER_SIZE);
      std::cout << "Exist" << std::endl;
    } else {
      std::cout << "Drop packet" << std::endl;
    }
  }
  raw_pool_.Release(recv_buf);
}

void Receiver::__RequestResend(const ChunkHeader header, const asio::ip::udp::endpoint endpoint) {
  const ChunkHeader n_header = HostToNetwork(header);
  uint8_t* data = resend_pool_.Acquire();
  std::memcpy(data, &n_header, CHUNKHEADER_SIZE);
  /*
  socket_->async_send_to(
    asio::buffer(data, CHUNKHEADER_SIZE), 
    endpoint, 
    [this, data, header](const std::error_code& error, std::size_t bytes_transferred) { 
      std::cout << "Send resend(" << header.id << "): " << (header.chunk_index + 1) << " / " << header.total_chunks << std::endl;
      if (error) {
        std::cerr << "Send request error(" << error << "): " << error.message() << std::endl;
      }
      resend_pool_.Release(data); 
    }
  );
  */
  try {
    size_t len = socket_->send_to(
      asio::buffer(data, CHUNKHEADER_SIZE), 
      endpoint
    );
  } catch (const std::error_code& error) {
    std::cout << "Send resend(" << header.id << "): " << (header.chunk_index + 1) << " / " << header.total_chunks << std::endl;
    if (error) {
      std::cerr << "Send request error(" << error << "): " << error.message() << std::endl;
    }
  }
  resend_pool_.Release(data); 
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