---@class ComputerParams
---@field session ComputerSession
---@field user Avatar

local computer = {}

-- Ids are built at call time: terrain, furniture and item types are not loaded yet
-- when this file is required during preload.

---@param str string
---@return TerIntId
local function ter(str) return TerId.new(str):int_id() end

---@param str string
---@return FurnIntId
local function furn(str) return FurnId.new(str):int_id() end

---@param str string
---@return FieldTypeIntId
local function field(str) return FieldTypeIntId.new(FieldTypeId.new(str)) end

---@param a integer
---@param b integer
---@return integer
local function div_nonzero(a, b) return a // math.max(b, 1) end

---@param origin TripointBubMs
---@param volume integer
local function alarm(origin, volume)
  gapi.sound({
    origin = origin,
    volume = volume,
    category = "alarm",
    description = locale.gettext("an alarm sound!"),
    id = "environment",
    variant = "alarm",
  })
end

-- Offsets of the reality bubble, used for seeds and log headers.
---@param user Avatar
---@return integer, integer, integer
local function level_offsets(user)
  local origin = gapi.bub_to_abs(TripointBubSm.new(0, 0, 0))
  return origin.x, origin.y, user:get_pos_ms().z
end

---@param text string
---@return boolean
local function query_yn(text)
  local popup = QueryPopup.new()
  popup:message(text)
  return popup:query_yn() == "YES"
end

---@return Item?
local function pick_usb()
  ---@param it Item
  local function is_usb(it) return it:get_type():str() == "usb_drive" end
  return gapi.choose_inventory_item(locale.gettext("Choose drive:"), is_usb)
end

-- Removes hostile console-linked turrets on the user's overmap tile.
---@param user Avatar
local function remove_submap_turrets(user)
  local omt = user:abs_pos():to_omt()
  for _, critter in ipairs(gapi.get_all_monsters()) do
    if
      critter:abs_pos():to_omt() == omt
      and critter:has_flag(MonsterFlag.CONSOLE_DESPAWN)
      and critter:attitude_to(user) == Attitude.Hostile
    then
      gapi.remove_monster(critter)
    end
  end
end

---@param missions Mission[]
---@param ids table<string, boolean>
---@return Mission?
local function find_mission(missions, ids)
  for _, miss in ipairs(missions) do
    if ids[miss:mission_id():str()] then return miss end
  end
  return nil
end

---@param usb Item
---@param software ItypeId
---@return Item
local function download_to(usb, software)
  usb:clear_contents()
  usb:put_in(Item.spawn(software, -1))
  return usb:get_contents()[1] --[[@as Item]]
end

---@param session ComputerSession
---@param category string
local function print_snippet(session, category) session:print_text(gapi.random_snippet(category) or "") end

---@param params ComputerParams
function computer.open(params)
  local here = gapi.get_map()
  here:translate_radius(ter("t_door_metal_locked"), ter("t_floor"), 25.0, params.user:get_pos_ms(), true)
  params.session:query_any(locale.gettext("Doors opened.  Press any key…"))
end

---@param params ComputerParams
function computer.open_disarm(params)
  remove_submap_turrets(params.user)
  computer.open(params)
end

-- Lock and unlock doors only within 8 tiles, so different computers in one building can control different doors.

---@param params ComputerParams
function computer.lock(params)
  local here = gapi.get_map()
  here:translate_radius(ter("t_door_metal_c"), ter("t_door_metal_locked"), 8.0, params.user:get_pos_ms(), true)
  params.session:query_any(locale.gettext("Lock enabled.  Press any key…"))
end

---@param params ComputerParams
function computer.unlock(params)
  local here = gapi.get_map()
  here:translate_radius(ter("t_door_metal_locked"), ter("t_door_metal_c"), 8.0, params.user:get_pos_ms(), true)
  params.session:query_any(locale.gettext("Lock disabled.  Press any key…"))
end

---@param params ComputerParams
function computer.unlock_disarm(params)
  remove_submap_turrets(params.user)
  computer.unlock(params)
end

---@param params ComputerParams
function computer.unlock_labpass(params)
  local labpass = ItypeId.new("labpass")
  if not params.user:has_amount(labpass, 1) then
    params.session:query_any(locale.gettext("Override code required!  Press any key…"))
    return
  end
  params.user:use_amount(labpass, 1)
  computer.unlock_disarm(params)
end

-- The church computer needs toll to ring the bells.
---@param params ComputerParams
function computer.toll(params)
  gapi.sound({
    origin = params.user:get_pos_ms(),
    volume = 130,
    category = "music",
    --~ the sound of a church bell ringing
    description = locale.gettext("Bohm…  Bohm…  Bohm…"),
    id = "environment",
    variant = "church_bells",
  })
end

---@param params ComputerParams
function computer.sample(params)
  params.user:mod_moves(-30)
  local here = gapi.get_map()
  local sewage = ItypeId.new("sewage")
  local full_charges = sewage:obj():charges_default()
  local pump = ter("t_sewage_pump")
  local counter = furn("f_counter")
  for _, p in ipairs(here:points_on_zlevel()) do
    if here:get_ter_at(p) == pump then
      for _, n in ipairs(here:points_in_radius(p, 1)) do
        if here:get_furn_at(n) == counter then
          local found_item = false
          for _, elem in ipairs(here:get_items_at(n):as_item_stack():items()) do
            local capa = elem:remaining_capacity_for_id(sewage, false)
            if capa > 0 then
              capa = math.min(full_charges, capa)
              local first = elem:get_contents()[1]
              if first == nil then
                elem:put_in(Item.spawn(sewage, capa))
              else
                first.charges = first.charges + capa
              end
              found_item = true
              break
            end
          end
          if not found_item then here:create_item_at(n, sewage, -1) end
        end
      end
    end
  end
end

---@param params ComputerParams
function computer.release(params)
  gapi.send_event("releases_subspace_specimens")
  local pos = params.user:get_pos_ms()
  alarm(pos, 80)
  gapi.get_map():translate_radius(ter("t_reinforced_glass"), ter("t_thconc_floor"), 25.0, pos, true)
  params.session:query_any(locale.gettext("Containment shields opened.  Press any key…"))
end

---@param params ComputerParams
function computer.release_bionics(params)
  local pos = params.user:get_pos_ms()
  alarm(pos, 80)
  gapi.get_map():translate_radius(ter("t_reinforced_glass"), ter("t_thconc_floor"), 3.0, pos, true)
  params.session:query_any(locale.gettext("Containment shields opened.  Press any key…"))
end

---@param params ComputerParams
function computer.release_disarm(params)
  remove_submap_turrets(params.user)
  computer.release_bionics(params)
end

-- Kills monsters held in glass cells: glass on one side and concrete wall on the other.
---@param params ComputerParams
function computer.terminate(params)
  gapi.send_event("terminates_subspace_specimens")
  local here = gapi.get_map()
  local glass = ter("t_reinforced_glass")
  local wall = ter("t_concrete_wall")
  local z = params.user:get_pos_ms().z
  for _, mon in ipairs(gapi.get_all_monsters()) do
    local p = mon:get_pos_ms()
    if p.z == z then
      local north = here:get_ter_at(TripointBubMs.new(p.x, p.y + -1, p.z))
      local south = here:get_ter_at(TripointBubMs.new(p.x, p.y + 1, p.z))
      if (north == glass and south == wall) or (south == glass and north == wall) then mon:die(params.user) end
    end
  end
  params.session:query_any(locale.gettext("Subjects terminated.  Press any key…"))
end

-- Toggles a portal trap on every tile with 4 or more radio towers within 2 tiles.
---@param _params ComputerParams
function computer.portal(_params)
  gapi.send_event("opens_portal")
  local here = gapi.get_map()
  local tower = ter("t_radio_tower")
  local portal = TrapId.new("tr_portal"):int_id()
  ---@type table<string, { pos: TripointBubMs, towers: integer }>
  local counts = {}
  for _, p in ipairs(here:points_on_zlevel()) do
    if here:get_ter_at(p) == tower then
      for _, near in ipairs(here:points_in_radius(p, 2)) do
        local key = near.x .. "," .. near.y
        local entry = counts[key] or { pos = near, towers = 0 }
        entry.towers = entry.towers + 1
        counts[key] = entry
      end
    end
  end
  for _, entry in pairs(counts) do
    if entry.towers >= 4 then
      if here:get_trap_at(entry.pos) == portal then
        here:remove_trap_at(entry.pos)
      else
        here:set_trap_at(entry.pos, portal)
      end
    end
  end
end

---@param params ComputerParams
function computer.cascade(params)
  if not params.session:query_bool(locale.gettext("WARNING: Resonance cascade carries severe risk!  Continue?")) then
    return
  end
  gapi.send_event("causes_resonance_cascade")
  local here = gapi.get_map()
  local tower = ter("t_radio_tower")
  local pos = params.user:get_pos_ms()
  local towers = {}
  for _, dest in ipairs(here:points_in_radius(pos, 10)) do
    if here:get_ter_at(dest) == tower then table.insert(towers, dest) end
  end
  gapi.resonance_cascade(#towers > 0 and towers[gapi.rng(1, #towers)] or pos)
end

---@param params ComputerParams
function computer.research(params)
  local session = params.session
  local levx, levy, levz = level_offsets(params.user)
  -- The seed makes one computer show the same log each time.
  local log = gapi.random_snippet("lab_notes", levx + levy + levz + session:get_alerts())
  if log == nil then
    log = locale.gettext("No data found.")
  else
    params.user:mod_moves(-70)
  end
  session:print_text(log)
  if session:get_alerts() == 0 then
    session:query_any(locale.gettext("Local data-access error logged, alerting helpdesk.  Press any key…"))
  else
    session:query_any(
      locale.gettext("Warning: anomalous archive-access activity detected at this node.  Press any key…")
    )
  end
  session:set_alerts(session:get_alerts() + 1)
end

---@param params ComputerParams
function computer.radio_archive(params)
  local session = params.session
  params.user:mod_moves(-300)
  gapi.fade_audio_channel(SfxChannel.radio, 100)
  gapi.play_ambient_variant_sound("radio", "inaudible_chatter", 100, SfxChannel.radio, 2000, -1.0, -1)
  session:print_text(
    string.format(
      "Accessing archive.  Playing audio recording nr %d.\n%s",
      gapi.rng(1, 9999),
      gapi.random_snippet("radio_archive") or ""
    )
  )
  if gapi.rng(1, 3) == 1 then
    session:query_any(locale.gettext("Warning: restricted data access.  Attempt logged.  Press any key…"))
    session:set_alerts(session:get_alerts() + 1)
  else
    session:query_any(locale.gettext("Press any key…"))
  end
  gapi.fade_audio_channel(SfxChannel.radio, 100)
end

---@param params ComputerParams
function computer.maps(params)
  local session = params.session
  params.user:mod_moves(-30)
  local center = params.user:abs_pos():to_omt()
  overmapbuffer.reveal(TripointAbsOmt.new(center.x, center.y, 0), 40)
  session:query_any(
    locale.gettext("Surface map data downloaded.  Local anomalous-access error logged.  Press any key…")
  )
  session:remove_option("maps")
  session:set_alerts(session:get_alerts() + 1)
end

-- Marks every overmap tile within 60 of the user that matches one of the patterns as seen.
---@param user Avatar
---@param patterns { [1]: string, [2]: OtMatchType }[]
local function reveal_network(user, patterns)
  local center = user:abs_pos():to_omt()
  for i = -60, 60 do
    for j = -60, 60 do
      local p = TripointAbsOmt.new(center.x + i, center.y + j, center.z)
      for _, pattern in ipairs(patterns) do
        if overmapbuffer.check_ot(pattern[1], pattern[2], p) then
          overmapbuffer.set_seen(p, true)
          break
        end
      end
    end
  end
end

---@param params ComputerParams
function computer.map_sewer(params)
  params.user:mod_moves(-30)
  reveal_network(params.user, { { "sewer", OtMatchType.TYPE }, { "sewage", OtMatchType.PREFIX } })
  params.session:query_any(locale.gettext("Sewage map data downloaded.  Press any key…"))
  params.session:remove_option("map_sewer")
end

---@param params ComputerParams
function computer.map_subway(params)
  params.user:mod_moves(-30)
  reveal_network(params.user, { { "subway", OtMatchType.TYPE }, { "lab_train_depot", OtMatchType.CONTAINS } })
  params.session:query_any(locale.gettext("Subway map data downloaded.  Press any key…"))
  params.session:remove_option("map_subway")
end

---@param params ComputerParams
function computer.miss_disarm(params)
  if not query_yn(locale.gettext("Disarm missile.")) then
    gapi.add_msg(MsgType.neutral, locale.gettext("Nuclear missile remains active."))
    return
  end
  gapi.send_event("disarms_nuke")
  gapi.add_msg(MsgType.info, locale.gettext("Nuclear missile disarmed!"))
  params.session:clear_options()
  params.session:activate_failure("shutdown")
end

---@param params ComputerParams
function computer.list_bionics(params)
  local session = params.session
  params.user:mod_moves(-30)
  local here = gapi.get_map()
  local limit = gapi.get_terminal_height() - 8
  local names = {}
  local more = 0
  for _, p in ipairs(here:points_on_zlevel()) do
    if here:has_items_at(p) then
      for _, elem in ipairs(here:get_items_at(p):as_item_stack():items()) do
        if elem:is_bionic() then
          if #names < limit then
            table.insert(names, elem:type_name())
          else
            more = more + 1
          end
        end
      end
    end
  end

  session:reset_terminal()
  session:print_newline()
  session:print_line(locale.gettext("Bionic access - Manifest:"))
  session:print_newline()
  for _, name in ipairs(names) do
    session:print_line(name)
  end
  if more > 0 then
    session:print_line(string.format(locale.vgettext("%d OTHER FOUND…", "%d OTHERS FOUND…", more), more))
  end
  session:print_newline()
  session:query_any(locale.gettext("Press any key…"))
end

---@param params ComputerParams
function computer.elevator_on(params)
  local here = gapi.get_map()
  local off = ter("t_elevator_control_off")
  local on = ter("t_elevator_control")
  for _, p in ipairs(here:points_on_zlevel()) do
    if here:get_ter_at(p) == off then here:set_ter_at(p, on) end
  end
  params.session:query_any(locale.gettext("Elevator activated.  Press any key…"))
end

---@param params ComputerParams
function computer.amigara_log(params)
  local session = params.session
  local user = params.user
  gapi.add_timed_event("amigara_whispers", gapi.current_turn() + TimeDuration.from_minutes(1))
  local levx, levy, levz = level_offsets(user)

  for _, category in ipairs({ "amigara1", "amigara2", "amigara3" }) do
    user:mod_moves(-30)
    session:reset_terminal()
    session:print_line(string.format(locale.gettext("NEPower Mine(%d:%d) Log"), levx, levy))
    print_snippet(session, category)
    if not session:query_bool(locale.gettext("Continue reading?")) then return end
  end

  session:reset_terminal()
  for _ = 1, 10 do
    session:print_gibberish_line()
  end
  session:print_newline()
  session:print_newline()
  session:print_newline()
  session:print_line(locale.gettext("AMIGARA PROJECT"))
  session:print_newline()
  session:print_newline()
  if not session:query_bool(locale.gettext("Continue reading?")) then return end

  user:mod_moves(-30)
  session:reset_terminal()
  session:print_line(
    string.format(
      locale.gettext("SITE %d%d%d\nPERTINENT FOREMAN LOGS WILL BE PREPENDED TO NOTES"),
      levx,
      levy,
      math.abs(levz)
    )
  )
  print_snippet(session, "amigara4")
  session:print_gibberish_line()
  session:print_gibberish_line()
  session:print_newline()
  session:print_error(locale.gettext("FILE CORRUPTED, PRESS ANY KEY…"))
  session:query_any()
  session:reset_terminal()
end

---@param params ComputerParams
function computer.amigara_start(params)
  gapi.add_timed_event("amigara", gapi.current_turn() + TimeDuration.from_seconds(10))
  -- Each run spawns more amigara monsters and artifacts, so the option runs once.
  params.session:remove_option("amigara_start")
end

---@param params ComputerParams
function computer.complete_disable_external_power(params)
  local session = params.session
  local miss = find_mission(params.user:get_active_missions(), { MISSION_OLD_GUARD_NEC_COMMO_2 = true })
  if miss == nil then
    session:print_error(locale.gettext("ACCESS DENIED"))
    session:query_any()
    return
  end
  session:print_error(locale.gettext("--ACCESS GRANTED--"))
  session:print_error(locale.gettext("Mission Complete!"))
  miss:step_complete(1)
  session:query_any()
end

---@param params ComputerParams
function computer.repeater_mod(params)
  local session = params.session
  local user = params.user
  local mod = ItypeId.new("radio_repeater_mod")
  if not user:has_amount(mod, 1) then
    session:print_error(locale.gettext("You do not have a repeater mod to install…"))
    session:query_any()
    return
  end
  local miss = find_mission(
    user:get_active_missions(),
    { MISSION_OLD_GUARD_NEC_COMMO_3 = true, MISSION_OLD_GUARD_NEC_COMMO_4 = true }
  )
  if miss == nil then return end
  miss:step_complete(1)
  session:print_error(locale.gettext("Repeater mod installed…"))
  session:print_error(locale.gettext("Mission Complete!"))
  user:use_amount(mod, 1)
  session:query_any()
  session:clear_options()
  session:activate_failure("shutdown")
end

---@param params ComputerParams
function computer.download_software(params)
  local session = params.session
  local usb = pick_usb()
  if usb == nil then
    session:print_error(locale.gettext("USB drive required!"))
    session:query_any()
    return
  end
  local miss = session:get_mission()
  if miss == nil then
    gdebug.debugmsg("Computer couldn't find its mission!")
    return
  end
  params.user:mod_moves(-30)
  download_to(usb, miss:get_item_id()).mission_id = miss:get_id()
  session:print_line(locale.gettext("Software downloaded."))
  session:query_any()
end

---@param session ComputerSession
---@param blood Item
local function report_blood(session, blood)
  if blood:has_use("POISON") then
    session:print_line(
      locale.gettext("Result: Unknown pathogen found.  Pathogen bonded to erythrocytes and leukocytes.")
    )
    if session:query_bool(locale.gettext("Download data?")) then
      local usb = pick_usb()
      if usb == nil then
        session:print_error(locale.gettext("USB drive required!"))
      else
        download_to(usb, ItypeId.new("software_blood_data"))
        session:print_line(locale.gettext("Software downloaded."))
      end
    end
  elseif blood:has_vitamin(VitaminId.new("human_flesh_vitamin")) then
    session:print_line(locale.gettext("Result: Human blood, no pathogens found."))
  else
    session:print_line(locale.gettext("Result: Unknown blood type.  Test non-conclusive."))
  end
end

---@param params ComputerParams
function computer.blood_anal(params)
  local session = params.session
  params.user:mod_moves(-70)
  local here = gapi.get_map()
  local centrifuge = furn("f_centrifuge")
  local is_blood = JsonFlagId.new("IS_BLOOD")
  for _, dest in ipairs(here:points_in_radius(params.user:get_pos_ms(), 2)) do
    if here:get_furn_at(dest) == centrifuge then
      local items = here:get_items_at(dest):as_item_stack():items()
      if #items == 0 then
        session:print_error(locale.gettext("ERROR: Please place sample in centrifuge."))
      elseif #items > 1 then
        session:print_error(locale.gettext("ERROR: Please remove all but one sample from centrifuge."))
      else
        local sample = (items[1] --[[@as Item]]):get_contents()[1]
        if not sample then
          session:print_error(locale.gettext("ERROR: Please only use container with blood sample."))
        elseif sample:has_flag(is_blood) then
          report_blood(session, sample)
        else
          session:print_error(locale.gettext("ERROR: Please only use blood samples."))
        end
      end
    end
  end
  session:query_any(locale.gettext("Press any key…"))
end

---@param params ComputerParams
function computer.data_anal(params)
  local session = params.session
  local user = params.user
  user:mod_moves(-30)
  local here = gapi.get_map()
  local scan_floor = ter("t_floor_blue")
  for _, dest in ipairs(here:points_in_radius(user:get_pos_ms(), 2)) do
    if here:get_ter_at(dest) == scan_floor then
      session:print_error(locale.gettext("PROCESSING DATA"))
      local items = here:get_items_at(dest):as_item_stack():items()
      local first = #items == 1 and items[1] or nil
      local kind = first and first:get_type():str()
      if #items == 0 then
        session:print_error(locale.gettext("ERROR: Please place memory bank in scan area."))
      elseif #items > 1 then
        session:print_error(locale.gettext("ERROR: Please only scan one item at a time."))
      elseif kind ~= "usb_drive" and kind ~= "black_box" then
        session:print_error(locale.gettext("ERROR: Memory bank destroyed or not present."))
      elseif kind == "usb_drive" and first and #first:get_contents() == 0 then
        session:print_error(locale.gettext("ERROR: Memory bank is empty."))
      elseif kind == "black_box" then
        session:print_line(locale.gettext("Memory Bank: Military Hexron Encryption\nPrinting Transcript\n"))
        here:create_item_at(user:get_pos_ms(), ItypeId.new("black_box_transcript"), -1)
      else
        session:print_line(locale.gettext("Memory Bank: Unencrypted\nNothing of interest.\n"))
      end
    end
  end
  session:query_any(locale.gettext("Press any key…"))
end

---@param params ComputerParams
function computer.disconnect(params)
  params.session:reset_terminal()
  params.session:print_line(
    locale.gettext(
      "\nERROR: NETWORK DISCONNECT\nUNABLE TO REACH NETWORK ROUTER OR PROXY.  PLEASE CONTACT YOUR\nSYSTEM ADMINISTRATOR TO RESOLVE THIS ISSUE.\n  \n"
    )
  )
  params.session:query_any(locale.gettext("Press any key to continue…"))
end

---@param params ComputerParams
function computer.emerg_mess(params)
  params.session:print_line(
    locale.gettext(
      "GREETINGS CITIZEN.  A BIOLOGICAL ATTACK HAS TAKEN PLACE AND A STATE OF\nEMERGENCY HAS BEEN DECLARED.  EMERGENCY PERSONNEL WILL BE AIDING YOU\nSHORTLY.  TO ENSURE YOUR SAFETY PLEASE FOLLOW THE STEPS BELOW.\n\n1. DO NOT PANIC.\n2. REMAIN INSIDE THE BUILDING.\n3. SEEK SHELTER IN THE BASEMENT.\n4. USE PROVIDED GAS MASKS.\n5. AWAIT FURTHER INSTRUCTIONS.\n\n  \n"
    )
  )
  params.session:query_any(locale.gettext("Press any key to continue…"))
end

---@param params ComputerParams
function computer.tower_unresponsive(params)
  params.session:print_line(
    locale.gettext(
      "  WARNING, RADIO TOWER IS UNRESPONSIVE.\n  \n  BACKUP POWER INSUFFICIENT TO MEET BROADCASTING REQUIREMENTS.\n  IN THE EVENT OF AN EMERGENCY, CONTACT LOCAL NATIONAL GUARD\n  UNITS TO RECEIVE PRIORITY WHEN GENERATORS ARE BEING DEPLOYED.\n  \n  \n"
    )
  )
  params.session:query_any(locale.gettext("Press any key to continue…"))
end

-- Actions that show snippet pages, one page per category.
---@type table<string, string[]>
local snippet_messages = {
  sr1_mess = { "sr1_mess" },
  sr2_mess = { "sr2_mess" },
  sr3_mess = { "sr3_mess" },
  sr4_mess = { "sr4_mess" },
  srcf_1_mess = { "scrf_1_mess" },
  srcf_2_mess = { "scrf_2_1_mess", "scrf_2_2_mess" },
  srcf_3_mess = { "scrf_3_mess" },
  srcf_seal_order = { "scrf_seal_order" },
}

for action, categories in pairs(snippet_messages) do
  ---@param params ComputerParams
  computer[action] = function(params)
    for _, category in ipairs(categories) do
      params.session:reset_terminal()
      print_snippet(params.session, category)
      params.session:query_any(locale.gettext("Press any key to continue…"))
    end
  end
end

---@param params ComputerParams
function computer.srcf_seal(params)
  local session = params.session
  local user = params.user
  gapi.send_event("seals_hazardous_material_sarcophagus")
  session:print_line(locale.gettext("Charges Detonated"))
  session:print_line(locale.gettext("Backup Generator Power Failing"))
  session:print_line(locale.gettext("Evacuate Immediately"))
  gapi.add_msg(MsgType.warning, locale.gettext("Evacuate Immediately!"))
  local here = gapi.get_map()
  local rubble = furn("f_rubble_rock")
  local dirt = ter("t_dirt")
  local elevator = ter("t_elevator")
  local vat = furn("f_cloning_vat")
  local glass = ter("t_wall_glass")
  local pump = ter("t_sewage_pump")
  ---@type table<TerIntId, boolean>
  local sewer = { [ter("t_sewage_pipe")] = true, [ter("t_sewage")] = true, [ter("t_grate")] = true }
  for _, p in ipairs(here:points_on_zlevel()) do
    if here:get_ter_at(p) == elevator or here:get_furn_at(p) == vat then
      here:make_rubble(p, rubble, dirt)
      gapi.explosion({ pos = p, power = 40, source = user, factor = 0.7, fire = true })
    end
    if here:get_ter_at(p) == glass then here:make_rubble(p, rubble, dirt) end
    if sewer[here:get_ter_at(p)] then here:make_rubble(p, rubble, dirt) end
    if here:get_ter_at(p) == pump then
      here:make_rubble(p, rubble, dirt)
      gapi.explosion({ pos = p, power = 50, source = user, factor = 0.7, fire = true })
    end
  end
  session:clear_options()
  session:activate_failure("shutdown")
end

---@param here Map
---@param z integer
---@return TripointBubMs?, boolean
local function find_elevator_control(here, z)
  local off = ter("t_elevator_control_off")
  local on = ter("t_elevator_control")
  local found = nil
  local is_on = false
  for _, p in ipairs(here:points_on_zlevel(z)) do
    local t = here:get_ter_at(p)
    if t == off or t == on then
      found = p
      is_on = t == on
    end
  end
  return found, is_on
end

---@param params ComputerParams
function computer.srcf_elevator(params)
  local session = params.session
  local user = params.user
  local here = gapi.get_map()
  local on = ter("t_elevator_control")
  local code = ItypeId.new("sarcophagus_access_code")
  local surface, surface_on = find_elevator_control(here, 0)
  local underground, underground_on = find_elevator_control(here, -2)

  if surface == nil or underground == nil then
    session:reset_terminal()
    session:print_error(locale.gettext("\nElevator control network unreachable!\n\n"))
  elseif not surface_on and not underground_on then
    if not user:has_amount(code, 1) then
      session:print_error(locale.gettext("Access code required!\n\n"))
    else
      user:use_amount(code, 1)
      here:set_ter_at(surface, on)
      here:set_ter_at(underground, on)
      surface_on = true
      underground_on = true
    end
  elseif surface_on and not underground_on then
    -- Saves from before both controls were switched together have only one on.
    here:set_ter_at(underground, on)
    underground_on = true
  elseif underground_on and not surface_on then
    here:set_ter_at(surface, on)
    surface_on = true
  end

  if surface_on and underground_on then
    session:reset_terminal()
    session:print_line(
      locale.gettext("\nPower:         Backup Only\nRadiation Level:  Very Dangerous\nOperational:   Overridden\n\n")
    )
  end
  session:query_any(locale.gettext("Press any key…"))
end

---@param here Map
---@param center TripointBubMs
---@param min integer
---@param max integer
local function irradiate_area(here, center, min, max)
  here:adjust_radiation_at(center, gapi.rng(min, max))
  for _, p in ipairs(here:points_in_radius(center, 5)) do
    here:adjust_radiation_at(p, div_nonzero(gapi.rng(min, max), coords.rl_dist(p, center) or 0))
  end
end

-- A radiation spike sets off electronic detonators on the platform.
---@param params ComputerParams
---@param here Map
---@param dest TripointBubMs
---@param it Item
local function irradiator_meltdown(params, here, dest, it)
  local session = params.session
  local user = params.user
  gapi.explosion({ pos = dest, power = 40, source = user })
  session:reset_terminal()
  session:print_error(locale.gettext("WARNING [409]: Primary sensors offline!"))
  session:print_error(locale.gettext("  >> Initialize secondary sensors: Geiger profiling…"))
  session:print_error(locale.gettext("  >> Radiation spike detected!\n"))
  session:print_error(locale.gettext("WARNING [912]: Catastrophic malfunction!  Contamination detected!"))
  session:print_error(locale.gettext("EMERGENCY PROCEDURE [1]:  Evacuate.  Evacuate.  Evacuate.\n"))
  alarm(user:get_pos_ms(), 90)
  here:remove_item_at(dest, it)
  here:make_rubble(dest, furn("f_rubble"), ter("t_dirt"))
  here:propagate_field(dest, field("fd_nuke_gas"), 100, 3)
  here:translate_radius(ter("t_water_pool"), ter("t_sewage"), 8.0, dest, true)
  irradiate_area(here, dest, 50, 500)
  local dist = coords.rl_dist(user:get_pos_ms(), dest) or 0
  if here:pl_sees(dest, 10) then
    user:irradiate(gapi.rng_float(50, 250) / dist, false)
  else
    user:irradiate(gapi.rng_float(20, 100) / dist, false)
  end
  session:query_any(locale.gettext("EMERGENCY SHUTDOWN!  Press any key…"))
  session:clear_options()
  session:activate_failure("shutdown")
end

-- Irradiates food on the radiation platform and raises radiation around it.
---@param params ComputerParams
function computer.irradiator(params)
  local session = params.session
  local user = params.user
  user:mod_moves(-30)
  local here = gapi.get_map()
  local platform = ter("t_rad_platform")
  local detonators = { mininuke = true, mininuke_act = true, c4 = true }
  local failed = false
  local platform_exists = false
  for _, dest in ipairs(here:points_in_radius(user:get_pos_ms(), 10)) do
    if here:get_ter_at(dest) == platform then
      platform_exists = true
      local items = here:get_items_at(dest):as_item_stack():items()
      if #items == 0 then
        session:print_error(locale.gettext("ERROR: Processing platform empty."))
      else
        user:mod_moves(-300)
        for _, it in ipairs(items) do
          local irradiated = ItypeId.new("irradiated_" .. it:get_type():str())
          if not it:rotten() and irradiated:is_valid() then it:convert(irradiated) end
          if detonators[it:get_type():str()] then
            irradiator_meltdown(params, here, dest, it)
            failed = true
            break
          end
          irradiate_area(here, dest, 20, 50)
          -- Without shielding the source irradiates the user, less with distance.
          if here:pl_sees(dest, 10) then
            user:irradiate(gapi.rng_float(5, 25) / (coords.rl_dist(user:get_pos_ms(), dest) or 0), false)
          end
        end
        if not failed then
          session:print_error(locale.gettext("PROCESSING…  CYCLE COMPLETE."))
          session:print_error(
            string.format(locale.gettext("GEIGER COUNTER @ PLATFORM: %s mSv/h."), here:get_radiation_at(dest))
          )
        end
      end
    end
  end
  if not platform_exists then
    session:print_error(
      locale.gettext("CRITICAL ERROR…  RADIATION PLATFORM UNRESPONSIVE.  COMPLY TO PROCEDURE RP_M_01_rev.03.")
    )
  end
  if not failed then session:query_any(locale.gettext("Press any key…")) end
end

---@param here Map
---@param center TripointBubMs
---@return TripointBubMs?
local function find_rad_platform(here, center)
  local platform = ter("t_rad_platform")
  local found = nil
  for _, dest in ipairs(here:points_in_radius(center, 10)) do
    if here:get_ter_at(dest) == platform then found = dest end
  end
  return found
end

-- Geiger counter for the irradiator: the platform zone, then the console and the user.
---@param params ComputerParams
function computer.geiger(params)
  local session = params.session
  local user = params.user
  user:mod_moves(-30)
  local here = gapi.get_map()
  session:print_error(locale.gettext("RADIATION MEASUREMENTS:"))
  local platform = find_rad_platform(here, user:get_pos_ms())
  if platform ~= nil then
    -- The zone average adds the platform reading once for each tile.
    local platform_rads = here:get_radiation_at(platform)
    local sum_rads = 0
    local peak_rad = 0
    local tiles_counted = 0
    for _, dest in ipairs(here:points_in_radius(platform, 3)) do
      local rads = here:get_radiation_at(dest)
      sum_rads = sum_rads + rads + platform_rads
      tiles_counted = tiles_counted + 2
      peak_rad = math.max(peak_rad, rads, platform_rads)
    end
    session:print_error(string.format(locale.gettext("GEIGER COUNTER @ ZONE:… AVG %s mSv/h."), sum_rads // tiles_counted))
    session:print_error(string.format(locale.gettext("GEIGER COUNTER @ ZONE:… MAX %s mSv/h."), peak_rad))
    session:print_newline()
  end
  session:print_error(
    string.format(locale.gettext("GEIGER COUNTER @ CONSOLE:… %s mSv/h."), here:get_radiation_at(user:get_pos_ms()))
  )
  session:print_error(string.format(locale.gettext("PERSONAL DOSIMETRY:… %s mSv."), user:get_rad()))
  session:print_newline()
  session:query_any(locale.gettext("Press any key…"))
end

-- Moves items platform -> unloading bay, then loading bay -> platform. Liquids on the
-- loading bay are lost.
---@param params ComputerParams
function computer.conveyor(params)
  local session = params.session
  params.user:mod_moves(-300)
  local here = gapi.get_map()
  local platform_ter = ter("t_rad_platform")
  local loading_ter = ter("t_floor_red")
  local unloading_ter = ter("t_floor_green")
  ---@type TripointBubMs?, TripointBubMs?, TripointBubMs?
  local loading, unloading, platform
  for _, dest in ipairs(here:points_in_radius(params.user:get_pos_ms(), 10)) do
    local t = here:get_ter_at(dest)
    if t == platform_ter then
      platform = dest
    elseif t == loading_ter then
      loading = dest
    elseif t == unloading_ter then
      unloading = dest
    end
  end
  if loading == nil or platform == nil or unloading == nil then
    session:print_error(locale.gettext("Conveyor belt malfunction.  Consult maintenance team."))
    session:query_any(locale.gettext("Press any key…"))
    return
  end

  local unloaded = here:get_items_at(platform):as_item_stack():items()
  if #unloaded > 0 then
    session:print_line(locale.gettext("Moving items: PLATFORM --> UNLOADING BAY."))
  else
    session:print_line(locale.gettext("No items detected at: PLATFORM."))
  end
  for _, it in ipairs(unloaded) do
    here:move_item_to(platform, it, unloading)
  end

  local loaded = here:get_items_at(loading):as_item_stack():items()
  if #loaded > 0 then
    session:print_line(locale.gettext("Moving items: LOADING BAY --> PLATFORM."))
  else
    session:print_line(locale.gettext("No items detected at: LOADING BAY."))
  end
  for _, it in ipairs(loaded) do
    if it:made_of_phase(Phase.liquid) then
      here:remove_item_at(loading, it)
    else
      here:move_item_to(loading, it, platform)
    end
  end
  session:query_any(locale.gettext("Conveyor belt cycle complete.  Press any key…"))
end

-- Opens closed reinforced glass shutters and closes open ones.
---@param params ComputerParams
function computer.shutters(params)
  params.user:mod_moves(-300)
  gapi
    .get_map()
    :translate_radius(
      ter("t_reinforced_glass_shutter_open"),
      ter("t_reinforced_glass_shutter"),
      8.0,
      params.user:get_pos_ms(),
      true,
      true
    )
  params.session:query_any(locale.gettext("Toggling shutters.  Press any key…"))
end

---@param params ComputerParams
function computer.extract_rad_source(params)
  local session = params.session
  local user = params.user
  if not query_yn(locale.gettext("Operation irreversible.  Extract radioactive material?")) then return end
  user:mod_moves(-300)
  local here = gapi.get_map()
  local platform = find_rad_platform(here, user:get_pos_ms())
  if platform == nil then
    session:query_any(locale.gettext("ERROR!  Radiation platform unresponsive…  Press any key."))
    return
  end
  local cobalt = ItypeId.new("cobalt_60")
  for _ = 1, gapi.rng(8, 15) do
    here:create_item_at(platform, cobalt, -1)
  end
  here:translate_radius(ter("t_rad_platform"), ter("t_concrete"), 8.0, user:get_pos_ms(), true)
  session:remove_option("irradiator")
  session:remove_option("extract_rad_source")
  session:query_any(locale.gettext("Extraction sequence complete…  Press any key."))
end

-- Removes shock vent fields and reports whether a plutonium generator is nearby.
---@param params ComputerParams
function computer.deactivate_shock_vent(params)
  local session = params.session
  params.user:mod_moves(-30)
  local here = gapi.get_map()
  local vent = field("fd_shock_vent")
  local generator = ter("t_plut_generator")
  local has_vent = false
  local has_generator = false
  for _, dest in ipairs(here:points_in_radius(params.user:get_pos_ms(), 10)) do
    has_vent = has_vent or here:has_field_at(dest, vent)
    has_generator = has_generator or here:get_ter_at(dest) == generator
    here:remove_field_at(dest, vent)
  end
  session:print_line(locale.gettext("Initiating POWER-DIAG ver.2.34…"))
  if has_vent then
    session:print_error(locale.gettext("Short circuit detected!"))
    session:print_error(locale.gettext("Short circuit rerouted."))
    session:print_error(locale.gettext("Fuse reset."))
    session:print_error(locale.gettext("Ground re-enabled."))
  else
    session:print_line(string.format(locale.gettext("Internal power lines status: 85%% OFFLINE.  Reason: DAMAGED.")))
  end
  session:print_line(
    string.format(
      locale.gettext("External power lines status: 100%% OFFLINE.  Reason: NO EXTERNAL POWER DETECTED.")
    )
  )
  if has_generator then
    session:print_line(locale.gettext("Backup power status: STANDBY MODE."))
  else
    session:print_error(locale.gettext("Backup power status: OFFLINE.  Reason: UNKNOWN"))
  end
  session:query_any(locale.gettext("Press any key…"))
end

---@param params ComputerParams
function computer.emerg_ref_center(params)
  local session = params.session
  local user = params.user
  session:reset_terminal()
  session:print_line(locale.gettext("SEARCHING FOR NEAREST REFUGEE CENTER, PLEASE WAIT…"))
  session:redraw()

  -- Completed missions count too, so the mission is given only once.
  local mission_type = "MISSION_REACH_REFUGEE_CENTER"
  local miss = find_mission(user:get_active_missions(), { [mission_type] = true })
    or find_mission(user:get_completed_missions(), { [mission_type] = true })
  if miss == nil then
    miss = Mission.reserve_new(MissionTypeIdRaw.new(mission_type), CharacterId.new())
    miss:assign(user)
  end
  local target = miss:get_target_point()
  local omt = user:abs_pos():to_omt()

  session:print_line(
    string.format(
      --~555-0164 is a fake phone number in the US, please replace it with a number that will not cause issues in your locale if possible.
      locale.gettext(
        "\nREFUGEE CENTER FOUND!  LOCATION: %d %s\n\nIF YOU HAVE ANY FEEDBACK CONCERNING YOUR VISIT PLEASE CONTACT\nTHE DEPARTMENT OF EMERGENCY MANAGEMENT PUBLIC AFFAIRS OFFICE.\nTHE LOCAL OFFICE CAN BE REACHED BETWEEN THE HOURS OF 9AM AND\n4PM AT 555-0164.\n\nIF YOU WOULD LIKE TO SPEAK WITH SOMEONE IN PERSON OR WOULD LIKE\nTO WRITE US A LETTER PLEASE SEND IT TO…\n"
      ),
      coords.rl_dist(omt, target),
      gapi.direction_name_short(gapi.direction_from(target - omt))
    )
  )
  session:query_any(locale.gettext("Press any key to continue…"))
  session:reset_terminal()
end

for action, fn in pairs(computer) do
  game.computer_functions[action] = fn
end
