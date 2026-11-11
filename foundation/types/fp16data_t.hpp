
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//	File:   fp16data_t.hpp
//	Author: Ritchie Brannan
//	Date:   18 Dec 2025
//
//	Half-precision (16-bit) floating point *storage* type.
//
//	- Represents IEEE-754 binary16 (sign + 5-bit exponent + 10-bit mantissa).
//	- Intended for GPU / file / network transport, not general-purpose arithmetic.
//	- Provides:
//      * Lossy conversions to/from float, double, int32_t, uint32_t.
//      * Saturating conversions to small integer types (8/16 bit).
//      * Classification helpers (isZero / isINF / isNAN / isReal / isFinite).
//      * A sortable key (asSortable) for use in ordered containers.
//
//	Rounding and overflow policy:
//
//	- Conversions that truncate bits use: floor(signed_value + 0.5 * ULP)
//    (biased "round half up", *not* IEEE round-to-nearest-even).
//	- Optionally clamps large finite values and +/-INF to the largest finite
//    half value (+/-65504) when clampToFinite == true, to avoid GPU artefacts
//    from non-finite values.
//
//	Comparison semantics:
//
//	- Relational operators (<, <=, >, >=) order by 'asSortable()';
//    this gives a *total order* (NaNs and +/-0 included) and is suitable for
//    sort keys, but does not follow IEEE 754 comparison rules.
//	- Equality is bitwise: +0 and -0 compare unequal; NaN payloads are visible.
//
//	This type deliberately avoids providing arithmetic operators;
//	use normal float / double or a library half type for real math.

#pragma once

#ifndef	__FP16DATA_T_INCLUDED__
#define	__FP16DATA_T_INCLUDED__

#include <cstdint>
#include <limits>
#include <type_traits>

//!	fp16data_t class definition

class fp16data_t
{
public:
	constexpr	fp16data_t() noexcept : m_bits(0u) {}
	constexpr	fp16data_t(const fp16data_t& h) noexcept = default;
	explicit	fp16data_t(const double d, const bool clampToFinite = true) noexcept { set(d, clampToFinite); }
	explicit	fp16data_t(const float f, const bool clampToFinite = true) noexcept { set(f, clampToFinite); }
	explicit	fp16data_t(const int32_t i, const bool clampToFinite = true) noexcept { set(i, clampToFinite); }
	explicit	fp16data_t(const uint32_t u, const bool clampToFinite = true) noexcept { set(u, clampToFinite); }

	~fp16data_t() noexcept = default;

	static constexpr fp16data_t	fromBits(const uint16_t bits) noexcept { fp16data_t h; h.setBits(bits); return h; }

	[[nodiscard]] constexpr bool	sign() const noexcept { return (m_bits & 0x8000u) != 0u; }
	[[nodiscard]] constexpr bool	isZero() const noexcept { return (m_bits & 0x7fffu) == 0x0000u; }
	[[nodiscard]] constexpr bool	isNAN() const noexcept { return (m_bits & 0x7fffu) > 0x7c00u; }
	[[nodiscard]] constexpr bool	isINF() const noexcept { return (m_bits & 0x7fffu) == 0x7c00u; }
	[[nodiscard]] constexpr bool	isReal() const noexcept { return (m_bits & 0x7fffu) < 0x7c00u; }
	[[nodiscard]] constexpr bool	isFinite() const noexcept { return (m_bits & 0x7c00u) != 0x7c00u; }
	[[nodiscard]] constexpr bool	isPositive() const noexcept { return (m_bits & 0x8000u) == 0u; }
	[[nodiscard]] constexpr bool	isNegative() const noexcept { return (m_bits & 0x8000u) != 0u; }

	[[nodiscard]] constexpr std::uint16_t	asSortable() const noexcept { return m_bits ^ (std::uint16_t(int16_t(m_bits) >> 15) | 0x8000u); }

	constexpr void	endianSwap() noexcept { m_bits = (m_bits << 8) | (m_bits >> 8); }

	constexpr std::uint16_t	getBits() const noexcept { return m_bits; }
	constexpr void			setBits(const std::uint16_t bits) noexcept { m_bits = bits; }
	constexpr void			set(const fp16data_t& h) noexcept { m_bits = h.m_bits; }
	inline void				set(const double d, const bool clampToFinite = true) noexcept { set(static_cast<float>(d), clampToFinite); }

	void	set(const float f, const bool clampToFinite = true) noexcept;
	void	set(const std::int32_t i, const bool clampToFinite = true) noexcept;
	void	set(const std::uint32_t u, const bool clampToFinite = true) noexcept;

	constexpr fp16data_t&	operator=(const fp16data_t& h) noexcept = default;

	inline fp16data_t&	operator=(const std::uint32_t u) noexcept { set(u); return *this; }
	inline fp16data_t&	operator=(const std::int32_t i) noexcept { set(i); return *this; }
	inline fp16data_t&	operator=(const float f) noexcept { set(f); return *this; }
	inline fp16data_t&	operator=(const double d) noexcept { set(d); return *this; }

	constexpr bool	operator==(const fp16data_t& h) const noexcept { return m_bits == h.m_bits; }
	constexpr bool	operator!=(const fp16data_t& h) const noexcept { return m_bits != h.m_bits; }
	constexpr bool	operator<(const fp16data_t& h) const noexcept { return asSortable() < h.asSortable(); }
	constexpr bool	operator<=(const fp16data_t& h) const noexcept { return asSortable() <= h.asSortable(); }
	constexpr bool	operator>(const fp16data_t& h) const noexcept { return asSortable() > h.asSortable(); }
	constexpr bool	operator>=(const fp16data_t& h) const noexcept { return asSortable() >= h.asSortable(); }

	operator std::uint32_t() const noexcept;
	operator std::int32_t() const noexcept;
	operator float() const noexcept;

	inline	operator double() const noexcept { float f = *this; return(static_cast<double>(f)); }

	inline	operator std::uint16_t() const noexcept { uint32_t u = *this; return static_cast<std::uint16_t>((u > 65535u) ? 65535u : u); }
	inline	operator std::int16_t() const noexcept { int32_t i = *this; return static_cast<std::int16_t>((i > 32767) ? 32767 : ((i < -32768) ? -32768 : i)); }
	inline	operator std::uint8_t() const noexcept { uint32_t u = *this; return static_cast<std::uint8_t>((u > 255u) ? 255u : u); }
	inline	operator std::int8_t() const noexcept { int32_t i = *this; return static_cast<std::int8_t>((i > 127) ? 127 : ((i < -128) ? -128 : i)); }

private:
	std::uint16_t	m_bits;
};

static_assert(std::numeric_limits<float>::is_iec559, "fp16data_t requires IEEE-754 float");
static_assert(std::numeric_limits<double>::is_iec559, "fp16data_t requires IEEE-754 double");
static_assert(sizeof(fp16data_t) == sizeof(std::uint16_t), "fp16data_t must be 16 bits");
static_assert(alignof(fp16data_t) == alignof(std::uint16_t), "fp16data_t alignment mismatch");
static_assert(std::is_trivially_copyable_v<fp16data_t>, "fp16data_t should be trivially copyable");

#endif	//	#ifndef	__FP16DATA_T_INCLUDED__
