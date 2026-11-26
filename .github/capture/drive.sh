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
  tmux new-session -d -s "$1" -x 100 -y 32 -c "$3" \
    "asciinema rec -q --stdin --overwrite -c 'env LD_LIBRARY_PATH=$(dirname $2) timeout --foreground -k 10s $5 $2 --userdir $4/ --configdir $4/config/' $6; sleep 2"
  log "started $1"
}
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
local fire_field = FieldTypeId.new("fd_fire"):int_id()

local function usable(map, p)
  return map:has_ter_flag_at("FLAT", p) and not map:has_ter_flag_at("NO_FLOOR", p)
end

local function pick(map, c, outside)
  local picked = {}
  for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } }) do
    local p = TripointBubMs.new(c.x + d[1], c.y + d[2], c.z)
    if usable(map, p) and (not outside or map:is_outside(p)) then picked[#picked + 1] = p end
  end
  return picked
end

game.add_hook("on_game_load", function()
  local av = gapi.get_avatar()
  local map = gapi.get_map()
  local c = av:get_pos_ms()
  local spots = pick(map, c, true)
  if #spots < 2 then spots = pick(map, c, false) end
  if #spots < 2 then
    gdebug.log_warn("CAPTURE_SCENE_FAIL no free tiles")
    return
  end
  local fire, pile = spots[1], spots[2]
  if map:has_furn_flag_at("FIRE_CONTAINER", fire) then return end
  map:set_furn_at(fire, FurnId.new("f_fireplace"):int_id())
  map:create_item_at(fire, ItypeId.new("splinter"), -1)
  map:add_field_at(fire, fire_field, 1, TimeDuration.from_turns(0))
  for _ = 1, wood do map:create_item_at(pile, ItypeId.new("2x4"), -1) end
  local a = av:abs_pos()
  gdebug.log_warn(string.format("CAPTURE_SCENE %d %d %d fire=%d,%d pile=%d,%d", a.x + pile.x - c.x, a.y + pile.y - c.y,
    a.z, fire.x - c.x, fire.y - c.y, pile.x - c.x, pile.y - c.y))
end)

game.add_hook("on_game_save", function()
  local av = gapi.get_avatar()
  local map = gapi.get_map()
  local c = av:get_pos_ms()
  for dx = -2, 2 do
    for dy = -2, 2 do
      local p = TripointBubMs.new(c.x + dx, c.y + dy, c.z)
      if map:has_furn_flag_at("FIRE_CONTAINER", p) then
        gdebug.log_warn(string.format("CAPTURE_STATE fire=%d fuel_on_fire=%d", map:get_field_int_at(p, fire_field),
          map:get_items_at(p):count()))
      elseif map:has_items_at(p) then
        gdebug.log_warn(string.format("CAPTURE_STATE items_at=%d,%d count=%d", dx, dy, map:get_items_at(p):count()))
      end
    end
  end
end)
EOF2
  # the heredoc is quoted: substitute the wood count by hand
  sed -i "s/\${WOOD_COUNT:-240}/${WOOD_COUNT:-240}/" "$d/preload.lua"
}

log_state() {
  grep -E 'CAPTURE_(SCENE|STATE)' "$1/config/debug.log" 2> /dev/null | tail -n 6 | while read -r l; do log "$2: $l"; done
  cp "$1/config/debug.log" "$out/debug-$2.log" 2> /dev/null
}

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
  log_state "$W/create" scene
  stop scene "$W/create"
  local line; line=$(grep 'CAPTURE_SCENE ' "$W/create/config/debug.log" | tail -n 1)
  log "scene line: $line"
  line=$(sed -E 's/.*CAPTURE_SCENE ([-0-9]+) ([-0-9]+) ([-0-9]+).*/\1 \2 \3/' <<< "$line")
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
  waitfor "$s" 'Safe *:' 300; log "$s: map shown"; snap "$s" "$s-loaded"
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
  waitfor "$s" 'Stop waiting' 30 && { key "$s" I; sleep 1; log "$s: ignore distractions"; }
  snap "$s" "$s-waiting"
  sleep 2
  local d1 i
  for ((i = 0; i < 1200; i++)); do
    pane "$s" | grep -q 'waiting' || break
    sleep 0.25
  done
  d1=$(pane "$s" | grep -E 'Date|Time :' | tr -s ' ' | tr '\n' ' ')
  log "$s: wait end ($d0 -> $d1)"; snap "$s" "$s-waited"
  sleep 3
  stop "$s" "$p"
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
