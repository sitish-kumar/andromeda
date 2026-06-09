-- Minimal reference plugin entry: a bar widget that shows a configurable label
-- and glyph, counts clicks, and accepts IPC to change its text at runtime.
--
-- Settings come from plugin.toml (seeded with their declared defaults); read them
-- with the single-arg getConfig — there is no inline fallback, the manifest owns
-- the default.
--
-- Drive it from the shell:
--   noctalia msg plugin noctalia/example:hello focused set "Hi there"

local label = barWidget.getConfig("label")
local glyph = barWidget.getConfig("glyph")
local showGlyph = barWidget.getConfig("show_glyph")

local clicks = 0

local function render()
  if showGlyph then
    barWidget.setGlyph(glyph)
  end
  barWidget.setText(label)
  barWidget.setTooltip("Example plugin — clicked " .. tostring(clicks) .. " time(s)")
end

-- Called on the widget's update interval. Static here, so slow the ticks right down.
function update()
  barWidget.setUpdateInterval(1000)
  render()
end

function onClick()
  clicks = clicks + 1
  noctalia.notify("Example", "Clicked " .. tostring(clicks) .. " time(s)")
  render()
end

function onIpc(event, payload)
  if event == "set" then
    label = payload
    render()
  end
end
