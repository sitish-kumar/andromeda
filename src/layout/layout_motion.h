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

  // The box a member occupies on the side of a transition where it has no layout slot: an opening tile's `from`
  // (`anchorIsTo == true`, `anchor` is its target slot) or a closing ghost's `to` (`anchorIsTo == false`, `anchor` is
  // the box it is fading at). Every neighbour is classified against `anchor` on the known side (left when
  // known.x + known.width <= anchor.x and the y ranges overlap; right, above, below likewise; anything else ignored)
  // and contributes its other-side edge, kept at the separation it has from the anchor on the known side, as a
  // bound: lo_x = max(left neighbours' other right edge + separation), hi_x = min(right neighbours' other.x -
  // separation), lo_y / hi_y from above / below. Result: anchor's x and right edges each clamped into [lo_x, hi_x],
  // y and bottom into [lo_y, hi_y]; when lo > hi on an axis both edges collapse to (lo + hi) / 2. Width or height
  // may come out 0.
  [[nodiscard]] wlr_box
  confineToNeighbours(const wlr_box& anchor, std::span<const MotionBox> neighbours, bool anchorIsTo);

  // Zero the extent along one axis at its low edge (x when !vertical, y when vertical). Used on a ghost whose confined
  // box still has area on both axes, so a closing window always shrinks along the layout's primary axis.
  [[nodiscard]] wlr_box collapseBox(const wlr_box& box, bool vertical);

  // True when some axis and order separates `a` and `b` on both sides of the transition, i.e. interpolating them with
  // a shared progress can never make them cross. False marks a pair whose side relation changes (a rearrangement).
  [[nodiscard]] bool keepsSeparation(const MotionBox& a, const MotionBox& b);

} // namespace umbriel
