#include "layout/layout_motion.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace umbriel {

  namespace {

    int lerpEdge(int from, int to, double progress) {
      return static_cast<int>(std::lround(from + (to - from) * progress));
    }

    bool rangesOverlap(int aLo, int aHi, int bLo, int bHi) { return aLo < bHi && bLo < aHi; }

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

  bool keepsSeparation(const MotionBox& a, const MotionBox& b) {
    // `lo` entirely on the low side of `hi` along one axis, touching allowed.
    const auto leftOf = [](const wlr_box& lo, const wlr_box& hi) { return lo.x + lo.width <= hi.x; };
    const auto above = [](const wlr_box& lo, const wlr_box& hi) { return lo.y + lo.height <= hi.y; };
    return (leftOf(a.from, b.from) && leftOf(a.to, b.to))
        || (leftOf(b.from, a.from) && leftOf(b.to, a.to))
        || (above(a.from, b.from) && above(a.to, b.to))
        || (above(b.from, a.from) && above(b.to, a.to));
  }

} // namespace umbriel
