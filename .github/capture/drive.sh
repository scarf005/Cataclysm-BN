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

# settle SESSION CAP_SECONDS: return when the screen has not changed for 4 s
settle() {
  local prev="" cur same=0 i
  for ((i = 0; i < $2; i++)); do
    cur=$(pane "$1")
    if [ "$cur" = "$prev" ]; then same=$((same + 1)); else same=0; fi
    [ $same -ge 4 ] && { log "settled after $i s"; return 0; }
    prev=$cur; sleep 1
  done
  log "settle cap reached"; return 1
}

# start SESSION BIN TREE PROFILE LIMIT CAST
start() {
  tmux kill-session -t "$1" 2> /dev/null
  tmux new-session -d -s "$1" -x 100 -y 32 -c "$3" \
    "asciinema rec -q --overwrite -c 'timeout -k 10s $5 $2 --userdir $4/ --configdir $4/config/' $6; sleep 2"
  log "started $1"
}
stop() { pkill -TERM -f "userdir $2" ; sleep 3; tmux kill-session -t "$1" 2> /dev/null; }

# 1. create the character once with the after binary, then give both binaries a copy of the profile
create() {
  start create "$abin" "$atree" "$W/create" 600s "$out/create.cast"
  waitfor create 'New Game' 300 || snap create create-menu
  sleep 1; key create n; sleep 1; snap create create-sub
  key create o
  sleep 5; settle create 300; snap create created
  sleep 1; key create S; sleep 1; snap create saveq; key create y
  sleep 5; snap create saved
  stop create "$W/create"
  cp -r "$W/create" "$W/before"; cp -r "$W/create" "$W/after"
}

# 2. load the save and wait: record the whole run
run_wait() {
  local s=$1 bin=$2 tree=$3 p=$4
  start "$s" "$bin" "$tree" "$p" 400s "$out/$s.cast"
  waitfor "$s" 'New Game' 300; sleep 1
  key "$s" a; sleep 1; snap "$s" "$s-load-menu"; key "$s" Enter
  settle "$s" 300; snap "$s" "$s-loaded"
  sleep 2
  lit "$s" '|'; sleep 1; snap "$s" "$s-waitmenu"
  key "$s" c; sleep 1; lit "$s" "${WAIT_FOR:-2d}"; sleep 1; snap "$s" "$s-waitinput"; key "$s" Enter
  sleep 2; snap "$s" "$s-waiting"
  settle "$s" 300; snap "$s" "$s-waited"
  sleep 2
  stop "$s" "$p"
}

run_load() {
  local s=$1 bin=$2 tree=$3 p=$4
  start "$s" "$bin" "$tree" "$p" 200s "$out/$s.cast"
  waitfor "$s" 'New Game' 300; snap "$s" "$s-menu"; sleep 1
  key "$s" a; sleep 1; snap "$s" "$s-load-menu"; key "$s" Enter
  settle "$s" 300; snap "$s" "$s-loaded"
  sleep 2
  stop "$s" "$p"
}

create
case $mode in
  wait) run_wait before "$bbin" "$btree" "$W/before"; run_wait after "$abin" "$atree" "$W/after" ;;
  load*) run_load before "$bbin" "$btree" "$W/before"; run_load after "$abin" "$atree" "$W/after" ;;
esac
ls -la "$out"
