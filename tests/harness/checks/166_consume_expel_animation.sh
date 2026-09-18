#!/usr/bin/env bash
# Consume and expel are direct user rearrangements. Each action should complete in one movement animation instead of
# holding one axis for a second full phase. Solid colors make the final destination observable from the rendered frame.
set -euo pipefail

readonly CLIENT="$UMBRIEL_UNMAP_CLIENT"
readonly SHOT="$UMBRIEL_RUNTIME_DIR/consume-expel-animation.png"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[colors]
background = "#000000FF"

[colors.border]
focused = "#000000FF"
unfocused = "#000000FF"
outer = "#000000FF"

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

[layout]
mode = "scrolling"
gap = 10

[layout.scrolling]
default_extent_fraction = 0.5

[animation]
duration_ms = 1200
curve = "linear"

[animation.windows_in]
enabled = false

[animation.windows_move]
duration_ms = 1200
curve = "linear"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn_color() {
  local title=$1 color=$2
  FILL_COLOR="$color" "$CLIENT" "$title" 1200 700 > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
}

wait_for_count() {
  local want=$1
  for _ in $(seq 80); do
    [[ $("$UMBRIEL" windows --json | jq 'length') -eq $want ]] && return 0
    sleep 0.025
  done
  echo "expected $want windows, got: $("$UMBRIEL" windows --json)"
  return 1
}

window_id() {
  "$UMBRIEL" windows --json | jq -r --arg title "$1" '.[] | select(.title == $title) | .id'
}

assert_blue_at() {
  local x=$1 y=$2 label=$3 blue
  grim "$SHOT"
  blue=$(magick "$SHOT" -crop "20x20+$x+$y" \
    -fx 'b > 0.8 && r < 0.1 && g < 0.1 ? 1 : 0' -format '%[fx:mean]' info:)
  if ! awk -v blue="$blue" 'BEGIN { exit !(blue > 0.95) }'; then
    echo "$label did not complete in one movement duration: blue coverage $blue"
    return 1
  fi
}

spawn_color consume-expel-red 0xFFFF0000
wait_for_count 1
spawn_color consume-expel-blue 0xFF0000FF
wait_for_count 2
sleep 1.3

blue_id=$(window_id consume-expel-blue)
if [[ -z $blue_id ]]; then
  echo "could not resolve the blue window: $("$UMBRIEL" windows --json)"
  exit 1
fi
"$UMBRIEL" msg "window-focus:$blue_id" > /dev/null

"$UMBRIEL" msg window-consume-left > /dev/null
sleep 1.5
assert_blue_at 400 540 "consume"

"$UMBRIEL" msg window-consume-or-expel-right > /dev/null
sleep 1.5
assert_blue_at 1080 150 "expel"

echo "consume and expel each completed as one smooth movement animation"
