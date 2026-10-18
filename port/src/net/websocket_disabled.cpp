#include "websocket.hpp"

namespace fh::net {
namespace {
class UnavailableSocket final : public WebSocket {
  bool pending = true;
public:
  void send_binary(const void*, size_t) override {}
  void send_text(std::string_view) override {}
  bool poll(SocketEvent& out) override {
    if (!pending) return false;
    pending = false;
    out = {SocketEvent::Type::Closed, "FOXHOLLOW_NETWORK_DISABLED", 4900};
    return true;
  }
};
}
std::unique_ptr<WebSocket> connect_websocket(const std::string&, const std::string&) {
  return std::make_unique<UnavailableSocket>();
}
}
