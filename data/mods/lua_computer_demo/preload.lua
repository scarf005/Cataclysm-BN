-- Shows the alert count. On a yes, it replaces itself with an "Open vault" option that
-- runs the built-in `unlock` action at security level 2.
---@param params ComputerParams
game.computer_functions.demo_status = function(params)
  local session = params.session
  session:reset_terminal()
  session:print_line(string.format(locale.gettext("Alerts: %d."), session:get_alerts()))
  if session:query_bool(locale.gettext("Add the vault option?")) then
    session:add_option(locale.gettext("Open vault"), "unlock", 2)
    session:remove_option("demo_status")
  end
  session:query_any()
end

-- Runs when a hack attempt fails. It raises the login security by 1 and adds the
-- built-in `alarm` failure, so later failed hacks can also sound the alarm.
---@param params ComputerParams
game.computer_failure_functions.demo_lockout = function(params)
  local session = params.session
  session:print_error(locale.gettext("LOCKOUT: security level raised."))
  session:set_security(session:get_security() + 1)
  session:add_failure("alarm")
  session:query_any()
end
