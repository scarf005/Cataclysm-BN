local mod = game.mod_runtime[game.current_mod]

-- Register mapgen postprocess hook (responsible for randomly spawning civilians)
---@param params any
---@return nil
local function on_mapgen_postprocess(params)
  if mod.on_mapgen_postprocess then mod.on_mapgen_postprocess(params) end
end

game.add_hook("on_mapgen_postprocess", on_mapgen_postprocess)

-- Register hook every 10 turns (10 seconds) (responsible for checking and executing civilian corpse pulping)
---@param _params HookParams
---@return nil
local function on_every_10_turns_civilian_update(_params)
  if mod and mod.on_every_10_turns_civilian_update then mod.on_every_10_turns_civilian_update() end
end

gapi.add_on_every_x_hook(TimeDuration.from_turns(10), on_every_10_turns_civilian_update)

gdebug.log_info("Civilians: Preload complete. Hooks registered.")
