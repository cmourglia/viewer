#pragma once

#include "defines.h"

#include <glm/vec2.hpp>

constexpr f32 Pi        = 3.14159265359f;
constexpr f32 Tau       = 2.0f * Pi;
constexpr f32 ToRadians = Pi / 180.0f;
constexpr f32 ToDegrees = 180.0f / Pi;

template <typename T>
inline constexpr T Min(const T a, const T b)
{
	return a < b ? a : b;
}

template <typename T>
inline constexpr T Max(const T a, const T b)
{
	return a > b ? a : b;
}

template <typename T>
inline constexpr T Clamp(const T x, const T a, const T b)
{
	return Min(b, Max(x, a));
}

template <typename T>
inline constexpr T Saturate(const T x)
{
	return Clamp(x, T(0), T(1));
}

inline glm::vec2 Hammersley(u32 i, f32 invN)
{
	constexpr f32 tof  = 0.5f / 0x80000000U;
	u32           bits = i;

	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return {i * invN, bits * tof};
}

constexpr inline f32 Pow5(const f32 x)
{
	const f32 x2 = x * x;
	return x2 * x2 * x;
}

template <u32 COUNT>
constexpr inline f32 Pow(const f32 x)
{
	f32 res = 1.0f;

	for (u32 i = 0; i < COUNT; ++i)
	{
		res *= x;
	}

	return res;
}