
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  TUnorderedSlots_test_harness.cpp
//
//  Standalone configurable test harness for TUnorderedSlots<TSlotBacking, TIndex>.
//
//  Build (Clang/GCC):
//    clang++ -std=c++14 -O0 -g TUnorderedSlots_test_harness.cpp -o tun_test
//    g++     -std=c++14 -O0 -g TUnorderedSlots_test_harness.cpp -o tun_test
//
//  Build (MSVC):
//    cl /std:c++14 /EHsc /O0 /Zi TUnorderedSlots_test_harness.cpp
//
//  Run:
//    ./tun_test --fast
//    ./tun_test --soak
//    ./tun_test --fuzz --seed=123 --steps=20000

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <random>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include "containers/TPodUnorderedSlots.hpp"
#include "containers/slots/TUnorderedSlots.hpp"
#include "tests/test_suites/TUnorderedSlots_test_harness.hpp"

#ifndef TUNORDERED_TESTHARNESS_WITH_MAIN
#define TUNORDERED_TESTHARNESS_WITH_MAIN    1
#endif

// -----------------------------
// Debug-break / fail policy
// -----------------------------
#if defined(_MSC_VER)
#   define TUN_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#   define TUN_DEBUG_BREAK() __builtin_trap()
#else
#   define TUN_DEBUG_BREAK() std::abort()
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
        if (debug_break_on_fail) { TUN_DEBUG_BREAK(); }
        if (stop_on_fail) { std::abort(); }
    }
};

// Hard-fail helper for contract violations inside slot-backing operations or harness adapter invariants.
// (Intentionally ignores stop_on_fail and always terminates.)
[[noreturn]] static void hard_fail_contract(const char* msg) noexcept
{
    std::cerr << "FAIL (contract): " << msg << "\n";
    TUN_DEBUG_BREAK();
    std::abort();
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

static std::string vec_i32_to_string(const std::vector<int32_t>& v)
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

// -----------------------------
// Harness: derived test adapter
// -----------------------------
template<typename TIndex>
class TUnorderedSlots_TestStorage
{
public:
    void on_move_payload(const int32_t source_index, const int32_t target_index) noexcept;
    [[nodiscard]] uint32_t on_reserve_empty(const uint32_t minimum_capacity, const uint32_t recommended_capacity) noexcept;
    void ensure_payload_capacity_matches_slots(const uint32_t capacity) noexcept;

protected:
    std::vector<int> m_payload;
    bool m_temp_valid = false;
    int m_temp = 0;
};

template<typename TIndex>
class TUnorderedSlots_Test final : private slots::TUnorderedSlots<TUnorderedSlots_TestStorage<TIndex>, TIndex>
{
    using SlotBacking = TUnorderedSlots_TestStorage<TIndex>;
    using Base = slots::TUnorderedSlots<SlotBacking, TIndex>;

public:
    TUnorderedSlots_Test() noexcept
        : m_next_value(1)
    {
    }

    bool init(uint32_t cap) noexcept
    {
        m_next_value = 1;
        this->m_temp_valid = false;
        this->m_temp = 0;

        if (!Base::initialise(cap))
            return false;

        this->ensure_payload_capacity_matches_slots(Base::capacity());
        // Initialize payload to a recognizable pattern (not required, but helps debug).
        for (size_t i = 0; i < this->m_payload.size(); ++i) this->m_payload[i] = 0;
        return true;
    }

    void shutdown() noexcept
    {
        (void)Base::shutdown();
        this->m_payload.clear();
        m_next_value = 1;
        this->m_temp_valid = false;
        this->m_temp = 0;
    }

    // Expose safe accessors
    bool is_initialised() const noexcept { return Base::is_initialised(); }
    uint32_t cap() const noexcept { return Base::capacity(); }
    uint32_t cap_limit() const noexcept { return Base::capacity_limit(); }
    uint32_t index_limit() const noexcept { return Base::index_limit(); }
    uint32_t min_safe_capacity() const noexcept { return Base::minimum_safe_capacity(); }

    uint32_t loose_count() const noexcept { return Base::loose_count(); }
    uint32_t empty_count() const noexcept { return Base::empty_count(); }
    int32_t high_index() const noexcept { return Base::high_index(); }
    uint32_t peak_usage() const noexcept { return Base::peak_usage(); }
    int32_t peak_index() const noexcept { return Base::peak_index(); }

    bool check_integrity() const noexcept { return Base::check_integrity(); }

    // Mutating interface (protected in base)
    bool clear() noexcept
    {
        const bool ok = Base::clear();
        if (ok)
        {
            this->ensure_payload_capacity_matches_slots(Base::capacity());
            std::fill(this->m_payload.begin(), this->m_payload.end(), 0);
        }
        return ok;
    }

    bool resize(uint32_t requested_capacity) noexcept
    {
        const uint32_t old_cap = Base::capacity();
        const bool ok = Base::safe_resize(requested_capacity);
        if (ok && Base::capacity() != old_cap)
        {
            this->ensure_payload_capacity_matches_slots(Base::capacity());
            // New elements are already zeroed by vector::resize if grown.
        }
        return ok;
    }

    bool reserve(uint32_t slot_count) noexcept
    {
        const uint32_t old_cap = Base::capacity();
        const bool ok = Base::reserve_empty(slot_count);
        if (ok && Base::capacity() != old_cap)
        {
            this->ensure_payload_capacity_matches_slots(Base::capacity());
        }
        return ok;
    }

    bool shrink_to_fit() noexcept
    {
        const uint32_t old_cap = Base::capacity();
        const bool ok = Base::shrink_to_fit();
        if (ok && Base::capacity() != old_cap)
        {
            this->ensure_payload_capacity_matches_slots(Base::capacity());
        }
        return ok;
    }

    int32_t acquire(int32_t slot_index = -1) noexcept
    {
        const int32_t idx = Base::acquire(slot_index);
        if (idx >= 0)
        {
            this->ensure_payload_capacity_matches_slots(Base::capacity());
            this->m_payload[static_cast<size_t>(idx)] = m_next_value++;
        }
        return idx;
    }

    int32_t reserve_and_acquire(int32_t slot_index = -1) noexcept
    {
        const int32_t idx = Base::reserve_and_acquire(slot_index);
        if (idx >= 0)
        {
            this->ensure_payload_capacity_matches_slots(Base::capacity());
            this->m_payload[static_cast<size_t>(idx)] = m_next_value++;
        }
        return idx;
    }

    bool erase(int32_t slot_index) noexcept
    {
        const bool ok = Base::erase(slot_index);
        if (ok)
        {
            this->ensure_payload_capacity_matches_slots(Base::capacity());
            if (static_cast<uint32_t>(slot_index) < Base::capacity())
                this->m_payload[static_cast<size_t>(slot_index)] = 0;
        }
        return ok;
    }

    bool is_loose_slot(int32_t slot_index) const noexcept { return Base::is_loose_slot(slot_index); }
    bool is_empty_slot(int32_t slot_index) const noexcept { return Base::is_empty_slot(slot_index); }

    int32_t first_loose() const noexcept { return Base::first_loose(); }
    int32_t last_loose() const noexcept { return Base::last_loose(); }
    int32_t next_loose(int32_t slot_index) const noexcept { return Base::next_loose(slot_index); }
    int32_t prev_loose(int32_t slot_index) const noexcept { return Base::prev_loose(slot_index); }

    void pack() noexcept
    {
        // Snapshot current loose payload in expected packing order: ascending source index (implementation-defined).
        m_expected_after_compact.clear();
        m_expected_after_compact.reserve(Base::loose_count());
        const uint32_t c = Base::capacity();
        for (uint32_t i = 0; i < c; ++i)
        {
            if (Base::is_loose_slot(static_cast<int32_t>(i)))
                m_expected_after_compact.push_back(this->m_payload[i]);
        }

        Base::pack();

        // After pack, payload should be packed to [0..loose_count), preserving ascending source index scan order.
        this->ensure_payload_capacity_matches_slots(Base::capacity());
    }

    void rebuild_loose_in_index_order() noexcept { Base::rebuild_loose_in_index_order(); }
    void rebuild_empty_in_index_order() noexcept { Base::rebuild_empty_in_index_order(); }

    // Traverse circular loose list by count (CRITICAL requirement).
    // Returns slot indices in traversal order and enforces circularity + uniqueness within that traversal.
    std::vector<int32_t> loose_slots_by_count_checked() const noexcept
    {
        std::vector<int32_t> out;
        const uint32_t n = Base::loose_count();
        out.reserve(n);

        if (n == 0) return out;

        const int32_t head = Base::first_loose();
        if (head < 0) hard_fail_contract("loose_slots_by_count_checked: loose_count>0 but first_loose()<0");

        int32_t idx = head;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (idx < 0) hard_fail_contract("loose_slots_by_count_checked: encountered -1 during traversal");

            // Must be loose
            if (!Base::is_loose_slot(idx)) hard_fail_contract("loose_slots_by_count_checked: non-loose slot in loose traversal");

            // No duplicates within the n steps
            for (size_t j = 0; j < out.size(); ++j)
                if (out[j] == idx) hard_fail_contract("loose_slots_by_count_checked: duplicate slot encountered");

            out.push_back(idx);
            idx = Base::next_loose(idx);
        }

        // Must return to head after exactly n steps
        if (idx != -1) hard_fail_contract("loose_slots_by_count_checked: did not return to head after loose_count() steps");

        return out;
    }

    // Traverse the empty list by count and validate category and uniqueness.
    std::vector<int32_t> empty_slots_by_count_checked() const noexcept
    {
        std::vector<int32_t> out;
        const uint32_t n = Base::empty_count();
        out.reserve(n);

        if (n == 0) return out;

        int32_t slot_index = Base::first_empty();
        if (slot_index < 0) hard_fail_contract("empty_slots_by_count_checked: empty_count>0 but first_empty()<0");

        for (uint32_t i = 0; i < n; ++i)
        {
            if (!Base::is_empty_slot(slot_index)) hard_fail_contract("empty_slots_by_count_checked: non-empty slot in empty traversal");
            for (size_t j = 0; j < out.size(); ++j)
                if (out[j] == slot_index) hard_fail_contract("empty_slots_by_count_checked: duplicate slot_index observed");
            out.push_back(slot_index);
            slot_index = Base::next_empty(slot_index);
        }

        if (slot_index != -1) hard_fail_contract("empty_slots_by_count_checked: traversal exceeded empty_count()");

        slot_index = Base::last_empty();
        if (slot_index != out.back()) hard_fail_contract("empty_slots_by_count_checked: last_empty() mismatch");
        for (size_t i = out.size(); i != 0; --i)
        {
            if (slot_index != out[i - 1u]) hard_fail_contract("empty_slots_by_count_checked: reverse traversal mismatch");
            slot_index = Base::prev_empty(slot_index);
        }
        if (slot_index != -1) hard_fail_contract("empty_slots_by_count_checked: reverse traversal exceeded empty_count()");

        return out;
    }

    // Post-pack payload expectations
    bool check_compact_payload_postcondition() const noexcept
    {
        const uint32_t n = Base::loose_count();
        if (m_expected_after_compact.size() != static_cast<size_t>(n))
            return false;

        for (uint32_t i = 0; i < n; ++i)
        {
            if (this->m_payload[static_cast<size_t>(i)] != m_expected_after_compact[static_cast<size_t>(i)])
                return false;
        }
        return true;
    }

    // Verify pack's metadata postconditions:
    // - Loose are [0..loose_count)
    // - Empty are [loose_count..capacity)
    // - high_index == loose_count-1 (or -1 if none)
    bool check_compact_metadata_postcondition() const noexcept
    {
        const uint32_t c = Base::capacity();
        const uint32_t n = Base::loose_count();

        for (uint32_t i = 0; i < c; ++i)
        {
            const bool loose = Base::is_loose_slot(static_cast<int32_t>(i));
            const bool empty = Base::is_empty_slot(static_cast<int32_t>(i));
            if (loose == empty) return false; // must be exactly one

            if (i < n)
            {
                if (!loose) return false;
            }
            else
            {
                if (!empty) return false;
            }
        }

        const int32_t expected_hi = (n == 0) ? -1 : static_cast<int32_t>(n - 1u);
        if (Base::high_index() != expected_hi) return false;

        return true;
    }

    // Convenience: scan all loose slot indices by brute force.
    std::vector<int32_t> loose_slots_by_scan() const
    {
        std::vector<int32_t> out;
        out.reserve(Base::loose_count());
        const uint32_t c = Base::capacity();
        for (uint32_t i = 0; i < c; ++i)
            if (Base::is_loose_slot(static_cast<int32_t>(i))) out.push_back(static_cast<int32_t>(i));
        return out;
    }

private:
    int m_next_value;
    std::vector<int> m_expected_after_compact;
};

template<typename TIndex>
void TUnorderedSlots_TestStorage<TIndex>::on_move_payload(const int32_t source_index, const int32_t target_index) noexcept
{
    if (source_index == target_index)
        hard_fail_contract("on_move_payload: source == target");

    const bool src_is_temp = (source_index < 0);
    const bool dst_is_temp = (target_index < 0);

    if (src_is_temp && dst_is_temp)
        hard_fail_contract("on_move_payload: both are temp");

    ensure_payload_capacity_matches_slots(static_cast<uint32_t>(m_payload.size()));

    if (source_index < 0)
    {
        if (!m_temp_valid)
            hard_fail_contract("on_move_payload: temp->target but temp invalid");
        m_payload[static_cast<size_t>(target_index)] = m_temp;
        m_temp_valid = false;
        return;
    }

    if (target_index < 0)
    {
        m_temp = m_payload[static_cast<size_t>(source_index)];
        m_temp_valid = true;
        return;
    }

    m_payload[static_cast<size_t>(target_index)] = m_payload[static_cast<size_t>(source_index)];
}

template<typename TIndex>
uint32_t TUnorderedSlots_TestStorage<TIndex>::on_reserve_empty(
    const uint32_t minimum_capacity,
    const uint32_t recommended_capacity) noexcept
{
    const uint32_t chosen = (recommended_capacity >= minimum_capacity) ? recommended_capacity : minimum_capacity;
    m_payload.resize(static_cast<size_t>(chosen));
    return chosen;
}

template<typename TIndex>
void TUnorderedSlots_TestStorage<TIndex>::ensure_payload_capacity_matches_slots(const uint32_t capacity) noexcept
{
    if (m_payload.size() != static_cast<size_t>(capacity))
        m_payload.resize(static_cast<size_t>(capacity));
}

// -----------------------------
// Tests
// -----------------------------
template<typename TIndex>
static bool test_smoke(const TUnorderedConfig& cfg, TestLogger& log)
{
    (void)cfg;
    using H = TUnorderedSlots_Test<TIndex>;

    H h;
    if (!h.init(cfg.initial_capacity))
    {
        log.fail("initialise failed");
        return false;
    }

    if (!h.check_integrity())
    {
        log.fail("integrity failed immediately after init");
        return false;
    }

    // Acquire a few (head)
    const int32_t a0 = h.acquire(-1);
    const int32_t a1 = h.acquire(-1);
    const int32_t a2 = h.acquire(-1);
    if (a0 < 0 || a1 < 0 || a2 < 0)
    {
        log.fail("acquire failed in smoke");
        return false;
    }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after acquires");
        return false;
    }

    // Erase one
    if (!h.erase(a1))
    {
        log.fail("erase failed in smoke");
        return false;
    }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after erase");
        return false;
    }

    // Clear resets state
    if (!h.clear())
    {
        log.fail("clear failed");
        return false;
    }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after clear");
        return false;
    }

    // Clear -> should be all empty
    if (h.loose_count() != 0 || h.empty_count() != h.cap())
    {
        log.fail("counts mismatch after clear");
        return false;
    }

    // Shutdown should be clean
    h.shutdown();
    return true;
}

template<typename TIndex>
static bool test_traversal_semantics(const TUnorderedConfig& cfg, TestLogger& log)
{
    (void)cfg;
    using H = TUnorderedSlots_Test<TIndex>;

    H h;
    if (!h.init(std::max<uint32_t>(cfg.initial_capacity, 16u)))
    {
        log.fail("initialise failed");
        return false;
    }

    // Acquire 5 (head), erase 2 to create mix and gaps.
    std::vector<int32_t> acquired;
    for (int i = 0; i < 5; ++i)
    {
        const int32_t idx = h.acquire(-1);
        if (idx < 0) { log.fail("acquire failed"); return false; }
        acquired.push_back(idx);
    }
    if (!h.erase(acquired[1])) { log.fail("erase failed"); return false; }
    if (!h.erase(acquired[3])) { log.fail("erase failed"); return false; }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after setup");
        return false;
    }

    // Loose traversal must be circular; verify list traversal count matches loose_count and equals scan set.
    const std::vector<int32_t> loose_by_count = h.loose_slots_by_count_checked();
    const std::vector<int32_t> loose_by_scan = h.loose_slots_by_scan();
    {
        auto a = loose_by_count;
        auto b = loose_by_scan;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b)
        {
            log.fail("loose traversal mismatch: by_count=" + vec_i32_to_string(loose_by_count) +
                " by_scan=" + vec_i32_to_string(loose_by_scan));
            return false;
        }
    }

    const std::vector<int32_t> empty_by_count = h.empty_slots_by_count_checked();
    if ((loose_by_count.size() + empty_by_count.size()) != static_cast<size_t>(h.cap()))
    {
        log.fail("traversal count does not match capacity");
        return false;
    }

    return true;
}

template<typename TIndex>
static bool test_compact_postconditions(const TUnorderedConfig& cfg, TestLogger& log)
{
    (void)cfg;
    using H = TUnorderedSlots_Test<TIndex>;

    H h;
    if (!h.init(std::max<uint32_t>(cfg.initial_capacity, 32u)))
    {
        log.fail("initialise failed");
        return false;
    }

    // Create sparse occupancy with gaps by acquiring specific indices (with reserve), then erasing some.
    // We purposefully create out-of-order and gaps to exercise pack() moves.
    const int32_t i5 = h.reserve_and_acquire(5);
    const int32_t i0 = h.reserve_and_acquire(0);
    const int32_t i9 = h.reserve_and_acquire(9);
    const int32_t i2 = h.reserve_and_acquire(2);
    const int32_t i7 = h.reserve_and_acquire(7);

    if (i5 != 5 || i0 != 0 || i9 != 9 || i2 != 2 || i7 != 7)
    {
        log.fail("reserve_and_acquire did not acquire requested indices (expected direct indices)");
        return false;
    }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after setup acquires");
        return false;
    }

    // Erase a couple to create holes among loose
    if (!h.erase(2)) { log.fail("erase(2) failed"); return false; }
    if (!h.erase(7)) { log.fail("erase(7) failed"); return false; }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after erases");
        return false;
    }

    const uint32_t before_loose = h.loose_count();
    const uint32_t before_empty = h.empty_count();
    (void)before_empty;

    h.pack();

    if (!h.check_integrity())
    {
        log.fail("integrity failed after pack");
        return false;
    }

    // Metadata postconditions per docs.
    if (!h.check_compact_metadata_postcondition())
    {
        log.fail("pack metadata postcondition failed");
        return false;
    }

    // Payload postcondition under the harness' assumption of pack order.
    if (!h.check_compact_payload_postcondition())
    {
        log.fail("pack payload postcondition failed (expected ascending source-index pack order)");
        return false;
    }

    // Loose count should be preserved; empty_count should be capacity-loose_count
    if (h.loose_count() != before_loose)
    {
        log.fail("pack changed loose_count");
        return false;
    }
    if (h.empty_count() != h.cap() - h.loose_count())
    {
        log.fail("pack empty_count mismatch after pack");
        return false;
    }

    // Loose traversal circularity still must hold and match scan set
    const auto loose_by_count = h.loose_slots_by_count_checked();
    const auto loose_by_scan = h.loose_slots_by_scan();
    {
        auto a = loose_by_count;
        auto b = loose_by_scan;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b)
        {
            log.fail("post-pack loose traversal mismatch");
            return false;
        }
    }

    // Empty-list traversal count check.
    (void)h.empty_slots_by_count_checked();

    return true;
}

template<typename TIndex>
static bool test_resize_reserve(const TUnorderedConfig& cfg, TestLogger& log)
{
    using H = TUnorderedSlots_Test<TIndex>;

    H h;
    if (!h.init(std::max<uint32_t>(cfg.initial_capacity, 8u)))
    {
        log.fail("initialise failed");
        return false;
    }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after init");
        return false;
    }

    // Acquire a few so minimum_safe_capacity becomes > 0
    const int32_t a = h.acquire(3);
    const int32_t b = h.acquire(1);
    if (a != 3 || b != 1)
    {
        // acquire may fail if capacity < requested+1, but we used initial >= 8, so it should work.
        log.fail("acquire specific failed in resize_reserve");
        return false;
    }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after specific acquires");
        return false;
    }

    // shrink_to_fit should shrink to high_index+1
    const uint32_t old_cap = h.cap();
    const uint32_t expected_min = h.min_safe_capacity();
    (void)old_cap;

    // shrink_to_fit can soft-fail if high_index < 0; here it is >=0.
    const bool stf = h.shrink_to_fit();
    if (!stf)
    {
        // Some implementations may choose to soft-fail; treat as unexpected here given the provided code.
        log.fail("shrink_to_fit unexpectedly failed");
        return false;
    }

    if (h.cap() != expected_min)
    {
        log.fail("shrink_to_fit did not set capacity to minimum_safe_capacity()");
        return false;
    }

    if (!h.check_integrity())
    {
        log.fail("integrity failed after shrink_to_fit");
        return false;
    }

    // reserve: request enough empties; should return true without changing cap
    const uint32_t cap_before = h.cap();
    const uint32_t empties = h.empty_count();
    const uint32_t want = (empties > 0) ? std::min<uint32_t>(empties, 2u) : 0u;
    if (want > 0)
    {
        if (!h.reserve(want))
        {
            log.fail("reserve should succeed when enough empties exist");
            return false;
        }
        if (h.cap() != cap_before)
        {
            log.fail("reserve changed capacity despite enough empties");
            return false;
        }
    }

    // resize smaller than minimum_safe_capacity should fail
    if (expected_min > 0)
    {
        const bool bad = h.resize(expected_min - 1);
        if (bad)
        {
            log.fail("resize below minimum_safe_capacity unexpectedly succeeded");
            return false;
        }
    }

    // resize larger should succeed
    const bool grow = h.resize(h.cap() + 10u);
    if (!grow)
    {
        log.fail("resize grow failed");
        return false;
    }
    if (!h.check_integrity())
    {
        log.fail("integrity failed after resize grow");
        return false;
    }

    return true;
}

static bool test_pod_unordered_slots_smoke(TestLogger& log)
{
    TPodUnorderedSlots<int> slots;
    if (!slots.initialise(8u))
    {
        log.fail("TPodUnorderedSlots initialise failed");
        return false;
    }
    if ((slots.memory_attribution().token_count != 2u) ||
        (slots.memory_attribution().allocation_count == 0u) ||
        (slots.memory_attribution().allocation_size == 0u) ||
        !memory::can_reattribute_to(slots) || !memory::reattribute(slots))
    {
        log.fail("TPodUnorderedSlots memory attribution audit failed");
        return false;
    }

    const int32_t a = slots.insert(11);
    const int32_t b = slots.insert(22);
    if ((a < 0) || (b < 0))
    {
        log.fail("TPodUnorderedSlots insert failed");
        return false;
    }

    const int* const a_slot = slots.get_slot(a);
    const int* const b_slot = slots.get_slot(b);
    if ((a_slot == nullptr) || (*a_slot != 11) || (b_slot == nullptr) || (*b_slot != 22))
    {
        log.fail("TPodUnorderedSlots payload lookup failed");
        return false;
    }

    if (!slots.erase(a))
    {
        log.fail("TPodUnorderedSlots erase failed");
        return false;
    }

    slots.pack();
    if (!slots.check_integrity())
    {
        log.fail("TPodUnorderedSlots integrity failed");
        return false;
    }

    slots.deallocate();
    if (!slots.initialise(4u))
    {
        log.fail("TPodUnorderedSlots reinitialise failed");
        return false;
    }

    slots.deallocate();
    return true;
}

// -----------------------------
// Fuzz
// -----------------------------
template<typename TIndex>
static bool test_fuzz(const TUnorderedConfig& cfg, TestLogger& log)
{
    using H = TUnorderedSlots_Test<TIndex>;

    H h;

    struct OpRec
    {
        uint32_t step = 0;
        const char* op = "";
        int32_t a = 0;
        uint32_t b = 0;
        bool ok = false;
    };

    std::vector<OpRec> hist;
    hist.reserve(cfg.fuzz_history ? cfg.fuzz_history : 1u);

    auto push_hist = [&](const OpRec& r)
        {
            if (cfg.fuzz_history == 0) return;
            if (hist.size() < cfg.fuzz_history) { hist.push_back(r); return; }
            // ring buffer: drop oldest
            for (size_t i = 1; i < hist.size(); ++i) hist[i - 1] = hist[i];
            hist.back() = r;
        };

    auto dump_hist = [&]()
        {
            std::cerr << "FUZZ FAIL CONTEXT:\n";
            std::cerr << "  seed=" << cfg.fuzz_seed << " step=" << (hist.empty() ? 0u : hist.back().step) << "\n";
            if (cfg.fuzz_print_state)
            {
                std::cerr << "  cap=" << h.cap()
                    << " loose=" << h.loose_count()
                    << " empty=" << h.empty_count()
                    << " high=" << h.high_index()
                    << " peak_usage=" << h.peak_usage()
                    << " peak_index=" << h.peak_index()
                    << "\n";
            }
            for (size_t i = 0; i < hist.size(); ++i)
            {
                const auto& r = hist[i];
                std::cerr << "  [" << r.step << "] " << r.op
                    << " a=" << r.a
                    << " b=" << r.b
                    << " ok=" << (r.ok ? 1 : 0)
                    << "\n";
            }
        };

    auto validate = [&](uint32_t step) -> bool
        {
            if (cfg.fuzz_validate_every == 0) return true;
            if ((step % cfg.fuzz_validate_every) != 0) return true;

            if (!h.check_integrity())
            {
                push_hist({ step, "check_integrity", 0, 0u, false });
                dump_hist();
                log.fail("fuzz: integrity failed at step=" + std::to_string(step));
                return false;
            }

            // Extra checks: loose- and empty-list traversal boundedness.
            // Respect circularity: traverse loose by count only.
            (void)h.loose_slots_by_count_checked();
            (void)h.empty_slots_by_count_checked();

            // Count sanity
            if (h.loose_count() + h.empty_count() != h.cap())
            {
                push_hist({ step, "count_sanity", 0, 0u, false });
                dump_hist();
                log.fail("fuzz: loose+empty != capacity at step=" + std::to_string(step));
                return false;
            }

            return true;
        };

    const uint32_t init_cap = std::max<uint32_t>(cfg.initial_capacity, 8u);
    if (!h.init(init_cap))
    {
        log.fail("fuzz: initialise failed");
        return false;
    }

    std::mt19937 rng(cfg.fuzz_seed);

    auto rand_u32 = [&]() -> uint32_t { return rng(); };

    auto choose_weighted = [&](uint32_t total) -> uint32_t
        {
            if (total == 0) return 0;
            return (rand_u32() % total);
        };

    auto rand_inclusive = [&](uint32_t lo, uint32_t hi) -> uint32_t
        {
            if (hi < lo) return lo;
            const uint32_t span = (hi - lo) + 1u;
            return lo + (rand_u32() % span);
        };

    // Helper to pick a random loose slot (by traversal list)
    auto pick_random_loose = [&]() -> int32_t
        {
            const uint32_t n = h.loose_count();
            if (n == 0) return -1;
            const auto slots = h.loose_slots_by_count_checked();
            const uint32_t k = rand_u32() % n;
            return slots[static_cast<size_t>(k)];
        };

    uint32_t total_w = 0;
    total_w += cfg.w_acquire_head;
    total_w += cfg.w_reserve_and_acquire_head;
    total_w += cfg.w_acquire_specific;
    total_w += cfg.w_erase_random;
    total_w += cfg.w_resize;
    total_w += cfg.w_shrink_to_fit;
    total_w += cfg.w_rebuild_loose;
    total_w += cfg.w_rebuild_empty;
    total_w += cfg.w_compact;

    for (uint32_t step = 1; step <= cfg.fuzz_steps; ++step)
    {
        // Periodic pack stress
        if (cfg.fuzz_compact_every != 0 && (step % cfg.fuzz_compact_every) == 0)
        {
            std::cout << "Fuzz step " << step << " of " << cfg.fuzz_steps << "\n";
            h.pack();
            push_hist({ step, "pack(periodic)", 0, 0u, true });
            if (!validate(step)) return false;
            continue;
        }

        const uint32_t r = choose_weighted(total_w);

        uint32_t cursor = 0;

        auto in_bucket = [&](uint32_t w) -> bool
            {
                const bool hit = (r >= cursor) && (r < cursor + w);
                cursor += w;
                return hit;
            };

        if (in_bucket(cfg.w_acquire_head))
        {
            const int32_t idx = h.acquire(-1);
            const bool ok = (idx >= 0);
            push_hist({ step, "acquire(-1)", idx, 0u, ok });
        }
        else if (in_bucket(cfg.w_reserve_and_acquire_head))
        {
            const int32_t idx = h.reserve_and_acquire(-1);
            const bool ok = (idx >= 0);
            push_hist({ step, "reserve_and_acquire(-1)", idx, 0u, ok });
        }
        else if (in_bucket(cfg.w_acquire_specific))
        {
            // Choose a potentially out-of-range index to test soft-fail vs reserve behavior (acquire won't reserve).
            const uint32_t cap = h.cap();
            const uint32_t max_try = std::min<uint32_t>(cap + 16u, h.cap_limit() ? (h.cap_limit() - 1u) : cap);
            const int32_t want = static_cast<int32_t>(rand_inclusive(0u, max_try));
            const int32_t idx = h.acquire(want);
            const bool ok = (idx == want);
            push_hist({ step, "acquire(idx)", idx, static_cast<uint32_t>(want), ok });
        }
        else if (in_bucket(cfg.w_erase_random))
        {
            const int32_t s = pick_random_loose();
            bool ok = false;
            if (s >= 0) ok = h.erase(s);
            push_hist({ step, "erase(random_loose)", s, 0u, ok });
        }
        else if (in_bucket(cfg.w_resize))
        {
            const uint32_t want = rand_inclusive(cfg.fuzz_resize_min, cfg.fuzz_resize_max);
            const bool ok = h.resize(want);
            push_hist({ step, "resize", 0, want, ok });
        }
        else if (in_bucket(cfg.w_shrink_to_fit))
        {
            const bool ok = h.shrink_to_fit();
            push_hist({ step, "shrink_to_fit", 0, 0u, ok });
        }
        else if (in_bucket(cfg.w_rebuild_loose))
        {
            h.rebuild_loose_in_index_order();
            push_hist({ step, "rebuild_loose_in_index_order", 0, 0u, true });
        }
        else if (in_bucket(cfg.w_rebuild_empty))
        {
            h.rebuild_empty_in_index_order();
            push_hist({ step, "rebuild_empty_in_index_order", 0, 0u, true });
        }
        else
        {
            h.pack();
            push_hist({ step, "pack", 0, 0u, true });

            // After pack, check postconditions (metadata always; payload order under harness assumption).
            if (!h.check_integrity())
            {
                dump_hist();
                log.fail("fuzz: integrity failed immediately after pack at step=" + std::to_string(step));
                return false;
            }
            if (!h.check_compact_metadata_postcondition())
            {
                dump_hist();
                log.fail("fuzz: pack metadata postcondition failed at step=" + std::to_string(step));
                return false;
            }
            if (!h.check_compact_payload_postcondition())
            {
                dump_hist();
                log.fail("fuzz: pack payload postcondition failed at step=" + std::to_string(step));
                return false;
            }
        }

        if (!validate(step)) return false;
    }

    // Final strong checks
    h.pack();
    if (!h.check_integrity())
    {
        dump_hist();
        log.fail("fuzz: integrity failed after final pack");
        return false;
    }
    if (!h.check_compact_metadata_postcondition())
    {
        dump_hist();
        log.fail("fuzz: final pack metadata postcondition failed");
        return false;
    }
    if (!h.check_compact_payload_postcondition())
    {
        dump_hist();
        log.fail("fuzz: final pack payload postcondition failed");
        return false;
    }

    return true;
}

// -----------------------------
// Runner
// -----------------------------
static void print_usage()
{
    std::cout <<
        "TUnorderedSlots test harness options:\n"
        "  --fast                 Quick run (no fuzz)\n"
        "  --soak                 Run all non-fuzz tests\n"
        "  --fuzz                 Enable deterministic fuzz\n"
        "  --seed=<n>             Fuzz seed\n"
        "  --steps=<n>            Fuzz steps\n"
        "  --validate_every=<n>   Validate every n ops (1=each op, 0=never)\n"
        "  --compact_every=<n>    Periodic pack every n ops (0=never)\n"
        "  --cap=<n>              Initial capacity\n"
        "  --continue             Don't abort on first failure\n";
}

static TUnorderedConfig parse_args(int argc, char** argv)
{
    TUnorderedConfig cfg;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];

        if (a == "--help" || a == "-h") { print_usage(); std::exit(0); }
        if (a == "--continue") { cfg.stop_on_fail = false; continue; }

        if (a == "--fast")
        {
            cfg.run_smoke = true;
            cfg.run_traversal_semantics = true;
            cfg.run_compact_postconditions = true;
            cfg.run_resize_reserve = true;
            cfg.run_fuzz = false;
            cfg.initial_capacity = 32;
            continue;
        }

        if (a == "--soak")
        {
            cfg.run_smoke = true;
            cfg.run_traversal_semantics = true;
            cfg.run_compact_postconditions = true;
            cfg.run_resize_reserve = true;
            cfg.run_fuzz = false;
            cfg.initial_capacity = 128;
            continue;
        }

        if (a == "--fuzz")
        {
            cfg.run_fuzz = true;
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
        if (starts_with(a, "--cap="))
        {
            int64_t v = 0;
            if (parse_i64(a.substr(std::strlen("--cap=")), v))
                cfg.initial_capacity = static_cast<uint32_t>(v);
            continue;
        }
    }

    cfg.initial_capacity = std::max<uint32_t>(cfg.initial_capacity, 1u);
    return cfg;
}

template<typename TIndex>
static bool run_suite_typed(const TUnorderedConfig& cfg, TestLogger& log)
{
    if (cfg.run_smoke)
    {
        std::cout << "UNORDERED SMOKE TEST STARTED\n";
        if (!test_smoke<TIndex>(cfg, log)) return false;
        std::cout << "UNORDERED SMOKE TEST PASSED\n";
    }

    if (cfg.run_traversal_semantics)
    {
        std::cout << "UNORDERED TRAVERSAL SEMANTICS TEST STARTED\n";
        if (!test_traversal_semantics<TIndex>(cfg, log)) return false;
        std::cout << "UNORDERED TRAVERSAL SEMANTICS TEST PASSED\n";
    }

    if (cfg.run_compact_postconditions)
    {
        std::cout << "UNORDERED COMPACT POSTCONDITIONS TEST STARTED\n";
        if (!test_compact_postconditions<TIndex>(cfg, log)) return false;
        std::cout << "UNORDERED COMPACT POSTCONDITIONS TEST PASSED\n";
    }

    if (cfg.run_resize_reserve)
    {
        std::cout << "UNORDERED RESISE RESERVE TEST STARTED\n";
        if (!test_resize_reserve<TIndex>(cfg, log)) return false;
        std::cout << "UNORDERED RESISE RESERVE TEST PASSED\n";
    }

    if (cfg.run_fuzz)
    {
        std::cout << "UNORDERED FUZZ TEST STARTED\n";
        if (!test_fuzz<TIndex>(cfg, log)) return false;
        std::cout << "UNORDERED FUZZ TEST PASSED\n";
    }

    return true;
}

int run_all_tests(const TUnorderedConfig& cfg_in)
{
    TUnorderedConfig cfg = cfg_in;

    TestLogger log;
    log.stop_on_fail = cfg.stop_on_fail;

    // Run both supported types
    if (!run_suite_typed<int32_t>(cfg, log)) return 1;
    if (!run_suite_typed<int16_t>(cfg, log)) return 1;
    if (!test_pod_unordered_slots_smoke(log)) return 1;

    if (log.failures == 0)
        std::cout << "ALL TESTS PASSED\n";
    else
        std::cout << "TESTS FAILED count=" << log.failures << "\n";

    return (log.failures == 0) ? 0 : 1;
}

#if TUNORDERED_TESTHARNESS_WITH_MAIN
int main(int argc, char** argv)
{
    const TUnorderedConfig cfg = parse_args(argc, argv);
    return run_all_tests(cfg);
}
#endif
