
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   bit_ops.hpp
//  Author: Ritchie Brannan
//  Date:   6 Mar 26
//
//  Bit-twiddling utility functions

#pragma once

#ifndef BIT_OPS_HPP_INCLUDED
#define BIT_OPS_HPP_INCLUDED

#include <algorithm>    //  std::max
#include <cstdint>      //  std::int32_t, std::uint32_t, std::int64_t, std::uint64_t
#include <cstring>      //  std::memcpy

namespace bit_ops
{

//==============================================================
// definitions:
//==============================================================

//==============================================================
// bit casting
//==============================================================

inline float bit_cast_to_float(const std::int32_t i) noexcept;
inline float bit_cast_to_float(const std::uint32_t u) noexcept;
inline double bit_cast_to_double(const std::int64_t i) noexcept;
inline double bit_cast_to_double(const std::uint64_t u) noexcept;
inline std::int32_t bit_cast_to_int(const float f) noexcept;
inline std::int64_t bit_cast_to_int(const double d) noexcept;
inline std::uint32_t bit_cast_to_uint(const float f) noexcept;
inline std::uint64_t bit_cast_to_uint(const double d) noexcept;

//==============================================================
// std::int32_t bit operations
//==============================================================

constexpr std::uint32_t sign_bit(const std::int32_t i) noexcept;    //  0 or 1
constexpr std::int32_t  sign_flag(const std::int32_t u) noexcept;   //  0 or -1
constexpr std::int32_t  sign_value(const std::int32_t u) noexcept;  //  1 or -1
constexpr std::int32_t  sign_3way(const std::int32_t u) noexcept;   //  1, 0 or -1

//==============================================================
// std::uint32_t bit operations
//==============================================================

constexpr std::uint32_t smear(const std::uint32_t u) noexcept;
constexpr std::uint32_t bit_width(const std::uint32_t u) noexcept;

constexpr std::uint32_t spread_to_even_bits(const std::uint32_t u) noexcept;
constexpr std::uint32_t pack_from_even_bits(const std::uint32_t u) noexcept;

constexpr std::uint32_t spread_to_odd_bits(const std::uint32_t u) noexcept;
constexpr std::uint32_t pack_from_odd_bits(const std::uint32_t u) noexcept;

constexpr std::uint32_t hi_bit_mask(const std::uint32_t u) noexcept;
constexpr std::uint32_t lo_bit_mask(const std::uint32_t u) noexcept;

//	bit index functions return -1 if no bit is set
constexpr std::int32_t hi_bit_index(const std::uint32_t u) noexcept;
constexpr std::int32_t lo_bit_index(const std::uint32_t u) noexcept;

constexpr std::uint32_t parity(const std::uint32_t u) noexcept;		//	0 == even, 1 == odd
constexpr std::uint32_t count_set_bits(const std::uint32_t u) noexcept;

constexpr bool is_pow2(const std::uint32_t u) noexcept;
constexpr bool is_pow2_or_zero(const std::uint32_t u) noexcept;

//  pow2 rounding functions
//	note: the pow2 parameter must be a power of 2
//  if the value is already a pow2 or pow2 multiple, it is returned as it was
//	returns 1u for u == 0u, and 0u on overflow/wrap
constexpr std::uint32_t round_up_to_pow2(const std::uint32_t u) noexcept;
constexpr std::uint32_t round_up_to_pow2_or_zero(const std::uint32_t u) noexcept;
constexpr std::uint32_t round_up_to_pow2_multiple(const std::uint32_t u, const std::uint32_t pow2) noexcept;
constexpr std::uint32_t next_multiple_of_pow2(const std::uint32_t u, const std::uint32_t pow2) noexcept;

//	memory alignment in powers of 2
constexpr std::uint32_t reduce_alignment_to_pow2(const std::uint32_t u) noexcept;
constexpr std::uint32_t highest_common_alignment(const std::uint32_t a, const std::uint32_t b) noexcept;

//	reverse_* functions reverse the specified number of bits and discard any upper bits
constexpr std::uint32_t reverse_8(const std::uint32_t u) noexcept;
constexpr std::uint32_t reverse_16(const std::uint32_t u) noexcept;
constexpr std::uint32_t reverse_32(const std::uint32_t u) noexcept;

//	reverse_low_bits preserves upper bits
constexpr std::uint32_t reverse_low_bits(const std::uint32_t u, const std::uint32_t bit_count) noexcept;

//	no-overflow range helper
constexpr bool range_in_bounds(const std::uint32_t offset, const std::uint32_t length, const std::uint32_t total) noexcept;

constexpr std::uint32_t highest_common_factor(std::uint32_t a, std::uint32_t b) noexcept;

constexpr std::uint32_t gray_encode(const std::uint32_t value) noexcept;
constexpr std::uint32_t gray_decode(const std::uint32_t gray) noexcept;

//==============================================================
// std::int64_t bit operations
//==============================================================

constexpr std::uint64_t sign_bit(const std::int64_t i) noexcept;    //  0 or 1
constexpr std::int64_t  sign_flag(const std::int64_t u) noexcept;   //  0 or -1
constexpr std::int64_t  sign_value(const std::int64_t u) noexcept;  //  1 or -1
constexpr std::int64_t  sign_3way(const std::int64_t u) noexcept;   //  1, 0 or -1

//==============================================================
// std::uint64_t bit operations
//==============================================================

constexpr std::uint64_t smear(const std::uint64_t u) noexcept;
constexpr std::uint64_t bit_width(const std::uint64_t u) noexcept;

constexpr std::uint64_t spread_to_even_bits(const std::uint64_t u) noexcept;
constexpr std::uint64_t pack_from_even_bits(const std::uint64_t u) noexcept;

constexpr std::uint64_t spread_to_odd_bits(const std::uint64_t u) noexcept;
constexpr std::uint64_t pack_from_odd_bits(const std::uint64_t u) noexcept;

constexpr std::uint64_t hi_bit_mask(const std::uint64_t u) noexcept;
constexpr std::uint64_t lo_bit_mask(const std::uint64_t u) noexcept;

//	bit index functions return -1 if no bit is set
constexpr std::int32_t hi_bit_index(const std::uint64_t u) noexcept;
constexpr std::int32_t lo_bit_index(const std::uint64_t u) noexcept;

constexpr std::uint64_t parity(const std::uint64_t u) noexcept; //	0 == even, 1 == odd
constexpr std::uint64_t count_set_bits(const std::uint64_t u) noexcept;

constexpr bool is_pow2(const std::uint64_t u) noexcept;
constexpr bool is_pow2_or_zero(const std::uint64_t u) noexcept;

//  pow2 rounding functions
//	note: the pow2 parameter must be a power of 2
//  if the value is already a pow2 or pow2 multiple, it is returned as it was
//	returns 1u for u == 0u, and 0u on overflow/wrap
constexpr std::uint64_t round_up_to_pow2(const std::uint64_t u) noexcept;
constexpr std::uint64_t round_up_to_pow2_or_zero(const std::uint64_t u) noexcept;
constexpr std::uint64_t round_up_to_pow2_multiple(const std::uint64_t u, const std::uint64_t pow2) noexcept;
constexpr std::uint64_t next_multiple_of_pow2(const std::uint64_t u, const std::uint64_t pow2) noexcept;

//	memory alignment in powers of 2
constexpr std::uint64_t reduce_alignment_to_pow2(const std::uint64_t u) noexcept;
constexpr std::uint64_t highest_common_alignment(const std::uint64_t a, const std::uint64_t b) noexcept;

//	reverse_* functions reverse the specified number of bits and discard any upper bits
constexpr std::uint64_t reverse_8(const std::uint64_t u) noexcept;
constexpr std::uint64_t reverse_16(const std::uint64_t u) noexcept;
constexpr std::uint64_t reverse_32(const std::uint64_t u) noexcept;
constexpr std::uint64_t reverse_64(const std::uint64_t u) noexcept;

//	reverse_low_bits preserves upper bits
constexpr std::uint64_t reverse_low_bits(const std::uint64_t u, const std::uint64_t bit_count) noexcept;

//	no-overflow range helper
constexpr bool range_in_bounds(const std::uint64_t offset, const std::uint64_t length, const std::uint64_t total) noexcept;

constexpr std::uint64_t highest_common_factor(std::uint64_t a, std::uint64_t b) noexcept;

constexpr std::uint64_t gray_encode(const std::uint64_t value) noexcept;
constexpr std::uint64_t gray_decode(const std::uint64_t gray) noexcept;

//==============================================================
// implementations:
//==============================================================

//==============================================================
// bit casting
//==============================================================

static_assert((sizeof(float) == 4u), "bit_ops.hpp requires 32-bit float");
static_assert((sizeof(double) == 8u), "bit_ops.hpp requires 64-bit double");

inline float bit_cast_to_float(const std::int32_t i) noexcept
{
	float f{ 0 }; std::memcpy(&f, &i, sizeof(f)); return f;
}

inline float bit_cast_to_float(const std::uint32_t u) noexcept
{
	float f{ 0 }; std::memcpy(&f, &u, sizeof(f)); return f;
}

inline double bit_cast_to_double(const std::int64_t i) noexcept
{
	double d{ 0 }; std::memcpy(&d, &i, sizeof(d)); return d;
}

inline double bit_cast_to_double(const std::uint64_t u) noexcept
{
	double d{ 0 }; std::memcpy(&d, &u, sizeof(d)); return d;
}

inline std::int32_t bit_cast_to_int(const float f) noexcept
{
	std::int32_t i{ 0 }; std::memcpy(&i, &f, sizeof(i)); return i;
}

inline std::int64_t bit_cast_to_int(const double d) noexcept
{
	std::int64_t i{ 0 }; std::memcpy(&i, &d, sizeof(i)); return i;
}

inline std::uint32_t bit_cast_to_uint(const float f) noexcept
{
	std::uint32_t u{ 0 }; std::memcpy(&u, &f, sizeof(u)); return u;
}

inline std::uint64_t bit_cast_to_uint(const double d) noexcept
{
	std::uint64_t u{ 0 }; std::memcpy(&u, &d, sizeof(u)); return u;
}

//==============================================================
// std::int32_t bit operations
//==============================================================

constexpr std::uint32_t sign_bit(const std::int32_t i) noexcept
{
	return ((static_cast<std::uint32_t>(i) >> 31) & 1u);
}

constexpr std::int32_t sign_flag(const std::int32_t i) noexcept
{
	return -static_cast<std::int32_t>((static_cast<std::uint32_t>(i) >> 31) & 1u);
}

constexpr std::int32_t sign_value(const std::int32_t i) noexcept
{
	return 1 - static_cast<std::int32_t>((static_cast<std::uint32_t>(i) >> 30) & 2u);
}

constexpr std::int32_t sign_3way(const std::int32_t i) noexcept
{
	return (i > 0) - (i < 0);
}

//==============================================================
// std::uint32_t bit operations
//==============================================================

constexpr std::uint32_t smear(const std::uint32_t u) noexcept
{
	std::uint32_t s = u;
	s |= s >> 16; s |= s >> 8; s |= s >> 4; s |= s >> 2; s |= s >> 1;
	return s;
}

constexpr std::uint32_t bit_width(const std::uint32_t u) noexcept
{
	std::uint32_t w = (~((u >> 16) - 1u) >> 16) & 16u;
	w += (~((u >> (w + 8u)) - 1u) >> 8) & 8u;
	w += (~((u >> (w + 4u)) - 1u) >> 4) & 4u;
	w += (~((u >> (w + 2u)) - 1u) >> 2) & 2u;
	w += (~((u >> (w + 1u)) - 1u) >> 1) & 1u;
	return w + (u >> w);
}

constexpr std::uint32_t spread_to_even_bits(const std::uint32_t u) noexcept
{
	std::uint32_t w = u & 0x0000ffffu;
	w = ((w << 8) | w) & 0x00ff00ffu;
	w = ((w << 4) | w) & 0x0f0f0f0fu;
	w = ((w << 2) | w) & 0x33333333u;
	w = ((w << 1) | w) & 0x55555555u;
	return w;
}

constexpr std::uint32_t pack_from_even_bits(const std::uint32_t u) noexcept
{
	std::uint32_t w = u & 0x55555555u;
	w = ((w >> 1) | w) & 0x33333333u;
	w = ((w >> 2) | w) & 0x0f0f0f0fu;
	w = ((w >> 4) | w) & 0x00ff00ffu;
	w = ((w >> 8) | w) & 0x0000ffffu;
	return w;
}

constexpr std::uint32_t spread_to_odd_bits(const std::uint32_t u) noexcept
{
	return spread_to_even_bits(u) << 1;
}

constexpr std::uint32_t pack_from_odd_bits(const std::uint32_t u) noexcept
{
	return pack_from_even_bits(u >> 1);
}

constexpr std::uint32_t hi_bit_mask(const std::uint32_t u) noexcept
{
	std::uint32_t s = smear(u);
	return s ^ (s >> 1);
}

constexpr std::uint32_t lo_bit_mask(const std::uint32_t u) noexcept
{
	return u & ~(u - 1u);
}

constexpr std::int32_t hi_bit_index(const std::uint32_t u) noexcept
{
	return static_cast<std::int32_t>(bit_width(u)) - 1;
}

constexpr std::int32_t lo_bit_index(const std::uint32_t u) noexcept
{
	return hi_bit_index(lo_bit_mask(u));
}

constexpr std::uint32_t parity(const std::uint32_t u) noexcept
{
	std::uint32_t s = u;
	s ^= (s >> 16);
	s ^= (s >> 8);
	s ^= (s >> 4);
	s &= 15u;
	return (0x00006996u >> s) & 1u;
}

constexpr std::uint32_t count_set_bits(const std::uint32_t u) noexcept
{
	std::uint32_t s = u;
	s -= (s >> 1) & 0x55555555u;
	s = (s & 0x33333333u) + ((s >> 2) & 0x33333333u);
	s = (s + (s >> 4)) & 0x0f0f0f0fu;
	return (s * 0x01010101u) >> 24;
}

constexpr bool is_pow2(const std::uint32_t u) noexcept
{
	return (u != 0u) && ((u & (u - 1u)) == 0u);
}

constexpr bool is_pow2_or_zero(const std::uint32_t u) noexcept
{
	return (u & (u - 1u)) == 0u;
}

constexpr std::uint32_t round_up_to_pow2(const std::uint32_t u) noexcept
{
	return (u != 0u) ? (smear(u - 1u) + 1u) : 1u;
}

constexpr std::uint32_t round_up_to_pow2_or_zero(const std::uint32_t u) noexcept
{
	return smear(u - 1u) + 1u;
}

constexpr std::uint32_t round_up_to_pow2_multiple(const std::uint32_t u, const std::uint32_t pow2) noexcept
{
	return (u + (pow2 - 1u)) & ~(pow2 - 1u);
}

constexpr std::uint32_t next_multiple_of_pow2(const std::uint32_t u, const std::uint32_t pow2) noexcept
{
	return (u + pow2) & ~(pow2 - 1u);
}

constexpr std::uint32_t reduce_alignment_to_pow2(const std::uint32_t u) noexcept
{   //  reduce to the power of 2 alignment
	return is_pow2(u) ? u : std::max(lo_bit_mask(u), std::uint32_t{ 1 });
}

constexpr std::uint32_t highest_common_alignment(const std::uint32_t a, const std::uint32_t b) noexcept
{   //  find the highest common power of 2 alignment
	return std::max(lo_bit_mask(a | b), std::uint32_t(1u));
}

constexpr std::uint32_t reverse_8(const std::uint32_t u) noexcept
{
	std::uint32_t r = u & 0xffu;
	r = ((r << 4) & 0x0000f0u) | ((r & 0x000000f0u) >> 4);
	r = ((r << 2) & 0x0000ccu) | ((r & 0x000000ccu) >> 2);
	r = ((r << 1) & 0x0000aau) | ((r & 0x000000aau) >> 1);
	return r;
}

constexpr std::uint32_t reverse_16(const std::uint32_t u) noexcept
{
	std::uint32_t r = u & 0xffffu;
	r = ((r << 8) & 0x0000ff00u) | ((r & 0x0000ff00u) >> 8);
	r = ((r << 4) & 0x0000f0f0u) | ((r & 0x0000f0f0u) >> 4);
	r = ((r << 2) & 0x0000ccccu) | ((r & 0x0000ccccu) >> 2);
	r = ((r << 1) & 0x0000aaaau) | ((r & 0x0000aaaau) >> 1);
	return r;
}

constexpr std::uint32_t reverse_32(const std::uint32_t u) noexcept
{
	std::uint32_t r = u;
	r = (r << 16) | (r >> 16);
	r = ((r << 8) & 0xff00ff00u) | ((r & 0xff00ff00u) >> 8);
	r = ((r << 4) & 0xf0f0f0f0u) | ((r & 0xf0f0f0f0u) >> 4);
	r = ((r << 2) & 0xccccccccu) | ((r & 0xccccccccu) >> 2);
	r = ((r << 1) & 0xaaaaaaaau) | ((r & 0xaaaaaaaau) >> 1);
	return r;
}

constexpr std::uint32_t reverse_low_bits(const std::uint32_t u, const std::uint32_t bit_count) noexcept
{
	return
		(bit_count == 0u) ? u :
		(bit_count > 31u) ? reverse_32(u) :
		((u & (0xffffffffu << bit_count)) | (reverse_32(u) >> (32u - bit_count)));
}

constexpr bool range_in_bounds(const std::uint32_t offset, const std::uint32_t length, const std::uint32_t total) noexcept
{
	return (offset <= total) && (length <= (total - offset));
}

constexpr std::uint32_t highest_common_factor(std::uint32_t a, std::uint32_t b) noexcept
{
	while (b != 0u)
	{
		const std::uint32_t r = a % b;
		a = b;
		b = r;
	}
	return a;
}

constexpr std::uint32_t gray_encode(const std::uint32_t value) noexcept
{
	return value ^ (value >> 1);
}

constexpr std::uint32_t gray_decode(const std::uint32_t gray) noexcept
{
	std::uint32_t value = gray;
	std::uint32_t check = value;
	while ((check >>= 1) != 0u) value ^= check;
	return value;
}

//==============================================================
// std::int64_t bit operations
//==============================================================

constexpr std::uint64_t sign_bit(const std::int64_t i) noexcept
{
	return ((static_cast<std::uint64_t>(i) >> 63) & 1u);
}

constexpr std::int64_t sign_flag(const std::int64_t i) noexcept
{
	return -static_cast<std::int64_t>((static_cast<std::uint64_t>(i) >> 63) & 1u);
}

constexpr std::int64_t sign_value(const std::int64_t i) noexcept
{
	return 1 - static_cast<std::int64_t>((static_cast<std::uint64_t>(i) >> 62) & 2u);
}

constexpr std::int64_t sign_3way(const std::int64_t i) noexcept
{
	return (i > 0) - (i < 0);
}

//==============================================================
// std::uint64_t bit operations
//==============================================================

constexpr std::uint64_t smear(const std::uint64_t u) noexcept
{
	std::uint64_t s = u;
	s |= s >> 32; s |= s >> 16; s |= s >> 8; s |= s >> 4; s |= s >> 2; s |= s >> 1;
	return s;
}

constexpr std::uint64_t bit_width(const std::uint64_t u) noexcept
{
	std::uint64_t w = (~((u >> 32) - 1u) >> 32) & 32u;
	w += (~((u >> (w + 16u)) - 1u) >> 16) & 16u;
	w += (~((u >> (w +  8u)) - 1u) >>  8) &  8u;
	w += (~((u >> (w +  4u)) - 1u) >>  4) &  4u;
	w += (~((u >> (w +  2u)) - 1u) >>  2) &  2u;
	w += (~((u >> (w +  1u)) - 1u) >>  1) &  1u;
	return w + (u >> w);
}


constexpr std::uint64_t spread_to_even_bits(const std::uint64_t u) noexcept
{
	std::uint64_t w = u & 0x00000000ffffffffu;
	w = ((w << 16) | w) & 0x0000ffff0000ffffu;
	w = ((w << 8) | w) & 0x00ff00ff00ff00ffu;
	w = ((w << 4) | w) & 0x0f0f0f0f0f0f0f0fu;
	w = ((w << 2) | w) & 0x3333333333333333u;
	w = ((w << 1) | w) & 0x5555555555555555u;
	return w;
}

constexpr std::uint64_t pack_from_even_bits(const std::uint64_t u) noexcept
{
	std::uint64_t w = u & 0x5555555555555555u;
	w = ((w >> 1) | w) & 0x3333333333333333u;
	w = ((w >> 2) | w) & 0x0f0f0f0f0f0f0f0fu;
	w = ((w >> 4) | w) & 0x00ff00ff00ff00ffu;
	w = ((w >> 8) | w) & 0x0000ffff0000ffffu;
	w = ((w >> 16) | w) & 0x00000000ffffffffu;
	return w;
}

constexpr std::uint64_t spread_to_odd_bits(const std::uint64_t u) noexcept
{
	return spread_to_even_bits(u) << 1;
}

constexpr std::uint64_t pack_from_odd_bits(const std::uint64_t u) noexcept
{
	return pack_from_even_bits(u >> 1);
}

constexpr std::uint64_t hi_bit_mask(const std::uint64_t u) noexcept
{
	std::uint64_t s = smear(u);
	return s ^ (s >> 1);
}

constexpr std::uint64_t lo_bit_mask(const std::uint64_t u) noexcept
{
	return u & ~(u - 1u);
}

constexpr std::int32_t hi_bit_index(const std::uint64_t u) noexcept
{
	return static_cast<std::int32_t>(bit_width(u)) - 1;
}

constexpr std::int32_t lo_bit_index(const std::uint64_t u) noexcept
{
	return hi_bit_index(lo_bit_mask(u));
}

constexpr std::uint64_t parity(const std::uint64_t u) noexcept
{
	std::uint64_t s = u;
	s ^= (s >> 32);
	s ^= (s >> 16);
	s ^= (s >> 8);
	s ^= (s >> 4);
	s &= 15u;
	return (0x00006996u >> s) & 1u;
}

constexpr std::uint64_t count_set_bits(const std::uint64_t u) noexcept
{
	std::uint64_t s = u;
	s -= (s >> 1) & 0x5555555555555555u;
	s = (s & 0x3333333333333333u) + ((s >> 2) & 0x3333333333333333u);
	s = (s + (s >> 4)) & 0x0f0f0f0f0f0f0f0fu;
	return (s * 0x0101010101010101u) >> 56;
}

constexpr bool is_pow2(const std::uint64_t u) noexcept
{
	return (u != 0u) && ((u & (u - 1u)) == 0u);
}

constexpr bool is_pow2_or_zero(const std::uint64_t u) noexcept
{
	return (u & (u - 1u)) == 0u;
}

constexpr std::uint64_t round_up_to_pow2(const std::uint64_t u) noexcept
{
	return (u != 0u) ? (smear(u - 1u) + 1u) : 1u;
}

constexpr std::uint64_t round_up_to_pow2_or_zero(const std::uint64_t u) noexcept
{
	return smear(u - 1u) + 1u;
}

constexpr std::uint64_t round_up_to_pow2_multiple(const std::uint64_t u, const std::uint64_t pow2) noexcept
{
	return (u + (pow2 - 1u)) & ~(pow2 - 1u);
}

constexpr std::uint64_t next_multiple_of_pow2(const std::uint64_t u, const std::uint64_t pow2) noexcept
{
	return (u + pow2) & ~(pow2 - 1u);
}

constexpr std::uint64_t reduce_alignment_to_pow2(const std::uint64_t u) noexcept
{   //  reduce to the power of 2 alignment
	return is_pow2(u) ? u : std::max(lo_bit_mask(u), std::uint64_t{ 1 });
}

constexpr std::uint64_t highest_common_alignment(const std::uint64_t a, const std::uint64_t b) noexcept
{   //  find the highest common power of 2 alignment
	return std::max(lo_bit_mask(a | b), std::uint64_t(1u));
}

constexpr std::uint64_t reverse_8(const std::uint64_t u) noexcept
{
	std::uint64_t r = u & 0xffu;
	r = ((r << 4) & 0x0000f0u) | ((r & 0x000000f0u) >> 4);
	r = ((r << 2) & 0x0000ccu) | ((r & 0x000000ccu) >> 2);
	r = ((r << 1) & 0x0000aau) | ((r & 0x000000aau) >> 1);
	return r;
}

constexpr std::uint64_t reverse_16(const std::uint64_t u) noexcept
{
	std::uint64_t r = u & 0xffffu;
	r = ((r << 8) & 0x0000ff00u) | ((r & 0x0000ff00u) >> 8);
	r = ((r << 4) & 0x0000f0f0u) | ((r & 0x0000f0f0u) >> 4);
	r = ((r << 2) & 0x0000ccccu) | ((r & 0x0000ccccu) >> 2);
	r = ((r << 1) & 0x0000aaaau) | ((r & 0x0000aaaau) >> 1);
	return r;
}

constexpr std::uint64_t reverse_32(const std::uint64_t u) noexcept
{
	std::uint64_t r = u & 0xffffffffu;
	r = (r << 16) | (r >> 16);
	r = ((r << 8) & 0xff00ff00u) | ((r & 0xff00ff00u) >> 8);
	r = ((r << 4) & 0xf0f0f0f0u) | ((r & 0xf0f0f0f0u) >> 4);
	r = ((r << 2) & 0xccccccccu) | ((r & 0xccccccccu) >> 2);
	r = ((r << 1) & 0xaaaaaaaau) | ((r & 0xaaaaaaaau) >> 1);
	return r;
}

constexpr std::uint64_t reverse_64(const std::uint64_t u) noexcept
{
	std::uint64_t r = u;
	r = (r << 32) | (r >> 32);
	r = ((r << 16) & 0xffff0000ffff0000u) | ((r & 0xffff0000ffff0000u) >> 16);
	r = ((r <<  8) & 0xff00ff00ff00ff00u) | ((r & 0xff00ff00ff00ff00u) >>  8);
	r = ((r <<  4) & 0xf0f0f0f0f0f0f0f0u) | ((r & 0xf0f0f0f0f0f0f0f0u) >>  4);
	r = ((r <<  2) & 0xccccccccccccccccu) | ((r & 0xccccccccccccccccu) >>  2);
	r = ((r <<  1) & 0xaaaaaaaaaaaaaaaau) | ((r & 0xaaaaaaaaaaaaaaaau) >>  1);
	return r;
}

constexpr std::uint64_t reverse_low_bits(const std::uint64_t u, const std::uint64_t bit_count) noexcept
{
	return
		(bit_count == 0u) ? u :
		(bit_count > 63u) ? reverse_64(u) :
		((u & (0xffffffffffffffffu << bit_count)) | (reverse_64(u) >> (64u - bit_count)));
}

constexpr bool range_in_bounds(const std::uint64_t offset, const std::uint64_t length, const std::uint64_t total) noexcept
{
	return (offset <= total) && (length <= (total - offset));
}

constexpr std::uint64_t highest_common_factor(std::uint64_t a, std::uint64_t b) noexcept
{
	while (b != 0u)
	{
		const std::uint64_t r = a % b;
		a = b;
		b = r;
	}
	return a;
}

constexpr std::uint64_t gray_encode(const std::uint64_t value) noexcept
{
	return value ^ (value >> 1);
}

constexpr std::uint64_t gray_decode(const std::uint64_t gray) noexcept
{
	std::uint64_t value = gray;
	std::uint64_t check = value;
	while ((check >>= 1) != 0u) value ^= check;
	return value;
}

}   //  namespace bit_ops

#endif  //  #ifndef BIT_OPS_HPP_INCLUDED

