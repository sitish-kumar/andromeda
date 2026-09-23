#!/usr/bin/env bash
# Established tiled peers share one windows_move transition and remain disjoint through interrupted geometry changes.
# Lifecycle actors are deliberately outside this contract, so opens and closes settle before sampling. Every window is
# 50% red over black with a green border ring. Two content layers raise red above 0.5, while a border crossing content
# mixes red with green. A blue tint supplied only by windows_move also proves the sampled transition really ran.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly SHOTS="$UMBRIEL_RUNTIME_DIR/layout-motion"
mkdir -p "$SHOTS"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[layout]
mode = "scrolling"

# Half-width columns make maximizing reflow visible neighbours.
[layout.scrolling]
default_extent_fraction = 0.5

[animation]
duration_ms = 1500
curve = "linear"

[animation.windows_in]
style = "popin"

[animation.windows_move]
shader = "layout-motion.glsl"

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
cat > "$UMBRIEL_RUNTIME_DIR/layout-motion.glsl" <<'GLSL'
vec4 animation(vec2 uv) {
    vec4 source = umbriel_sample(uv);
    return vec4(source.r, source.g, source.a, source.a);
}
GLSL
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
  "$UMBRIEL_PIXEL_PROBE" "$1" count '(r > 0.56 && g < 0.1) || (r > 0.1 && g > 0.2)'
}

move_marker_pixels() {
  "$UMBRIEL_PIXEL_PROBE" "$1" count 'b > 0.2 && r > 0.2'
}

# Fourteen frames 100 ms apart, captured first and analysed afterwards so the samples span the whole 1500 ms motion.
sample() {
  local phase=$1
  local i
  local saw_marker=0
  for i in $(seq 14); do
    grim "$SHOTS/$phase-$i.png"
    sleep 0.1
  done
  for i in $(seq 14); do
    local overlap
    overlap=$(overlap_pixels "$SHOTS/$phase-$i.png")
    if ((overlap > 9)); then
      echo "$phase: frame $i shows overlapping tiles ($overlap pixels): $SHOTS/$phase-$i.png"
      exit 1
    fi
    if (( $(move_marker_pixels "$SHOTS/$phase-$i.png") > 1000 )); then
      saw_marker=1
    fi
  done
  if ((!saw_marker)); then
    echo "$phase: no windows_move shader marker appeared during the established-peer transition"
    exit 1
  fi
}

settle_motion() {
  sleep 0.3
}

settle_lifecycle() {
  sleep 1.7
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
  settle_lifecycle

  spawn "motion-$tag-b"
  wait_for_windows 2
  settle_lifecycle

  spawn "motion-$tag-c"
  wait_for_windows 3
  settle_lifecycle

  "$UMBRIEL" msg "window-focus:$(window_id "motion-$tag-a")" > /dev/null
  "$UMBRIEL" msg window-toggle-maximize > /dev/null
  sleep 0.3
  "$UMBRIEL" msg window-toggle-maximize > /dev/null
  sample "$tag-maximize-interrupt"
  settle_motion

  close_all
  wait_for_windows 0
  sleep 1.8
}

run_mode scrolling 0.5
run_mode dwindle 0.5
run_mode master 0.5

echo "established tiled peers used windows_move and remained disjoint in scrolling, dwindle, and master"
