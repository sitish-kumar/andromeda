#!/usr/bin/env bash
# Tiled windows and closing snapshots on a workspace animate from one shared transition, so no two of them ever
# overlap mid-motion: opens, closes, an interrupted maximize, and a close-everything reflow, in every layout mode,
# with the default popin open and at both a half and the default column extent in scrolling. Every window is 50% red
# over the black headless background with a pure green border ring, so a single content layer never reads above
# r = 0.5 while two read r = 0.5 + 0.25 * alpha, and a ring crossing another window's content mixes red with green.
# A frame with either kind of pixel is a failure.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly SHOTS="$UMBRIEL_RUNTIME_DIR/layout-motion"
mkdir -p "$SHOTS"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[layout]
mode = "scrolling"

# Half-width columns make opening, closing and maximizing reflow visible neighbours; the scrolling pass at the default
# extent covers the strip scrolling a new full-width column into view.
[layout.scrolling]
default_extent_fraction = 0.5

[animation]
duration_ms = 1500
curve = "linear"

[animation.windows_in]
style = "popin"

[appearance]
border_width = 2
outer_border_width = 8
corner_radius = 0

[colors.border]
focused = "#00FF00FF"
unfocused = "#00FF00FF"
outer = "#00FF00FF"

[appearance.shadow]
enabled = false
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  FILL_COLOR=0x80800000 "$CLIENT" "$1" 1200 700 > "$SHOTS/$1.log" 2>&1 &
}

wait_for_windows() {
  local want=$1
  for _ in $(seq 80); do
    if [[ $("$UMBRIEL" windows --json | jq 'length') -eq $want ]]; then
      return 0
    fi
    sleep 0.05
  done
  echo "timed out waiting for $want window(s), saw: $("$UMBRIEL" windows --json)"
  return 1
}

window_id() {
  "$UMBRIEL" windows --json | jq -r --arg title "$1" '.[] | select(.title == $title) | .id'
}

# Fraction of the output covered by pixels only two red layers, or a ring over red content, can produce.
overlap_pixels() {
  magick "$1" -alpha off -fx '(r > 0.56 && g < 0.1 && b < 0.1) || (r > 0.1 && g > 0.2) ? 1 : 0' \
    -format '%[fx:mean]' info:
}

# Fourteen frames 100 ms apart, captured first and analysed afterwards so the samples span the whole 1500 ms motion.
sample() {
  local phase=$1
  local i
  for i in $(seq 14); do
    grim "$SHOTS/$phase-$i.png"
    sleep 0.1
  done
  for i in $(seq 14); do
    local overlap
    overlap=$(overlap_pixels "$SHOTS/$phase-$i.png")
    if ! awk -v value="$overlap" 'BEGIN { exit !(value < 0.00001) }'; then
      echo "$phase: frame $i shows overlapping tiles (overlap fraction $overlap): $SHOTS/$phase-$i.png"
      exit 1
    fi
  done
}

# Capturing and analysing a phase takes longer than the 1500 ms motion, so only a short margin is needed after it.
settle() {
  sleep 0.3
}

close_all() {
  local id
  for id in $("$UMBRIEL" windows --json | jq -r '.[].id'); do
    "$UMBRIEL" msg "window-close:$id" > /dev/null
  done
}

run_mode() {
  local mode=$1
  local extent=$2
  local tag="$mode-$extent"
  sed -i -e "s/^mode = \"[a-z]*\"$/mode = \"$mode\"/" -e "s/^default_extent_fraction = .*$/default_extent_fraction = $extent/" "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  sleep 0.2

  spawn "motion-$tag-a"
  wait_for_windows 1
  settle

  spawn "motion-$tag-b"
  sample "$tag-open-b"
  wait_for_windows 2
  settle

  spawn "motion-$tag-c"
  sample "$tag-open-c"
  wait_for_windows 3
  settle

  "$UMBRIEL" msg "window-close:$(window_id "motion-$tag-b")" > /dev/null
  sample "$tag-close-b"
  wait_for_windows 2
  settle

  "$UMBRIEL" msg "window-focus:$(window_id "motion-$tag-a")" > /dev/null
  "$UMBRIEL" msg window-toggle-maximize > /dev/null
  sleep 0.3
  "$UMBRIEL" msg window-toggle-maximize > /dev/null
  sample "$tag-maximize-interrupt"
  settle

  close_all
  sample "$tag-close-all"
  wait_for_windows 0
  settle
}

run_mode scrolling 0.5
run_mode scrolling 1.0
run_mode dwindle 0.5
run_mode master 0.5

echo "tiled windows and closing snapshots never overlapped while animating in scrolling, dwindle, and master"
