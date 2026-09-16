#!/usr/bin/env bash
# A critically damped overview row must not appear stationary and then move again by a rendered pixel. Captures are
# requested in parallel so screencopy latency does not serialize the observation past the spring's tail.
set -euo pipefail

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation.overview]
workspace_curve = "spring:1,1000"

[output."HEADLESS-1"]
workspaces = 5
EOF
"$UMBRIEL" msg config-reload > /dev/null

"$UMBRIEL_UNMAP_CLIENT" overview-settle 1200 700 > /dev/null 2>&1 &
for _ in $(seq 60); do
  [[ $("$UMBRIEL" windows --json | jq 'length') -eq 1 ]] && break
  sleep 0.05
done
[[ $("$UMBRIEL" windows --json | jq 'length') -eq 1 ]]

# Record the exact settled destination with workspace 5 already active.
"$UMBRIEL" msg workspace-switch:5 > /dev/null
sleep 0.4
"$UMBRIEL" msg overview-open > /dev/null
sleep 0.6
grim "$UMBRIEL_RUNTIME_DIR/settle-target.png"
"$UMBRIEL" msg overview-close > /dev/null
sleep 0.4

# Return to workspace 1, reopen the overview, and switch across several rows. Queue captures around the spring tail;
# each grim waits independently for a frame instead of delaying the next sample.
"$UMBRIEL" msg workspace-switch:1 > /dev/null
sleep 0.4
"$UMBRIEL" msg overview-open > /dev/null
sleep 0.6
"$UMBRIEL" msg workspace-switch:5 > /dev/null

capture_pids=()
for sample in $(seq 0 20); do
  delay=$(awk -v sample="$sample" 'BEGIN { printf "%.2f", 0.14 + sample * 0.02 }')
  (sleep "$delay"; grim "$UMBRIEL_RUNTIME_DIR/settle-$sample.png") &
  capture_pids+=("$!")
done
for pid in "${capture_pids[@]}"; do
  wait "$pid"
done

for sample in $(seq 0 20); do
  difference=$(magick "$UMBRIEL_RUNTIME_DIR/settle-$sample.png" "$UMBRIEL_RUNTIME_DIR/settle-target.png" \
    -compose difference -composite -format '%[fx:mean]' info:)
  if ((sample >= 7)) && awk -v difference="$difference" 'BEGIN { exit !(difference > 0) }'; then
    echo "overview preview still moved at the one-pixel spring tail (frame $sample difference $difference)"
    exit 1
  fi
done

echo 'overview spring has no late one-pixel preview step'
