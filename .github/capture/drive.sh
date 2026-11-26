#!/usr/bin/env bash
# usage: drive.sh MODE BEFORE_BIN BEFORE_TREE AFTER_BIN AFTER_TREE OUT
mode=$1 bbin=$2 btree=$3 abin=$4 atree=$5 out=$6
W=$RUNNER_TEMP/profiles
mkdir -p "$W" "$out"
chmod +x "$bbin" "$abin"

pane() { tmux capture-pane -p -t "$1"; }
log() { echo "[$(date +%T.%N | cut -c1-12)] $*" | tee -a "$out/drive.log"; }
snap() { pane "$1" > "$out/pane-$2.txt"; }
key() { tmux send-keys -t "$1" "${@:2}"; }
lit() { tmux send-keys -t "$1" -l "$2"; }

# waitfor SESSION REGEX CAP_SECONDS
waitfor() {
  local i
  for ((i = 0; i < $3 * 4; i++)); do
    pane "$1" | grep -Eq "$2" && { log "found /$2/ after $((i / 4)) s"; return 0; }
    sleep 0.25
  done
  log "TIMEOUT /$2/"; return 1
}

# settle_re SESSION CAP REGEX: return when the matching lines have not changed for 4 s
settle_re() {
  local prev="" cur same=0 i
  for ((i = 0; i < $2; i++)); do
    cur=$(pane "$1" | grep -E "$3")
    if [ "$cur" = "$prev" ] && [ -n "$cur" ]; then same=$((same + 1)); else same=0; fi
    [ $same -ge 4 ] && { log "settled_re after $i s"; return 0; }
    prev=$cur; sleep 1
  done
  log "settle_re cap reached"; return 1
}

# start SESSION BIN TREE PROFILE LIMIT CAST
start() {
  tmux kill-session -t "$1" 2> /dev/null
  # CPU compute everywhere: a CI runner has no GPU, so a local GPU run would not match it.
  mkdir -p "$4/config"
  [ -f "$4/config/options.json" ] || echo '[{"name":"COMPUTE_ACCELERATION","value":"cpu"}]' > "$4/config/options.json"
  tmux new-session -d -s "$1" -x 100 -y 32 -c "$3" \
    "asciinema rec -q --stdin --overwrite -c 'env LD_LIBRARY_PATH=$(dirname $2) timeout --foreground -k 10s $5 $2 --userdir $4/ --configdir $4/config/' $6; sleep 2"
  log "started $1"
}
quit_clean() { key "$1" q; sleep 2; key "$1" y; sleep 3; }
stop() { pkill -TERM -f "userdir $2" ; sleep 3; tmux kill-session -t "$1" 2> /dev/null; }


# scene: a mod puts a lit fireplace and a 2x4 pile next to the avatar when the world loads
install_mod() {
  local d=$1/data/mods/capture_scene
  mkdir -p "$d"
  cat > "$d/modinfo.json" << 'EOF2'
[
  {
    "type": "MOD_INFO",
    "id": "capture_scene",
    "name": "Capture scene",
    "authors": [ "capture" ],
    "description": "Lit fireplace and firewood pile next to the avatar.",
    "category": "content",
    "lua_api_version": 2,
    "dependencies": [ "bn" ]
  }
]
EOF2
  cat > "$d/preload.lua" << 'EOF2'
local wood = ${WOOD_COUNT:-240}
-- debug.log stays empty, so the driver reads these values from the save (lua_state.json)
local storage = game.mod_storage[game.current_mod]
local function fire_field() return FieldTypeId.new("fd_fire"):int_id() end

game.add_hook("on_game_load", function()
  local av = gapi.get_avatar()
  local map = gapi.get_map()
  local c = av:get_pos_ms()
  -- monsters in the reality bubble would interrupt the wait
  for dx = -60, 60 do
    for dy = -60, 60 do
      local m = gapi.get_monster_at(TripointBubMs.new(c.x + dx, c.y + dy, c.z))
      if m then m:set_hp(0) end
    end
  end
  local fire = TripointBubMs.new(c.x + 1, c.y, c.z)
  local pile = TripointBubMs.new(c.x - 1, c.y, c.z)
  if map:has_furn_flag_at("FIRE_CONTAINER", fire) then return end
  -- the start is random: clear bare ground around the avatar so the fire has nothing else to burn
  for dx = -3, 3 do
    for dy = -3, 3 do
      local p = TripointBubMs.new(c.x + dx, c.y + dy, c.z)
      if dx ~= 0 or dy ~= 0 then
        map:set_ter_at(p, TerId.new("t_dirt"):int_id())
        map:set_furn_at(p, FurnId.new("f_null"):int_id())
        map:clear_items_at(p)
      end
    end
  end
  map:set_furn_at(fire, FurnId.new("f_fireplace"):int_id())
  map:create_item_at(fire, ItypeId.new("splinter"), -1)
  map:add_field_at(fire, fire_field(), 1, TimeDuration.from_turns(0))
  for _ = 1, wood do map:create_item_at(pile, ItypeId.new("2x4"), -1) end
  local a = av:abs_pos()
  storage.scene = (string.format("%d %d %d fire=%d,%d pile=%d,%d", a.x + pile.x - c.x, a.y + pile.y - c.y,
    a.z, fire.x - c.x, fire.y - c.y, pile.x - c.x, pile.y - c.y))
end)

game.add_hook("on_game_save", function()
  local av = gapi.get_avatar()
  local map = gapi.get_map()
  local c = av:get_pos_ms()
  local state = {}
  for dx = -2, 2 do
    for dy = -2, 2 do
      local p = TripointBubMs.new(c.x + dx, c.y + dy, c.z)
      if map:has_furn_flag_at("FIRE_CONTAINER", p) then
        state[#state + 1] = (string.format("fire=%d fuel_on_fire=%d", map:get_field_int_at(p, fire_field()),
          map:get_items_at(p):count()))
      elseif map:has_items_at(p) then
        state[#state + 1] = (string.format("items_at=%d,%d count=%d", dx, dy, map:get_items_at(p):count()))
      end
    end
  end
  storage.state = table.concat(state, " ")
end)
EOF2
  # the heredoc is quoted: substitute the wood count by hand
  sed -i "s/\${WOOD_COUNT:-240}/${WOOD_COUNT:-240}/" "$d/preload.lua"
}

# lua_val PROFILE KEY: a string the scene mod stored in the save
lua_val() {
  python3 - "$(ls "$1"/save/*/lua_state.json | head -1)" "$2" << 'EOF2'
import json, sys
e = json.load(open(sys.argv[1])).get("capture_scene", {}).get("entries", [])
vals = {e[i]["data"]: e[i + 1]["data"] for i in range(0, len(e) - 1, 2)}
print(vals.get(sys.argv[2], ""))
EOF2
}
log_state() { log "$2: scene=$(lua_val "$1" scene) state=$(lua_val "$1" state)"; }

scene_setup() {
  install_mod "$atree"; install_mod "$btree"
  local wd; wd=$(dirname "$(ls "$W"/create/save/*/*.sav | head -1)")
  log "world dir $wd"; ls -la "$wd" | tee -a "$out/drive.log"
  python3 - "$wd/mods.json" << 'EOF2'
import json, os, sys
p = sys.argv[1]
mods = json.load(open(p)) if os.path.exists(p) else ["bn"]
if "capture_scene" not in mods:
    mods.append("capture_scene")
json.dump(mods, open(p, "w"))
print("mods", mods)
EOF2
  start scene "$abin" "$atree" "$W/create" 400s "$out/scene.cast"
  load_save scene
  sleep 4; snap scene scene-loaded
  key scene S; sleep 1; key scene Y
  waitfor scene 'New Game' 120; sleep 2
  quit_clean scene
  stop scene "$W/create"
  log_state "$W/create" scene
  local line; line=$(lua_val "$W/create" scene)
  log "scene line: $line"
  line=$(cut -d' ' -f1-3 <<< "$line")
  local sav; sav=$(ls "$wd"/*.sav | head -1)
  python3 - "${sav%.sav}.zones.json" $line << 'EOF2'
import json, sys
path, x, y, z = sys.argv[1:5]
pt = [int(x), int(y), int(z)]
zone = {"name": "firewood", "type": "SOURCE_FIREWOOD", "faction": "your_followers", "invert": False,
        "enabled": True, "is_vehicle": False, "start": pt, "end": pt}
json.dump([zone], open(path, "w"))
print("zones", path, zone)
EOF2
  cat "${sav%.sav}.zones.json" | tee -a "$out/drive.log"
}

# 1. create the character once with the after binary, then give both binaries a copy of the profile
create() {
  start create "$abin" "$atree" "$W/create" 600s "$out/create.cast"
  waitfor create "New Game" 120 || snap create create-menu
  sleep 1; key create n; sleep 1; snap create create-sub
  key create o
  waitfor create 'Safe *:' 300; sleep 3; snap create created
  key create S; sleep 1; snap create saveq; key create Y
  waitfor create 'New Game' 120; snap create saved
  sleep 2
  stop create "$W/create"
  [ "$mode" = fire ] && scene_setup
  cp -a "$W/create" "$W/before"; cp -a "$W/create" "$W/after"
}

# 2. load the save: main menu, Load, world, character
load_save() {
  local s=$1
  waitfor "$s" 'New Game' 60; sleep 2
  log "$s: load keys"
  key "$s" a; sleep 1; snap "$s" "$s-load-menu"; key "$s" Enter; sleep 1; snap "$s" "$s-load-chars"; key "$s" Enter
  waitfor "$s" 'Safe *:' 300 || snap "$s" "$s-load-timeout"; log "$s: map shown"; snap "$s" "$s-loaded"
  sleep 3
}

run_wait() {
  local s=$1 bin=$2 tree=$3 p=$4 d0
  start "$s" "$bin" "$tree" "$p" 400s "$out/$s.cast"
  load_save "$s"
  lit "$s" '|'; sleep 1; key "$s" w; sleep 1; snap "$s" "$s-waitmenu"
  key "$s" c; sleep 1; lit "$s" "${WAIT_FOR:-10h}"; sleep 1; snap "$s" "$s-waitinput"
  d0=$(pane "$s" | grep -Eo 'Date *: .*day [0-9]+' | head -1)
  log "$s: wait start"; key "$s" Enter
  sleep 1; snap "$s" "$s-waiting"
  local d1 i
  local scr
  for ((i = 0; i < 1200; i++)); do
    scr=$(pane "$s")
    grep -q 'Stop waiting' <<< "$scr" && { key "$s" I; log "$s: ignore distractions"; sleep 0.5; continue; }
    grep -q 'waiting…' <<< "$scr" || break
    sleep 0.25
  done
  d1=$(pane "$s" | grep -E 'Date|Time :' | tr -s ' ' | tr '\n' ' ')
  log "$s: wait end ($d0 -> $d1)"; snap "$s" "$s-waited"
  sleep 3
  if [ "$mode" = fire ]; then
    key "$s" S; sleep 1; key "$s" Y; waitfor "$s" 'New Game' 120; sleep 1
    quit_clean "$s"
  fi
  stop "$s" "$p"
  [ "$mode" = fire ] && log_state "$p" "$s"
}

run_load() {
  local s=$1 bin=$2 tree=$3 p=$4
  start "$s" "$bin" "$tree" "$p" 200s "$out/$s.cast"
  load_save "$s"
  stop "$s" "$p"
}

create
case $mode in
  wait|fire) run_wait before "$bbin" "$btree" "$W/before"; run_wait after "$abin" "$atree" "$W/after" ;;
  load*) run_load before "$bbin" "$btree" "$W/before"; run_load after "$abin" "$atree" "$W/after" ;;
esac
ls -la "$out"
