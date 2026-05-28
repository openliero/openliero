#pragma once

// Rollback Step 7 — in-process transport that models variable delivery
// delay between two RollbackController instances. Step 7's scope is
// rollback-on-misprediction, so this version supports per-packet random
// delay only (no loss or duplication; Step 7.5 will add those once
// input redundancy is in place).
//
// The transport is controller-agnostic: it carries (inputFrame, input)
// pairs that the caller forwards to the destination controller's
// injectRemoteInput. Each tick() advances an internal clock and delivers
// any packets whose scheduled arrival has elapsed. The RNG is seeded so
// test failures are reproducible — surface the seed from any failing
// Catch2 SECTION when reporting bugs.

#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace rollback_test {

struct JitterTransport {
  struct Params {
    uint32_t seed = 0xC0FFEE;
    int minDelayFrames = 0;
    int maxDelayFrames = 0;
  };

  struct InFlight {
    int deliverAtFrame;
    uint32_t inputFrame;
    uint8_t input;
  };

  using Deliver = std::function<void(uint32_t inputFrame, uint8_t input)>;

  Params params;
  std::mt19937 rng;
  std::vector<InFlight> aToB;
  std::vector<InFlight> bToA;
  int currentFrame = 0;

  explicit JitterTransport(Params p) : params(p), rng(p.seed) {}

  int randomDelay() {
    int lo = params.minDelayFrames;
    int hi = params.maxDelayFrames;
    if (hi <= lo) return lo;
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng);
  }

  void sendAToB(uint32_t inputFrame, uint8_t input) {
    aToB.push_back({currentFrame + randomDelay(), inputFrame, input});
  }
  void sendBToA(uint32_t inputFrame, uint8_t input) {
    bToA.push_back({currentFrame + randomDelay(), inputFrame, input});
  }

  // Deliver every packet whose deliverAtFrame has passed, then advance
  // the clock. Within a queue, packets are scanned in send order — the
  // earlier-sent packet may have a later deliverAtFrame than a packet
  // queued after it, which is exactly the out-of-order arrival pattern
  // a jittery network produces.
  void tick(Deliver const& deliverA, Deliver const& deliverB) {
    drainDue(aToB, deliverB);
    drainDue(bToA, deliverA);
    ++currentFrame;
  }

  // Hand every in-flight packet to its destination immediately. Used at
  // the end of a test to converge both peers to the same confirmedFrame
  // regardless of how late the tail packets were.
  void flush(Deliver const& deliverA, Deliver const& deliverB) {
    for (auto const& p : aToB) deliverB(p.inputFrame, p.input);
    for (auto const& p : bToA) deliverA(p.inputFrame, p.input);
    aToB.clear();
    bToA.clear();
  }

  bool empty() const { return aToB.empty() && bToA.empty(); }

 private:
  void drainDue(std::vector<InFlight>& q, Deliver const& deliver) {
    auto it = q.begin();
    while (it != q.end()) {
      if (it->deliverAtFrame <= currentFrame) {
        deliver(it->inputFrame, it->input);
        it = q.erase(it);
      } else {
        ++it;
      }
    }
  }
};

}  // namespace rollback_test
