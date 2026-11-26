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
  local s=$1 bin=$2 tree=$3 p=$4
  start "$s" "$bin" "$tree" "$p" 400s "$out/$s.cast"
  load_save "$s"
  lit "$s" '|'; sleep 1; key "$s" w; sleep 1; snap "$s" "$s-waitmenu"
  key "$s" c; sleep 1; lit "$s" "${WAIT_FOR:-2d}"; sleep 1; snap "$s" "$s-waitinput"
  log "$s: wait start"; key "$s" Enter
  sleep 2; snap "$s" "$s-waiting"
  settle_re "$s" 300 'Date|Time'; log "$s: wait end"; snap "$s" "$s-waited"
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
  wait) run_wait before "$bbin" "$btree" "$W/before"; run_wait after "$abin" "$atree" "$W/after" ;;
  load*) run_load before "$bbin" "$btree" "$W/before"; run_load after "$abin" "$atree" "$W/after" ;;
esac
ls -la "$out"
