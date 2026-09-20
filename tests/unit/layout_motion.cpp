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
using umbriel::vacancyNeedsCollapse;

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

  MotionBox still(const wlr_box& box) { return {box, box}; }

  MotionBox
  ghost(const wlr_box& from, std::span<const MotionBox> neighbours, bool vertical, bool layoutReflows = true) {
    wlr_box to = confineToNeighbours(from, neighbours, false);
    if (layoutReflows && to.width > 0 && to.height > 0) {
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

UMBRIEL_TEST(establishedColumnsStaySeparateDuringInsertionReflow) {
  const std::vector<MotionBox> peers{
      {{0, 0, 600, 800}, kColumnA},
      {{600 + kGap, 0, 600, 800}, kColumnB},
  };
  checkDisjointThroughout(peers, kGap);
  checkAllKeepSeparation(peers);
}

UMBRIEL_TEST(establishedColumnsStaySeparateDuringClosingReflow) {
  const std::vector<MotionBox> peers{
      {kColumnA, {0, 0, 600, 800}},
      {kColumnC, {600 + kGap, 0, 600, 800}},
  };
  checkDisjointThroughout(peers, kGap);
  checkAllKeepSeparation(peers);
}

UMBRIEL_TEST(middleColumnGhostShrinksAsNeighbourSlidesIn) {
  const std::vector<MotionBox> neighbours{still(kColumnA), {kColumnC, kColumnB}};
  const MotionBox closing = ghost(kColumnB, neighbours, false);
  CHECK(sameBox(closing.to, {kColumnB.x - kGap / 2, 0, 0, 800}));
  std::vector<MotionBox> members = neighbours;
  members.push_back(closing);
  checkDisjointThroughout(members, kGap / 2);
  checkAllKeepSeparation(members);
}

UMBRIEL_TEST(rowGhostIsAbsorbedByRowAbove) {
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

UMBRIEL_TEST(ghostWithoutLayoutReflowKeepsCapturedBox) {
  const std::vector<MotionBox> neighbours{still(kColumnA)};
  const MotionBox besideStaticNeighbour = ghost(kColumnB, neighbours, false, false);
  CHECK(sameBox(besideStaticNeighbour.to, kColumnB));
  const MotionBox alone = ghost(kColumnB, {}, true, false);
  CHECK(sameBox(alone.to, kColumnB));
  std::vector<MotionBox> members = neighbours;
  members.push_back(besideStaticNeighbour);
  checkDisjointThroughout(members);
}

UMBRIEL_TEST(unrelatedMotionDoesNotCollapseRetainedVacancy) {
  const wlr_box vacancy{800, 0, 300, 300};
  const std::vector<MotionBox> unrelated{
      {{0, 0, 300, 300}, {50, 0, 300, 300}},
  };
  const wlr_box confined = confineToNeighbours(vacancy, unrelated, false);
  CHECK(!sameBox(confined, vacancy));
  CHECK(!boxesOverlap(vacancy, unrelated.front().to));
  CHECK(!vacancyNeedsCollapse(vacancy, confined, unrelated, false, false));
  CHECK(vacancyNeedsCollapse(vacancy, confined, unrelated, true, false));
  CHECK(vacancyNeedsCollapse(vacancy, confined, unrelated, false, true));
}

UMBRIEL_TEST(openerClaimingRetainedVacancyStartsCollapse) {
  const std::vector<MotionBox> opener{still(kColumnB)};
  const wlr_box confined = confineToNeighbours(kColumnB, opener, false);
  CHECK(sameBox(confined, kColumnB));
  CHECK(vacancyNeedsCollapse(kColumnB, confined, opener, false, false));
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
