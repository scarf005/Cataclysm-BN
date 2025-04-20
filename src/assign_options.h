#pragma once

/// Determines how strict the JSON parser should be when assigning values
enum class strict_level {
    NONE = 0,
    STRICT = 1,
    PEDANTIC = 2,
};
