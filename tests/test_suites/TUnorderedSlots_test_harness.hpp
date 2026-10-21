//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    TUnorderedSlots_test_harness.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    21 Oct 26

#pragma once

#ifndef TUNORDERED_SLOTS_TEST_HARNESS_HPP_INCLUDED
#define TUNORDERED_SLOTS_TEST_HARNESS_HPP_INCLUDED

#include <cstdint>

#define TUNORDERED_TESTHARNESS_WITH_MAIN   0

// -----------------------------
// Config
// -----------------------------
struct TUnorderedConfig
{
    // Selection
    bool run_smoke = true;
    bool run_traversal_semantics = true;
    bool run_compact_postconditions = true;
    bool run_resize_reserve = true;
    bool run_fuzz = false;

    // Parameters
    uint32_t initial_capacity = 32;
    bool stop_on_fail = true;

    // Fuzz (deterministic)
    uint32_t fuzz_seed = 1;
    uint32_t fuzz_steps = 20000;
    uint32_t fuzz_validate_every = 1;   // 1 = validate each op, 0 = never
    uint32_t fuzz_compact_every = 2000; // 0 = never
    uint32_t fuzz_history = 64;         // number of last ops to print on failure
    bool     fuzz_print_state = true;

    // Fuzz op weights (sum arbitrary; zero disables op)
    uint32_t w_acquire_head = 35;
    uint32_t w_reserve_and_acquire_head = 10;
    uint32_t w_acquire_specific = 8;
    uint32_t w_erase_random = 20;
    uint32_t w_resize = 5;
    uint32_t w_shrink_to_fit = 2;
    uint32_t w_rebuild_loose = 5;
    uint32_t w_rebuild_empty = 5;
    uint32_t w_compact = 10;            // in addition to fuzz_compact_every

    // Resize bounds
    uint32_t fuzz_resize_min = 1;
    uint32_t fuzz_resize_max = 256;
};

int run_all_tests(const TUnorderedConfig& cfg_in);

#endif  //  TUNORDERED_SLOTS_TEST_HARNESS_HPP_INCLUDED
