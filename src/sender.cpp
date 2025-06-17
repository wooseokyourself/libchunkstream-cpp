#include "sender.h"

namespace chunkstream {

Sender::Sender(const std::string& ip, const int port, const int mtu) 
: socket_(io_context_, asio::ip::udp::v4()), 
  ENDPOINT(asio::ip::address::from_string(ip), port), 
  MTU(mtu) {

}

}