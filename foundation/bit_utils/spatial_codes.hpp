
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   spatial_codes.hpp
//  Author: Ritchie Brannan
//  Date:   6 Mar 26
//
//  Bit-twiddling spatial code functions (Gray, Morton and Hilbert)
//
//  Gray    : binary - reflected Gray encoding
//  Morton  : Z - order interleaving
//  Hilbert : locality - preserving space - filling curve transform

#pragma once

#ifndef SPATIAL_CODES_HPP_INCLUDED
#define SPATIAL_CODES_HPP_INCLUDED

#include <cstdint>

namespace spatial_codes
{

//==============================================================
// definitions:
//==============================================================

//==============================================================
// grey code
//==============================================================

constexpr std::uint32_t gray_encode(const std::uint32_t value) noexcept;
constexpr std::uint32_t gray_decode(const std::uint32_t gray) noexcept;

//==============================================================
// morton 2d, 5 bits per channel
//==============================================================

constexpr std::uint32_t morton_encode_2d_5bit(std::uint32_t x, std::uint32_t y) noexcept;
constexpr void morton_decode_2d_5bit(const std::uint32_t morton, std::uint32_t& x, std::uint32_t& y) noexcept;
constexpr std::uint32_t morton_decode_x_2d_5bit(const std::uint32_t morton) noexcept;
constexpr std::uint32_t morton_decode_y_2d_5bit(const std::uint32_t morton) noexcept;

//==============================================================
// morton 2d, 16 bits per channel
//==============================================================

constexpr std::uint32_t morton_encode_2d_16bit(std::uint32_t x, std::uint32_t y) noexcept;
constexpr void morton_decode_2d_16bit(const std::uint32_t morton, std::uint32_t& x, std::uint32_t& y) noexcept;
constexpr std::uint32_t morton_decode_x_2d_16bit(const std::uint32_t morton) noexcept;
constexpr std::uint32_t morton_decode_y_2d_16bit(const std::uint32_t morton) noexcept;

//==============================================================
// morton 3D, 5 bits per channel
//==============================================================

constexpr std::uint32_t morton_encode_3d_5bit(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept;
constexpr void morton_decode_3d_5bit(const std::uint32_t morton, std::uint32_t& x, std::uint32_t& y, std::uint32_t& z) noexcept;
constexpr std::uint32_t morton_decode_x_3d_5bit(const std::uint32_t morton) noexcept;
constexpr std::uint32_t morton_decode_y_3d_5bit(const std::uint32_t morton) noexcept;
constexpr std::uint32_t morton_decode_z_3d_5bit(const std::uint32_t morton) noexcept;

//==============================================================
// morton 3D, 10 bits per channel
//==============================================================

constexpr std::uint32_t morton_encode_3d_10bit(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept;
constexpr void morton_decode_3d_10bit(const std::uint32_t morton, std::uint32_t& x, std::uint32_t& y, std::uint32_t& z) noexcept;
constexpr std::uint32_t morton_decode_x_3d_10bit(const std::uint32_t morton) noexcept;
constexpr std::uint32_t morton_decode_y_3d_10bit(const std::uint32_t morton) noexcept;
constexpr std::uint32_t morton_decode_z_3d_10bit(const std::uint32_t morton) noexcept;

//==============================================================
// hilbert transforms 2d
// note: levels: 1u..16u (auto infers levels)
//==============================================================

constexpr std::uint32_t morton_to_hilbert_2d(const std::uint32_t morton, const std::uint32_t levels = 16u) noexcept;
constexpr std::uint32_t morton_to_hilbert_2d_auto(const std::uint32_t morton) noexcept;
constexpr std::uint32_t hilbert_to_morton_2d(const std::uint32_t hilbert, const std::uint32_t levels = 16u) noexcept;
constexpr std::uint32_t hilbert_to_morton_2d_auto(const std::uint32_t hilbert) noexcept;

//==============================================================
// hilbert transforms 3D
// note: levels: 1u..10u (auto infers levels)
//==============================================================

constexpr std::uint32_t morton_to_hilbert_3d(const std::uint32_t morton, const std::uint32_t levels = 10u) noexcept;
constexpr std::uint32_t morton_to_hilbert_3d_auto(const std::uint32_t morton) noexcept;
constexpr std::uint32_t hilbert_to_morton_3d(const std::uint32_t hilbert, const std::uint32_t levels = 10u) noexcept;
constexpr std::uint32_t hilbert_to_morton_3d_auto(const std::uint32_t hilbert) noexcept;

//==============================================================
// implementations:
//==============================================================

//==============================================================
// grey code
//==============================================================

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
// morton 2d, 5 bits per channel
//==============================================================

constexpr std::uint32_t morton_encode_2d_5bit(std::uint32_t x, std::uint32_t y) noexcept
{
	x &= 0x0000001fu;
	y &= 0x0000001fu;
	x *= 0x01041041u;
	y *= 0x01041041u;
	x &= 0x10204081u;
	y &= 0x10204081u;
	x *= 0x00108421u;
	y *= 0x00108421u;
	x &= 0x15500000u;
	y &= 0x15500000u;
	return (x >> 20) | (y >> 19);
}

constexpr void morton_decode_2d_5bit(const std::uint32_t morton, std::uint32_t& x, std::uint32_t& y) noexcept
{
	x = morton;
	y = x >> 1;
	x &= 0x00000155u;
	y &= 0x00000155u;
	x |= x >> 1;
	y |= y >> 1;
	x &= 0x00000133u;
	y &= 0x00000133u;
	x |= x >> 2;
	y |= y >> 2;
	x &= 0x0000010fu;
	y &= 0x0000010fu;
	x |= x >> 4;
	y |= y >> 4;
	x &= 0x0000001fu;
	y &= 0x0000001fu;
}

constexpr std::uint32_t morton_decode_x_2d_5bit(const std::uint32_t morton) noexcept
{
	std::uint32_t x = morton;
	x &= 0x00000155u;
	x |= x >> 1;
	x &= 0x00000133u;
	x |= x >> 2;
	x &= 0x0000010fu;
	return (x & 0x0000000fu) | (x >> 4);
}

constexpr std::uint32_t morton_decode_y_2d_5bit(const std::uint32_t morton) noexcept
{
	return morton_decode_x_2d_5bit(morton >> 1);
}

//==============================================================
// morton 2d, 16 bits per channel
//==============================================================

constexpr std::uint32_t morton_encode_2d_16bit(std::uint32_t x, std::uint32_t y) noexcept
{
	x &= 0x0000ffffu;
	y &= 0x0000ffffu;
	x |= x << 8;
	y |= y << 8;
	x &= 0x00ff00ffu;
	y &= 0x00ff00ffu;
	x |= x << 4;
	y |= y << 4;
	x &= 0x0f0f0f0fu;
	y &= 0x0f0f0f0fu;
	x |= x << 2;
	y |= y << 2;
	x &= 0x33333333u;
	y &= 0x33333333u;
	x |= x << 1;
	y |= y << 1;
	x &= 0x55555555u;
	y &= 0x55555555u;
	return x | (y << 1);
}

constexpr void morton_decode_2d_16bit(const std::uint32_t morton, std::uint32_t& x, std::uint32_t& y) noexcept
{
	x = morton;
	y = x >> 1;
	x &= 0x55555555u;
	y &= 0x55555555u;
	x |= x >> 1;
	y |= y >> 1;
	x &= 0x33333333u;
	y &= 0x33333333u;
	x |= x >> 2;
	y |= y >> 2;
	x &= 0x0f0f0f0fu;
	y &= 0x0f0f0f0fu;
	x |= x >> 4;
	y |= y >> 4;
	x &= 0x00ff00ffu;
	y &= 0x00ff00ffu;
	x |= x >> 8;
	y |= y >> 8;
	x &= 0x0000ffffu;
	y &= 0x0000ffffu;
}

constexpr std::uint32_t morton_decode_x_2d_16bit(const std::uint32_t morton) noexcept
{
	std::uint32_t x = morton;
	x &= 0x55555555u;
	x |= x >> 1;
	x &= 0x33333333u;
	x |= x >> 2;
	x &= 0x0f0f0f0fu;
	x |= x >> 4;
	x &= 0x00ff00ffu;
	x |= x >> 8;
	x &= 0x0000ffffu;
	return x;
}

constexpr std::uint32_t morton_decode_y_2d_16bit(const std::uint32_t morton) noexcept
{
	return morton_decode_x_2d_16bit(morton >> 1);
}

//==============================================================
// morton 3D, 5 bits per channel
//==============================================================

constexpr std::uint32_t morton_encode_3d_5bit(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
{
	x &= 0x0000001fu;
	y &= 0x0000001fu;
	z &= 0x0000001fu;
	x *= 0x01041041u;
	y *= 0x01041041u;
	z *= 0x01041041u;
	x &= 0x10204081u;
	y &= 0x10204081u;
	z &= 0x10204081u;
	x *= 0x00011111u;
	y *= 0x00011111u;
	z *= 0x00011111u;
	x &= 0x12490000u;
	y &= 0x12490000u;
	z &= 0x12490000u;
	return (x >> 16) | (y >> 15) | (z >> 14);
}

constexpr void morton_decode_3d_5bit(const std::uint32_t morton, std::uint32_t& x, std::uint32_t& y, std::uint32_t& z) noexcept
{
	x = morton;
	y = (x >> 1);
	z = (y >> 1);
	x &= 0x00001249u;
	y &= 0x00001249u;
	z &= 0x00001249u;
	x |= x >> 2;
	y |= y >> 2;
	z |= z >> 2;
	x &= 0x000010c3u;
	y &= 0x000010c3u;
	z &= 0x000010c3u;
	x |= x >> 4;
	y |= y >> 4;
	z |= z >> 4;
	x &= 0x0000100fu;
	y &= 0x0000100fu;
	z &= 0x0000100fu;
	x |= x >> 8;
	y |= y >> 8;
	z |= z >> 8;
	x &= 0x0000001fu;
	y &= 0x0000001fu;
	z &= 0x0000001fu;
}

constexpr std::uint32_t morton_decode_x_3d_5bit(const std::uint32_t morton) noexcept
{
	std::uint32_t x = morton;
	x &= 0x00001249u;
	x |= x >> 2;
	x &= 0x000010c3u;
	x |= x >> 4;
	x &= 0x0000100fu;
	return (x & 0x0000000f) | (x >> 8);
}

constexpr std::uint32_t morton_decode_y_3d_5bit(const std::uint32_t morton) noexcept
{
	return morton_decode_x_3d_5bit(morton >> 1);
}

constexpr std::uint32_t morton_decode_z_3d_5bit(const std::uint32_t morton) noexcept
{
	return morton_decode_x_3d_5bit(morton >> 2);
}

//==============================================================
// morton 3D, 10 bits per channel
//==============================================================

constexpr std::uint32_t morton_encode_3d_10bit(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
{
	x &= 0x000003ffu;
	y &= 0x000003ffu;
	z &= 0x000003ffu;
	x |= x << 16;
	y |= y << 16;
	z |= z << 16;
	x &= 0x030000ffu;
	y &= 0x030000ffu;
	z &= 0x030000ffu;
	x |= x << 8;
	y |= y << 8;
	z |= z << 8;
	x &= 0x0300f00fu;
	y &= 0x0300f00fu;
	z &= 0x0300f00fu;
	x |= x << 4;
	y |= y << 4;
	z |= z << 4;
	x &= 0x030c30c3u;
	y &= 0x030c30c3u;
	z &= 0x030c30c3u;
	x |= x << 2;
	y |= y << 2;
	z |= z << 2;
	x &= 0x09249249u;
	y &= 0x09249249u;
	z &= 0x09249249u;
	return x | (y << 1) | (z << 2);
}

constexpr void morton_decode_3d_10bit(const std::uint32_t morton, std::uint32_t& x, std::uint32_t& y, std::uint32_t& z) noexcept
{
	x = morton;
	y = x >> 1;
	z = y >> 1;
	x &= 0x09249249u;
	y &= 0x09249249u;
	z &= 0x09249249u;
	x |= x >> 2;
	y |= y >> 2;
	z |= z >> 2;
	x &= 0x030c30c3u;
	y &= 0x030c30c3u;
	z &= 0x030c30c3u;
	x |= x >> 4;
	y |= y >> 4;
	z |= z >> 4;
	x &= 0x0300f00fu;
	y &= 0x0300f00fu;
	z &= 0x0300f00fu;
	x |= x >> 8;
	y |= y >> 8;
	z |= z >> 8;
	x &= 0x030000ffu;
	y &= 0x030000ffu;
	z &= 0x030000ffu;
	x |= x >> 16;
	y |= y >> 16;
	z |= z >> 16;
	x &= 0x000003ffu;
	y &= 0x000003ffu;
	z &= 0x000003ffu;
}

constexpr std::uint32_t morton_decode_x_3d_10bit(const std::uint32_t morton) noexcept
{
	std::uint32_t x = morton;
	x &= 0x09249249u;
	x |= x >> 2;
	x &= 0x030c30c3u;
	x |= x >> 4;
	x &= 0x0300f00fu;
	x |= x >> 8;
	x &= 0x030000ffu;
	x |= x >> 16;
	x &= 0x000003ffu;
	return x;
}

constexpr std::uint32_t morton_decode_y_3d_10bit(const std::uint32_t morton) noexcept
{
	return morton_decode_x_3d_10bit(morton >> 1);
}

constexpr std::uint32_t morton_decode_z_3d_10bit(const std::uint32_t morton) noexcept
{
	return morton_decode_x_3d_10bit(morton >> 2);
}

//==============================================================
// hilbert transforms 2d
//==============================================================

constexpr std::uint32_t morton_to_hilbert_2d(const std::uint32_t morton, const std::uint32_t levels) noexcept
{
	std::uint32_t index = 0u;
	std::uint32_t remap = 0xb4u;
	std::uint32_t block = levels << 1;
	while (block)
	{
		block -= 2;
		std::uint32_t mcode = (morton >> block) & 3u;
		std::uint32_t hcode = (remap >> (mcode << 1)) & 3u;
		remap ^= 0x82000028u >> (hcode << 3);
		index = (index << 2) + hcode;
	}
	return index;
}

constexpr std::uint32_t morton_to_hilbert_2d_auto(const std::uint32_t morton) noexcept
{
	std::uint32_t hilbert = 0u;
	std::uint32_t remap = 0xb4u;
	std::uint32_t block = ((0u - (morton >> 16)) >> 16) & 16u;
	block += ((0u - (morton >> (block + 8u))) >> 8) & 8u;
	block += ((0u - (morton >> (block + 4u))) >> 4) & 4u;
	block += 2u;
	block += ((0u - (morton >> block)) >> 2) & 2u;
	block |= 2u;
	block += 2u;
	while (block)
	{
		block -= 2u;
		std::uint32_t mcode = (morton >> block) & 3u;
		std::uint32_t hcode = (remap >> (mcode << 1)) & 3u;
		remap ^= 0x82000028u >> (hcode << 3);
		hilbert = ((hilbert << 2) + hcode);
	}
	return hilbert;
}

constexpr std::uint32_t hilbert_to_morton_2d(const std::uint32_t hilbert, const std::uint32_t levels) noexcept
{
	std::uint32_t index = 0u;
	std::uint32_t remap = 0xb4u;
	std::uint32_t block = levels << 1;
	while (block)
	{
		block -= 2;
		std::uint32_t hcode = (hilbert >> block) & 3u;
		std::uint32_t mcode = (remap >> (hcode << 1)) & 3u;
		remap ^= 0x330000ccu >> (hcode << 3);
		index = (index << 2) + mcode;
	}
	return index;
}

constexpr std::uint32_t hilbert_to_morton_2d_auto(const std::uint32_t hilbert) noexcept
{
	std::uint32_t morton = 0u;
	std::uint32_t remap = 0xb4u;
	std::uint32_t block = ((0u - (hilbert >> 16)) >> 16) & 16u;
	block += ((0u - (hilbert >> (block + 8u))) >> 8) & 8u;
	block += ((0u - (hilbert >> (block + 4u))) >> 4) & 4u;
	block += 2u;
	block += ((0u - (hilbert >> block)) >> 2) & 2u;
	block |= 2u;
	block += 2u;
	while (block)
	{
		block -= 2u;
		std::uint32_t hcode = (hilbert >> block) & 3u;
		std::uint32_t mcode = (remap >> (hcode << 1)) & 3u;
		remap ^= 0x330000ccu >> (hcode << 3);
		morton = (morton << 2) + mcode;
	}
	return morton;
}

//==============================================================
// hilbert transforms 3D
//==============================================================

constexpr std::uint32_t morton_to_hilbert_3d(const std::uint32_t morton, const std::uint32_t levels) noexcept
{
	std::uint32_t index = morton;
	if (levels > 1)
	{
		std::uint32_t block = (levels * 3u) - 3u;
		std::uint32_t hcode = (index >> block) & 7u;
		std::uint32_t mcode = 0u;
		std::uint32_t shift = 0u;
		std::uint32_t signs = 0u;
		while (block)
		{
			block -= 3u;
			hcode <<= 2;
			mcode = (0x20212021u >> hcode) & 3u;
			shift = (0x49u >> (4u - shift - mcode)) & 3u;
			signs = (((signs | (signs << 3)) >> mcode) ^ (0x53560300u >> hcode)) & 7u;
			mcode = (index >> block) & 7u;
			hcode = mcode;
			hcode = ((hcode | (hcode << 3)) >> shift) & 7u;
			hcode ^= signs;
			index ^= (mcode ^ hcode) << block;
		}
	}
	index ^= (index >> 1) & 0x92492492u;
	index ^= (index & 0x92492492u) >> 1;
	return index;
}

constexpr std::uint32_t morton_to_hilbert_3d_auto(const std::uint32_t morton) noexcept
{
	std::uint32_t index = morton;
	if (index)
	{
		std::uint32_t block = ((0u - (index >> 16)) >> 16) & 16u;
		block += ((0u - (index >> (block + 8u))) >> 8) & 8u;
		block += ((0u - (index >> (block + 4u))) >> 4) & 4u;
		block += ((0u - (index >> (block + 2u))) >> 2) & 2u;
		block += index >> (block + 1u);
		block = (block + 8u) / 9u;
		block = (block * 9u) - 3u;
		std::uint32_t hcode = (index >> block) & 7u;
		std::uint32_t mcode = 0u;
		std::uint32_t shift = 0u;
		std::uint32_t signs = 0u;
		while (block)
		{
			block -= 3u;
			hcode <<= 2;
			mcode = (0x20212021u >> hcode) & 3u;
			shift = (0x49u >> (4u - shift - mcode)) & 3u;
			signs = (((signs | (signs << 3)) >> mcode) ^ (0x53560300u >> hcode)) & 7u;
			mcode = (index >> block) & 7u;
			hcode = mcode;
			hcode = ((hcode | (hcode << 3)) >> shift) & 7u;
			hcode ^= signs;
			index ^= (mcode ^ hcode) << block;
		}
	}
	index ^= (index >> 1) & 0x92492492u;
	index ^= (index & 0x92492492u) >> 1;
	return index;
}

constexpr std::uint32_t hilbert_to_morton_3d(const std::uint32_t hilbert, const std::uint32_t levels) noexcept
{
	std::uint32_t index = hilbert;
	index ^= (index & 0x92492492u) >> 1;
	index ^= (index >> 1) & 0x92492492u;
	if (levels > 1)
	{
		std::uint32_t block = (levels * 3u) - 3u;
		std::uint32_t hcode = (index >> block) & 7u;
		std::uint32_t mcode = 0u;
		std::uint32_t shift = 0u;
		std::uint32_t signs = 0u;
		while (block)
		{
			block -= 3u;
			hcode <<= 2;
			mcode = (0x20212021u >> hcode) & 3u;
			shift = (0x49u >> (4u - shift + mcode)) & 3u;
			signs = (((signs | (signs << 3)) >> mcode) ^ (0x53560300u >> hcode)) & 7u;
			hcode = (index >> block) & 7u;
			mcode = hcode;
			mcode ^= signs;
			mcode = ((mcode | (mcode << 3)) >> shift) & 7u;
			index ^= (hcode ^ mcode) << block;
		}
	}
	return index;
}

constexpr std::uint32_t hilbert_to_morton_3d_auto(const std::uint32_t hilbert) noexcept
{
	std::uint32_t index = hilbert;
	index ^= (index & 0x92492492u) >> 1;
	index ^= (index >> 1) & 0x92492492u;
	if (index)
	{
		std::uint32_t block = ((0u - (index >> 16)) >> 16) & 16u;
		block += ((0u - (index >> (block + 8u))) >> 8) & 8u;
		block += ((0u - (index >> (block + 4u))) >> 4) & 4u;
		block += ((0u - (index >> (block + 2u))) >> 2) & 2u;
		block += index >> (block + 1u);
		block = (block + 8u) / 9u;
		block = (block * 9u) - 3u;
		std::uint32_t hcode = (index >> block) & 7u;
		std::uint32_t mcode = 0u;
		std::uint32_t shift = 0u;
		std::uint32_t signs = 0u;
		while (block)
		{
			block -= 3u;
			hcode <<= 2;
			mcode = (0x20212021u >> hcode) & 3u;
			shift = (0x49u >> (4u - shift + mcode)) & 3u;
			signs = (((signs | (signs << 3)) >> mcode) ^ (0x53560300u >> hcode)) & 7u;
			hcode = (index >> block) & 7u;
			mcode = hcode;
			mcode ^= signs;
			mcode = ((mcode | (mcode << 3)) >> shift) & 7u;
			index ^= (hcode ^ mcode) << block;
		}
	}
	return index;
}

}   //  namespace spatial_codes

#endif  //  #ifndef SPATIAL_CODES_HPP_INCLUDED

