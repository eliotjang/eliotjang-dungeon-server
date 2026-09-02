#include "core/shard_worker.h"

#include <chrono>
#include <stop_token>
#include <thread>
#include <vector>

#include "core/dispatcher.h"
#include "core/mpsc_queue.h"
#include "core/session_packet.h"

namespace ejd::core {

ShardWorker::ShardWorker(Dispatcher& dispatcher, ResponseSink deliver)
    : dispatcher_(dispatcher), deliver_(std::move(deliver)) {
  thread_ = std::jthread([this](std::stop_token st) { this->Loop(st); });
}

void ShardWorker::Loop(std::stop_token st) {
  std::vector<SessionPacket> inbound{};
  std::vector<SessionPacket> responses{};
  auto tick_at = std::chrono::steady_clock::now() + kTickInterval;

  while (!st.stop_requested()) {
    queue_.WaitDrainUntil(inbound, st, tick_at);

    for (size_t i = 0; i < inbound.size(); ++i) {
      dispatcher_.Dispatch(inbound[i], responses);
    }

    if (!responses.empty()) {
      deliver_(std::move(responses));
      responses.clear();
    }

    inbound.clear();

    if (std::chrono::steady_clock::now() >= tick_at) {
      // Tick()으로 작업 예정
      tick_at += kTickInterval;
    }
  }
}

}  // namespace ejd::core
