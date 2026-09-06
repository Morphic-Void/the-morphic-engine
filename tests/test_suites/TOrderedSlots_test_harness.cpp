
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:    TOrderedSlots_test_harness.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26
//
//  Update summary vs prior harness:
//  - lexical_index -> rank_index
//  - lex_index_of -> rank_index_of
//  - find_by_lexical_index -> find_by_rank_index
//  - sort_and_pack no longer takes preserve flag:
//      previous preserve_loose_index_order=true  ==> call rebuild_loose_in_index_order() then sort_and_pack()
//
//  Notes:
//  - This harness still uses STL freely (debug harness).
//  - Updated for full-domain rank-index contract:
//      lexed  : [0 .. lexed_count)
//      loose  : [lexed_count .. occupied_count)
//      empty  : [occupied_count .. capacity)
//  - Loose and empty list traversal is bounded by the corresponding category count.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <set>
#include <random>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include "containers/TPodOrderedSlots.hpp"
#include "containers/slots/TOrderedSlots.hpp"
#include "tests/environment/test_environment.hpp"
#include "tests/test_suites/TOrderedSlots_test_harness.hpp"

#ifndef TORDERED_TESTHARNESS_WITH_MAIN
#   define TORDERED_TESTHARNESS_WITH_MAIN  1
#endif

// -----------------------------
// Debug-break / fail policy
// -----------------------------
#if defined(_MSC_VER)
#   define TLEX_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#   define TLEX_DEBUG_BREAK() __builtin_trap()
#else
#   define TLEX_DEBUG_BREAK() std::abort()
#endif

struct TestLogger
{
    bool stop_on_fail = true;
    bool debug_break_on_fail =
#if !defined(NDEBUG)
        true;
#else
        false;
#endif

    uint32_t failures = 0;

    void fail(const std::string& msg) noexcept
    {
        ++failures;
        std::cerr << "FAIL: " << msg << "\n";
        if (debug_break_on_fail) { TLEX_DEBUG_BREAK(); }
        if (stop_on_fail) { std::abort(); }
    }
};

// Hard-fail helper for contract violations inside slot-backing operations.
// (Intentionally ignores stop_on_fail and always terminates.)
[[noreturn]] static void hard_fail_contract(const char* msg) noexcept
{
    std::cerr << "FAIL (contract): " << msg << "\n";
    TLEX_DEBUG_BREAK();
    std::abort();
}

static std::string vec_to_string(const std::vector<int>& v)
{
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i) oss << ", ";
        oss << v[i];
    }
    oss << "]";
    return oss.str();
}

static bool starts_with(const std::string& s, const char* prefix)
{
    const size_t n = std::strlen(prefix);
    return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

static bool parse_i64(const std::string& s, int64_t& out_v)
{
    if (s.empty()) return false;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out_v = static_cast<int64_t>(v);
    return true;
}

// -----------------------------
// Harness: derived test adapter
// -----------------------------
template<typename TIndex, typename TMeta>
class TOrderedSlots_TestStorage
{
public:
    int32_t on_compare_keys(const int32_t source, const int32_t target) const noexcept;
    void on_move_payload(const int32_t source, const int32_t target) noexcept;
    uint32_t on_reserve_empty(const uint32_t minimum_capacity, const uint32_t recommended_capacity) noexcept;
    void ensure_payload_capacity_matches_slots(const uint32_t capacity) noexcept;

protected:
    mutable int m_query_key = 0;
    std::vector<int> m_payload;
    int  m_temp = 0;
    bool m_temp_valid = false;
};

template<typename TIndex, typename TMeta>
class TOrderedSlots_Test final : private slots::TOrderedSlots<TOrderedSlots_TestStorage<TIndex, TMeta>, TIndex, TMeta>
{
    using SlotBacking = TOrderedSlots_TestStorage<TIndex, TMeta>;
    using Base = slots::TOrderedSlots<SlotBacking, TIndex, TMeta>;

public:
    TOrderedSlots_Test() noexcept = default;

    bool init(uint32_t cap) noexcept
    {
        this->m_payload.clear();
        this->m_payload.shrink_to_fit();
        this->m_temp_valid = false;
        this->m_query_key = 0;

        if (!Base::initialise(cap))
            return false;

        this->m_payload.assign(Base::capacity(), 0);
        return true;
    }

    void shutdown() noexcept
    {
        (void)Base::shutdown();
        this->m_payload.clear();
        this->m_temp_valid = false;
        this->m_query_key = 0;
    }

    bool insert_key_lexed(int key, bool require_unique = true) noexcept
    {
        this->m_query_key = key;
        const int32_t idx = Base::acquire(-1, /*lex=*/true, /*require_unique=*/require_unique);
        if (idx < 0) return false;

        this->ensure_payload_capacity_matches_slots(Base::capacity());
        this->m_payload[static_cast<size_t>(idx)] = key;
        return true;
    }

    bool insert_key_loose(int key) noexcept
    {
        this->m_query_key = key;
        const int32_t idx = Base::acquire(-1, /*lex=*/false, /*require_unique=*/false);
        if (idx < 0) return false;

        this->ensure_payload_capacity_matches_slots(Base::capacity());
        this->m_payload[static_cast<size_t>(idx)] = key;
        return true;
    }

    bool lex_slot(int32_t slot_index) noexcept { return Base::lex(slot_index); }
    bool unlex_slot(int32_t slot_index) noexcept { return Base::unlex(slot_index); }
    void lex_all_slots() noexcept { Base::lex_all(); }
    void unlex_all_slots() noexcept { Base::unlex_all(); }
    void relex_all_slots() noexcept { Base::relex_all(); }

    void rebuild_loose_in_index_order() noexcept { Base::rebuild_loose_in_index_order(); }
    void sort_and_compact_slots() noexcept { Base::sort_and_pack(); }

    bool remove_key_any_equal_lexed(int key) noexcept
    {
        this->m_query_key = key;
        const int32_t idx = Base::find_any_equal();
        if (idx < 0) return false;

        this->m_payload[static_cast<size_t>(idx)] = 0;
        return Base::erase(idx);
    }

    bool erase_slot(int32_t slot_index) noexcept
    {
        if (slot_index < 0) return false;
        if (static_cast<uint32_t>(slot_index) >= Base::capacity()) return false;
        this->m_payload[static_cast<size_t>(slot_index)] = 0;
        return Base::erase(slot_index);
    }

    uint32_t size() const noexcept { return Base::occupied_count(); }
    uint32_t cap() const noexcept { return Base::capacity(); }
    uint32_t lexed_count() const noexcept { return Base::lexed_count(); }
    uint32_t loose_count() const noexcept { return Base::loose_count(); }
    uint32_t empty_count() const noexcept { return Base::empty_count(); }

    struct ValidateResult
    {
        bool integrity = false;
        bool tree = false;
        bool order = false;
    };

    ValidateResult validate_detailed(bool check_lex_order) const noexcept
    {
        ValidateResult r;
        r.integrity = Base::check_integrity();

        if (!r.integrity)
        {
            r.tree = false;
            r.order = false;
            return r;
        }

        r.tree = Base::validate_tree(Base::LexCheck::None);

        if (!r.tree)
        {
            r.order = false;
            return r;
        }

        r.order = check_lex_order ? Base::validate_tree(Base::LexCheck::InOrder) : true;
        return r;
    }

    bool validate_all(bool check_lex_order) const noexcept
    {
        const ValidateResult r = validate_detailed(check_lex_order);
        return r.integrity && r.tree && r.order;
    }

    std::vector<int> keys_in_lex_order() const noexcept
    {
        std::vector<int> out;
        out.reserve(Base::lexed_count());

        int32_t idx = Base::first_lexed();
        while (idx >= 0)
        {
            out.push_back(this->m_payload[static_cast<size_t>(idx)]);
            idx = Base::next_lexed(idx);
        }
        return out;
    }

    std::vector<int32_t> slots_in_lex_order() const noexcept
    {
        std::vector<int32_t> out;
        out.reserve(Base::lexed_count());

        int32_t idx = Base::first_lexed();
        while (idx >= 0)
        {
            out.push_back(idx);
            idx = Base::next_lexed(idx);
        }
        return out;
    }

    std::vector<int32_t> slots_in_loose_index_order() const noexcept
    {
        std::vector<int32_t> out;
        const uint32_t n = Base::loose_count();
        out.reserve(n);

        if (n == 0)
            return out;

        const int32_t head = Base::first_loose();
        if (head < 0)
            hard_fail_contract("slots_in_loose_index_order: loose_count>0 but first_loose() < 0");

        int32_t idx = head;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (idx < 0)
                hard_fail_contract("slots_in_loose_index_order: encountered -1 while traversing circular loose list");

            out.push_back(idx);
            idx = Base::next_loose(idx);
        }

        if (idx != -1)
            hard_fail_contract("slots_in_loose_index_order: did not return to head after loose_count() steps");

        return out;
    }

    // Traverse the empty list by count and validate category and uniqueness.
    std::vector<int32_t> slots_in_empty_order_checked() const noexcept
    {
        std::vector<int32_t> out;
        const uint32_t n = Base::empty_count();
        out.reserve(n);

        if (n == 0) return out;

        int32_t slot_index = Base::first_empty();
        if (slot_index < 0)
            hard_fail_contract("slots_in_empty_order_checked: empty_count>0 but first_empty() < 0");

        for (uint32_t i = 0; i < n; ++i)
        {
            if (!Base::is_empty_slot(slot_index))
                hard_fail_contract("slots_in_empty_order_checked: non-empty slot in empty traversal");
            for (size_t j = 0; j < out.size(); ++j)
            {
                if (out[j] == slot_index)
                    hard_fail_contract("slots_in_empty_order_checked: duplicate slot_index observed");
            }
            out.push_back(slot_index);
            slot_index = Base::next_empty(slot_index);
        }

        if (slot_index != -1)
            hard_fail_contract("slots_in_empty_order_checked: traversal exceeded empty_count()");

        slot_index = Base::last_empty();
        if (slot_index != out.back())
            hard_fail_contract("slots_in_empty_order_checked: last_empty() mismatch");
        for (size_t i = out.size(); i != 0; --i)
        {
            if (slot_index != out[i - 1u])
                hard_fail_contract("slots_in_empty_order_checked: reverse traversal mismatch");
            slot_index = Base::prev_empty(slot_index);
        }
        if (slot_index != -1)
            hard_fail_contract("slots_in_empty_order_checked: reverse traversal exceeded empty_count()");

        return out;
    }

    int32_t find_any_equal(int key) const noexcept { this->m_query_key = key; return Base::find_any_equal(); }
    int32_t find_first_equal(int key) const noexcept { this->m_query_key = key; return Base::find_first_equal(); }
    int32_t find_last_equal(int key) const noexcept { this->m_query_key = key; return Base::find_last_equal(); }
    int32_t lower_bound(int key) const noexcept { this->m_query_key = key; return Base::lower_bound_by_lex(); }
    int32_t upper_bound(int key) const noexcept { this->m_query_key = key; return Base::upper_bound_by_lex(); }

    int32_t rank_index_of(int32_t slot_index) const noexcept { return Base::rank_index_of(slot_index); }
    int32_t find_by_rank_index(int32_t rank_index) const noexcept { return Base::find_by_rank_index(rank_index); }

    bool has_duplicate_key_any(int key) const noexcept { this->m_query_key = key; return Base::has_duplicate_key(-1); }

    std::vector<int32_t> slots_in_occupied_order() const noexcept
    {
        std::vector<int32_t> out = slots_in_lex_order();
        out.reserve(Base::occupied_count());
        const std::vector<int32_t> loose = slots_in_loose_index_order();
        out.insert(out.end(), loose.begin(), loose.end());
        return out;
    }

private:
};

template<typename TIndex, typename TMeta>
int32_t TOrderedSlots_TestStorage<TIndex, TMeta>::on_compare_keys(const int32_t source, const int32_t target) const noexcept
{
    const int a = (source < 0) ? m_query_key : m_payload[static_cast<size_t>(source)];
    const int b = m_payload[static_cast<size_t>(target)];
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

template<typename TIndex, typename TMeta>
void TOrderedSlots_TestStorage<TIndex, TMeta>::on_move_payload(const int32_t source, const int32_t target) noexcept
{
    if (source == target)
        hard_fail_contract("on_move_payload: source == target");

    const bool src_is_temp = (source < 0);
    const bool dst_is_temp = (target < 0);
    if (src_is_temp && dst_is_temp)
        hard_fail_contract("on_move_payload: both source and target are temp (-1)");

    ensure_payload_capacity_matches_slots(static_cast<uint32_t>(m_payload.size()));

    if (source < 0)
    {
        if (!m_temp_valid)
            hard_fail_contract("on_move_payload: temp -> target but temp invalid");
        m_payload[static_cast<size_t>(target)] = m_temp;
        m_temp_valid = false;
        return;
    }

    if (target < 0)
    {
        m_temp = m_payload[static_cast<size_t>(source)];
        m_temp_valid = true;
        return;
    }

    m_payload[static_cast<size_t>(target)] = m_payload[static_cast<size_t>(source)];
}

template<typename TIndex, typename TMeta>
uint32_t TOrderedSlots_TestStorage<TIndex, TMeta>::on_reserve_empty(
    const uint32_t minimum_capacity,
    const uint32_t recommended_capacity) noexcept
{
    (void)minimum_capacity;
    m_payload.resize(recommended_capacity);
    return recommended_capacity;
}

template<typename TIndex, typename TMeta>
void TOrderedSlots_TestStorage<TIndex, TMeta>::ensure_payload_capacity_matches_slots(const uint32_t capacity) noexcept
{
    if (m_payload.size() != capacity) m_payload.resize(capacity);
}

// -----------------------------
// Tests
// -----------------------------
template<typename TIndex, typename TMeta>
static bool test_exhaustive_delete_orders(const TOrderedConfig& cfg, TestLogger& log)
{
    using Harness = TOrderedSlots_Test<TIndex, TMeta>;

    const int N = cfg.N;
    std::vector<int> base_keys;
    base_keys.reserve(static_cast<size_t>(N));
    for (int i = 1; i <= N; ++i) base_keys.push_back(i);

    std::vector<std::vector<int>> insertion_orders;
    if (!cfg.vary_insertion_orders)
    {
        insertion_orders.push_back(base_keys);
    }
    else
    {
        insertion_orders.push_back(base_keys);
        auto desc = base_keys;
        std::reverse(desc.begin(), desc.end());
        insertion_orders.push_back(desc);

        if (N == 3)
        {
            insertion_orders.push_back({ 3, 1, 2 });
            insertion_orders.push_back({ 2, 1, 3 });
            insertion_orders.push_back({ 2, 3, 1 });
        }
    }

    for (const auto& ins : insertion_orders)
    {
        std::vector<int> del = base_keys;
        std::sort(del.begin(), del.end());

        uint64_t perm_count = 0;

        do
        {
            ++perm_count;
            if (cfg.max_delete_perms && perm_count > cfg.max_delete_perms) break;

            Harness h;
            const uint32_t cap = std::max(cfg.initial_capacity, static_cast<uint32_t>(N + 4));
            if (!h.init(cap))
            {
                log.fail("initialise failed");
                return false;
            }

            std::set<int> ref;

            for (int k : ins)
            {
                if (!h.insert_key_lexed(k, true))
                {
                    log.fail("insert failed key=" + std::to_string(k) + " ins=" + vec_to_string(ins));
                    return false;
                }
                ref.insert(k);

                if (!h.validate_all(cfg.check_lex_order_in_validate))
                {
                    log.fail("validation failed after insert key=" + std::to_string(k) + " ins=" + vec_to_string(ins));
                    return false;
                }
            }

            for (int k : del)
            {
                if (!h.remove_key_any_equal_lexed(k))
                {
                    log.fail("remove failed key=" + std::to_string(k) + " ins=" + vec_to_string(ins) + " del=" + vec_to_string(del));
                    return false;
                }
                ref.erase(k);

                if (!h.validate_all(cfg.check_lex_order_in_validate))
                {
                    auto got = h.keys_in_lex_order();
                    std::vector<int> expected(ref.begin(), ref.end());
                    log.fail("validation failed after remove key=" + std::to_string(k) +
                        "\n  ins=" + vec_to_string(ins) +
                        "\n  del=" + vec_to_string(del) +
                        "\n  expected=" + vec_to_string(expected) +
                        "\n  got=" + vec_to_string(got));
                    return false;
                }

                auto got = h.keys_in_lex_order();
                std::vector<int> expected(ref.begin(), ref.end());
                if (got != expected)
                {
                    log.fail("in-order mismatch after remove key=" + std::to_string(k) +
                        "\n  ins=" + vec_to_string(ins) +
                        "\n  del=" + vec_to_string(del) +
                        "\n  expected=" + vec_to_string(expected) +
                        "\n  got=" + vec_to_string(got));
                    return false;
                }
            }

            if (h.size() != 0)
            {
                log.fail("expected empty at end, size=" + std::to_string(h.size()));
                return false;
            }

            if (!h.validate_all(cfg.check_lex_order_in_validate))
            {
                log.fail("validation failed at end (empty) ins=" + vec_to_string(ins) + " del=" + vec_to_string(del));
                return false;
            }

            if (cfg.progress_every && (perm_count % cfg.progress_every) == 0)
            {
                std::cout << "  ins=" << vec_to_string(ins) << " perms=" << perm_count << "\n";
            }

        } while (std::next_permutation(del.begin(), del.end()));

        std::cout << "Exhaustive delete-order done: N=" << N
            << " ins=" << vec_to_string(ins)
            << " perms=" << perm_count
            << (cfg.max_delete_perms ? " (limited)" : "")
            << "\n";
    }

    return true;
}

template<typename TIndex, typename TMeta>
static bool test_exhaustive_insert_delete_orders(const TOrderedConfig& cfg, TestLogger& log)
{
    using Harness = TOrderedSlots_Test<TIndex, TMeta>;

    const int N = cfg.N;
    std::vector<int> keys;
    keys.reserve(static_cast<size_t>(N));
    for (int i = 1; i <= N; ++i) keys.push_back(i);

    std::sort(keys.begin(), keys.end());

    uint64_t ins_perm_count = 0;

    do
    {
        ++ins_perm_count;
        if (cfg.max_insert_perms && ins_perm_count > cfg.max_insert_perms) break;

        const auto ins = keys;

        std::vector<int> del = keys;
        std::sort(del.begin(), del.end());

        uint64_t del_perm_count = 0;

        do
        {
            ++del_perm_count;
            if (cfg.max_delete_perms_each_insert && del_perm_count > cfg.max_delete_perms_each_insert) break;

            Harness h;
            const uint32_t cap = std::max(cfg.initial_capacity, static_cast<uint32_t>(N + 4));
            if (!h.init(cap))
            {
                log.fail("initialise failed");
                return false;
            }

            std::set<int> ref;

            for (int k : ins)
            {
                if (!h.insert_key_lexed(k, true))
                {
                    log.fail("insert failed key=" + std::to_string(k) + " ins=" + vec_to_string(ins));
                    return false;
                }
                ref.insert(k);

                if (!h.validate_all(cfg.check_lex_order_in_validate))
                {
                    log.fail("validation failed after insert key=" + std::to_string(k) + " ins=" + vec_to_string(ins));
                    return false;
                }
            }

            for (int k : del)
            {
                if (!h.remove_key_any_equal_lexed(k))
                {
                    log.fail("remove failed key=" + std::to_string(k) + " ins=" + vec_to_string(ins) + " del=" + vec_to_string(del));
                    return false;
                }
                ref.erase(k);

                if (!h.validate_all(cfg.check_lex_order_in_validate))
                {
                    auto got = h.keys_in_lex_order();
                    std::vector<int> expected(ref.begin(), ref.end());
                    log.fail("validation failed after remove key=" + std::to_string(k) +
                        "\n  ins=" + vec_to_string(ins) +
                        "\n  del=" + vec_to_string(del) +
                        "\n  expected=" + vec_to_string(expected) +
                        "\n  got=" + vec_to_string(got));
                    return false;
                }

                auto got = h.keys_in_lex_order();
                std::vector<int> expected(ref.begin(), ref.end());
                if (got != expected)
                {
                    log.fail("in-order mismatch after remove key=" + std::to_string(k) +
                        "\n  ins=" + vec_to_string(ins) +
                        "\n  del=" + vec_to_string(del) +
                        "\n  expected=" + vec_to_string(expected) +
                        "\n  got=" + vec_to_string(got));
                    return false;
                }
            }

            if (h.size() != 0)
            {
                log.fail("expected empty at end size=" + std::to_string(h.size()) +
                    "\n  ins=" + vec_to_string(ins) +
                    "\n  del=" + vec_to_string(del));
                return false;
            }

        } while (std::next_permutation(del.begin(), del.end()));

        std::cout << "Insertion perm done: ins_perm=" << ins_perm_count
            << " delete_perms=" << del_perm_count
            << (cfg.max_delete_perms_each_insert ? " (limited)" : "")
            << "\n";

    } while (std::next_permutation(keys.begin(), keys.end()));

    std::cout << "Exhaustive insert+delete done: N=" << N
        << " ins_perms=" << ins_perm_count
        << (cfg.max_insert_perms ? " (limited)" : "")
        << "\n";
    return true;
}

template<typename TIndex, typename TMeta>
static bool test_duplicates_and_stable_equals(const TOrderedConfig& cfg, TestLogger& log)
{
    (void)cfg;
    using Harness = TOrderedSlots_Test<TIndex, TMeta>;

    Harness h;
    if (!h.init(64))
    {
        log.fail("initialise failed");
        return false;
    }

    const std::vector<int> ins = { 10, 5, 10, 10, 7, 10, 5 };
    for (int k : ins)
    {
        if (!h.insert_key_lexed(k, /*require_unique=*/false))
        {
            log.fail("insert failed for key=" + std::to_string(k));
            return false;
        }
        if (!h.validate_all(true))
        {
            log.fail("validate failed after insert key=" + std::to_string(k));
            return false;
        }
    }

    auto keys = h.keys_in_lex_order();
    if (!std::is_sorted(keys.begin(), keys.end()))
    {
        log.fail("keys_in_lex_order not nondecreasing: " + vec_to_string(keys));
        return false;
    }

    const auto before_slots = h.slots_in_lex_order();
    h.relex_all_slots();
    if (!h.validate_all(true))
    {
        log.fail("validate failed after relex_all");
        return false;
    }
    const auto after_slots = h.slots_in_lex_order();
    if (before_slots != after_slots)
    {
        log.fail("stable equals/order violated by relex_all: slots changed");
        return false;
    }

    h.sort_and_compact_slots();
    if (!h.validate_all(true))
    {
        log.fail("validate failed after sort_and_pack");
        return false;
    }
    const auto after_compact_slots = h.slots_in_lex_order();
    for (int32_t i = 0; i < static_cast<int32_t>(after_compact_slots.size()); ++i)
    {
        if (after_compact_slots[static_cast<size_t>(i)] != i)
        {
            log.fail("sort_and_pack lexed slots not compacted to [0..lexed_count): got slot " +
                std::to_string(after_compact_slots[static_cast<size_t>(i)]) + " at lex rank " + std::to_string(i));
            return false;
        }
    }

    return true;
}

template<typename TIndex, typename TMeta>
static bool test_find_and_bounds(const TOrderedConfig& cfg, TestLogger& log)
{
    (void)cfg;
    using Harness = TOrderedSlots_Test<TIndex, TMeta>;

    Harness h;
    if (!h.init(64))
    {
        log.fail("initialise failed");
        return false;
    }

    const std::vector<int> ins = { 2, 1, 2, 3, 2, 4, 2 };
    for (int k : ins)
    {
        if (!h.insert_key_lexed(k, /*require_unique=*/false))
        {
            log.fail("insert failed key=" + std::to_string(k));
            return false;
        }
    }
    if (!h.validate_all(true))
    {
        log.fail("validate failed after inserts");
        return false;
    }

    const auto slots = h.slots_in_lex_order();
    const auto keys = h.keys_in_lex_order();

    auto first_pos = [&](int key) -> int32_t {
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == key) return static_cast<int32_t>(i);
        return -1;
        };
    auto last_pos = [&](int key) -> int32_t {
        for (size_t i = keys.size(); i-- > 0;) if (keys[i] == key) return static_cast<int32_t>(i);
        return -1;
        };
    auto lower_pos = [&](int key) -> int32_t {
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] >= key) return static_cast<int32_t>(i);
        return -1;
        };
    auto upper_pos = [&](int key) -> int32_t {
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] > key) return static_cast<int32_t>(i);
        return -1;
        };

    const std::vector<int> queries = { 0, 1, 2, 3, 4, 5 };
    for (int q : queries)
    {
        const int32_t fp = first_pos(q);
        const int32_t lp = last_pos(q);
        const int32_t lb = lower_pos(q);
        const int32_t ub = upper_pos(q);

        const int32_t got_first = h.find_first_equal(q);
        const int32_t got_last = h.find_last_equal(q);
        const int32_t got_lb = h.lower_bound(q);
        const int32_t got_ub = h.upper_bound(q);

        if ((fp < 0) != (got_first < 0))
        {
            log.fail("find_first_equal presence mismatch q=" + std::to_string(q));
            return false;
        }
        if (fp >= 0 && got_first != slots[static_cast<size_t>(fp)])
        {
            log.fail("find_first_equal mismatch q=" + std::to_string(q));
            return false;
        }

        if ((lp < 0) != (got_last < 0))
        {
            log.fail("find_last_equal presence mismatch q=" + std::to_string(q));
            return false;
        }
        if (lp >= 0 && got_last != slots[static_cast<size_t>(lp)])
        {
            log.fail("find_last_equal mismatch q=" + std::to_string(q));
            return false;
        }

        if ((lb < 0) != (got_lb < 0))
        {
            log.fail("lower_bound presence mismatch q=" + std::to_string(q));
            return false;
        }
        if (lb >= 0 && got_lb != slots[static_cast<size_t>(lb)])
        {
            log.fail("lower_bound mismatch q=" + std::to_string(q));
            return false;
        }

        if ((ub < 0) != (got_ub < 0))
        {
            log.fail("upper_bound presence mismatch q=" + std::to_string(q));
            return false;
        }
        if (ub >= 0 && got_ub != slots[static_cast<size_t>(ub)])
        {
            log.fail("upper_bound mismatch q=" + std::to_string(q));
            return false;
        }
    }

    return true;
}

template<typename TIndex, typename TMeta>
static bool test_rank_index_mapping(const TOrderedConfig& cfg, TestLogger& log)
{
    (void)cfg;
    using Harness = TOrderedSlots_Test<TIndex, TMeta>;

    Harness h;
    if (!h.init(64))
    {
        log.fail("initialise failed");
        return false;
    }

    for (int k : { 5, 1, 3, 2, 4 })
        if (!h.insert_key_lexed(k, true)) { log.fail("insert lexed failed"); return false; }
    for (int k : { 42, 41, 40 })
        if (!h.insert_key_loose(k)) { log.fail("insert loose failed"); return false; }

    if (!h.validate_all(true))
    {
        log.fail("validate failed after inserts");
        return false;
    }

    const auto lex_slots = h.slots_in_lex_order();
    const auto loose_slots = h.slots_in_loose_index_order();
    const auto empty_slots = h.slots_in_empty_order_checked();

    for (size_t i = 0; i < lex_slots.size(); ++i)
    {
        const int32_t slot = lex_slots[i];
        const int32_t ri = h.rank_index_of(slot);
        if (ri != static_cast<int32_t>(i))
        {
            log.fail("rank_index_of(lexed) mismatch slot=" + std::to_string(slot) +
                " expected=" + std::to_string(i) + " got=" + std::to_string(ri));
            return false;
        }

        const int32_t back = h.find_by_rank_index(static_cast<int32_t>(i));
        if (back != slot)
        {
            log.fail("find_by_rank_index mismatch rank=" + std::to_string(i));
            return false;
        }
    }

    const int32_t lex_base = static_cast<int32_t>(h.lexed_count());
    for (size_t i = 0; i < loose_slots.size(); ++i)
    {
        const int32_t slot = loose_slots[i];
        const int32_t expect = lex_base + static_cast<int32_t>(i);
        const int32_t ri = h.rank_index_of(slot);
        if (ri != expect)
        {
            log.fail("rank_index_of(loose) mismatch slot=" + std::to_string(slot) +
                " expected=" + std::to_string(expect) + " got=" + std::to_string(ri));
            return false;
        }

        const int32_t back = h.find_by_rank_index(expect);
        if (back != slot)
        {
            log.fail("find_by_rank_index mismatch rank=" + std::to_string(expect));
            return false;
        }
    }

    const int32_t occ_base = static_cast<int32_t>(h.size());
    for (size_t i = 0; i < empty_slots.size(); ++i)
    {
        const int32_t slot = empty_slots[i];
        const int32_t expect = occ_base + static_cast<int32_t>(i);
        const int32_t ri = h.rank_index_of(slot);
        if (ri != expect)
        {
            log.fail("rank_index_of(empty) mismatch slot=" + std::to_string(slot) +
                " expected=" + std::to_string(expect) + " got=" + std::to_string(ri));
            return false;
        }

        const int32_t back = h.find_by_rank_index(expect);
        if (back != slot)
        {
            log.fail("find_by_rank_index(empty) mismatch rank=" + std::to_string(expect));
            return false;
        }
    }

    if (h.find_by_rank_index(-1) != -1) { log.fail("find_by_rank_index(-1) should be -1"); return false; }
    if (h.find_by_rank_index(static_cast<int32_t>(h.cap())) != -1)
    {
        log.fail("find_by_rank_index(capacity) should be -1");
        return false;
    }

    return true;
}

template<typename TIndex, typename TMeta>
static bool test_traversal_semantics(const TOrderedConfig& cfg, TestLogger& log)
{
    (void)cfg;
    using Harness = TOrderedSlots_Test<TIndex, TMeta>;

    Harness h;
    if (!h.init(32))
    {
        log.fail("initialise failed");
        return false;
    }

    for (int k : { 2, 1, 3 })
        if (!h.insert_key_lexed(k, true)) { log.fail("insert lexed failed"); return false; }
    for (int k : { 10, 11 })
        if (!h.insert_key_loose(k)) { log.fail("insert loose failed"); return false; }

    if (!h.validate_all(true))
    {
        log.fail("validate failed");
        return false;
    }

    const auto lex_slots = h.slots_in_lex_order();
    const auto loose_slots = h.slots_in_loose_index_order();
    const auto empty_slots = h.slots_in_empty_order_checked();
    if (empty_slots.size() != static_cast<size_t>(h.empty_count()))
    {
        log.fail("empty traversal count mismatch");
        return false;
    }

    if (h.lexed_count() + h.loose_count() + h.empty_count() != h.cap())
    {
        log.fail("count sum mismatch after traversal checks");
        return false;
    }

    const auto occupied_slots = h.slots_in_occupied_order();
    if (occupied_slots.size() != static_cast<size_t>(h.size()))
    {
        log.fail("occupied traversal count mismatch");
        return false;
    }

    for (size_t i = 0; i < occupied_slots.size(); ++i)
    {
        for (size_t j = 0; j < empty_slots.size(); ++j)
        {
            if (occupied_slots[i] == empty_slots[j])
            {
                log.fail("occupied and empty traversals overlap");
                return false;
            }
        }
    }

    return true;
}

template<typename TIndex, typename TMeta>
static bool test_sort_and_compact_postconditions(const TOrderedConfig& cfg, TestLogger& log)
{
    (void)cfg;
    using Harness = TOrderedSlots_Test<TIndex, TMeta>;

    for (int pass = 0; pass < 2; ++pass)
    {
        const bool preserve_loose = (pass == 1);

        Harness hh;
        if (!hh.init(64)) { log.fail("initialise failed"); return false; }

        for (int k : { 3, 1, 2, 2, 4 })
            if (!hh.insert_key_lexed(k, /*require_unique=*/false)) { log.fail("insert lexed failed"); return false; }
        for (int k : { 20, 21, 22, 23 })
            if (!hh.insert_key_loose(k)) { log.fail("insert loose failed"); return false; }

        if (!hh.validate_all(true)) { log.fail("validate failed after setup"); return false; }

        const auto lex_slots0 = hh.slots_in_lex_order();
        if (lex_slots0.size() >= 2)
            if (!hh.erase_slot(lex_slots0[1])) { log.fail("erase failed"); return false; }

        const auto loose_slots0 = hh.slots_in_loose_index_order();
        if (!loose_slots0.empty())
            if (!hh.erase_slot(loose_slots0[0])) { log.fail("erase failed"); return false; }

        if (!hh.validate_all(true)) { log.fail("validate failed after erases"); return false; }

        const auto lex_keys_pre = hh.keys_in_lex_order();
        const auto loose_slots_pre = hh.slots_in_loose_index_order();

        if (preserve_loose)
            hh.rebuild_loose_in_index_order();

        hh.sort_and_compact_slots();

        if (!hh.validate_all(true)) { log.fail("validate failed after sort_and_pack"); return false; }

        const auto lex_keys_post = hh.keys_in_lex_order();
        if (lex_keys_post != lex_keys_pre)
        {
            log.fail(std::string("sort_and_pack changed lex key sequence (should be stable). preserve_loose=") +
                (preserve_loose ? "true" : "false"));
            return false;
        }

        const auto lex_slots_post = hh.slots_in_lex_order();
        for (int32_t i = 0; i < static_cast<int32_t>(lex_slots_post.size()); ++i)
        {
            if (lex_slots_post[static_cast<size_t>(i)] != i)
            {
                log.fail("sort_and_pack did not pack lexed to front: lex_rank=" + std::to_string(i) +
                    " slot=" + std::to_string(lex_slots_post[static_cast<size_t>(i)]));
                return false;
            }
        }

        const auto loose_slots_post = hh.slots_in_loose_index_order();
        const int32_t lexN = static_cast<int32_t>(lex_slots_post.size());
        for (size_t i = 0; i < loose_slots_post.size(); ++i)
        {
            const int32_t s = loose_slots_post[i];
            if (s < lexN || s >= lexN + static_cast<int32_t>(loose_slots_post.size()))
            {
                log.fail("sort_and_pack loose slot out of packed range: slot=" + std::to_string(s));
                return false;
            }
        }

        for (int32_t s : lex_slots_post)
        {
            if (hh.rank_index_of(s) != s)
            {
                log.fail("post-pack rank_index_of(lexed) should equal slot index: slot=" + std::to_string(s));
                return false;
            }
        }
        for (int32_t s : loose_slots_post)
        {
            if (hh.rank_index_of(s) != s)
            {
                log.fail("post-pack rank_index_of(loose) should equal slot index: slot=" + std::to_string(s));
                return false;
            }
        }

        const auto empty_slots_post = hh.slots_in_empty_order_checked();
        const int32_t occN = static_cast<int32_t>(hh.size());
        for (size_t i = 0; i < empty_slots_post.size(); ++i)
        {
            const int32_t s = empty_slots_post[i];
            const int32_t expect_rank = occN + static_cast<int32_t>(i);
            if (hh.rank_index_of(s) != expect_rank)
            {
                log.fail("post-pack rank_index_of(empty) mismatch: slot=" + std::to_string(s));
                return false;
            }
        }

        if (preserve_loose)
        {
            if (loose_slots_post.size() >= 2)
            {
                bool ok = false;
                for (size_t rot = 0; rot < loose_slots_post.size(); ++rot)
                {
                    bool inc = true;
                    for (size_t i = 1; i < loose_slots_post.size(); ++i)
                    {
                        const int32_t prev = loose_slots_post[(rot + i - 1) % loose_slots_post.size()];
                        const int32_t cur = loose_slots_post[(rot + i) % loose_slots_post.size()];
                        if (cur != prev + 1) { inc = false; break; }
                    }
                    if (inc) { ok = true; break; }
                }
                if (!ok)
                {
                    log.fail("preserve_loose: expected loose traversal to be contiguous-increasing after compaction");
                    return false;
                }
            }
        }

        (void)loose_slots_pre;
    }

    return true;
}

template<typename TIndex, typename TMeta>
static bool test_fuzz_lightweight(const TOrderedConfig& cfg, TestLogger& log)
{
    using Harness = TOrderedSlots_Test<TIndex, TMeta>;

    Harness h;

    struct OpRec
    {
        uint32_t step = 0;
        const char* op = "";
        int key = 0;
        int32_t slot = -1;
        bool flag = false;
    };

    std::vector<OpRec> hist;
    hist.reserve(cfg.fuzz_history ? cfg.fuzz_history : 1u);

    auto push_hist = [&](const OpRec& r)
        {
            if (cfg.fuzz_history == 0) return;
            if (hist.size() < cfg.fuzz_history) { hist.push_back(r); return; }
            for (size_t i = 1; i < hist.size(); ++i) hist[i - 1] = hist[i];
            hist.back() = r;
        };

    auto dump_hist = [&]()
        {
            std::cerr << "FUZZ FAIL CONTEXT:\n";
            std::cerr << "  seed=" << cfg.fuzz_seed << " step=" << (hist.empty() ? 0u : hist.back().step) << "\n";
            if (cfg.fuzz_print_state)
            {
                std::cerr << "  counts: lexed=" << h.lexed_count()
                    << " loose=" << h.loose_count()
                    << " empty=" << h.empty_count()
                    << " occupied=" << h.size()
                    << "\n";
            }
            const size_t start = (hist.size() > cfg.fuzz_history) ? (hist.size() - cfg.fuzz_history) : 0;
            for (size_t i = start; i < hist.size(); ++i)
            {
                const auto& r = hist[i];
                std::cerr << "  [" << r.step << "] " << r.op
                    << " key=" << r.key
                    << " slot=" << r.slot
                    << " flag=" << (r.flag ? 1 : 0)
                    << "\n";
            }
        };

    const uint32_t cap = std::max(cfg.initial_capacity, 64u);
    if (!h.init(cap))
    {
        log.fail("initialise failed");
        return false;
    }

    std::mt19937 rng(cfg.fuzz_seed);

    auto rand_u32 = [&]() -> uint32_t { return rng(); };
    auto rand_int = [&](int lo, int hi_inclusive) -> int
        {
            const uint32_t span = static_cast<uint32_t>(hi_inclusive - lo + 1);
            return lo + static_cast<int>(rand_u32() % span);
        };

    auto maybe_validate = [&](uint32_t step) -> bool
        {
            if (cfg.fuzz_validate_every == 0) return true;
            if ((step % cfg.fuzz_validate_every) != 0) return true;
            const auto vr = h.validate_detailed(true);
            if (!(vr.integrity && vr.tree && vr.order))
            {
                push_hist({ step, "VALIDATE", 0, -1, false });
                dump_hist();

                std::cerr << "VALIDATION BREAKDOWN:\n"
                    << "  integrity=" << (vr.integrity ? 1 : 0) << "\n"
                    << "  tree=" << (vr.tree ? 1 : 0) << "\n"
                    << "  order=" << (vr.order ? 1 : 0) << "\n";

                log.fail("fuzz: validate failed at step=" + std::to_string(step));
                return false;
            }
            return true;
        };

    const auto do_compact = [&](bool preserve_loose) -> bool
        {
            if (preserve_loose)
                h.rebuild_loose_in_index_order();

            h.sort_and_compact_slots();

            if (!h.validate_all(true))
            {
                dump_hist();
                log.fail(std::string("fuzz: validate failed after sort_and_pack preserve_loose=") +
                    (preserve_loose ? "true" : "false"));
                return false;
            }
            (void)h.slots_in_empty_order_checked();
            return true;
        };

    for (uint32_t step = 1; step <= cfg.fuzz_steps; ++step)
    {
        if (cfg.fuzz_compact_every != 0 && (step % cfg.fuzz_compact_every) == 0)
        {
            const bool preserve_loose = ((rand_u32() & 1u) != 0);
            if (!do_compact(preserve_loose)) return false;
            continue;
        }

        const int op = rand_int(0, 99);

        if (op <= 39)
        {
            const int key = rand_int(0, std::max(1, cfg.fuzz_key_range) - 1);
            const bool require_unique = ((rand_u32() & 3u) == 0);
            push_hist({ step, "insert_lexed", key, -1, require_unique });
            (void)h.insert_key_lexed(key, require_unique);
        }
        else if (op <= 54)
        {
            const int key = rand_int(0, std::max(1, cfg.fuzz_key_range) - 1);
            push_hist({ step, "insert_loose", key, -1, false });
            (void)h.insert_key_loose(key);
        }
        else if (op <= 74)
        {
            const auto occ = h.slots_in_occupied_order();
            if (!occ.empty())
            {
                const int32_t s = occ[static_cast<size_t>(rand_int(0, static_cast<int>(occ.size()) - 1))];
                push_hist({ step, "erase_slot", 0, s, false });
                (void)h.erase_slot(s);
            }
            else
            {
                push_hist({ step, "erase_slot(noop)", 0, -1, false });
            }
        }
        else if (op <= 84)
        {
            const auto occ = h.slots_in_occupied_order();
            if (!occ.empty())
            {
                const int32_t s = occ[static_cast<size_t>(rand_int(0, static_cast<int>(occ.size()) - 1))];
                push_hist({ step, "lex(slot)", 0, s, false });
                (void)h.lex_slot(s);
            }
            else
            {
                push_hist({ step, "lex(noop)", 0, -1, false });
            }
        }
        else if (op <= 94)
        {
            const auto occ = h.slots_in_occupied_order();
            if (!occ.empty())
            {
                const int32_t s = occ[static_cast<size_t>(rand_int(0, static_cast<int>(occ.size()) - 1))];
                push_hist({ step, "unlex(slot)", 0, s, false });
                (void)h.unlex_slot(s);
            }
            else
            {
                push_hist({ step, "unlex(noop)", 0, -1, false });
            }
        }
        else if (op <= 97)
        {
            push_hist({ step, "relex_all", 0, -1, false });
            h.relex_all_slots();
        }
        else
        {
            push_hist({ step, "noop", 0, -1, false });
        }

        if (!maybe_validate(step)) return false;

        if ((step & 255u) == 0)
        {
            (void)h.slots_in_empty_order_checked();
        }
    }

    if (!do_compact(false)) return false;
    if (!do_compact(true)) return false;

    return true;
}

class OrderedSlotsTestKey
{
public:
    OrderedSlotsTestKey() noexcept = default;
    OrderedSlotsTestKey(const std::int32_t value) noexcept
        : m_value(value)
    {
    }

    [[nodiscard]] std::int32_t relationship(const OrderedSlotsTestKey& other) const noexcept
    {
        if (m_value < other.m_value)
        {
            return -1;
        }
        if (m_value > other.m_value)
        {
            return 1;
        }
        return 0;
    }

private:
    std::int32_t m_value = 0;
};

static bool test_pod_ordered_slots_wrapper(TestLogger& log)
{
    TPodOrderedSlots<std::int32_t, OrderedSlotsTestKey> slots;
    if (!slots.initialise(8u))
    {
        log.fail("TPodOrderedSlots initialise failed");
        return false;
    }
    if (!slots.is_valid() || !slots.is_ready() || !slots.check_integrity())
    {
        log.fail("TPodOrderedSlots failed initial integrity");
        return false;
    }
    if ((slots.key_at_slot(-1) != nullptr) || (slots.key_at_slot(0) != nullptr))
    {
        log.fail("TPodOrderedSlots exposed a key for an invalid or empty slot");
        return false;
    }
    if ((slots.memory_token_count() != 3u) ||
        (slots.memory_allocation_count() == 0u) ||
        (slots.memory_allocation_size() == 0u) ||
        !slots.can_reattribute_to() || !slots.reattribute())
    {
        log.fail("TPodOrderedSlots memory attribution audit failed");
        return false;
    }

    const std::int32_t slot_a = slots.insert(OrderedSlotsTestKey{ 3 }, 30);
    const std::int32_t slot_b = slots.insert(OrderedSlotsTestKey{ 1 }, 10);
    const std::int32_t slot_c = slots.insert(OrderedSlotsTestKey{ 2 }, 20);
    if ((slot_a < 0) || (slot_b < 0) || (slot_c < 0))
    {
        log.fail("TPodOrderedSlots insert failed");
        return false;
    }

    if (!slots.check_integrity())
    {
        log.fail("TPodOrderedSlots integrity failed after insert");
        return false;
    }

    const OrderedSlotsTestKey* const key_a = slots.key_at_slot(slot_a);
    const OrderedSlotsTestKey* const key_b = slots.key_at_slot(slot_b);
    const OrderedSlotsTestKey* const key_c = slots.key_at_slot(slot_c);
    if ((key_a == nullptr) || (key_a->relationship(OrderedSlotsTestKey{ 3 }) != 0) ||
        (key_b == nullptr) || (key_b->relationship(OrderedSlotsTestKey{ 1 }) != 0) ||
        (key_c == nullptr) || (key_c->relationship(OrderedSlotsTestKey{ 2 }) != 0))
    {
        log.fail("TPodOrderedSlots slot-to-key lookup mismatch");
        return false;
    }

    const std::int32_t* const found = slots.get_slot(OrderedSlotsTestKey{ 2 });
    if ((found == nullptr) || (*found != 20))
    {
        log.fail("TPodOrderedSlots key lookup failed");
        return false;
    }

    slots.sort_and_pack();
    if (!slots.check_integrity())
    {
        log.fail("TPodOrderedSlots integrity failed after sort_and_pack");
        return false;
    }

    const std::int32_t* const first_live = slots.get_slot(slots.first_live());
    const std::int32_t* const last_live = slots.get_slot(slots.last_live());
    const OrderedSlotsTestKey* const first_key = slots.key_at_slot(slots.first_live());
    const OrderedSlotsTestKey* const last_key = slots.key_at_slot(slots.last_live());
    if ((first_live == nullptr) || (*first_live != 10) ||
        (last_live == nullptr) || (*last_live != 30) ||
        (first_key == nullptr) || (first_key->relationship(OrderedSlotsTestKey{ 1 }) != 0) ||
        (last_key == nullptr) || (last_key->relationship(OrderedSlotsTestKey{ 3 }) != 0))
    {
        log.fail("TPodOrderedSlots traversal order mismatch");
        return false;
    }

    const std::int32_t erased_slot = slots.find_index(OrderedSlotsTestKey{ 2 });
    if ((erased_slot < 0) || !slots.erase(erased_slot) ||
        (slots.key_at_slot(erased_slot) != nullptr) || !slots.check_integrity())
    {
        log.fail("TPodOrderedSlots erase failed");
        return false;
    }

    return true;
}

// -----------------------------
// Runner
// -----------------------------
template<typename TIndex, typename TMeta>
static bool run_suite_typed(const TOrderedConfig& cfg, TestLogger& log)
{
    if (cfg.run_exhaustive_delete)
        if (!test_exhaustive_delete_orders<TIndex, TMeta>(cfg, log)) return false;

    if (cfg.run_exhaustive_insert_delete)
        if (!test_exhaustive_insert_delete_orders<TIndex, TMeta>(cfg, log)) return false;

    if (cfg.run_duplicates_and_stability)
        if (!test_duplicates_and_stable_equals<TIndex, TMeta>(cfg, log)) return false;

    if (cfg.run_find_and_bounds)
        if (!test_find_and_bounds<TIndex, TMeta>(cfg, log)) return false;

    if (cfg.run_rank_index_mapping)
        if (!test_rank_index_mapping<TIndex, TMeta>(cfg, log)) return false;

    if (cfg.run_traversal_semantics)
        if (!test_traversal_semantics<TIndex, TMeta>(cfg, log)) return false;

    if (cfg.run_sort_and_compact)
        if (!test_sort_and_compact_postconditions<TIndex, TMeta>(cfg, log)) return false;

    if (cfg.run_fuzz_lightweight)
        if (!test_fuzz_lightweight<TIndex, TMeta>(cfg, log)) return false;

    return true;
}

static void print_usage()
{
    std::cout <<
        "TOrderedSlots test harness options:\n"
        "  --fast                 Quick run (small N, limits perms, no insert+delete exhaustive)\n"
        "  --soak                 Longer run (bigger N, full perms unless limited)\n"
        "  --N=<int>              Set N for exhaustive tests\n"
        "  --vary_insertion=<0|1> Toggle subset insertion orders for exhaustive_delete\n"
        "  --max_delete_perms=<n> Limit deletion permutations (0=unlimited)\n"
        "  --max_insert_perms=<n> Limit insertion permutations (0=unlimited)\n"
        "  --max_delete_perms_each_insert=<n>\n"
        "  --test=<name>          Enable only one test (name: exhaustive_delete, exhaustive_insert_delete,\n"
        "                         duplicates, find, rankmap, traversal, pack)\n"
        "  --continue             Don't abort on first failure\n"
        "  --fuzz                 Enable lightweight fuzz\n"
        "  --seed=<n>             Fuzz seed\n"
        "  --steps=<n>            Fuzz steps\n"
        "  --key_range=<n>        Fuzz key range [0..n)\n"
        "  --validate_every=<n>   Validate every n ops (1=each op, 0=never)\n"
        "  --compact_every=<n>    Run sort_and_pack every n ops (0=never)\n";
}

static void apply_only_test(TOrderedConfig& cfg, const std::string& name)
{
    cfg.run_exhaustive_delete = false;
    cfg.run_exhaustive_insert_delete = false;
    cfg.run_duplicates_and_stability = false;
    cfg.run_find_and_bounds = false;
    cfg.run_rank_index_mapping = false;
    cfg.run_traversal_semantics = false;
    cfg.run_sort_and_compact = false;

    if (name == "exhaustive_delete") cfg.run_exhaustive_delete = true;
    else if (name == "exhaustive_insert_delete") cfg.run_exhaustive_insert_delete = true;
    else if (name == "duplicates") cfg.run_duplicates_and_stability = true;
    else if (name == "find") cfg.run_find_and_bounds = true;
    else if (name == "rankmap") cfg.run_rank_index_mapping = true;
    else if (name == "traversal") cfg.run_traversal_semantics = true;
    else if (name == "pack") cfg.run_sort_and_compact = true;
}

static TOrderedConfig parse_args(int argc, char** argv)
{
    TOrderedConfig cfg;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];

        if (a == "--help" || a == "-h") { print_usage(); std::exit(0); }
        if (a == "--continue") { cfg.stop_on_fail = false; continue; }

        if (a == "--fast")
        {
            cfg.N = 7;
            cfg.vary_insertion_orders = false;
            cfg.max_delete_perms = 3000;
            cfg.run_exhaustive_insert_delete = false;
            continue;
        }

        if (a == "--soak")
        {
            cfg.N = 9;
            cfg.vary_insertion_orders = true;
            cfg.max_delete_perms = 0;
            continue;
        }

        if (a == "--fuzz")
        {
            cfg.run_fuzz_lightweight = true;
            continue;
        }

        if (starts_with(a, "--N="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(4), v)) cfg.N = static_cast<int>(v);
            continue;
        }

        if (starts_with(a, "--vary_insertion="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--vary_insertion=")), v))
                cfg.vary_insertion_orders = (v != 0);
            continue;
        }

        if (starts_with(a, "--max_delete_perms="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--max_delete_perms=")), v))
                cfg.max_delete_perms = static_cast<uint64_t>(v);
            continue;
        }

        if (starts_with(a, "--max_insert_perms="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--max_insert_perms=")), v))
                cfg.max_insert_perms = static_cast<uint64_t>(v);
            continue;
        }

        if (starts_with(a, "--max_delete_perms_each_insert="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--max_delete_perms_each_insert=")), v))
                cfg.max_delete_perms_each_insert = static_cast<uint64_t>(v);
            continue;
        }

        if (starts_with(a, "--test="))
        {
            apply_only_test(cfg, a.substr(std::strlen("--test=")));
            continue;
        }

        if (starts_with(a, "--seed="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--seed=")), v))
                cfg.fuzz_seed = static_cast<uint32_t>(v);
            continue;
        }
        if (starts_with(a, "--steps="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--steps=")), v))
                cfg.fuzz_steps = static_cast<uint32_t>(v);
            continue;
        }
        if (starts_with(a, "--key_range="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--key_range=")), v))
                cfg.fuzz_key_range = static_cast<int>(v);
            continue;
        }
        if (starts_with(a, "--validate_every="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--validate_every=")), v))
                cfg.fuzz_validate_every = static_cast<uint32_t>(v);
            continue;
        }
        if (starts_with(a, "--compact_every="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--compact_every=")), v))
                cfg.fuzz_compact_every = static_cast<uint32_t>(v);
            continue;
        }
    }

    cfg.initial_capacity = std::max(cfg.initial_capacity, static_cast<uint32_t>(cfg.N + 4));
    return cfg;
}

int run_all_tests(const TOrderedConfig& cfg_in)
{
    TOrderedConfig cfg = cfg_in;

    TestLogger log;
    log.stop_on_fail = cfg.stop_on_fail;

    if (!run_suite_typed<int32_t, int16_t>(cfg, log))
        return 1;

    if (!run_suite_typed<int16_t, int8_t>(cfg, log))
        return 1;

    if (!test_pod_ordered_slots_wrapper(log))
        return 1;

    if (log.failures == 0)
        std::cout << "ALL TESTS PASSED\n";
    else
        std::cout << "TESTS FAILED count=" << log.failures << "\n";

    return (log.failures == 0) ? 0 : 1;
}

#if TORDERED_TESTHARNESS_WITH_MAIN
int main(int argc, char** argv)
{
    if (!test_environment::install())
    {
        return 1;
    }

    const TOrderedConfig cfg = parse_args(argc, argv);
    return run_all_tests(cfg);
}
#endif
