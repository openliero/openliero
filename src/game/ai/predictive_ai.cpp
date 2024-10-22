#define _USE_MATH_DEFINES
#include "predictive_ai.hpp"
#include <cmath>

#include <cfloat>
#include <limits>
#include <sstream>
#include "../game.hpp"
#include "../gfx/blit.hpp"
#include "../gfx/renderer.hpp"
#include "../stats.hpp"

double totalHealth(Worm* w) {
  if (w->lives == 0)
    return -10000.0;
  int h = std::max(w->health, 0);
  if (!w->visible)
    h = w->settings->health;
  return w->lives * (w->settings->health * 5.0 / 4.0) + h;
}

double totalHealthNorm(Worm* w) {
  return totalHealth(w) * 100.0 / w->settings->health;
}

// 0..6*len(w->weapons)
double totalAmmoWorth(const Game& game, Worm* w) {
  double ammoWorth = 0;
  for (int i = 0; i < 5; ++i) {
    const WormWeapon& weap = w->weapons[i];
    Weapon const& winfo = *weap.type;

    if (weap.loadingLeft > 0)
      ammoWorth += 3.0 - (weap.loadingLeft * 3.0) / winfo.loadingTime;
    else
      ammoWorth += 3.0 + (weap.ammo * 3.0) / winfo.ammo;
  }

  return ammoWorth;
}

int readyWeapons(const Game& game, Worm* w) {
  int count = 0;
  for (int i = 0; i < 5; ++i) {
    const WormWeapon& weap = w->weapons[i];

    if (weap.loadingLeft == 0 && weap.delayLeft < 70)
      ++count;
  }

  return count;
}

int wormDistance(Worm* from, Worm* to) {
  return vectorLength(
      ftoi(to->pos.x) - ftoi(from->pos.x), ftoi(to->pos.y) - ftoi(from->pos.y));
}

// langle in fixed point 0..128
double langleToRadians(int langle) {
  double a = ((langle + itof(32)) & (itof(128) - 1)) *
             0.04908738521234051935097880286374 / 65536.0;
  return a;
}

inline int normalizedLangle(int langle) {
  if (langle < itof(64))
    langle = itof(128) - langle;
  return langle;
}

inline double radianDiff(double a, double b) {
  double aimDiff = b - a;
  while (aimDiff < -M_PI)
    aimDiff += 2 * M_PI;
  while (aimDiff > M_PI)
    aimDiff -= 2 * M_PI;
  return aimDiff;
}

double aimingDiff(Worm* from, Worm* to) {
  double xo = std::abs(to->pos.x - from->pos.x);
  double yo = to->pos.y - from->pos.y;

  double angleToTarget = xo != 0 || yo != 0 ? std::atan2(yo, xo) : 0;

  int aim = normalizedLangle(from->aimingAngle);

  double currentAim = langleToRadians(aim);

  double tolerance = M_PI / 8;
  double aimDiff = std::abs(radianDiff(angleToTarget, currentAim));

  return std::max(aimDiff - tolerance, 0.0) / 6.0;
}

int obstacles(Game& game, gvl::ivec2 from, gvl::ivec2 to) {
  typedef gvl::basic_vec<double, 2> dvec2;

  dvec2 org(from.x, from.y);
  dvec2 dir(to.x, to.y);
  dir -= org;

  double l = length(dir);
  dir /= l;

  int obst = 0;

  for (double d = 0; d < l; d += 2.0) {
    dvec2 p = org + dir * d;

    auto m = game.common->materials[game.level.checkedPixelWrap(
        static_cast<int>(p.x), static_cast<int>(p.y))];

    if (!m.background()) {
      if (m.dirt())
        obst += 2;
      else
        obst += 3;
    } else {
      obst += 0;
    }
  }

  return obst;
}

int obstacles(Game& game, Worm* from, Worm* to) {
  double orgX = from->pos.x / 65536.0;
  double orgY = from->pos.y / 65536.0;
  double dirX = (to->pos.x - from->pos.x) / 65536.0;
  double dirY = (to->pos.y - from->pos.y) / 65536.0;

  double l = std::sqrt(dirX * dirX + dirY * dirY);
  dirX /= l;
  dirY /= l;

  int obst = 0;

  for (double d = 0; d < l; d += 2.0) {
    double px = orgX + dirX * d;
    double py = orgY + dirY * d;

    auto m = game.common->materials[game.level.checkedPixelWrap(
        static_cast<int>(px), static_cast<int>(py))];

    if (!m.background()) {
      if (m.dirt())
        obst += 2;
      else
        obst += 3;
    } else {
      obst += 0;
    }
  }

  return obst;
}

double
aimingDiff(AiContext& context, Game& game, const Worm* from, level_cell* cell) {
  if (!cell || !cell->parent)
    return 1.0;

  auto org = context.dlevel.coords(cell);
  auto orgl = context.dlevel.coords_level(cell);

  double dirx = 0, diry = 0;
  auto* c = cell;

  if (from->index == 0) {
    int count = 15;
    dirx = 1;
    while (c->parent && count-- > 0) {
      c = static_cast<level_cell*>(c->parent);
      auto path = context.dlevel.coords_level(c);

      if ((path.x != orgl.x && path.y != orgl.y) &&
          obstacles(game, orgl, path) > 4)
        break;

      dirx = std::abs(path.x - orgl.x);
      diry = path.y - orgl.y;
    }
  } else {
    int count = 10;

    while (count-- > 0) {
      c = static_cast<level_cell*>(c->parent);
      if (!c)
        break;

      auto path = context.dlevel.coords(c);

      int xdiff = std::abs(path.x - org.x);
      int ydiff = path.y - org.y;

      double len = std::sqrt(xdiff * xdiff + ydiff * ydiff);
      dirx += xdiff / len;
      diry += ydiff / len;
    }
  }

  int aim = normalizedLangle(from->aimingAngle);
  double currentAim = langleToRadians(aim);

  double angleToTarget = std::atan2(diry, dirx);

  double tolerance = M_PI / 8;
  double aimDiff = std::abs(radianDiff(angleToTarget, currentAim));

  return std::max(aimDiff - tolerance, 0.0) / 6.0;
}

inline int weaponChangeOffset(int wantedWeapon, int currentWeapon) {
  int offset = wantedWeapon - currentWeapon;
  offset = ((offset + 2 + 5) % 5) - 2;
  return offset;
}

enum MutationType { MtIdentity, MtRange, MtOptimize };

struct MutationStrategy {
  MutationStrategy(MutationType type, uint32_t start = 0, uint32_t stop = 0)
      : type(type), start(start), stop(stop) {}

  static MutationStrategy identity() { return MutationStrategy(MtIdentity); }

  static MutationStrategy optimize() { return MutationStrategy(MtOptimize); }

  MutationType type;
  uint32_t start;
  uint32_t stop;
};

InputState
generate(FollowAI& ai, std::mt19937& rand, const InputContext& prev) {
  return ai.model.random(prev, rand);
}

double sigmoid(double x) {
  return 1.0 / (1.0 + exp(-x));
}

// psigmoid(0) = 0
// psigmoid(1) ~= 0.5
// psigmoid(inf) = 1
double psigmoid(double x) {
  return (sigmoid(x) - 0.5) * 2.0;
}

level_cell* AiContext::pathFind(int x, int y) {
  auto* pf_cell = dlevel.cell_from_px(x, y);
  bool path = dlevel.run(
      [=] { return pf_cell->state == path_node::closed; }, level_cell_succ());
  return path ? pf_cell : 0;
}

double evaluateState(
    FollowAI& ai,
    Worm* me,
    Game& game,
    const InputContext& context,
    Worm* target,
    Game& orgGame,
    std::size_t index) {
  double score = 0;

  Weights weights = ai.weights;

  int posx = ftoi(me->pos.x), posy = ftoi(me->pos.y);
  auto* wormCell = ai.pathFind(posx, posy);
  const Worm* meOrg = orgGame.wormByIdx(me->index);

  double len = 200.0;
  if (wormCell) {
    double optimalDist = 10.0;

    if (readyWeapons(orgGame, orgGame.wormByIdx(me->index)) <= 1) {
      optimalDist = 50.0;
    }

    double d =
        std::max(std::abs(wormCell->g / 256.0 - optimalDist) - 10.0, 0.0) *
        weights.distanceWeight;
    len *= psigmoid(d / 100.0);
  } else {
    len += wormDistance(me, target) / 10.0;
  }

  if (meOrg->steerableCount == 1) {
    const auto* missileCell =
        ai.dlevel.cell_from_px(me->steerableSumX, me->steerableSumY);
    bool missilePath = ai.dlevel.run(
        [=] { return missileCell->state == path_node::closed; },
        level_cell_succ());

    if (missilePath && wormCell && missileCell->g < wormCell->g) {
      double closer = static_cast<double>(wormCell->g) - missileCell->g;
      score += psigmoid((closer / static_cast<double>(wormCell->g))) * 100.0 *
               weights.missileWeight;
    }
  }

  double meHealth = totalHealthNorm(me);
  double targetHealth = totalHealthNorm(target);

  score += meHealth * weights.healthWeight * weights.defenseWeight;
  score -= targetHealth * weights.healthWeight;

  if (game.settings->gameMode == Settings::GameMode::Holdazone &&
      game.holdazone.holderIdx == me->index) {
    double aimDiff = aimingDiff(me, target);
    score -= aimDiff * 2.0 * weights.aimWeight;
  } else {
    double aimDiff = aimingDiff(ai, game, me, wormCell);
    score -= aimDiff * 2.0 * weights.aimWeight;
  }

  {
    double meAmmoWorth = totalAmmoWorth(game, me);

    score += meAmmoWorth * 2.0 * weights.ammoWeight;
  }

  if (game.settings->gameMode == Settings::GameMode::Holdazone) {
    double scale = 1.0 / 4.0;
    if (game.holdazone.holderIdx != me->index)
      scale = 1.0;

    score -= len * scale;
  } else if (game.settings->gameMode == Settings::GameMode::GameOfTag) {
    if (game.lastKilledIdx >= 0 && game.lastKilledIdx != me->index)
      score += len;
    else
      score -= len;
  } else {
    score -= len;
  }

  if (!me->visible && !me->ready) {
  } else if (me->visible) {
  }

  if (game.settings->gameMode == Settings::GameMode::Holdazone) {
    if (game.holdazone.holderIdx == me->index)
      score += 50.0;
    if (game.holdazone.contenderIdx == me->index)
      score += game.holdazone.contenderFrames * 30.0 / 70.0;

    if (game.holdazone.holderIdx == target->index)
      score -= 50.0;
    if (game.holdazone.contenderIdx == target->index)
      score -= game.holdazone.contenderFrames * 30.0 / 70.0;
  }

  return score;
}

double EvaluateResult::weightedScore() const {
  double r = 0.0;

  for (std::size_t i = 1; i < scoreOverTime.size(); ++i) {
    double weight = double(scoreOverTime.size() - i) / scoreOverTime.size();

    double diff = scoreOverTime[i] * weight;
    r += diff;
  }

  return r + futureScore;
}

void SimpleAI::process(Game& game, Worm& worm) {
  Worm* target = game.wormByIdx(worm.index ^ 1);

  auto cs = worm.controlStates;

  if (!worm.visible) {
    cs.set(static_cast<int>(Worm::Control::Fire), true);
  } else {
    int aim = normalizedLangle(worm.aimingAngle);
    double currentAim = langleToRadians(aim);

    double dirx = std::abs(target->pos.x - worm.pos.x);
    double diry = target->pos.y - worm.pos.y;
    double angleToTarget = std::atan2(diry, dirx);

    double tolerance = 2 * M_PI / 32.0;

    double aimDiff = radianDiff(angleToTarget, currentAim);

    bool fire = aimDiff >= -tolerance && aimDiff <= tolerance &&
                obstacles(game, &worm, target) < 4;
    {
      cs = initial;
      cs.set(static_cast<int>(Worm::Control::Down), aimDiff < -tolerance);
      cs.set(static_cast<int>(Worm::Control::Up), aimDiff > tolerance);
      cs.set(
          static_cast<int>(Worm::Control::Fire),
          fire || initial[static_cast<int>(Worm::Control::Fire)]);
      cs.set(static_cast<int>(Worm::Control::Change), false);

      if (cs[static_cast<int>(Worm::Control::Fire)] &&
          target->pos.x < worm.pos.x &&
          worm.direction != static_cast<int>(Worm::Direction::Left)) {
        cs.set(static_cast<int>(Worm::Control::Left), true);
        cs.set(static_cast<int>(Worm::Control::Right), false);
      } else if (
          cs[static_cast<int>(Worm::Control::Fire)] &&
          target->pos.x > worm.pos.x &&
          worm.direction != static_cast<int>(Worm::Direction::Right)) {
        cs.set(static_cast<int>(Worm::Control::Left), false);
        cs.set(static_cast<int>(Worm::Control::Right), true);
      }
    }
  }

  worm.controlStates = cs;
}

void evaluate(
    EvaluateResult& result,
    FollowAI& ai,
    const Worm* me,
    Game& game,
    const Worm* target,
    Plan& plan,
    std::size_t planSize,
    MutationStrategy const& ms) {
  Game copy(game);
  copy.postClone(game);
  copy.quickSim = true;

  Worm* meCopy = copy.wormByIdx(me->index);
  Worm* targetCopy = copy.wormByIdx(target->index);

  auto context = ai.currentContext;

  SimpleAI simpleAi;

  simpleAi.initial = targetCopy->controlStates;

  double prevS = evaluateState(ai, meCopy, copy, context, targetCopy, game, 0);

  if (result.scoreOverTime.size() < planSize + 1)
    result.scoreOverTime.resize(planSize + 1);
  result.scoreOverTime[0] = 0.0;

  std::vector<int> weaponChangesLeft;
  if (ms.type == MtOptimize) {
    weaponChangesLeft.resize(planSize);
    int changesLeft = 0;
    for (std::size_t j = std::min(planSize, plan.size()); j-- > 0;) {
      if (plan[j].isFiring()) {
        changesLeft = 0;
      } else if (plan[j].isNeutral()) {
        ++changesLeft;
      }
      weaponChangesLeft[j] = changesLeft;
    }
  }

  for (std::size_t i = 0; i < planSize; ++i) {
    if (plan.size() <= i) {
      plan.push_back(generate(ai, ai.rand, context));
    }

    if (ms.type == MtIdentity) {
      // Do nothing
    } else if (ms.type == MtRange) {
      if (i >= ms.start && i < ms.stop) {
        plan[i] = generate(ai, ai.rand, context);
      }
    } else if (ms.type == MtOptimize) {
      // If current InputState is move/jump/fire neutral, make it change weapon
      // to a loading weapon as long as there's time to change back

      if (plan[i].isNeutral() &&
          meCopy->weapons[meCopy->currentWeapon].loadingLeft == 0) {
        // Out of all loading weapons that are at most weaponChangesLeft[i] - 1
        // away from a loaded weapon, pick the one that is closest to loaded.
        for (int w = 0; w < 5; ++w) {
          if (meCopy->weapons[w].loadingLeft > 0) {
            int distanceFromCurrent =
                std::abs(weaponChangeOffset(w, meCopy->currentWeapon));
            int loadedDistance = 5;
            for (int lw = 0; lw < 5; ++lw) {
              if (meCopy->weapons[lw].loadingLeft == 0) {
                loadedDistance = std::min(
                    loadedDistance, std::abs(weaponChangeOffset(lw, w)));
              }
            }

            int totalDistance = distanceFromCurrent + loadedDistance;
            if (totalDistance <= weaponChangesLeft[i]) {
              plan[i] = InputState::compose(InputState::ChangeWeapon, w, 0, 0);
              break;
            }
          }
        }
      }
    }

    meCopy->controlStates = context.update(plan[i], copy, meCopy, ai);

    simpleAi.process(copy, *targetCopy);

    copy.processFrame();

    double s =
        evaluateState(ai, meCopy, copy, context, targetCopy, game, i + 1);

    if (game.settings->aiTraces) {
      int t = 119 - static_cast<int>(i * (119 - 104 + 1) / planSize);

      ai.evaluatePositions.push_back(
          std::make_tuple(gvl::ivec2(meCopy->pos.x, meCopy->pos.y), t));
    }

    result.scoreOverTime[i + 1] = s - prevS;

    prevS = s;
  }
}

void mutate(
    EvaluateResult& result,
    FollowAI& ai,
    Game& game,
    const Worm& worm,
    const Worm* target,
    Plan& candidate,
    EvaluateResult const& prevResult) {
  MutationStrategy ms(MtRange, 0, static_cast<uint32_t>(candidate.size()));

  {
    // Find the minimum suffix sum
    uint32_t j = static_cast<uint32_t>(prevResult.scoreOverTime.size() - 1);

    double sum = prevResult.scoreOverTime[j];
    double min = sum;
    auto minj = j;

    while (j-- > 0) {
      sum += prevResult.scoreOverTime[j];
      if (sum < min) {
        minj = j;
        min = sum;
      }
    }

    // TODO code segfaults after old PRNG removed, find out why we
    // have to remove -1 from ms.stop/ms.start calculations below
    if (std::uniform_int_distribution<int>(0, 8 - 1)(ai.rand) < 7) {
      ms.stop = std::uniform_int_distribution<int>(0, minj)(ai.rand);
      ms.start = std::uniform_int_distribution<int>(
          std::max(ms.stop, static_cast<uint32_t>(10)) - 10, ms.stop)(ai.rand);
    } else {
      ms.start = std::uniform_int_distribution<int>(
          0, static_cast<uint32_t>(candidate.size()))(ai.rand);
      ms.stop = std::uniform_int_distribution<int>(
          ms.start,
          std::min(ms.start + 10, static_cast<uint32_t>(candidate.size())))(
          ai.rand);
    }
  }

  evaluate(
      result, ai, &worm, game, target, candidate, game.settings->aiFrames, ms);
}

void FollowAI::drawDebug(
    Game& game,
    Worm const& worm,
    Renderer& renderer,
    int offsX,
    int offsY) {
  for (const auto& p : evaluatePositions) {
    gvl::ivec2 v;
    PalIdx t;
    std::tie(v, t) = p;
    renderer.bmp.setPixel(ftoi(v.x) + offsX, ftoi(v.y) + offsY, t);
  }
}

void FollowAI::process(Game& game, Worm& worm) {
  Common& common = *game.common;

  Worm* target = game.worms[worm.index ^ 1];

  {
    int targetx = ftoi(target->pos.x), targety = ftoi(target->pos.y);

    if (game.settings->gameMode == Settings::GameMode::Holdazone) {
      targetx = game.holdazone.rect.center_x();
      targety = game.holdazone.rect.center_y();
    }

    dlevel.build(game.level, common);
    auto* targetCell = dlevel.cell_from_px(targetx, targety);
    targetCell->cost = 1;
    dlevel.set_origin(targetCell);
  }

  update(*this, worm);

  double bestScore = -std::numeric_limits<double>::infinity();
  evaluatePositions.clear();

  {
    unsigned int candIdx;

    evaluationBudget +=
        (game.settings->aiMutations + 1) * game.settings->aiFrames;

    std::vector<std::pair<double, int>> prio;

    for (candIdx = 0; candIdx < candPlan.size(); ++candIdx) {
      auto& cand = candPlan[candIdx];

      if (cand.prevResultAge < 2 && !cand.prevResult.scoreOverTime.empty()) {
      } else if (cand.prevResult.scoreOverTime.empty()) {
        evaluate(
            cand.prevResult, *this, &worm, game, target, cand.plan,
            game.settings->aiFrames,
            testing ? MutationStrategy::optimize()
                    : MutationStrategy::identity());
        evaluationBudget -= game.settings->aiFrames;
        cand.prevResultAge = 0;
      } else {
        evaluate(
            cand.prevResult, *this, &worm, game, target, cand.plan,
            game.settings->aiFrames / 2,
            testing ? MutationStrategy::optimize()
                    : MutationStrategy::identity());
        evaluationBudget -= game.settings->aiFrames / 2;
        cand.prevResultAge = 0;
      }

      double weightedScore = cand.prevResult.weightedScore();
      if (weightedScore >= bestScore) {
        bestScore = weightedScore;
        best = &cand;
      }

      prio.push_back(std::make_pair(weightedScore, candIdx));
    }

    std::sort(
        prio.begin(), prio.end(),
        [](std::pair<double, int> const& a, std::pair<double, int> const& b) {
          return a.first > b.first;
        });

    for (candIdx = 0; evaluationBudget > 0;) {
      auto& cand = candPlan[prio[candIdx].second];
      auto candidate = cand.plan;
      EvaluateResult result;
      mutate(result, *this, game, worm, target, candidate, cand.prevResult);
      evaluationBudget -= game.settings->aiFrames;

      double weightedScore = result.weightedScore();
      if (weightedScore > bestScore) {
        cand.plan = std::move(candidate);
        best = &cand;
        bestScore = weightedScore;
        cand.prevResult = std::move(result);
        cand.prevResultAge = 0;
      } else {
        candIdx = (candIdx + 1) % candPlan.size();
      }
    }
  }

  worm.controlStates = currentContext.update(best->plan[0], game, &worm, *this);

  if (frame < 70 * 2 * 60) {
    model.update(currentContext, best->plan[0]);
  }

  for (auto& p : candPlan) {
    p.plan.erase(p.plan.begin());

    // Shift result
    p.prevResult.scoreOverTime.erase(p.prevResult.scoreOverTime.begin());
    p.prevResult.scoreOverTime.push_back(0.0);
    ++p.prevResultAge;
  }

  ++frame;
}

Worm::ControlState InputContext::update(
    InputState newState,
    const Game& game,
    Worm* worm,
    const AiContext& aiContext) {
  Worm::ControlState cs;

  if (worm->visible && wantedWeapon != worm->currentWeapon) {
    int offset = weaponChangeOffset(wantedWeapon, worm->currentWeapon);
    cs.set(static_cast<int>(Worm::Control::Left), offset < 0);
    cs.set(static_cast<int>(Worm::Control::Right), offset > 0);
    cs.set(static_cast<int>(Worm::Control::Change), true);
  } else {
    int pa, pb, pc;
    switch (newState.decompose(pa, pb, pc)) {
      case InputState::MoveJumpFire: {
        cs.set(static_cast<int>(Worm::Control::Up), (pa >> 1) & 1);
        cs.set(static_cast<int>(Worm::Control::Down), pa & 1);
        cs.set(static_cast<int>(Worm::Control::Left), (pb >> 1) & 1);
        cs.set(static_cast<int>(Worm::Control::Right), pb & 1);
        cs.set(static_cast<int>(Worm::Control::Fire), (pc >> 1) & 1);
        cs.set(static_cast<int>(Worm::Control::Jump), pc & 1);
        break;
      }

      case InputState::ChangeWeapon: {
        if (wantedWeapon != pa) {
          wantedWeapon = pa;
          cs.set(static_cast<int>(Worm::Control::Change), true);
        }
        break;
      }

      case InputState::RopeUpDown: {
        cs.set(static_cast<int>(Worm::Control::Up), (pa >> 1) & 1);
        cs.set(static_cast<int>(Worm::Control::Down), pa & 1);
        cs.set(static_cast<int>(Worm::Control::Change), 1);
        if (pa == 0)
          cs.set(static_cast<int>(Worm::Control::Jump), 1);
        break;
      }
    }
  }

  currentState = newState;

  if (!worm->visible && !worm->ready)
    ++hiddenFrames;
  else
    hiddenFrames = 0;

  facingEnemy =
      (game.worms[worm->index ^ 1]->pos.x > worm->pos.x) == worm->direction;
  ninjaropeOut = worm->ninjarope.out;

  return cs;
}

void transToM(
    const Weights& weights,
    double& p,
    int pa,
    int pb,
    int pc,
    int facingEnemy,
    int ninjaropeOut,
    int pa2,
    int pb2,
    int pc2) {
  assert(pa < 3 && pb < 4 && pc < 4);
  assert(pa2 < 3 && pb2 < 4 && pc2 < 4);
  assert(facingEnemy < 2 && ninjaropeOut < 2);

  if (pa == 0)
    p *= select(pa2, 0.1, 0.45, 0.45);  // Start aiming
  if (pa == 1)
    p *= select(pa2, 0.025, 0.9, 0.075);  // Aiming down
  if (pa == 2)
    p *= select(pa2, 0.025, 0.075, 0.9);  // Aiming up

  if (pb == 0)
    p *= select(pb2, 0.05, 0.4, 0.4, 0.15);  // Not moving

  if (pb == 1)
    p *= select(pb2, 0.005, 0.965, 0.005, 0.025);  // Moving right
  if (pb == 2)
    p *= select(pb2, 0.005, 0.005, 0.965, 0.025);  // Moving left
  if (pb == 3)
    p *= select(pb2, 0.1, 0.35, 0.35, 0.2);  // Digging

  // Fire
  double startShootFacingP = 0.15 * weights.firingWeight;
  double startShootUnfacingP = 0.03 * weights.firingWeight;
  double startShootP = facingEnemy ? startShootFacingP : startShootUnfacingP;
  p *= select(
      (pc & 2) | ((pc2 & 2) >> 1), 1.0 - startShootP, startShootP, 0.4, 0.6);

  // Jump
  double startJump = ninjaropeOut ? 0.015 : 0.1;
  p *= select(
      ((pc & 1) << 1) | (pc2 & 1), 1.0 - startJump,
      startJump,      // 0 -> 0, 0 -> 1
      0.999, 0.001);  // 1 -> 0, 1 -> 1
}

TransModel::TransModel(Weights& weights, bool testing) {
  for (int i = 0; i < this->states; ++i) {
    int facingEnemy, ninjaropeOut;
    auto prev = InputContext::unpack(i, facingEnemy, ninjaropeOut);
    int pa, pb, pc;
    auto type = prev.decompose(pa, pb, pc);

    for (int j = 0; j < this->freeStates; ++j) {
      int pa2, pb2, pc2;
      auto type2 = InputState(j).decompose(pa2, pb2, pc2);

      double p = 1;

      if (type == InputState::MoveJumpFire) {
        double m = 0.95, c = 0.03, r = 0.02;

        c += 0.03;
        m -= 0.03;

        if (type2 == InputState::MoveJumpFire) {
          p *= m;
          transToM(
              weights, p, pa, pb, pc, facingEnemy, ninjaropeOut, pa2, pb2, pc2);
        } else if (type2 == InputState::ChangeWeapon) {
          p *= c;
          p *= 1.0 / 5.0;  // Each weapon has equal probability
        } else if (type2 == InputState::RopeUpDown) {
          p *= r;
          p *= select(pa2, 0.98, 0.01, 0.01);  // TODO: Rope out
        }
      } else if (type == InputState::ChangeWeapon) {
        if (type2 == InputState::MoveJumpFire) {
          // Same as from the idle m-state
          p *= 0.97;
          transToM(
              weights, p, 0, 0, 0, facingEnemy, ninjaropeOut, pa2, pb2, pc2);
        } else if (type2 == InputState::ChangeWeapon) {
          p *= 0.001;      // Very little chance
          p *= 1.0 / 5.0;  // Each weapon has equal probability
        } else if (type2 == InputState::RopeUpDown) {
          p *= 0.029;
          p *= select(pa2, 0.98, 0.01, 0.01);  // TODO: Rope out
        }
      } else if (type == InputState::RopeUpDown) {
        if (type2 == InputState::MoveJumpFire) {
          // Same as from the idle state
          p *= 0.95;
          transToM(
              weights, p, 0, 0, 0, facingEnemy, ninjaropeOut, pa2, pb2, pc2);
        } else if (type2 == InputState::ChangeWeapon) {
          p *= 0.049;
          p *= 1.0 / 5.0;  // Each weapon has equal probability
        } else if (type2 == InputState::RopeUpDown) {
          p *= 0.001;                          // Very little chance
          p *= select(pa2, 0.98, 0.01, 0.01);  // TODO: Rope out
        }
      }

      trans[i][j] = p;
    }

    auto& v = trans[i];
    double sum = std::accumulate(v, v + this->freeStates, 0.0);

    if (sum < 0.999 || sum > 1.0001)
      printf("%d: %f\n", i, sum);
  }
}

void AiContext::update(const FollowAI& ai, Worm& worm) {
  double presence = 0, damage = 0;

  if (worm.visible) {
    presence = 4.0 / (70.0 * 5.0);
  }

  double hp = totalHealthNorm(&worm);

  if (hp < prevHp) {
    damage = (prevHp - hp);
  }

  incArea(worm.pos.x, worm.pos.y, presence, damage);

  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      state[x][y].presence =
          std::max(state[x][y].presence - 0.5 / (70.0 * 5.0), 0.0);
    }
}
