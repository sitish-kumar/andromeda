#!/usr/bin/env bash
# A tiled close clips only reclaimed edges. Chrome on a captured outer edge remains visible, while the inward mask
# edge does not reveal close content across the layout gap.
set -euo pipefail

readonly SHOTS="$UMBRIEL_RUNTIME_DIR/tiled-close-mask-chrome"
readonly BORDER=20
readonly GAP=40
readonly MOVE_MS=2400
mkdir -p "$SHOTS"

cat > "$UMBRIEL_RUNTIME_DIR/identity.glsl" <<'GLSL'
vec4 animation(vec2 uv) { return umbriel_sample(uv); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<EOF

[colors]
backdrop = "#000000FF"
shadow = "#00FF00FF"

[colors.border]
focused = "#FF00FFFF"
unfocused = "#FFFF00FF"
outer = "#FFFF00FF"

[appearance]
border_width = $BORDER
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = true
softness = 24
offset_x = 0
offset_y = 0

[layout]
mode = "master"
gap = $GAP

[layout.master]
default_width_fraction = 0.5

[animation.windows_in]
enabled = false

[animation.windows_out]
enabled = true
duration_ms = 4000
curve = "linear"
shader = "identity.glsl"

[animation.windows_move]
enabled = true
duration_ms = $MOVE_MS
curve = "linear"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  local title=$1 color=$2
  FILL_COLOR="$color" "$UMBRIEL_UNMAP_CLIENT" "$title" 1200 700 > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
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

color_bounds() {
  local image=$1 expression=$2
  magick "$image" -alpha off -fx "$expression" -bordercolor black -border 1 -trim \
    -format '%X %Y %w %h\n' info: 2> /dev/null
}

pixel() {
  local image=$1 x=$2 y=$3
  magick "$image" -alpha off -crop "2x2+$x+$y" +repage \
    -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)] %[fx:round(mean.b*255)]\n' info:
}

spawn close-mask-chrome-survivor 0xFFFF0000
sleep 0.2
spawn close-mask-chrome-closing 0xFF0000FF
sleep 2.5

closing=$window
closing_id=$(jq -r .id <<< "$closing")

# Capture the closer unfocused, so its yellow border stays distinguishable from the survivor's magenta border.
survivor_id=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "close-mask-chrome-survivor") | .id')
"$UMBRIEL" msg "window-focus:$survivor_id" > /dev/null
sleep 0.1
grim "$SHOTS/before.png"

# IPC retains the client's committed size while layout motion presents a different box. Read the settled content from
# the frame instead. color_bounds adds a one-pixel analysis border, hence the coordinate correction below.
read -r before_blue_x before_blue_y before_blue_width before_blue_height \
  < <(color_bounds "$SHOTS/before.png" '(b > 0.7 && r < 0.15 && g < 0.15) ? 1 : 0')
closing_right=$((before_blue_x + before_blue_width - 1))
closing_middle=$((before_blue_y - 1 + before_blue_height / 2))

read -r border_red border_green border_blue \
  < <(pixel "$SHOTS/before.png" "$((closing_right + BORDER - 3))" "$closing_middle")
if ! ((border_red > 180 && border_green > 180 && border_blue < 30)); then
  echo "setup did not expose the closer's outer yellow border: $border_red $border_green $border_blue"
  exit 1
fi
read -r shadow_red shadow_green shadow_blue \
  < <(pixel "$SHOTS/before.png" "$((closing_right + BORDER + 8))" "$closing_middle")
if ! ((shadow_green > 15 && shadow_red < 15 && shadow_blue < 15)); then
  echo "setup did not expose the closer's outer green shadow: $shadow_red $shadow_green $shadow_blue"
  exit 1
fi

read -r before_red_x _ before_red_width _ \
  < <(color_bounds "$SHOTS/before.png" '(r > 0.7 && g < 0.15 && b < 0.15) ? 1 : 0')
before_red_right=$((before_red_x + before_red_width))

"$UMBRIEL" msg "window-close:$closing_id" > /dev/null
wait_unmapped close-mask-chrome-closing

# Analyse after capture so image processing does not perturb the compositor's animation cadence. The selected early
# frame has moved far enough to prove the inward clip is active, but not far enough for the raw mask to reach the
# captured right border or shadow on its own.
for i in $(seq 0 12); do
  grim "$SHOTS/frame-$i.png"
  sleep 0.08
done

selected=-1
for i in $(seq 0 12); do
  image="$SHOTS/frame-$i.png"
  read -r red_x _ red_width _ \
    < <(color_bounds "$image" '(r > 0.7 && g < 0.15 && b < 0.15) ? 1 : 0')
  red_right=$((red_x + red_width))
  red_advance=$((red_right - before_red_right))
  read -r blue_x _ blue_width _ \
    < <(color_bounds "$image" '(b > 0.7 && r < 0.15 && g < 0.15) ? 1 : 0')
  if ((red_advance >= 25 && red_advance <= 100 && blue_width >= 100)); then
    selected=$i
    selected_red_right=$red_right
    selected_blue_x=$blue_x
    break
  fi
done
if ((selected < 0)); then
  echo "no early close frame exposed a measurable constrained vacancy mask"
  exit 1
fi

image="$SHOTS/frame-$selected.png"
separation=$((selected_blue_x - selected_red_right))
readonly EXPECTED_GAP=$((GAP + 2 * BORDER))
if ((separation < EXPECTED_GAP - 5)); then
  echo "the inward mask edge exposed close content across the layout gap: gap=$separation expected=$EXPECTED_GAP"
  exit 1
fi

read -r border_red border_green border_blue \
  < <(pixel "$image" "$((closing_right + BORDER - 3))" "$closing_middle")
if ! ((border_red > 120 && border_green > 120 && border_blue < 30)); then
  echo "the captured outer border was clipped with the inward vacancy edge: $border_red $border_green $border_blue"
  exit 1
fi
read -r shadow_red shadow_green shadow_blue \
  < <(pixel "$image" "$((closing_right + BORDER + 8))" "$closing_middle")
if ! ((shadow_green > 10 && shadow_red < 15 && shadow_blue < 15)); then
  echo "the captured outer shadow was clipped with the inward vacancy edge: $shadow_red $shadow_green $shadow_blue"
  exit 1
fi

echo "the tiled close mask clipped its inward edge while preserving captured outer border and shadow"
