#include "layout/layout_motion.h"

#include "check.h"

#include <algorithm>
#include <span>
#include <vector>

using umbriel::boxesOverlap;
using umbriel::collapseBox;
using umbriel::confineToNeighbours;
using umbriel::interpolateBox;
using umbriel::keepsSeparation;
using umbriel::MotionBox;

namespace {

  constexpr int kGap = 12;

  bool sameBox(const wlr_box& a, const wlr_box& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  }

  // The largest gap separating two boxes along one axis; negative when they overlap on both.
  int separation(const wlr_box& a, const wlr_box& b) {
    return std::max({b.x - (a.x + a.width), a.x - (b.x + b.width), b.y - (a.y + a.height), a.y - (b.y + b.height)});
  }

  // Every pair of members stays disjoint, at least `minGap` apart, at every shared progress step.
  void checkDisjointThroughout(std::span<const MotionBox> members, int minGap = 0) {
    for (int step = 0; step <= 20; ++step) {
      const double progress = step * 0.05;
      for (size_t i = 0; i < members.size(); ++i) {
        for (size_t j = i + 1; j < members.size(); ++j) {
          const wlr_box a = interpolateBox(members[i].from, members[i].to, progress);
          const wlr_box b = interpolateBox(members[j].from, members[j].to, progress);
          CHECK(!boxesOverlap(a, b));
          CHECK(separation(a, b) >= minGap);
        }
      }
    }
  }

  void checkAllKeepSeparation(std::span<const MotionBox> members) {
    for (size_t i = 0; i < members.size(); ++i) {
      for (size_t j = i + 1; j < members.size(); ++j) {
        CHECK(keepsSeparation(members[i], members[j]));
      }
    }
  }

  // The opener's start box lies inside the union of its neighbours' current boxes and its own slot, widened by one
  // gap: a zero-extent start sits one gap outside the neighbour it grows away from.
  void checkOpenerInsideLayout(const wlr_box& from, const wlr_box& to, std::span<const MotionBox> neighbours) {
    int left = to.x - kGap;
    int top = to.y - kGap;
    int right = to.x + to.width + kGap;
    int bottom = to.y + to.height + kGap;
    for (const MotionBox& neighbour : neighbours) {
      left = std::min(left, neighbour.from.x);
      top = std::min(top, neighbour.from.y);
      right = std::max(right, neighbour.from.x + neighbour.from.width);
      bottom = std::max(bottom, neighbour.from.y + neighbour.from.height);
    }
    CHECK(from.x >= left);
    CHECK(from.y >= top);
    CHECK(from.x + from.width <= right);
    CHECK(from.y + from.height <= bottom);
  }

  MotionBox still(const wlr_box& box) { return {box, box}; }

  MotionBox opener(const wlr_box& to, std::span<const MotionBox> neighbours) {
    return {confineToNeighbours(to, neighbours, true), to};
  }

  MotionBox ghost(const wlr_box& from, std::span<const MotionBox> neighbours, bool vertical) {
    wlr_box to = confineToNeighbours(from, neighbours, false);
    if (to.width > 0 && to.height > 0) {
      to = collapseBox(to, vertical);
    }
    return {from, to};
  }

  constexpr wlr_box kColumnA{0, 0, 400, 800};
  constexpr wlr_box kColumnB{400 + kGap, 0, 400, 800};
  constexpr wlr_box kColumnC{2 * (400 + kGap), 0, 400, 800};

} // namespace

UMBRIEL_TEST(interpolateBoxEndpointsAreExact) {
  const wlr_box from{3, 7, 100, 50};
  const wlr_box to{-40, 12, 5, 0};
  CHECK(sameBox(interpolateBox(from, to, 0.0), from));
  CHECK(sameBox(interpolateBox(from, to, 1.0), to));
}

UMBRIEL_TEST(interpolateBoxEdgesMoveMonotonically) {
  const wlr_box from{0, 0, 7, 7};
  const wlr_box to{100, 0, 13, 7};
  int lastLeft = from.x;
  int lastRight = from.x + from.width;
  for (int step = 1; step <= 20; ++step) {
    const wlr_box box = interpolateBox(from, to, step * 0.05);
    CHECK(box.x >= lastLeft);
    CHECK(box.x + box.width >= lastRight);
    lastLeft = box.x;
    lastRight = box.x + box.width;
  }
}

UMBRIEL_TEST(boxesOverlapIgnoresTouchingEdges) {
  CHECK(!boxesOverlap({0, 0, 10, 10}, {10, 0, 10, 10}));
  CHECK(!boxesOverlap({0, 0, 10, 10}, {0, 10, 10, 10}));
  CHECK(boxesOverlap({0, 0, 10, 10}, {9, 9, 10, 10}));
  CHECK(!boxesOverlap({0, 0, 0, 10}, {0, 0, 10, 10}));
}

UMBRIEL_TEST(scrollingInsertBetweenColumnsStartsInsideTheGap) {
  const std::vector<MotionBox> neighbours{still(kColumnA), {kColumnB, kColumnC}};
  const MotionBox inserted = opener(kColumnB, neighbours);
  // Only one gap separates A from C's current edge, so the opener starts in its middle and both gaps grow from half.
  CHECK(sameBox(inserted.from, {kColumnB.x - kGap / 2, 0, 0, 800}));
  checkOpenerInsideLayout(inserted.from, inserted.to, neighbours);
  std::vector<MotionBox> members = neighbours;
  members.push_back(inserted);
  checkDisjointThroughout(members, kGap / 2);
  checkAllKeepSeparation(members);
}

UMBRIEL_TEST(scrollingInsertWhileStripShiftsLeft) {
  const int shift = 500;
  const std::vector<MotionBox> neighbours{
      {kColumnA, {kColumnA.x - shift, 0, 400, 800}},
      {kColumnB, {kColumnC.x - shift, 0, 400, 800}},
  };
  const wlr_box slot{kColumnB.x - shift, 0, 400, 800};
  const MotionBox inserted = opener(slot, neighbours);
  checkOpenerInsideLayout(inserted.from, inserted.to, neighbours);
  std::vector<MotionBox> members = neighbours;
  members.push_back(inserted);
  checkDisjointThroughout(members, kGap / 2);
  checkAllKeepSeparation(members);
}

UMBRIEL_TEST(dwindleVerticalSplitStartsOneGapBelowLeaf) {
  const int rowHeight = (800 - kGap) / 2;
  const std::vector<MotionBox> neighbours{
      still(kColumnA),
      {kColumnB, {kColumnB.x, 0, 400, rowHeight}},
  };
  const wlr_box slot{kColumnB.x, rowHeight + kGap, 400, 800 - rowHeight - kGap};
  const MotionBox leaf = opener(slot, neighbours);
  CHECK(sameBox(leaf.from, {kColumnB.x, 800 + kGap, 400, 0}));
  checkOpenerInsideLayout(leaf.from, leaf.to, neighbours);
  std::vector<MotionBox> members = neighbours;
  members.push_back(leaf);
  checkDisjointThroughout(members, kGap);
  checkAllKeepSeparation(members);
}

UMBRIEL_TEST(masterStackAppendResizesRowsToThirds) {
  const wlr_box master{0, 0, 600, 800};
  const int stackX = 600 + kGap;
  const int half = (800 - kGap) / 2;
  const int third = (800 - 2 * kGap) / 3;
  const std::vector<MotionBox> neighbours{
      still(master),
      {{stackX, 0, 400, half}, {stackX, 0, 400, third}},
      {{stackX, half + kGap, 400, 800 - half - kGap}, {stackX, third + kGap, 400, third}},
  };
  const wlr_box slot{stackX, 2 * (third + kGap), 400, 800 - 2 * (third + kGap)};
  const MotionBox appended = opener(slot, neighbours);
  checkOpenerInsideLayout(appended.from, appended.to, neighbours);
  std::vector<MotionBox> members = neighbours;
  members.push_back(appended);
  checkDisjointThroughout(members, kGap);
  checkAllKeepSeparation(members);
}

UMBRIEL_TEST(ghostOfMiddleColumnShrinksAsNeighbourSlidesIn) {
  const std::vector<MotionBox> neighbours{still(kColumnA), {kColumnC, kColumnB}};
  const MotionBox closing = ghost(kColumnB, neighbours, false);
  CHECK(sameBox(closing.to, {kColumnB.x - kGap / 2, 0, 0, 800}));
  std::vector<MotionBox> members = neighbours;
  members.push_back(closing);
  checkDisjointThroughout(members, kGap / 2);
  checkAllKeepSeparation(members);
}

UMBRIEL_TEST(ghostOfRowIsAbsorbedByRowAbove) {
  const int rowHeight = (800 - kGap) / 2;
  const std::vector<MotionBox> neighbours{
      still(kColumnA),
      {{kColumnB.x, 0, 400, rowHeight}, kColumnB},
  };
  const MotionBox closing = ghost({kColumnB.x, rowHeight + kGap, 400, 800 - rowHeight - kGap}, neighbours, false);
  CHECK(sameBox(closing.to, {kColumnB.x, 800 + kGap, 400, 0}));
  std::vector<MotionBox> members = neighbours;
  members.push_back(closing);
  checkDisjointThroughout(members, kGap);
  checkAllKeepSeparation(members);
}

UMBRIEL_TEST(ghostWithoutMovingNeighbourCollapsesAlongPrimaryAxis) {
  const std::vector<MotionBox> neighbours{still(kColumnA)};
  const MotionBox horizontal = ghost(kColumnB, neighbours, false);
  CHECK(sameBox(horizontal.to, {kColumnB.x, 0, 0, 800}));
  const MotionBox vertical = ghost(kColumnB, neighbours, true);
  CHECK(sameBox(vertical.to, {kColumnB.x, 0, 400, 0}));
  std::vector<MotionBox> members = neighbours;
  members.push_back(horizontal);
  checkDisjointThroughout(members);
}

UMBRIEL_TEST(replaceInPlaceKeepsGhostAndOpenerApart) {
  const std::vector<MotionBox> positioned{still(kColumnA), still(kColumnC)};
  const MotionBox closing = ghost(kColumnB, positioned, false);
  std::vector<MotionBox> neighbours = positioned;
  neighbours.push_back(closing);
  const MotionBox replacement = opener(kColumnB, neighbours);
  checkOpenerInsideLayout(replacement.from, replacement.to, neighbours);
  std::vector<MotionBox> members = neighbours;
  members.push_back(replacement);
  checkDisjointThroughout(members);
  checkAllKeepSeparation(members);
}

UMBRIEL_TEST(keepsSeparationFlagsRearrangements) {
  const int rowHeight = (800 - kGap) / 2;
  // A above B becomes A left of B (expel), C shifts right to make room.
  const MotionBox a{{0, 0, 400, rowHeight}, kColumnA};
  const MotionBox b{{0, rowHeight + kGap, 400, 800 - rowHeight - kGap}, kColumnB};
  const MotionBox c{kColumnB, kColumnC};
  CHECK(!keepsSeparation(a, b));
  CHECK(keepsSeparation(b, c));
  CHECK(keepsSeparation(a, c));
}

int main() { return RUN_TESTS(); }
