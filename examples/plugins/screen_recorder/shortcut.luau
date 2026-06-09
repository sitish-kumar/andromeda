-- GPU Screen Recorder — control-center quick tile.
--
-- This tile holds no recording logic of its own. It mirrors the recorder widget
-- through the plugin's shared state:
--   reads  noctalia.state "status"  -> { state = ..., available = ... }
--   writes noctalia.state "command" -> "toggle" / "replay-toggle"
--
-- The recorder widget must be placed on a bar — it owns the recording process
-- and reacts to the command channel.

local recording = false
local replaying = false
local available = false

local function applyStatus(status)
    local st = (type(status) == "table" and status.state) or "idle"
    available = (type(status) == "table" and status.available) or false
    recording = (st == "recording" or st == "pending")
    replaying = (st == "replaying" or st == "replay_pending")

    if recording then
        shortcut.setLabel("Recording")
    elseif replaying then
        shortcut.setLabel("Replay buffer")
    else
        shortcut.setLabel("Record screen")
    end
    shortcut.setIcon("video", "video-off")
    shortcut.setActive(recording or replaying)
    shortcut.setEnabled(available)
end

-- Reflect whatever the recorder last published, then track live changes.
applyStatus(noctalia.state.get("status"))
noctalia.state.watch("status", applyStatus)

function onClick()
    noctalia.state.set("command", "toggle")
end

function onRightClick()
    noctalia.state.set("command", "replay-toggle")
end
