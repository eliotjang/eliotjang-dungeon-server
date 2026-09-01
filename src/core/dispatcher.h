#pragma once

#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <cassert>

#include "core/session_packet.h"
#include "proto/messages.h"
#include "proto/packet_header.h"

namespace ejd::core {

using Handler = std::function<void(const SessionPacket& in,
                                   std::vector<SessionPacket>& out)>;

class Dispatcher {
 public:
  void Register(proto::MsgId id, Handler handler) {
    handlers_.insert_or_assign(static_cast<uint16_t>(id), std::move(handler));
  }
  void Dispatch(const SessionPacket& in, std::vector<SessionPacket>& out) {
    assert(in.packet.size() >= proto::kHeaderSize);

    proto::PacketHeader h{};
    std::memcpy(&h, in.packet.data(), sizeof(h));

    auto it = handlers_.find(h.msg_id);
    if (it == handlers_.end()) {
      std::cerr << std::format("미등록 MsgId: msg_id={}, session_id={}\n",
                               h.msg_id, in.session_id);
      return;
    }

    it->second(in, out);
  }

 private:
  std::unordered_map<uint16_t, Handler> handlers_;
};

}  // namespace ejd::core
