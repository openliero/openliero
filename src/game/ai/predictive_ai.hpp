#pragma once

#include <SDL.h>

#include <algorithm>
#include <functional>
#include <gvl/math/vec.hpp>
#include <numeric>
#include <random>
#include "../math.hpp"
#include "../worm.hpp"
#include "dijkstra.hpp"

struct InputState {
  enum Type {
    MoveJumpFire = 0,  // 48, aabb0cc, a != 3
    ChangeWeapon = 1,  // 5,  aaa, 0..4, translated to 0010100 or 0001100
    RopeUpDown = 2,    // 3,  aa00110, a != 3
  };

  InputState(const Worm* w) {
    auto cs = w->controlStates;
    auto v = cs.pack();

    if (!cs[static_cast<int>(Worm::Control::Change)]) {
      // MoveJumpFire
      // TODO check out `git blame` for this particular code if there are any
      // issues. idx was assigned/overwritten a few times *prior*.
      idx = 2;
      idx |= (v & 1);
    } else {
      if (!cs[static_cast<int>(Worm::Control::Jump)]) {
        // ChangeWeapon
        idx = 48 + w->currentWeapon;
      } else {
        // RopeUpDown
        idx = 48 + 5 + (v >> 5);
      }
    }
  }

  InputState(int idx = 0) : idx(idx) {}

  int idx;  // 0..56

  bool isNeutral() const {
    int pa, pb, pc;
    auto type = decompose(pa, pb, pc);
    return type == ChangeWeapon;
  }

  bool isFiring() const {
    int dummy, pc;
    return decompose(dummy, dummy, pc) == MoveJumpFire && ((pc >> 1) & 1);
  }

  Type decompose(int& pa, int& pb, int& pc) const {
    int i = idx;
    if (i < 48) {
      pa = i >> 4;
      pb = (i >> 2) & 3;
      pc = i & 3;
      return MoveJumpFire;
    }
    i -= 48;

    if (i < 5) {
      pa = i;
      return ChangeWeapon;
    }
    i -= 5;

    if (i < 3) {
      pa = i;
      return RopeUpDown;
    }

    assert(false);
    return MoveJumpFire;
  }

  static InputState compose(Type type, int pa, int pb, int pc) {
    int idx = 0;
    switch (type) {
      case MoveJumpFire: {
        idx = (pa << 4) | (pb << 2) | pc;
        break;
      }

      case ChangeWeapon: {
        idx = 48 + pa;
        break;
      }

      case RopeUpDown: {
        idx = 48 + 5 + pa;
        break;
      }
    }

    return InputState(idx);
  }
};

typedef std::vector<InputState> Plan;

template <typename T>
inline T select(int n, T first) {
  assert(n == 0);
  return first;
}

template <typename T>
inline T select(int n, T first, T a) {
  if (n == 0)
    return first;
  return select(n - 1, a);
}

template <typename T>
inline T select(int n, T first, T a, T b) {
  if (n == 0)
    return first;
  return select(n - 1, a, b);
}

template <typename T>
inline T select(int n, T first, T a, T b, T c) {
  if (n == 0)
    return first;
  return select(n - 1, a, b, c);
}

struct AiContext;

struct InputContext {
  InputContext()
      : wantedWeapon(0), hiddenFrames(0), facingEnemy(0), ninjaropeOut(0) {}

  Worm::ControlState update(
      InputState newState,
      const Game& game,
      Worm* worm,
      const AiContext& aiContext);

  int pack() {
    int i = ninjaropeOut;
    i = i * 2 + facingEnemy;
    i = i * 56 + currentState.idx;
    return i;
  }

  static InputState unpack(int idx, int& facingEnemy, int& ninjaropeOut) {
    int s = idx % 56;
    idx /= 56;
    facingEnemy = idx % 2;
    idx /= 2;
    ninjaropeOut = idx;
    return InputState(s);
  }

  static int const Size = 56 * 2 * 2;

  // Free part
  InputState currentState;

  // Dependent part
  int wantedWeapon;
  int hiddenFrames;
  int facingEnemy;
  int ninjaropeOut;
};

template <int States, int FreeStates>
struct Model {
  static int const states = States;
  static int const freeStates = FreeStates;
  double trans[States][FreeStates];

  int random(int context, std::mt19937& rand) {
    assert(context < States);
    auto& v = trans[context];

    double max, el = 0.0;

    max = std::accumulate(v, v + FreeStates, 0.0);
    if (max != 0.0) {
      el = std::uniform_real_distribution<double>(0.0, max)(rand);
    }

    for (int i = 0; i < FreeStates; ++i) {
      el -= v[i];
      if (el < 0.0)
        return i;
    }

    return FreeStates - 1;
  }
};

struct Weights {
  Weights()
      : healthWeight(1.0),
        aimWeight(1.0),
        distanceWeight(1.0),
        ammoWeight(1.0),
        missileWeight(1.0),
        defenseWeight(1.3),
        firingWeight(1.0) {}

  double healthWeight, aimWeight, distanceWeight, ammoWeight, missileWeight;
  double defenseWeight, firingWeight;
};

struct TransModel : Model<InputContext::Size, 56> {
  TransModel(Weights& weights, bool testing);

  void update(InputContext context, InputState v) {
    trans[context.pack()][v.idx] += 0.005;
  }

  InputState random(InputContext context, std::mt19937& rand) {
    return InputState(
        Model<InputContext::Size, 56>::random(context.pack(), rand));
  }
};

struct CellState {
  double presence;
  double damage;  // Health decrease in this cell
};

struct FollowAI;

struct AiContext {
  static int const width = (504 + 31) >> 5;
  static int const height = (350 + 31) >> 5;

  AiContext() : prevHp(0), maxDamage(0), maxPresence(0) {}

  dijkstra_level dlevel;

  CellState state[width][height];

  int prevHp;
  double maxDamage, maxPresence;

  void incArea(int fx, int fy, double presence, double damage) {
    int wx = ftoi(fx) >> 5;
    int wy = ftoi(fy) >> 5;

    for (int y = wy - 1; y <= wy + 1; ++y)
      for (int x = wx - 1; x <= wx + 1; ++x) {
        if (y >= 0 && y < height && x >= 0 && x < width) {
          double d = 1.0;
          if (x != wx)
            d *= 0.5;
          if (y != wy)
            d *= 0.5;
          auto& c = state[x][y];
          c.presence = d * presence;
          c.damage += d * damage;
          maxDamage = std::max(maxDamage, c.damage);
          maxPresence = std::max(maxPresence, c.presence);
        }
      }
  }

  CellState& cell(int fx, int fy) {
    int wx = ftoi(fx) >> 5;
    int wy = ftoi(fy) >> 5;

    wx = std::max(std::min(wx, width), 0);
    wy = std::max(std::min(wy, height), 0);

    return state[wx][wy];
  }

  void update(const FollowAI& ai, Worm& worm);
  level_cell* pathFind(int x, int y);
};

struct EvaluateResult {
  EvaluateResult() : futureScore(0.0) {}

  double weightedScore() const;

  std::vector<double> scoreOverTime;
  double futureScore;
};

struct SimpleAI : WormAI {
  void process(Game& game, Worm& worm) override;

  Worm::ControlState initial;
};

struct AIThread {
  AIThread() : th(0) {}

  SDL_Thread* th;
};

struct CandPlan {
  CandPlan() : prevResultAge(0) {}

  Plan plan;
  EvaluateResult prevResult;
  int prevResultAge;
};

struct FollowAI : WormAI, AiContext {
  FollowAI(
      Weights weights,
      int candPopSize,
      bool testing,
      FollowAI* targetAiInit = 0)
      : frame(0),
        model(weights, testing),
        evaluationBudget(0),
        effectScaler(0),
        targetAi(targetAiInit),
        candPlan(candPopSize),
        best(0),
        testing(testing),
        weights(weights) {}

  ~FollowAI() override {}

  void process(Game& game, Worm& worm) override;

  void drawDebug(
      Game& game,
      Worm const& worm,
      Renderer& renderer,
      int offsX,
      int offsY) override;

  std::mt19937 rand;
  int frame;
  InputContext currentContext;
  TransModel model;
  int evaluationBudget;

  std::vector<std::tuple<gvl::ivec2, PalIdx>> evaluatePositions;

  std::vector<double> negEffect, posEffect;
  int effectScaler;

  FollowAI* targetAi;

  std::vector<CandPlan> candPlan;
  CandPlan* best;

  bool testing;

  Weights weights;
};
