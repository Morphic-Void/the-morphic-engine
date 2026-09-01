
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)

#pragma once

#ifndef TORDERED_SLOTS_TEST_HARNESS_HPP_INCLUDED
#define TORDERED_SLOTS_TEST_HARNESS_HPP_INCLUDED

#include <cstdint>

#ifndef TORDERED_TESTHARNESS_WITH_MAIN
#define TORDERED_TESTHARNESS_WITH_MAIN  0
#endif

// -----------------------------
// Config
// -----------------------------
struct TOrderedConfig
{
    // Selection
    bool run_exhaustive_delete = true;
    bool run_exhaustive_insert_delete = false;

    bool run_duplicates_and_stability = true;
    bool run_find_and_bounds = true;
    bool run_rank_index_mapping = true;
    bool run_traversal_semantics = true;
    bool run_sort_and_compact = true;
    bool run_fuzz_lightweight = false;

    // Parameters
    int  N = 5;

    // Exhaustive delete order options
    bool vary_insertion_orders = true;        // subset (asc/desc + a few patterns)
    uint64_t max_delete_perms = 0;            // 0 = unlimited (full permutation)
    uint32_t progress_every = 5000;

    // Exhaustive insert+delete options
    uint64_t max_insert_perms = 0;            // 0 = unlimited
    uint64_t max_delete_perms_each_insert = 0;// 0 = unlimited

    // General harness options
    uint32_t initial_capacity = 32;           // will be max(initial_capacity, N+4)
    bool check_lex_order_in_validate = true;  // validate_tree(Base::CheckLex::InOrder)
    bool stop_on_fail = true;

    // Fuzz (deterministic)
    uint32_t fuzz_seed = 1;
    uint32_t fuzz_steps = 20000;
    int      fuzz_key_range = 128;           // keys in [0, key_range)
    uint32_t fuzz_validate_every = 1;        // 1 = validate each op, 0 = never
    uint32_t fuzz_compact_every = 2000;      // 0 = never
    uint32_t fuzz_history = 64;              // number of last ops to print on failure
    bool     fuzz_print_state = true;        // print counts on failure
};

int run_all_tests(const TOrderedConfig& cfg_in);

#endif  //  TORDERED_SLOTS_TEST_HARNESS_HPP_INCLUDED
