#!/usr/bin/env bash
# A tiled close keeps a fixed windows_out shader canvas. Its output-only mask and established survivors immediately
# share one windows_move geometry transition, including when a consume or expel transition is interrupted.
set -euo pipefail

readonly SHOTS="$UMBRIEL_RUNTIME_DIR/tiled-close-vacancy"
readonly OUT_MS=1600
readonly MOVE_MS=800
readonly OVERLAP_TOLERANCE=1500
mkdir -p "$SHOTS"

cat > "$UMBRIEL_RUNTIME_DIR/close-halves.glsl" <<'GLSL'
vec4 animation(vec2 uv) {
    // A clipped shader input loses this fixed point. Pure blue is reserved as the failure sentinel.
    if (umbriel_sample(vec2(0.1, 0.5)).a < 0.25) {
        return vec4(0.0, 0.0, 0.5, 0.5);
    }
    // Premultiplied half-alpha halves expose both overlap and accidental resizing of the shader canvas.
    return uv.x < 0.5 ? vec4(0.0, 0.5, 0.0, 0.5) : vec4(0.0, 0.5, 0.5, 0.5);
}
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/move-blue.glsl" <<'GLSL'
vec4 animation(vec2 uv) {
    vec4 source = umbriel_sample(uv);
    return vec4(source.r, source.g, max(source.b, source.a * 0.8), source.a);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<EOF

[colors]
backdrop = "#000000FF"

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

[layout]
mode = "master"
gap = 0

[layout.master]
default_width_fraction = 0.5

[layout.scrolling]
default_extent_fraction = 0.5
center_focused = "never"
center_underfull_strip = false

[animation.windows_in]
enabled = false

[animation.windows_out]
enabled = true
duration_ms = $OUT_MS
curve = "linear"
shader = "close-halves.glsl"

[animation.windows_move]
enabled = true
duration_ms = $MOVE_MS
curve = "linear"
shader = "move-blue.glsl"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  local title=$1 color=$2
  FILL_COLOR="$color" "$UMBRIEL_UNMAP_CLIENT" "$title" 1280 720 > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
  for _ in $(seq 100); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "$title" '.[] | select(.title == $title)')
    [[ -n $window ]] && return 0
    sleep 0.025
  done
  echo "timed out waiting for $title"
  return 1
}

wait_unmapped() {
  local title=$1
  for _ in $(seq 100); do
    if grep -q '^unmapped$' "$UMBRIEL_RUNTIME_DIR/$title.log" \
        && ! "$UMBRIEL" windows --json | jq -e --arg title "$title" 'any(.[]; .title == $title)' > /dev/null; then
      return 0
    fi
    sleep 0.025
  done
  echo "timed out waiting for $title to unmap"
  return 1
}

mask_pixels() {
  magick "$1" -alpha off -fx '(g > 0.08) ? 1 : 0' -format '%[fx:round(mean*w*h)]\n' info:
}

move_pixels() {
  magick "$1" -alpha off -fx '(r > 0.2 && g < 0.08 && b > 0.15) ? 1 : 0' \
    -format '%[fx:round(mean*w*h)]\n' info:
}

overlap_pixels() {
  magick "$1" -alpha off -fx '(r > 0.08 && g > 0.08) ? 1 : 0' \
    -format '%[fx:round(mean*w*h)]\n' info:
}

input_crop_pixels() {
  magick "$1" -alpha off -fx '(r < 0.08 && g < 0.08 && b > 0.12) ? 1 : 0' \
    -format '%[fx:round(mean*w*h)]\n' info:
}

left_half_pixels() {
  magick "$1" -alpha off -fx '(r < 0.08 && g > 0.12 && b < 0.08) ? 1 : 0' \
    -format '%[fx:round(mean*w*h)]\n' info:
}

right_half_pixels() {
  magick "$1" -alpha off -fx '(r < 0.08 && g > 0.12 && b > 0.12) ? 1 : 0' \
    -format '%[fx:round(mean*w*h)]\n' info:
}

red_bounds() {
  magick "$1" -alpha off -fx '(r > 0.08) ? 1 : 0' -bordercolor black -border 1 -trim \
    -format '%X %Y %w %h\n' info: 2> /dev/null
}

mask_bounds() {
  magick "$1" -alpha off -fx '(g > 0.08) ? 1 : 0' -bordercolor black -border 1 -trim \
    -format '%X %Y %w %h\n' info: 2> /dev/null
}

bounds_match() {
  local tolerance=$1
  local ax=$2 ay=$3 aw=$4 ah=$5
  local bx=$6 by=$7 bw=$8 bh=$9
  ((ax >= bx - tolerance && ax <= bx + tolerance \
    && ay >= by - tolerance && ay <= by + tolerance \
    && aw >= bw - tolerance && aw <= bw + tolerance \
    && ah >= bh - tolerance && ah <= bh + tolerance))
}

verify_close() {
  local phase=$1 closing_title=$2 kind=$3
  local expected_x=$4 expected_y=$5 expected_width=$6 expected_height=$7
  local closing id image i
  local pre_image="$SHOTS/$phase-pre.png"
  closing=$("$UMBRIEL" windows --json | jq -c --arg title "$closing_title" '.[] | select(.title == $title)')
  if [[ -z $closing ]]; then
    echo "$phase: closing tile disappeared before the close request"
    return 1
  fi

  grim "$pre_image"
  local pre_red pre_mask

  id=$(jq -r .id <<< "$closing")
  "$UMBRIEL" msg "window-close:$id" > /dev/null
  wait_unmapped "$closing_title"

  for i in $(seq 0 11); do
    grim "$SHOTS/$phase-$i.png"
    sleep 0.1
  done

  # Analyze only after all time-sensitive captures. ImageMagick is slow enough to advance an interrupted move
  # substantially, which would make the first close sample look like a geometry jump.
  pre_red=$(red_bounds "$pre_image")
  pre_mask=$(mask_bounds "$pre_image")

  local -a masks=() markers=() overlaps=() sentinels=() left_halves=() right_halves=()
  local -a red_boxes=() mask_boxes=()
  for i in $(seq 0 11); do
    image="$SHOTS/$phase-$i.png"
    masks[$i]=$(mask_pixels "$image")
    markers[$i]=$(move_pixels "$image")
    overlaps[$i]=$(overlap_pixels "$image")
    sentinels[$i]=$(input_crop_pixels "$image")
    left_halves[$i]=$(left_half_pixels "$image")
    right_halves[$i]=$(right_half_pixels "$image")
    if ((masks[$i] >= 500)); then
      mask_boxes[$i]=$(mask_bounds "$image")
    else
      mask_boxes[$i]='0 0 0 0'
    fi
    red_boxes[$i]=$(red_bounds "$image")
  done

  local pre_rx pre_ry pre_rw pre_rh pre_mx pre_my pre_mw pre_mh
  local first_rx first_ry first_rw first_rh first_mx first_my first_mw first_mh
  read -r pre_rx pre_ry pre_rw pre_rh <<< "$pre_red"
  read -r pre_mx pre_my pre_mw pre_mh <<< "$pre_mask"
  read -r first_rx first_ry first_rw first_rh <<< "${red_boxes[0]}"
  read -r first_mx first_my first_mw first_mh <<< "${mask_boxes[0]}"
  if ! bounds_match 96 "$first_rx" "$first_ry" "$first_rw" "$first_rh" \
      "$pre_rx" "$pre_ry" "$pre_rw" "$pre_rh"; then
    echo "$phase: survivor jumped when the close rebased: before=$pre_red first=${red_boxes[0]}"
    return 1
  fi
  if ! bounds_match 96 "$first_mx" "$first_my" "$first_mw" "$first_mh" \
      "$pre_mx" "$pre_my" "$pre_mw" "$pre_mh"; then
    echo "$phase: close mask jumped away from its presented box: before=$pre_mask first=${mask_boxes[0]}"
    return 1
  fi

  local marker_active=0 marker_episodes=0 marker_frames=0 first_marker=-1 last_marker=-1
  local saw_intermediate=0 fixed_canvas_sample=0
  for i in $(seq 0 11); do
    if ((sentinels[$i] >= 50)); then
      echo "$phase: the mask cropped shader input before composition at frame $i: blue_pixels=${sentinels[$i]}"
      return 1
    fi
    if ((i > 0 && masks[$i] > masks[$((i - 1))] + 2500)); then
      echo "$phase: close mask grew again at frame $i: previous=${masks[$((i - 1))]} current=${masks[$i]}"
      return 1
    fi

    if ((markers[$i] >= 500)); then
      if ((marker_active == 0)); then
        marker_episodes=$((marker_episodes + 1))
        marker_active=1
      fi
      if ((first_marker < 0)); then
        first_marker=$i
      fi
      last_marker=$i
      marker_frames=$((marker_frames + 1))

      local rx ry rw rh
      read -r rx ry rw rh <<< "${red_boxes[$i]}"
      if ! bounds_match 8 "$rx" "$ry" "$rw" "$rh" "$pre_rx" "$pre_ry" "$pre_rw" "$pre_rh" \
          && ! bounds_match 8 "$rx" "$ry" "$rw" "$rh" \
            "$expected_x" "$expected_y" "$expected_width" "$expected_height"; then
        saw_intermediate=1
      fi
    else
      marker_active=0
    fi

    if [[ $kind == ordinary ]] && ((overlaps[$i] >= OVERLAP_TOLERANCE)); then
      echo "$phase: close snapshot covered the moving survivor at frame $i: overlap_pixels=${overlaps[$i]}"
      return 1
    fi
    if [[ $kind == ordinary ]] && ((masks[$i] >= 500)); then
      local mx my mw mh
      read -r mx my mw mh <<< "${mask_boxes[$i]}"
      if ((mx > pre_mx + pre_mw / 2 + 12 && mw > 40)); then
        if ((left_halves[$i] >= 50 || right_halves[$i] < 500)); then
          echo "$phase: the shrinking mask resized shader UVs at frame $i: left=${left_halves[$i]} right=${right_halves[$i]} box=${mask_boxes[$i]}"
          return 1
        fi
        fixed_canvas_sample=1
      fi
    fi
  done

  if ((masks[0] < 500)); then
    echo "$phase: windows_out did not render from the captured close canvas"
    return 1
  fi
  if ((masks[11] >= 500)); then
    echo "$phase: close mask did not vacate on the windows_move timeline: mask_pixels=${masks[11]}"
    return 1
  fi
  if ((marker_episodes != 1 || first_marker > 1 || marker_frames < 4 || marker_frames > 10 || last_marker > 9)); then
    echo "$phase: windows_move marker was not one immediate bounded episode: episodes=$marker_episodes first=$first_marker last=$last_marker frames=$marker_frames"
    return 1
  fi
  if ((saw_intermediate == 0)); then
    echo "$phase: survivor did not expose intermediate windows_move geometry"
    return 1
  fi
  if [[ $kind == ordinary ]] && ((fixed_canvas_sample == 0)); then
    echo "$phase: no late mask frame exposed only the fixed canvas's right UV half"
    return 1
  fi
  if ((overlaps[11] >= OVERLAP_TOLERANCE)); then
    echo "$phase: survivor and close mask still overlapped after movement: overlap_pixels=${overlaps[11]}"
    return 1
  fi

  local x y width height
  read -r x y width height <<< "${red_boxes[11]}"
  if ! bounds_match 2 "$x" "$y" "$width" "$height" \
      "$expected_x" "$expected_y" "$expected_width" "$expected_height"; then
    echo "$phase: survivor missed final geometry: got=$x $y $width $height expected=$expected_x $expected_y $expected_width $expected_height"
    return 1
  fi
}

readonly SURVIVOR_COLOR=0x80800000
readonly CLOSER_COLOR=0x80008000

# The master survivor expands while the right-hand close mask contracts at the same boundary.
spawn vacancy-master-survivor "$SURVIVOR_COLOR"
sleep 0.9
spawn vacancy-master-close "$CLOSER_COLOR"
sleep 0.9
verify_close master vacancy-master-close ordinary 0 0 1280 720

# Consume starts a horizontal crossing and vertical resize. Closing during it must rebase both presented boxes.
"$UMBRIEL" msg workspace-switch:2 > /dev/null
sed -i 's/^mode = "master"$/mode = "scrolling"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
sleep 0.2
spawn vacancy-consume-survivor "$SURVIVOR_COLOR"
sleep 0.9
spawn vacancy-consume-close "$CLOSER_COLOR"
sleep 0.9
"$UMBRIEL" msg window-consume-left > /dev/null
sleep 0.28
verify_close consume vacancy-consume-close interrupted 0 0 640 720

# Expel is the reverse crossing and must use the same immediate shared move episode.
"$UMBRIEL" msg workspace-switch:3 > /dev/null
sleep 0.2
spawn vacancy-expel-survivor "$SURVIVOR_COLOR"
sleep 0.9
spawn vacancy-expel-close "$CLOSER_COLOR"
sleep 0.9
"$UMBRIEL" msg window-consume-left > /dev/null
sleep 0.9
"$UMBRIEL" msg window-consume-or-expel-right > /dev/null
sleep 0.28
verify_close expel vacancy-expel-close interrupted 0 0 640 720

echo "ordinary, consume, and expel closes used one immediate move while masking a fixed shader canvas"
