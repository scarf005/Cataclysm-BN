#pragma once

namespace loading
{

/// Pumps window events at most once per frame while loading data.
/// Keeps the window responsive without paying for a full event pump after every loaded object.
auto pump_events() -> void;

} // namespace loading
