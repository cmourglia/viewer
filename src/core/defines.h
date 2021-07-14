#pragma once

#include <stdint.h>

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using f32 = float;
using f64 = double;

// Macro helpers
#define global_variable static
#define local_variable static

// https://www.gingerbill.org/article/2015/08/19/defer-in-cpp
template <typename Fn>
struct PrivDefer
{
	Fn fn;

	PrivDefer(Fn fn)
	    : fn(fn)
	{
	}
	~PrivDefer()
	{
		fn();
	}
};

template <typename Fn>
PrivDefer<Fn> DeferFunc(Fn fn)
{
	return PrivDefer<Fn>(fn);
}

#define DEFER_1(x, y) x##y
#define DEFER_2(x, y) DEFER_1(x, y)
#define DEFER_3(x) DEFER_2(x, __COUNTER__)
#define defer(code) auto DEFER_3(_defer_) = DeferFunc([&]() { code; })