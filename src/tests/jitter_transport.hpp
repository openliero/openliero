#pragma once

// Rollback Step 7 / 7.5 — in-process transport between two
// RollbackControllers. Models per-packet random delivery delay, packet
// loss, and optional duplication. Out-of-order arrival happens naturally
// because per-packet delays are picked from a uniform distribution
// independently — a packet sent later may have a smaller delay than one
// sent earlier and arrive first.
//
// As of Step 7.5 the transport carries batched input packets
// (baseFrame, count, inputs[count]) matching RollbackController's
// InputBatchSendCallback contract. Tests typically wire each peer's send
// callback to sendAToB / sendBToA and translate received batches into
// per-frame injectRemoteInput calls.

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "rollback/buffer.hpp"

namespace rollback_test {

struct JitterTransport {
  struct Params {
    uint32_t seed = 0xC0FFEE;
    int minDelayFrames = 0;
    int maxDelayFrames = 0;
    double lossProbability = 0.0;
    double duplicateProbability = 0.0;
  };

  static constexpr std::size_t kMaxBatch = rollback::kMaxRollback + 1;

  struct InFlight {
    int deliverAtFrame;
    uint32_t baseFrame;
    uint8_t count;
    std::array<uint8_t, kMaxBatch> inputs;
  };

  using Deliver = std::function<
      void(uint32_t baseFrame, uint8_t count, uint8_t const* inputs)>;

  Params params;
  std::mt19937 rng;
  std::vector<InFlight> aToB;
  std::vector<InFlight> bToA;
  int currentFrame = 0;

  uint64_t packetsSent = 0;
  uint64_t packetsDropped = 0;
  uint64_t packetsDuplicated = 0;

  explicit JitterTransport(Params p) : params(p), rng(p.seed) {}

  int randomDelay() {
    int lo = params.minDelayFrames;
    int hi = params.maxDelayFrames;
    if (hi <= lo) return lo;
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng);
  }

  bool roll(double p) {
    if (p <= 0.0) return false;
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(rng) < p;
  }

  void sendAToB(uint32_t baseFrame, uint8_t count, uint8_t const* inputs) {
    enqueue(aToB, baseFrame, count, inputs);
  }
  void sendBToA(uint32_t baseFrame, uint8_t count, uint8_t const* inputs) {
    enqueue(bToA, baseFrame, count, inputs);
  }

  // Drain anything whose deliverAtFrame has elapsed, then advance the
  // clock. Within a queue, packets scan in send order — an earlier-sent
  // packet may carry a later deliverAtFrame than a packet queued after
  // it, exactly modelling jittery out-of-order delivery.
  void tick(Deliver const& deliverA, Deliver const& deliverB) {
    drainDue(aToB, deliverB);
    drainDue(bToA, deliverA);
    ++currentFrame;
  }

  // Force-deliver every in-flight packet. Used at the end of a test to
  // converge both peers regardless of how late tail packets were.
  void flush(Deliver const& deliverA, Deliver const& deliverB) {
    for (auto const& p : aToB) deliverB(p.baseFrame, p.count, p.inputs.data());
    for (auto const& p : bToA) deliverA(p.baseFrame, p.count, p.inputs.data());
    aToB.clear();
    bToA.clear();
  }

  bool empty() const { return aToB.empty() && bToA.empty(); }

 private:
  void enqueue(std::vector<InFlight>& q, uint32_t baseFrame, uint8_t count,
               uint8_t const* inputs) {
    ++packetsSent;
    if (roll(params.lossProbability)) {
      ++packetsDropped;
      return;
    }
    InFlight p{};
    p.deliverAtFrame = currentFrame + randomDelay();
    p.baseFrame = baseFrame;
    p.count = count;
    for (uint8_t i = 0; i < count; ++i) p.inputs[i] = inputs[i];
    q.push_back(p);
    if (roll(params.duplicateProbability)) {
      ++packetsDuplicated;
      InFlight d = p;
      d.deliverAtFrame = currentFrame + randomDelay();
      q.push_back(d);
    }
  }

  void drainDue(std::vector<InFlight>& q, Deliver const& deliver) {
    auto it = q.begin();
    while (it != q.end()) {
      if (it->deliverAtFrame <= currentFrame) {
        deliver(it->baseFrame, it->count, it->inputs.data());
        it = q.erase(it);
      } else {
        ++it;
      }
    }
  }
};

}  // namespace rollback_test
