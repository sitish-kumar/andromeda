#!/usr/bin/env bash
# Consume and expel keep their live-window crossing order. If either motion is interrupted by closing its moving tile,
# the frozen windows_out snapshot stays below every live participant while the remaining layout retargets.
set -euo pipefail

readonly SHOTS="$UMBRIEL_RUNTIME_DIR/consume-expel-stacking"
mkdir -p "$SHOTS"

cat > "$UMBRIEL_RUNTIME_DIR/consume-expel-move.glsl" <<'GLSL'
vec4 animation(vec2 uv) {
    vec4 source = umbriel_sample(uv);
    return vec4(source.r, 1.0, source.b, source.a);
}
GLSL

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[colors]
backdrop = "#000000FF"

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

[layout]
mode = "scrolling"

[layout.scrolling]
default_extent_fraction = 0.5
center_focused = "never"
center_underfull_strip = false

[output.HEADLESS-1]
min_workspaces = 3

[animation.windows_in]
enabled = false

[animation.windows_out]
enabled = true
duration_ms = 2000
curve = "linear"
style = "fade"

[animation.windows_move]
enabled = true
duration_ms = 1000
curve = "linear"
shader = "consume-expel-move.glsl"

[animation.workspaces]
enabled = false
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  local title=$1 color=$2
  FILL_COLOR="$color" "$UMBRIEL_UNMAP_CLIENT" "$title" 1200 700 > "$SHOTS/$title.log" 2>&1 &
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "$title" '.[] | select(.title == $title)')
    [[ -n $window ]] && return 0
    sleep 0.025
  done
  echo "timed out waiting for $title"
  return 1
}

window_json() {
  "$UMBRIEL" windows --json | jq -c --arg title "$1" '.[] | select(.title == $title)'
}

focus_title() {
  local id
  id=$(window_json "$1" | jq -r .id)
  "$UMBRIEL" msg "window-focus:$id" > /dev/null
}

close_title() {
  local title=$1 id
  id=$(window_json "$title" | jq -r .id)
  "$UMBRIEL" msg "window-close:$id" > /dev/null
  for _ in $(seq 80); do
    [[ -z $(window_json "$title") ]] && return 0
    sleep 0.025
  done
  echo "$title did not close"
  return 1
}

rgb_at() {
  local image=$1 x=$2 y=$3
  magick "$image" -alpha off -crop "8x8+$((x - 4))+$((y - 4))" +repage \
    -format '%[fx:round(255*mean.r)] %[fx:round(255*mean.g)] %[fx:round(255*mean.b)]\n' info:
}

is_yellow() {
  local red=$1 green=$2 blue=$3
  ((red > 180 && green > 180 && blue < 40))
}

is_cyan() {
  local red=$1 green=$2 blue=$3
  ((red < 40 && green > 180 && blue > 180))
}

is_red() {
  local red=$1 green=$2 blue=$3
  ((red > 180 && green < 40 && blue < 40))
}

sample_crossing_sequence() {
  local phase=$1 x=$2 y=$3 expected=$4
  local image red green blue sample sequence=""
  for i in $(seq 14); do
    sleep 0.07
    image="$SHOTS/$phase-$i.png"
    grim "$image"
    read -r red green blue < <(rgb_at "$image" "$x" "$y")
    if is_yellow "$red" "$green" "$blue"; then
      sample=Y
    elif is_cyan "$red" "$green" "$blue"; then
      sample=C
    else
      sample=.
    fi
    sequence+=$sample
  done
  case "$expected" in
    YCY)
      [[ $sequence == *Y*C*Y* ]] || {
        echo "$phase did not preserve yellow, cyan, yellow crossing order: $sequence"
        return 1
      }
      ;;
    CYC)
      [[ $sequence == *C*Y*C* ]] || {
        echo "$phase did not preserve cyan, yellow, cyan crossing order: $sequence"
        return 1
      }
      ;;
  esac
}

assert_no_move_marker() {
  local phase=$1 image marker
  image="$SHOTS/$phase-settled.png"
  grim "$image"
  marker=$(magick "$image" -alpha off -fx 'g > 0.7 && (r > 0.5 || b > 0.5) ? 1 : 0' \
    -format '%[fx:round(mean*w*h)]\n' info:)
  if ((marker > 100)); then
    echo "$phase retained $marker windows_move marker pixels after settling"
    return 1
  fi
}

assert_probe() {
  local phase=$1 x=$2 y=$3 expected=$4 image
  local red green blue
  image="$SHOTS/$phase.png"
  grim "$image"
  read -r red green blue < <(rgb_at "$image" "$x" "$y")
  if [[ $expected == yellow ]]; then
    if ! is_yellow "$red" "$green" "$blue"; then
      echo "$phase expected the live yellow tile above its close snapshot, got $red $green $blue"
      return 1
    fi
  elif ! is_red "$red" "$green" "$blue"; then
    echo "$phase expected the settled red survivor, got $red $green $blue"
    return 1
  fi
}

# Baseline both verbs before involving a close snapshot. The farther-travelling tile must keep the established
# crossing order while both windows use the shared windows_move clock.
spawn motion-a 0xFFFF0000
spawn motion-b 0xFF0000FF
sleep 1.1
focus_title motion-b
"$UMBRIEL" msg window-consume-left > /dev/null
sample_crossing_sequence consume 400 300 YCY
sleep 0.2
assert_no_move_marker consume

motion_a=$(window_json motion-a)
motion_b=$(window_json motion-b)
if [[ $(jq -r .x <<< "$motion_a") != "$(jq -r .x <<< "$motion_b")" \
    || $(jq -r .y <<< "$motion_a") == "$(jq -r .y <<< "$motion_b")" ]]; then
  echo "consume did not settle both windows into one stack"
  exit 1
fi

"$UMBRIEL" msg window-consume-or-expel-left > /dev/null
sample_crossing_sequence expel 400 400 CYC
sleep 0.2
assert_no_move_marker expel

motion_a=$(window_json motion-a)
motion_b=$(window_json motion-b)
if [[ $(jq -r .x <<< "$motion_a") == "$(jq -r .x <<< "$motion_b")" ]]; then
  echo "expel did not settle the focused window into its own column"
  exit 1
fi

# A close during consume freezes the blue mover at its current presented box. Red remains underneath at this probe;
# its green windows_move marker must win while motion runs, then plain red must remain after motion settles.
"$UMBRIEL" msg workspace-switch:2 > /dev/null
spawn consume-close-a 0xFFFF0000
spawn consume-close-b 0xFF0000FF
sleep 1.1
focus_title consume-close-b
"$UMBRIEL" msg window-consume-left > /dev/null
sleep 0.35
close_title consume-close-b
sleep 0.08
assert_probe consume-close-moving 500 300 yellow
sleep 1.05
assert_probe consume-close-settled 500 300 red
sleep 1

# Build the same stack on a fresh workspace, then close the blue mover during expel. The farther-travelling red peer
# remains above it at the crossing probe and continues above the new tiled-close underlay after blue becomes a snapshot.
"$UMBRIEL" msg workspace-switch:3 > /dev/null
spawn expel-close-a 0xFFFF0000
spawn expel-close-b 0xFF0000FF
sleep 1.1
focus_title expel-close-b
"$UMBRIEL" msg window-consume-left > /dev/null
sleep 1.1
"$UMBRIEL" msg window-consume-or-expel-left > /dev/null
sleep 0.35
close_title expel-close-b
sleep 0.08
assert_probe expel-close-moving 400 400 yellow
sleep 1.05
assert_probe expel-close-settled 400 400 red

echo "consume and expel kept live crossing order and placed interrupted close snapshots underneath"
