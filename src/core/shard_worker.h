#pragma once

#include <chrono>
#include <functional>
#include <stop_token>
#include <thread>
#include <vector>

#include "core/dispatcher.h"
#include "core/mpsc_queue.h"
#include "core/session_packet.h"

namespace ejd::core {

class ShardWorker {
 public:
  using ResponseSink = std::function<void(std::vector<SessionPacket>)>;

  ShardWorker(Dispatcher& dispatcher, ResponseSink deliver);
  void Push(SessionPacket packet) { queue_.Push(std::move(packet)); }

 private:
  void Loop(std::stop_token st);
  static constexpr auto kTickInterval = std::chrono::milliseconds(100);
  Dispatcher& dispatcher_;
  ResponseSink deliver_;
  MpscQueue<SessionPacket> queue_;

  // 마지막 소멸을 위한 최하단 선언
  std::jthread thread_;
};

}  // namespace ejd::core
