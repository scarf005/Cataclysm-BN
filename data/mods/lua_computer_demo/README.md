# Lua computer demo

This debug mod adds a terminal with one Lua action and one Lua failure. See
`preload.lua` for the code and `mapgen.json` for the terminal.

- `demo_status` in `game.computer_functions` shows the alert count. On a yes, it adds an
  "Open vault" option and removes itself.
- `demo_lockout` in `game.computer_failure_functions` runs when a hack fails. It raises the login
  security by 1 and adds the built-in `alarm` failure.

## Try it

1. Enable **Lua Computer Demo** in a test world.
2. Open the debug menu, select **Map**, then **Spawn nested mapgen**, and pick
   `lua_computer_demo_terminal`. Pick a tile next to the player.
3. Use the terminal. The login has security 1, so you must hack it.
4. Fail a hack. The terminal shows `LOCKOUT: security level raised.` Each later failed hack runs
   `demo_lockout` or `alarm` at random.
5. Wait 45 minutes. A failed hack blocks the login for that time.
6. Hack the terminal and select **Status report**. Answer yes. The menu now shows **Open vault**
   instead of **Status report**.
