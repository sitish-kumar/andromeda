#!/usr/bin/env bash
# A tiled close and the reflow that absorbs its vacancy keep their independent configured clocks. The shorter movement
# starts late enough for both to end together, while one shared geometry progress keeps the ghost and survivors apart.
# Exercise ordinary closure plus interrupted consume and expel arrangements.
set -euo pipefail

readonly SHOTS="$UMBRIEL_RUNTIME_DIR/tiled-close-barrier"
readonly OUT_MS=1200
readonly MOVE_MS=800
mkdir -p "$SHOTS"

cat > "$UMBRIEL_RUNTIME_DIR/close-halves.glsl" <<'GLSL'
vec4 animation(vec2 uv) {
    if (umbriel_sample(vec2(0.1, 0.5)).a < 0.25) {
        return vec4(0.0, 0.0, 0.5, 0.5);
    }
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
  FILL_COLOR="$color" LOG_CONFIGURES=1 "$UMBRIEL_UNMAP_CLIENT" "$title" 1280 720 \
    > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
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

color_pixels() {
  local image=$1 expression=$2
  magick "$image" -alpha off -fx "$expression ? 1 : 0" -format '%[fx:round(mean*w*h)]\n' info:
}

color_bounds() {
  local image=$1 expression=$2
  magick "$image" -alpha off -fx "$expression ? 1 : 0" -bordercolor black -border 1 -trim \
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
  local phase=$1 survivor_title=$2 closing_title=$3
  local expected_x=$4 expected_y=$5 expected_width=$6 expected_height=$7
  local closing id image i
  local before="$SHOTS/$phase-before.png"
  local survivor_log="$UMBRIEL_RUNTIME_DIR/$survivor_title.log"
  closing=$("$UMBRIEL" windows --json | jq -c --arg title "$closing_title" '.[] | select(.title == $title)')
  if [[ -z $closing ]]; then
    echo "$phase: closing tile disappeared before the close request"
    return 1
  fi
  grim "$before"
  local base_target_config_count
  base_target_config_count=$(grep -c "^configured-size=${expected_width}x${expected_height}$" "$survivor_log" || true)

  # Issue close immediately after the continuity anchor. Image analysis can take hundreds of milliseconds under a
  # parallel harness load, which must not consume the interrupted move's remaining clock before this request lands.
  id=$(jq -r .id <<< "$closing")
  "$UMBRIEL" msg "window-close:$id" > /dev/null
  wait_unmapped "$closing_title"

  local -a close_pixels=() move_pixels=() overlap_pixels=() sentinels=() left_halves=() right_halves=()
  local -a config_counts=()
  local -a close_boxes=() red_boxes=()
  for i in $(seq 0 23); do
    grim "$SHOTS/$phase-$i.png"
    config_counts[$i]=$(
      grep -c "^configured-size=${expected_width}x${expected_height}$" "$survivor_log" || true
    )
    sleep 0.1
  done

  for i in $(seq 0 23); do
    image="$SHOTS/$phase-$i.png"
    close_pixels[$i]=$(color_pixels "$image" 'g > 0.08')
    move_pixels[$i]=$(color_pixels "$image" 'r > 0.2 && g < 0.08 && b > 0.15')
    overlap_pixels[$i]=$(color_pixels "$image" 'r > 0.08 && g > 0.08')
    sentinels[$i]=$(color_pixels "$image" 'r < 0.08 && g < 0.08 && b > 0.12')
    left_halves[$i]=$(color_pixels "$image" 'r < 0.08 && g > 0.12 && b < 0.08')
    right_halves[$i]=$(color_pixels "$image" 'r < 0.08 && g > 0.12 && b > 0.12')
    red_boxes[$i]=$(color_bounds "$image" '(r > 0.08)')
    if ((close_pixels[$i] >= 500)); then
      close_boxes[$i]=$(color_bounds "$image" '(g > 0.08)')
    else
      close_boxes[$i]='0 0 0 0'
    fi
  done

  local before_rx before_ry before_rw before_rh before_cx before_cy before_cw before_ch before_overlap before_move
  read -r before_rx before_ry before_rw before_rh \
    <<< "$(color_bounds "$before" '(r > 0.08)')"
  read -r before_cx before_cy before_cw before_ch \
    <<< "$(color_bounds "$before" '(g > 0.08)')"
  before_overlap=$(color_pixels "$before" 'r > 0.08 && g > 0.08')
  before_move=$(color_pixels "$before" 'r > 0.2 && g < 0.08 && b > 0.15')

  local close_last=-1 move_first=-1 move_last=-1
  for i in $(seq 0 23); do
    if ((close_pixels[$i] >= 500)); then
      close_last=$i
    fi
    if ((move_pixels[$i] >= 500)); then
      if ((move_first < 0)); then
        move_first=$i
      fi
      move_last=$i
    fi
  done
  if ((close_last < 7 || close_last > 15)); then
    echo "$phase: close shader did not keep its configured windows_out lifetime: last=$close_last"
    return 1
  fi
  if ((move_first <= 0 || move_first >= close_last || move_last <= move_first)); then
    echo "$phase: windows_move did not overlap the tail of windows_out: close=$close_last move=$move_first..$move_last"
    return 1
  fi
  if ((move_last < close_last - 2 || move_last > close_last + 2)); then
    echo "$phase: independently timed close and move did not finish together: close=$close_last move=$move_first..$move_last"
    return 1
  fi
  if ((move_last - move_first < 5 || move_last - move_first > 10)); then
    echo "$phase: windows_move did not retain its configured duration: frames=$move_first..$move_last"
    return 1
  fi

  local configure_first=-1
  for i in $(seq 0 23); do
    if ((config_counts[$i] > base_target_config_count)); then
      configure_first=$i
      break
    fi
  done
  # Clients may receive the target early so their new buffer is ready when geometry starts. They must never receive it
  # after the visible reflow begins, which would prolong a stale scaled buffer beyond the calculated alignment delay.
  if ((configure_first < 0 || configure_first > move_first + 1)); then
    echo "$phase: target configure arrived after windows_move started: configure=$configure_first move=$move_first"
    return 1
  fi

  local held_rx held_ry held_rw held_rh held_cx held_cy held_cw held_ch
  read -r held_rx held_ry held_rw held_rh <<< "${red_boxes[0]}"
  read -r held_cx held_cy held_cw held_ch <<< "${close_boxes[0]}"
  # An interrupted consume or expel keeps advancing while the close request and screenshot round trips run. Once the
  # close rebases that motion, frame zero is the continuity anchor. A settled arrangement must match on both sides.
  if ((before_move < 500)) \
      && { ! bounds_match 3 "$held_rx" "$held_ry" "$held_rw" "$held_rh" \
          "$before_rx" "$before_ry" "$before_rw" "$before_rh" \
        || ! bounds_match 3 "$held_cx" "$held_cy" "$held_cw" "$held_ch" \
          "$before_cx" "$before_cy" "$before_cw" "$before_ch"; }; then
    echo "$phase: close rebased with a geometry jump: before=+$before_rx +$before_ry $before_rw $before_rh / +$before_cx +$before_cy $before_cw $before_ch frame0=${red_boxes[0]} / ${close_boxes[0]}"
    return 1
  fi
  for i in $(seq 0 $((move_first - 1))); do
    local rx ry rw rh cx cy cw ch
    read -r rx ry rw rh <<< "${red_boxes[$i]}"
    read -r cx cy cw ch <<< "${close_boxes[$i]}"
    if ! bounds_match 3 "$rx" "$ry" "$rw" "$rh" "$held_rx" "$held_ry" "$held_rw" "$held_rh"; then
      echo "$phase: survivor moved before its aligned windows_move start at frame $i"
      return 1
    fi
    if ! bounds_match 3 "$cx" "$cy" "$cw" "$ch" "$held_cx" "$held_cy" "$held_cw" "$held_ch"; then
      echo "$phase: close ghost moved before the aligned windows_move start at frame $i"
      return 1
    fi
    if ((move_pixels[$i] >= 500)); then
      echo "$phase: windows_move shader ran during the alignment delay at frame $i"
      return 1
    fi
  done

  local saw_ghost_motion=0 saw_survivor_motion=0 previous_overlap=${overlap_pixels[0]}
  for i in $(seq 0 "$close_last"); do
    if ((sentinels[$i] >= 50)); then
      echo "$phase: close geometry lost its shader input at frame $i: pixels=${sentinels[$i]}"
      return 1
    fi
    if ((close_pixels[$i] >= 2000 && (left_halves[$i] < 300 || right_halves[$i] < 300))); then
      echo "$phase: moving close ghost lost a shader half at frame $i: left=${left_halves[$i]} right=${right_halves[$i]}"
      return 1
    fi
    if ((i >= move_first)); then
      local rx ry rw rh cx cy cw ch
      read -r rx ry rw rh <<< "${red_boxes[$i]}"
      read -r cx cy cw ch <<< "${close_boxes[$i]}"
      # Consume and expel can already be crossing when close interrupts them. The rebase must preserve that first frame
      # and drain the inherited overlap monotonically; an ordinary close starts at zero and therefore stays at zero.
      if ((overlap_pixels[$i] > previous_overlap + 1000)); then
        echo "$phase: close reflow increased inherited overlap at frame $i: before=$before_overlap frame0=${overlap_pixels[0]} previous=$previous_overlap current=${overlap_pixels[$i]}"
        return 1
      fi
      previous_overlap=${overlap_pixels[$i]}
      if ! bounds_match 6 "$rx" "$ry" "$rw" "$rh" \
        "$held_rx" "$held_ry" "$held_rw" "$held_rh" \
        && ! bounds_match 6 "$rx" "$ry" "$rw" "$rh" \
          "$expected_x" "$expected_y" "$expected_width" "$expected_height"; then
        saw_survivor_motion=1
      fi
      if ! bounds_match 3 "$cx" "$cy" "$cw" "$ch" "$held_cx" "$held_cy" "$held_cw" "$held_ch"; then
        saw_ghost_motion=1
      fi
    fi
  done
  if ((saw_survivor_motion == 0 || saw_ghost_motion == 0)); then
    echo "$phase: shared motion was not observable: survivor=$saw_survivor_motion ghost=$saw_ghost_motion"
    return 1
  fi
  local clear_frame=$((close_last + 1))
  if ((clear_frame <= 23 && overlap_pixels[$clear_frame] >= 500)); then
    echo "$phase: inherited overlap remained after the close completed: frame=$clear_frame pixels=${overlap_pixels[$clear_frame]}"
    return 1
  fi

  local final_x final_y final_width final_height
  read -r final_x final_y final_width final_height <<< "${red_boxes[23]}"
  if ! bounds_match 2 "$final_x" "$final_y" "$final_width" "$final_height" \
      "$expected_x" "$expected_y" "$expected_width" "$expected_height"; then
    echo "$phase: survivor missed final geometry: got=${red_boxes[23]} expected=$expected_x $expected_y $expected_width $expected_height"
    return 1
  fi

  printf '%s: close=%d move=%d..%d held=%s final=%s\n' \
    "$phase" "$close_last" "$move_first" "$move_last" "${red_boxes[0]}" "${red_boxes[23]}"
}

readonly SURVIVOR_COLOR=0x80800000
readonly CLOSER_COLOR=0x80008000

spawn barrier-master-survivor "$SURVIVOR_COLOR"
sleep 0.9
spawn barrier-master-close "$CLOSER_COLOR"
sleep 0.9
verify_close master barrier-master-survivor barrier-master-close 0 0 1280 720

"$UMBRIEL" msg workspace-switch:2 > /dev/null
sed -i 's/^mode = "master"$/mode = "scrolling"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
sleep 0.2
spawn barrier-consume-survivor "$SURVIVOR_COLOR"
sleep 0.9
spawn barrier-consume-close "$CLOSER_COLOR"
sleep 0.9
"$UMBRIEL" msg window-consume-left > /dev/null
sleep 0.28
verify_close consume barrier-consume-survivor barrier-consume-close 0 0 640 720

"$UMBRIEL" msg workspace-switch:3 > /dev/null
sleep 0.2
spawn barrier-expel-survivor "$SURVIVOR_COLOR"
sleep 0.9
spawn barrier-expel-close "$CLOSER_COLOR"
sleep 0.9
"$UMBRIEL" msg window-consume-left > /dev/null
sleep 0.9
"$UMBRIEL" msg window-consume-or-expel-right > /dev/null
# Keep ample clock headroom even when other timing checks run in parallel.
sleep 0.08
verify_close expel barrier-expel-survivor barrier-expel-close 0 0 640 720

echo "ordinary, consume, and expel closes kept full shader clocks while coordinated geometry drained overlap"
