#pragma once
#include <cstring>
#include <span>
template<typename T> void memcpySpan(std::span<T> to, std::span<const T> from) { std::memcpy(to.data(), from.data(), from.size() * sizeof(T)); }
template<typename T> void memcpySpan(std::span<T> to, std::span<T> from) { std::memcpy(to.data(), from.data(), from.size() * sizeof(T)); }
