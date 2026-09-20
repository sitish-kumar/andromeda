#pragma once

#include <span>

extern "C" {
#include <wlr/util/box.h>
}

namespace umbriel {

  // One member of a layout transition: the box it leaves and the box it arrives at. Every member of a transition is
  // interpolated with the same progress, so two members separated along an axis in both `from` and `to` never cross.
  struct MotionBox {
    wlr_box from{};
    wlr_box to{};
  };

  // Edges are interpolated independently in double and rounded with lround; width = right - left (may be 0). Rounding
  // edges rather than origin+size keeps two boxes that do not cross in reals from crossing after rounding.
  [[nodiscard]] wlr_box interpolateBox(const wlr_box& from, const wlr_box& to, double progress);

  // True when the interiors intersect (touching edges do not count).
  [[nodiscard]] bool boxesOverlap(const wlr_box& a, const wlr_box& b);

  // Find the box an actor may occupy on the side of a transition where it has no layout slot. Neighbours are
  // classified against `anchor` on the known side, then their opposite-side edges bound the result. This lets a
  // closing ghost shrink into the vacancy its live neighbours leave without ever crossing them.
  [[nodiscard]] wlr_box
  confineToNeighbours(const wlr_box& anchor, std::span<const MotionBox> neighbours, bool anchorIsTo);

  // True when a close vacancy should collapse after confinement. An already constrained vacancy keeps vacating;
  // a fresh close follows confinement caused by its own removal; a retained no-reflow close waits until live final
  // geometry directly claims its currently presented box.
  [[nodiscard]] bool vacancyNeedsCollapse(
      const wlr_box& from, const wlr_box& confined, std::span<const MotionBox> neighbours, bool constrained,
      bool newlyCaptured
  );

  // Zero the extent along the layout axis at its low edge. This is the conservative target for a closing ghost when
  // neighbour confinement alone leaves it with area after the live layout has reflowed.
  [[nodiscard]] wlr_box collapseBox(const wlr_box& box, bool vertical);

  // True when some axis and order separates `a` and `b` on both sides of the transition, i.e. interpolating them with
  // a shared progress can never make them cross. False marks a pair whose side relation changes (a rearrangement).
  [[nodiscard]] bool keepsSeparation(const MotionBox& a, const MotionBox& b);

} // namespace umbriel
