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

// TO DO: Test this method
// It also delete frames whose status is ASSEMBLING.
void Receiver::Flush() {
  while (!assembling_queue_.empty()) {
    uint8_t* data = assembling_queue_.front().second->GetData();
    assembling_queue_.pop_front();
    data_pool_.Release(data);
  }
}

size_t Receiver::GetFrameCount() const {
  return assembled_count_;
}

size_t Receiver::GetDropCount() const {
  return dropped_count_;
}

void Receiver::__Receive() {
  uint8_t* recv_buf = raw_pool_.Acquire();
  if (!recv_buf) {
    std::cerr << "Receive error: Buffer overflow; bigger max_data_size is required" << std::endl;
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
        raw_pool_.Release(recv_buf);
      }
      if (running_) __Receive();
    }
  );
}

void Receiver::__HandlePacket(const asio::ip::udp::endpoint& sender_endpoint, uint8_t* recv_buf) {
  
  ChunkHeader header;
  std::memcpy(&header, recv_buf, CHUNKHEADER_SIZE);
  
  NetworkToHost(&header);
  
  if (assembling_queue_.empty()
      || (/*assembling_queue_.front().first < header.id &&*/
         !assembling_queue_.find(header.id) && 
         header.transmission_type == 0)) {
    
    // Buffering
    while (!dropped_queue_.empty()) {
      const std::pair<uint32_t, uint8_t*> dropped = dropped_queue_.front();
      dropped_queue_.pop();
      //std::cout << "Drop frame " << dropped.first << std::endl;
      assembling_queue_.erase(dropped.first);
      //std::cout << " >> drop frame " << dropped.first << " from queue" << std::endl;
      data_pool_.Release(dropped.second);
      //std::cout << " >> drop frame " << dropped.first << " from data pool" << std::endl;
    }

    uint8_t* data_pool_starting = data_pool_.Acquire();
    
    if (data_pool_starting) {
      // std::lock_guard<std::mutex> lock(assembling_queue_push_mutex_);

      auto frame_ptr = std::make_shared<ReceivingFrame>(
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
          //std::cout << "Frame " << id << " dropped" << std::endl;
          dropped_queue_.push({id, data});
          dropped_count_++;
        }
      );

      // Push new frame
      assembling_queue_.push_back(header.id, frame_ptr);
      
      //std::cout << "Recv(" << header.id << "): " << (header.chunk_index + 1) << " / " << header.total_chunks << " >> New" << std::endl;

      // Push chunk to the frame
      //std::cout << " >> Before Add Chunk";
      frame_ptr->AddChunk(header, recv_buf + CHUNKHEADER_SIZE);
      //std::cout << " >> After Add Chunk";
    } else {
      // Buffer is full, drop packet
      std::cerr << "Receive error: Buffer overflow; bigger buffer_size is required" << std::endl;
    }
  } else {
    auto* frame_ptr = assembling_queue_.find(header.id);
    if (frame_ptr && *frame_ptr && !(*frame_ptr)->IsTimeout() && !(*frame_ptr)->IsChunkAdded(header.chunk_index)) {

      // Push chunk to the frame
      (*frame_ptr)->AddChunk(header, recv_buf + CHUNKHEADER_SIZE);
      
      //std::cout << "Recv(" << header.id << "): " << (header.chunk_index + 1) << " / " << header.total_chunks << " >> Exist" << std::endl;
    } else {
      //std::cout << "Drop packet";
    }
  }
  // std::cout << " / buf=" << assembling_queue_.size() << std::endl;
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
    //std::cout << "Requested #" << header.id << "'s " << (header.chunk_index + 1) << " / " << header.total_chunks << std::endl;
  } catch (const std::error_code& error) {
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
    //std::cout << "Received #" << id << ", size: " << size << std::endl;
    grabbed_(
      std::move(buffer), 
      [this, id, data]() { // Delegate responsibility for freeing buffers to the user 
        //std::cout << "  (" << id << ") is released by user." << std::endl;
        assembling_queue_.erase(id); // Release assembling_queue_
        data_pool_.Release(data);
      }
    );
  } else {
    //std::cout << "  (" << id << ") is released by system." << std::endl;
    assembling_queue_.erase(id);
    data_pool_.Release(data);
  }
}

}