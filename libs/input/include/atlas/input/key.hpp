/**
 * @file
 * @brief Portable semantic keyboard identifiers, independent of native SDK values.
 */
#pragma once
#include <cstdint>

namespace atlas::input
{
/// @brief ASCII digits/uppercase letters also use their ASCII value; no text/IME interpretation.
enum class key : std::uint16_t
{
    unknown = 0,  ///< Unsupported key.
    tab = 9,      ///< Tab semantic identifier.
    enter = 13,   ///< Enter semantic identifier.
    escape = 27,  ///< Escape semantic identifier.
    space = 32,   ///< Space semantic identifier.
    digit_0 = 48, ///< ASCII semantic identifier.
    digit_1 = 49, ///< ASCII semantic identifier.
    digit_2 = 50, ///< ASCII semantic identifier.
    digit_3 = 51, ///< ASCII semantic identifier.
    digit_4 = 52, ///< ASCII semantic identifier.
    digit_5 = 53, ///< ASCII semantic identifier.
    digit_6 = 54, ///< ASCII semantic identifier.
    digit_7 = 55, ///< ASCII semantic identifier.
    digit_8 = 56, ///< ASCII semantic identifier.
    digit_9 = 57, ///< ASCII semantic identifier.
    a = 65,       ///< ASCII semantic identifier.
    b = 66,       ///< ASCII semantic identifier.
    c = 67,       ///< ASCII semantic identifier.
    d = 68,       ///< ASCII semantic identifier.
    e = 69,       ///< ASCII semantic identifier.
    f = 70,       ///< ASCII semantic identifier.
    g = 71,       ///< ASCII semantic identifier.
    h = 72,       ///< ASCII semantic identifier.
    i = 73,       ///< ASCII semantic identifier.
    j = 74,       ///< ASCII semantic identifier.
    k = 75,       ///< ASCII semantic identifier.
    l = 76,       ///< ASCII semantic identifier.
    m = 77,       ///< ASCII semantic identifier.
    n = 78,       ///< ASCII semantic identifier.
    o = 79,       ///< ASCII semantic identifier.
    p = 80,       ///< ASCII semantic identifier.
    q = 81,       ///< ASCII semantic identifier.
    r = 82,       ///< ASCII semantic identifier.
    s = 83,       ///< ASCII semantic identifier.
    t = 84,       ///< ASCII semantic identifier.
    u = 85,       ///< ASCII semantic identifier.
    v = 86,       ///< ASCII semantic identifier.
    w = 87,       ///< ASCII semantic identifier.
    x = 88,       ///< ASCII semantic identifier.
    y = 89,       ///< ASCII semantic identifier.
    z = 90,       ///< ASCII semantic identifier.
    left = 256,   ///< Left navigation.
    right,        ///< Right navigation.
    up,           ///< Up navigation.
    down,         ///< Down navigation.
    home,         ///< Home navigation.
    end,          ///< End navigation.
    backspace,    ///< Backward deletion intent.
    erase         ///< Forward deletion intent.
};

} // namespace atlas::input
