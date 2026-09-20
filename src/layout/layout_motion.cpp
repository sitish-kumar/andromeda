#include "layout/layout_motion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace umbriel {

  namespace {

    int lerpEdge(int from, int to, double progress) {
      return static_cast<int>(std::lround(from + (to - from) * progress));
    }

    bool rangesOverlap(int aLo, int aHi, int bLo, int bHi) { return aLo < bHi && bLo < aHi; }

    int64_t overlapArea(const wlr_box& a, const wlr_box& b) {
      const int width = std::max(0, std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x));
      const int height = std::max(0, std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y));
      return static_cast<int64_t>(width) * height;
    }

    struct CollapseScore {
      int64_t largestIncrease = 0;
      int64_t totalIncrease = 0;
      int64_t centreTravel = 0;
      int64_t overlapIntegral = 0;

      [[nodiscard]] auto rank() const {
        return std::tie(largestIncrease, totalIncrease, centreTravel, overlapIntegral);
      }
    };

    CollapseScore collapseScore(const wlr_box& from, const wlr_box& to, std::span<const MotionBox> neighbours) {
      constexpr int kSamples = 128;
      CollapseScore score;
      std::vector<int64_t> previous;
      previous.reserve(neighbours.size());
      for (const MotionBox& neighbour : neighbours) {
        const int64_t overlap = overlapArea(from, neighbour.from);
        previous.push_back(overlap);
        score.overlapIntegral += overlap;
      }
      for (int step = 1; step <= kSamples; ++step) {
        const double progress = static_cast<double>(step) / kSamples;
        const wlr_box ghost = interpolateBox(from, to, progress);
        for (size_t i = 0; i < neighbours.size(); ++i) {
          const MotionBox& neighbour = neighbours[i];
          const int64_t overlap = overlapArea(ghost, interpolateBox(neighbour.from, neighbour.to, progress));
          const int64_t increase = std::max<int64_t>(0, overlap - previous[i]);
          score.largestIncrease = std::max(score.largestIncrease, increase);
          score.totalIncrease += increase;
          score.overlapIntegral += overlap;
          previous[i] = overlap;
        }
      }
      const int64_t centreDx = (2LL * to.x + to.width) - (2LL * from.x + from.width);
      const int64_t centreDy = (2LL * to.y + to.height) - (2LL * from.y + from.height);
      score.centreTravel = std::abs(centreDx) + std::abs(centreDy);
      return score;
    }

  } // namespace

  wlr_box interpolateBox(const wlr_box& from, const wlr_box& to, double progress) {
    const int left = lerpEdge(from.x, to.x, progress);
    const int top = lerpEdge(from.y, to.y, progress);
    const int right = lerpEdge(from.x + from.width, to.x + to.width, progress);
    const int bottom = lerpEdge(from.y + from.height, to.y + to.height, progress);
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
  }

  bool boxesOverlap(const wlr_box& a, const wlr_box& b) {
    if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0) {
      return false;
    }
    return rangesOverlap(a.x, a.x + a.width, b.x, b.x + b.width)
        && rangesOverlap(a.y, a.y + a.height, b.y, b.y + b.height);
  }

  wlr_box confineToNeighbours(const wlr_box& anchor, std::span<const MotionBox> neighbours, bool anchorIsTo) {
    const int anchorRight = anchor.x + anchor.width;
    const int anchorBottom = anchor.y + anchor.height;
    int loX = std::numeric_limits<int>::min();
    int hiX = std::numeric_limits<int>::max();
    int loY = std::numeric_limits<int>::min();
    int hiY = std::numeric_limits<int>::max();
    for (const MotionBox& neighbour : neighbours) {
      const wlr_box& known = anchorIsTo ? neighbour.to : neighbour.from;
      const wlr_box& other = anchorIsTo ? neighbour.from : neighbour.to;
      const bool sharesRows = rangesOverlap(known.y, known.y + known.height, anchor.y, anchorBottom);
      const bool sharesColumns = rangesOverlap(known.x, known.x + known.width, anchor.x, anchorRight);
      if (sharesRows && known.x + known.width <= anchor.x) {
        loX = std::max(loX, other.x + other.width + (anchor.x - (known.x + known.width)));
      } else if (sharesRows && known.x >= anchorRight) {
        hiX = std::min(hiX, other.x - (known.x - anchorRight));
      } else if (sharesColumns && known.y + known.height <= anchor.y) {
        loY = std::max(loY, other.y + other.height + (anchor.y - (known.y + known.height)));
      } else if (sharesColumns && known.y >= anchorBottom) {
        hiY = std::min(hiY, other.y - (known.y - anchorBottom));
      }
    }

    wlr_box result{};
    if (loX > hiX) {
      result.x = (loX + hiX) / 2;
    } else {
      result.x = std::clamp(anchor.x, loX, hiX);
      result.width = std::clamp(anchorRight, loX, hiX) - result.x;
    }
    if (loY > hiY) {
      result.y = (loY + hiY) / 2;
    } else {
      result.y = std::clamp(anchor.y, loY, hiY);
      result.height = std::clamp(anchorBottom, loY, hiY) - result.y;
    }
    return result;
  }

  bool vacancyNeedsCollapse(
      const wlr_box& from, const wlr_box& confined, std::span<const MotionBox> neighbours, bool constrained,
      bool newlyCaptured
  ) {
    if (constrained) {
      return true;
    }
    if (newlyCaptured
        && (from.x != confined.x
            || from.y != confined.y
            || from.width != confined.width
            || from.height != confined.height)) {
      return true;
    }
    return std::ranges::any_of(neighbours, [&](const MotionBox& neighbour) {
      return boxesOverlap(from, neighbour.to);
    });
  }

  wlr_box collapseBox(const wlr_box& box, bool vertical) {
    wlr_box collapsed = box;
    if (vertical) {
      collapsed.height = 0;
    } else {
      collapsed.width = 0;
    }
    return collapsed;
  }

  wlr_box
  collapseVacancy(const wlr_box& from, const wlr_box& confined, std::span<const MotionBox> neighbours, bool vertical) {
    const wlr_box ordinary = collapseBox(confined, vertical);

    const auto candidatesForAxis = [&](bool candidateVertical) {
      std::vector<wlr_box> candidates;
      const auto add = [&](int edge) {
        wlr_box candidate = from;
        if (candidateVertical) {
          candidate.y = edge;
          candidate.height = 0;
        } else {
          candidate.x = edge;
          candidate.width = 0;
        }
        if (std::ranges::none_of(candidates, [&](const wlr_box& existing) {
              return existing.x == candidate.x
                  && existing.y == candidate.y
                  && existing.width == candidate.width
                  && existing.height == candidate.height;
            })) {
          candidates.push_back(candidate);
        }
      };
      if (candidateVertical) {
        add(from.y);
        add(from.y + from.height);
        add(confined.y);
        add(confined.y + confined.height);
        for (const MotionBox& neighbour : neighbours) {
          add(neighbour.to.y);
          add(neighbour.to.y + neighbour.to.height);
        }
      } else {
        add(from.x);
        add(from.x + from.width);
        add(confined.x);
        add(confined.x + confined.width);
        for (const MotionBox& neighbour : neighbours) {
          add(neighbour.to.x);
          add(neighbour.to.x + neighbour.to.width);
        }
      }
      return candidates;
    };

    const auto bestCandidate = [&](bool candidateVertical) {
      std::optional<std::pair<wlr_box, CollapseScore>> best;
      for (const wlr_box& candidate : candidatesForAxis(candidateVertical)) {
        const CollapseScore score = collapseScore(from, candidate, neighbours);
        if (!best || score.rank() < best->second.rank()) {
          best = std::pair{candidate, score};
        }
      }
      return best;
    };

    std::pair<wlr_box, CollapseScore> chosen{ordinary, collapseScore(from, ordinary, neighbours)};
    const auto consider = [&](const std::optional<std::pair<wlr_box, CollapseScore>>& candidate) {
      if (candidate && candidate->second.rank() < chosen.second.rank()) {
        chosen = *candidate;
      }
    };
    // Evaluate both axes for every layout. `vertical` remains the stable tie-break through the order here, while the
    // score selects the axis that actually drains this transition without making inherited overlap worse.
    consider(bestCandidate(vertical));
    consider(bestCandidate(!vertical));
    return chosen.first;
  }

  bool keepsSeparation(const MotionBox& a, const MotionBox& b) {
    // `lo` entirely on the low side of `hi` along one axis, touching allowed.
    const auto leftOf = [](const wlr_box& lo, const wlr_box& hi) { return lo.x + lo.width <= hi.x; };
    const auto above = [](const wlr_box& lo, const wlr_box& hi) { return lo.y + lo.height <= hi.y; };
    return (leftOf(a.from, b.from) && leftOf(a.to, b.to))
        || (leftOf(b.from, a.from) && leftOf(b.to, a.to))
        || (above(a.from, b.from) && above(a.to, b.to))
        || (above(b.from, a.from) && above(b.to, a.to));
  }

  uint64_t alignedMotionDelay(uint64_t closeRemainingMs, uint64_t moveDurationMs) {
    return closeRemainingMs > moveDurationMs ? closeRemainingMs - moveDurationMs : 0;
  }

} // namespace umbriel
