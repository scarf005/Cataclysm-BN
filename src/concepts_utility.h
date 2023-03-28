#pragma once
#ifndef CATA_SRC_CONCEPTS_UTILITY_DEF_H
#define CATA_SRC_CONCEPTS_UTILITY_DEF_H
#include <cmath>

template<typename T>
concept Arithmatic = std::is_arithmetic_v<T>;

template <Arithmatic A, Arithmatic B>
using WiderType = std::common_type_t<A, B>;

#endif // CATA_SRC_CONCEPTS_UTILITY_DEF_H
