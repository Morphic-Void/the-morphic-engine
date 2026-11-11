
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//	File:   atomic_types.hpp
//	Author: Ritchie Brannan
//	Date:   22 Jul 2025
//
//	Atomic type wrappers

#pragma once

#ifndef	__ATOMIC_TYPES_INCLUDED__
#define	__ATOMIC_TYPES_INCLUDED__

#include <atomic>
#include <cstdint>

template<typename T>
struct alignas(128) TCacheLineAtomic
{
    static_assert(sizeof(std::atomic<T>) <= 128u, "Atomic object is larger than 128 bytes.");

    TCacheLineAtomic() noexcept = default;

    TCacheLineAtomic(const T initial_value) noexcept : value{ initial_value } {}

    std::atomic<T> value{ T{ 0u } };
    std::uint8_t padding[128u - sizeof(std::atomic<T>)]{ 0u };
};

#endif	//	#ifndef	__ATOMIC_TYPES_INCLUDED__
