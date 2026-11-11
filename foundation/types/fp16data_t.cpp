
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//	File:   fp16data_t.cpp
//	Author: Ritchie Brannan
//	Date:   18 Dec 2025
//
//  fp16data_t - raw storage for IEEE-754 binary16 values (half precision).
//  Transport/encoding type only; not a general-purpose arithmetic type.
//  See header file for rounding / clamp / comparison semantics.

#include "types/fp16data_t.hpp"

#include <cstring>

//!	fp16data_t class function bodies

void fp16data_t::set(const std::uint32_t u, const bool clampToFinite) noexcept
{
	if (u == 0)
	{	//	zero
		m_bits = 0x0000u;
	}
	else
	{	//	non-zero
		if (u > 65519)
		{	//	+INF or max finite
			m_bits = clampToFinite ? 0x7bffu : 0x7c00u;
		}
		else
		{
			std::uint32_t ui = u;
			std::uint32_t exp = 15u;
			if (ui & 0x0000ff00u)
			{
				exp += 8u;
			}
			else
			{
				ui <<= 8;
			}
			if (ui & 0x0000f000u)
			{
				exp += 4u;
			}
			else
			{
				ui <<= 4;
			}
			if (ui & 0x0000c000u)
			{
				exp += 2u;
			}
			else
			{
				ui <<= 2;
			}
			if (ui & 0x00008000u)
			{
				exp += 1u;
			}
			else
			{
				ui <<= 1;
			}
			ui += 0x00000010u;
			if (ui > 0x0000ffffu)
			{
				++exp;
				ui >>= 1;
			}
			m_bits = static_cast<std::uint16_t>((exp << 10) + ((ui >> 5) & 0x000003ffu));
		}
	}
}

void fp16data_t::set(const std::int32_t i, const bool clampToFinite) noexcept
{
	if (i == 0)
	{	//	zero
		m_bits = 0x0000u;
	}
	else if (i < 0)
	{	//	negative
		if (i < -65520)
		{	//	-INF or min finite
			m_bits = clampToFinite ? 0xfbffu : 0xfc00u;
		}
		else
		{
			std::uint32_t ui = static_cast<std::uint32_t>(0u - i);
			std::uint32_t exp = 15u;
			if (ui & 0x0000ff00u)
			{
				exp += 8u;
			}
			else
			{
				ui <<= 8;
			}
			if (ui & 0x0000f000u)
			{
				exp += 4u;
			}
			else
			{
				ui <<= 4;
			}
			if (ui & 0x0000c000u)
			{
				exp += 2u;
			}
			else
			{
				ui <<= 2;
			}
			if (ui & 0x00008000u)
			{
				exp += 1u;
			}
			else
			{
				ui <<= 1;
			}
			ui += 0x0000000fu;
			if (ui > 0x0000ffffu)
			{
				++exp;
				ui >>= 1;
			}
			m_bits = static_cast<std::uint16_t>(0x00008000u + (exp << 10) + ((ui >> 5) & 0x000003ffu));
		}
	}
	else
	{	//	positive
		if (i > 65519)
		{	//	+INF or max finite
			m_bits = clampToFinite ? 0x7bffu : 0x7c00u;
		}
		else
		{
			std::uint32_t ui = static_cast<std::uint32_t>(i);
			std::uint32_t exp = 15u;
			if (ui & 0x0000ff00u)
			{
				exp += 8u;
			}
			else
			{
				ui <<= 8;
			}
			if (ui & 0x0000f000u)
			{
				exp += 4u;
			}
			else
			{
				ui <<= 4;
			}
			if (ui & 0x0000c000u)
			{
				exp += 2u;
			}
			else
			{
				ui <<= 2;
			}
			if (ui & 0x00008000u)
			{
				exp += 1u;
			}
			else
			{
				ui <<= 1;
			}
			ui += 0x00000010u;
			if (ui > 0x0000ffffu)
			{
				++exp;
				ui >>= 1;
			}
			m_bits = static_cast<std::uint16_t>((exp << 10) + ((ui >> 5) & 0x000003ffu));
		}
	}
}

void fp16data_t::set(const float f, const bool clampToFinite) noexcept
{
	std::uint32_t ui;
	std::memcpy(&ui, &f, sizeof ui);
	std::uint32_t sign = ui & 0x80000000u;
	ui &= 0x7fffffffu;
	if (sign)
	{	//	negative
		if (ui > 0x477ff000u)
		{	//	overflow range or special
			if (ui <= 0x7f800000u)
			{	//	-INF or finite negative overflow
				m_bits = clampToFinite ? 0xfbffu : 0xfc00u;
			}
			else
			{	//	-NaN (preserve payload/sign as far as possible)
				m_bits = static_cast<std::uint16_t>(ui >> 13) | 0xfc00u;
			}
		}
		else if (ui > 0x33000000u)
		{	//	normalised or denormalised
			if (ui > 0x387fe000u)	//	normalised limit == 0x387ff000u
			{	//	normalised (some overlap into denormalised)
				m_bits = static_cast<std::uint16_t>((ui + 0xc8000fffu) >> 13) | 0x8000u;
			}
			else
			{	//	denormalised
				std::uint32_t exp = (ui & 0x7f800000u) >> 23;
				m_bits = static_cast<std::uint16_t>(((ui & 0x007fffffu) + (1u << (125u - exp)) + 0x007fffffu) >> (126u - exp)) | 0x8000u;
			}
		}
		else
		{	//	zero
			m_bits = 0x8000u;
		}
	}
	else
	{	//	positive
		if (ui > 0x477fefffu)
		{	//	overflow range or special
			if (ui <= 0x7f800000u)
			{	//	+INF or finite positive overflow
				m_bits = clampToFinite ? 0x7bffu : 0x7c00u;
			}
			else
			{	//	+NaN (preserve payload/sign as far as possible)
				m_bits = (static_cast<std::uint16_t>(ui >> 13) | 0x7c00u) & 0x7fffu;
			}
		}
		else if (ui > 0x32ffffffu)
		{	//	normalised or denormalised
			if (ui > 0x387fdfffu)	//	normalised limit == 0x387fefffu
			{	//	normalised (some overlap into denormalised)
				m_bits = static_cast<std::uint16_t>((ui + 0xc8001000u) >> 13) & 0x7fffu;
			}
			else
			{	//	denormalised
				std::uint32_t exp = ((ui & 0x7f800000u) >> 23);
				m_bits = static_cast<std::uint16_t>(((ui & 0x007fffffu) + (1u << (125u - exp)) + 0x00800000u) >> (126u - exp));
			}
		}
		else
		{	//	zero
			m_bits = 0x0000u;
		}
	}
}

fp16data_t::operator std::uint32_t() const noexcept
{
	if ((m_bits < 0x3800u) || (m_bits > 0x7c00u))
	{	//	< 0.5f or NaN
		return 0;
	}
	else if (m_bits < 0x7c00u)
	{	//	finite
		return ((static_cast<std::uint32_t>((m_bits & 0x03ffu) | 0x0400u) << (((m_bits & 0x7c00u) >> 10) - 14)) + 0x00000400u) >> 11;
	}
	else
	{	//	INF
		return 0xffffffffu;
	}
}
fp16data_t::operator std::int32_t() const noexcept
{
	if (m_bits & 0x8000u)
	{
		if ((m_bits <= 0xb800u) || (m_bits > 0xfc00u))
		{	//	>= -0.5f or NaN
			return 0;
		}
		else if (m_bits < 0xfc00u)
		{	//	finite
			return -(((static_cast<std::int32_t>((m_bits & 0x03ffu) | 0x0400u) << (((m_bits & 0x7c00u) >> 10) - 14)) + 0x000003ff) >> 11);
		}
		else
		{	//	INF
			return std::numeric_limits<std::int32_t>::min();
		}
	}
	else
	{
		if ((m_bits < 0x3800u) || (m_bits > 0x7c00u))
		{	//	< 0.5f or NaN
			return 0;
		}
		else if (m_bits < 0x7c00u)
		{	//	finite
			return ((static_cast<std::int32_t>((m_bits & 0x03ffu) | 0x0400u) << (((m_bits & 0x7c00u) >> 10) - 14)) + 0x00000400) >> 11;
		}
		else
		{	//	INF
			return std::numeric_limits<std::int32_t>::max();
		}
	}
}

fp16data_t::operator float() const noexcept
{
	std::uint32_t uif = static_cast<std::uint32_t>(m_bits & 0x8000u) << 16;
	std::uint32_t exp = m_bits & 0x7c00u;
	if (exp)
	{	//	normalised or special
		if (exp == 0x00007c00u)
		{	//	special
			uif += 0x7f800000u + (static_cast<std::uint32_t>(m_bits & 0x03ffu) << 13);
		}
		else
		{	//	normalised
			uif += 0x38000000u + (static_cast<std::uint32_t>(m_bits & 0x7fffu) << 13);
		}
	}
	else
	{	//	denormalised or zero
		std::uint32_t ui = (m_bits & 0x03ffu);
		if (ui)
		{	//	fp16data_t is denormalised and not +/- 0
			exp = 103u;
			if (ui & 0x0000ff00u)
			{
				exp += 8u;
			}
			else
			{
				ui <<= 8;
			}
			if (ui & 0x0000f000u)
			{
				exp += 4u;
			}
			else
			{
				ui <<= 4;
			}
			if (ui & 0x0000c000u)
			{
				exp += 2u;
			}
			else
			{
				ui <<= 2;
			}
			if (ui & 0x00008000u)
			{
				exp += 1u;
			}
			else
			{
				ui <<= 1;
			}
			uif += (exp << 23) + ((ui << 8) & 0x007fffffu);
		}
	}
	float f;
	std::memcpy(&f, &uif, sizeof f);
	return f;
}
