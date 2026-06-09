-- Headless [[service]] entry: runs in the background with no UI, ticking once a
-- second and publishing an incrementing counter to the plugin's shared state.
-- Any of this plugin's entries (the hello widget) can read it with
-- noctalia.state.get / noctalia.state.watch.

local count = 0

function update()
  -- Drive this service's own tick rate (runtime concern, not presentation).
  noctalia.setUpdateInterval(1000)
  count = count + 1
  noctalia.state.set("tick", count)
end
