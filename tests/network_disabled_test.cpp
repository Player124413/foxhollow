#include "../port/src/net/websocket.hpp"
#include <cassert>
int main() {
  auto socket = fh::net::connect_websocket("wss://example.invalid", "");
  assert(socket);
  socket->send_text("hello");
  socket->send_binary(nullptr, 0);
  fh::net::SocketEvent event;
  assert(socket->poll(event));
  assert(event.type == fh::net::SocketEvent::Type::Closed);
  assert(event.code == 4900);
  assert(event.data == "FOXHOLLOW_NETWORK_DISABLED");
  assert(!socket->poll(event));
}
