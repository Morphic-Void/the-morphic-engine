
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TOrderedSlots.hpp
//  Author: Ritchie Brannan
//  Date:   10 Jan 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Metadata-only ordering and slot-management toolkit over an
//  externally owned slot-aligned payload domain.
//
//  Does not own payload, keys, or capacity policy.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  "lexed" means ordered by the derived key comparator.
//
//  Ordering is entirely defined by the derived class comparator.
//
//  Traversal order defines rank order.
//
//  Migration notes
//  ---------------
//  Do not assume occupied-only rank semantics from TUnorderedSlots.
//
//  Rank is full-domain. Ordered, loose, and empty slots all participate
//  in rank and remapping.
//
//  See docs/containers/slots/TOrderedSlots.md for the full documentation.

#pragma once

#ifndef TORDERED_SLOTS_HPP_INCLUDED
#define TORDERED_SLOTS_HPP_INCLUDED

#include <algorithm>    //  std::fill_n, std::max, std::min
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::int8_t, std::int16_t, std::int32_t, std::uint32_t, std::uintptr_t
#include <cstring>      //  std::memcpy
#include <limits>       //  std::numeric_limits
#include <type_traits>  //  std::is_trivially_copyable_v, std::is_signed_v, std::is_same_v
#include <utility>      //  std::move

#include "SlotsRankMap.hpp"
#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"
#include "debug/macros.hpp"

namespace slots
{

/// An ordered index over slot metadata for externally stored payload items.
///
/// TOrderedSlots stores only slot metadata (tree/list links, balance, occupancy).
/// The derived class owns the payload items, defines how keys are compared and
/// how payload items are moved between slots.
///
/// See docs/TOrderedSlots.md for full terminology and usage patterns.
template<typename TSlotBacking, typename TIndex = std::int32_t, typename TMeta = std::int16_t>
class TOrderedSlots : protected TSlotBacking
{
public:
    TOrderedSlots() noexcept = default;
    TOrderedSlots(TOrderedSlots&& src) noexcept;
    TOrderedSlots(const TOrderedSlots& src) noexcept;
    TOrderedSlots(const std::uint32_t capacity) noexcept { (void)initialise(capacity); }
    ~TOrderedSlots() noexcept { (void)shutdown(); }

public:

    //  The derived class is expected to provide the public interface.

protected:

    //  Protected functions form the derived-facing interface.

private:

    //  Private functions are implementation details.

protected:

    TOrderedSlots& operator=(TOrderedSlots&& src) noexcept;
    TOrderedSlots& operator=(const TOrderedSlots& src) noexcept;

    [[nodiscard]] bool take(TOrderedSlots& src) noexcept;
    [[nodiscard]] bool clone(const TOrderedSlots& src) noexcept;

    //  Limits
    [[nodiscard]] static constexpr std::uint32_t index_limit() { return k_index_limit; }
    [[nodiscard]] static constexpr std::uint32_t capacity_limit() { return k_capacity_limit; }

    //  Status
    [[nodiscard]] bool is_initialised() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;

    //  Simple accessors
    [[nodiscard]] std::uint32_t capacity() const noexcept;
    [[nodiscard]] std::uint32_t minimum_safe_capacity() const noexcept;
    [[nodiscard]] std::uint32_t peak_usage() const noexcept;
    [[nodiscard]] std::int32_t  peak_index() const noexcept;
    [[nodiscard]] std::int32_t  high_index() const noexcept;
    [[nodiscard]] std::uint32_t lexed_count() const noexcept;
    [[nodiscard]] std::uint32_t loose_count() const noexcept;
    [[nodiscard]] std::uint32_t empty_count() const noexcept;
    [[nodiscard]] std::uint32_t occupied_count() const noexcept;

    //  Reset all management state and return every slot to the empty category.
    [[nodiscard]] bool clear() noexcept;

    //  Deallocate all management data and reset to the uninitialised state.
    [[nodiscard]] bool shutdown() noexcept;

    //  Initialise or re-initialise management data.
    //
    //  Calls shutdown() and then allocates and initialises the metadata domain.
    [[nodiscard]] bool initialise(const std::uint32_t capacity = 32) noexcept;

    //  Capacity management.
    //
    //  These functions preserve existing slot categories and do not reorder payload.
    [[nodiscard]] bool safe_resize(const std::uint32_t requested_capacity) noexcept;
    [[nodiscard]] bool reserve_empty(const std::uint32_t slot_count) noexcept;
    [[nodiscard]] bool shrink_to_fit() noexcept;

    //  Acquire an empty slot into the requested occupied category.
    //
    //  slot_index == -1 selects the default empty slot.
    //  acquire() does not grow capacity.
    //  reserve_and_acquire() may grow capacity first.
    //  Both return the acquired slot index, or -1 on failure.
    [[nodiscard]] std::int32_t acquire(const std::int32_t slot_index = -1, const bool lex = false, const bool require_unique = false) noexcept;
    [[nodiscard]] std::int32_t reserve_and_acquire(const std::int32_t slot_index = -1, const bool lex = false, const bool require_unique = false) noexcept;

    //  Return an occupied slot to the empty category.
    //
    //  Payload handling is the responsibility of the derived class.
    //  Returns false if slot_index is invalid or not occupied.
    bool erase(const std::int32_t slot_index) noexcept;

    //  Slot-category queries by slot index.
    //
    //  is_safe_slot() checks that the structure is safe and
    //  slot_index is in [0, capacity()).
    //
    //  is_occupied(), is_lexed_slot(), is_loose_slot(), and is_empty_slot()
    //  add the corresponding category check.
    [[nodiscard]] bool is_occupied(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] bool is_safe_slot(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] bool is_lexed_slot(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] bool is_loose_slot(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] bool is_empty_slot(const std::int32_t slot_index) const noexcept;

    //  Lexed traversal by slot index.
    //
    //  These functions operate in lex order over the lexed subset only.
    //
    //  first_lexed()/last_lexed() return the first/last lexed slot, or -1 if there
    //  are no lexed slots or the structure is not safe.
    //
    //  prev_lexed()/next_lexed() return the adjacent lexed slot in lex order,
    //  or -1 if slot_index is invalid, not lexed, or at the end of traversal.
    [[nodiscard]] std::int32_t first_lexed() const noexcept;
    [[nodiscard]] std::int32_t last_lexed() const noexcept;
    [[nodiscard]] std::int32_t prev_lexed(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::int32_t next_lexed(const std::int32_t slot_index) const noexcept;

    //  Loose-list traversal by slot index.
    //
    //  These functions operate in loose-list order only.
    //  Loose-list order is traversal order for loose slots, but not lex order.
    //
    //  first_loose()/last_loose() return the list head/tail, or -1 if there
    //  are no loose slots or the structure is not safe.
    //
    //  prev_loose()/next_loose() return the adjacent loose slot in list order,
    //  or -1 if slot_index is invalid, not loose, or at the end of the list.
    [[nodiscard]] std::int32_t first_loose() const noexcept;
    [[nodiscard]] std::int32_t last_loose() const noexcept;
    [[nodiscard]] std::int32_t prev_loose(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::int32_t next_loose(const std::int32_t slot_index) const noexcept;

    //  Empty-list traversal by slot index.
    //
    //  These functions mirror loose-list traversal for the empty-slot domain.
    [[nodiscard]] std::int32_t first_empty() const noexcept;
    [[nodiscard]] std::int32_t last_empty() const noexcept;
    [[nodiscard]] std::int32_t prev_empty(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::int32_t next_empty(const std::int32_t slot_index) const noexcept;

    //  Duplicate-key queries.
    //
    //  slot_index == -1 compares the currently staged query key.
    //  Otherwise the specified slot key is used.
    //
    //  The category-specific variants restrict the search domain accordingly.
    [[nodiscard]] bool has_duplicate_key(const std::int32_t slot_index = -1) const noexcept;
    [[nodiscard]] bool has_duplicate_key_in_lexed(const std::int32_t slot_index = -1) const noexcept;
    [[nodiscard]] bool has_duplicate_key_in_loose(const std::int32_t slot_index = -1) const noexcept;

    //  Move a loose slot into the lexed tree.
    //
    //  Returns false if slot_index is invalid or not loose.
    bool lex(const std::int32_t slot_index) noexcept;

    //  Move a lexed slot into the loose list.
    //
    //  Returns false if slot_index is invalid or not lexed.
    bool unlex(const std::int32_t slot_index) noexcept;

    //  Remove and re-insert a lexed slot in the lexed tree.
    //
    //  Use this when a slot key has changed, or may have changed.
    //  Returns false if slot_index is invalid or not lexed.
    bool relex(const std::int32_t slot_index) noexcept;

    //  Move all loose slots into the lexed tree.
    void lex_all() noexcept;

    //  Move all lexed slots into the loose list.
    void unlex_all() noexcept;

    //  Remove and re-insert all lexed slots in the lexed tree.
    //
    //  Use this when multiple slot keys have changed, or may have changed.
    //  This may also marginally improve tree shape.
    void relex_all() noexcept;

    //  Build a full-domain rank/slot mapping for the current slot state.
    //
    //  Traversal order is lexed, then loose, then empty.
    //  RankMap size is capacity().
    //  Both rank_to_slot and slot_to_rank are defined over the full domain.
    //
    //  On failure, or if no mapping can be built, the returned RankMap is empty.
    [[nodiscard]] RankMap build_rank_map() const noexcept;

    //  Physically reorder payload into canonical packed order.
    //
    //  After completion:
    //      - Lexed payload occupies slot indices [0, lexed_count()) in lex order.
    //      - Loose payload occupies slot indices [lexed_count(), lexed_count() + loose_count()).
    //      - Remaining slots are Empty.
    //      - Lexed metadata is rebuilt as a balanced AVL tree.
    //      - Loose and empty metadata are rebuilt as linear lists.
    //
    //  Uses derived payload movement to coordinate reordering.
    //  In-place mode uses cycle resolution with derived temporary storage (-1).
    //  External mode performs a single pass to a complete external payload domain.
    //
    //  See docs/TOrderedSlots.md for further detail.
    void sort_and_pack(const bool use_external_payload = false) noexcept;

    //  Rebuild list order in ascending slot index order.
    //
    //  For loose slots, index order and rank order are identical after rebuild.
    void rebuild_loose_in_index_order() noexcept;
    void rebuild_empty_in_index_order() noexcept;

    //  Return the full-domain rank of a slot by slot index.
    //
    //  Rank is defined by traversal order: lexed, then loose, then empty.
    //  Returns -1 if slot_index is invalid.
    [[nodiscard]] std::int32_t rank_index_of(const std::int32_t slot_index) const noexcept;

    //  Return the slot index by full-domain rank.
    //
    //  Valid rank domain is [0, capacity()).
    //  Returns -1 if rank_index is out of range.
    [[nodiscard]] std::int32_t find_by_rank_index(const std::int32_t rank_index) const noexcept;

    //  Lexed search and bound queries.
    //
    //  These functions search the lexed subset only.
    //  They use the staged query key against slot_index, where -1 denotes the derived
    //  class's currently staged query key.
    //
    //  They return a matching or bound slot index, or -1 if no such lexed slot exists.
    [[nodiscard]] std::int32_t find_any_equal() const noexcept;
    [[nodiscard]] std::int32_t find_first_equal() const noexcept;
    [[nodiscard]] std::int32_t find_first_greater() const noexcept;
    [[nodiscard]] std::int32_t find_first_greater_equal() const noexcept;
    [[nodiscard]] std::int32_t find_last_equal() const noexcept;
    [[nodiscard]] std::int32_t find_last_less() const noexcept;
    [[nodiscard]] std::int32_t find_last_less_equal() const noexcept;

    //  Bound-query aliases.
    //
    //  lower_bound_by_lex() aliases find_first_greater_equal().
    //  upper_bound_by_lex() aliases find_first_greater().
    [[nodiscard]] std::int32_t lower_bound_by_lex() const noexcept;
    [[nodiscard]] std::int32_t upper_bound_by_lex() const noexcept;

    //  Tree-shape diagnostics over the lexed AVL subset.
    [[nodiscard]] std::uint32_t tree_height() const noexcept;
    [[nodiscard]] std::uint32_t tree_weight() const noexcept;

    //  Validate lexed-tree structure in stable state.
    //
    //  Validates AVL structure and balance.
    //  If lex_check is not LexCheck::None, also validates comparator-defined
    //  in-order semantics via the derived comparator.
    enum class LexCheck : std::int32_t { InOrder = 0, Unique = 1, None = 2 };
    [[nodiscard]] bool validate_tree(const LexCheck lex_check = LexCheck::None) const noexcept;

    //  Validate metadata integrity in stable state.
    //
    //  Validates metadata invariants, counts, list structure, tree balance,
    //  and index ranges. Comparator-defined lex order is not checked here.
    [[nodiscard]] bool check_integrity() const noexcept;

//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe all owned backing storage without changing its attribution.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace all owned contexts, including unallocated members.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

private:

    inline [[nodiscard]] bool is_safe(const bool allow_null = false) const noexcept;
    inline [[nodiscard]] TSlotBacking& slot_backing() noexcept;
    inline [[nodiscard]] const TSlotBacking& slot_backing() const noexcept;

private:

    //  Private slot metadata structures.

    enum class SlotState : TMeta
    {
        is_unassigned = static_cast<TMeta>(~std::uint32_t(0) / 0x01u),   // 0xffffffff
        is_empty_slot = static_cast<TMeta>(~std::uint32_t(0) / 0x03u),   // 0x55555555
        is_loose_slot = static_cast<TMeta>(~std::uint32_t(0) / 0x05u),   // 0x33333333
        is_lexed_slot = static_cast<TMeta>(~std::uint32_t(0) / 0x0fu)    // 0x11111111
    };

    struct Slot
    {   //  Slot meta data structure
        TIndex  parent_index;
        TIndex  child_index[2];
        TMeta   balance_factor;
        TMeta   state;

        inline void set_slot_state(const SlotState slot_state) noexcept { state = static_cast<TMeta>(slot_state); }

        inline void set_is_unassigned() noexcept { set_slot_state(SlotState::is_unassigned); }
        inline void set_is_empty_slot() noexcept { set_slot_state(SlotState::is_empty_slot); }
        inline void set_is_loose_slot() noexcept { set_slot_state(SlotState::is_loose_slot); }
        inline void set_is_lexed_slot() noexcept { set_slot_state(SlotState::is_lexed_slot); }

        constexpr SlotState get_slot_state() const noexcept { return static_cast<SlotState>(state); }

        constexpr bool is_unassigned() const noexcept { return get_slot_state() == SlotState::is_unassigned; }
        constexpr bool is_empty_slot() const noexcept { return get_slot_state() == SlotState::is_empty_slot; }
        constexpr bool is_loose_slot() const noexcept { return get_slot_state() == SlotState::is_loose_slot; }
        constexpr bool is_lexed_slot() const noexcept { return get_slot_state() == SlotState::is_lexed_slot; }

        constexpr bool is_lexed_root() const noexcept { return parent_index < 0; }
        constexpr bool is_lexed_leaf() const noexcept { return (child_index[0] & child_index[1]) < 0; }
        constexpr bool is_lexed_twig() const noexcept { return (child_index[0] ^ child_index[1]) < 0; }
        constexpr bool is_lexed_stem() const noexcept { return (child_index[0] | child_index[1]) >= 0; }

        constexpr bool is_occupied() const noexcept;
        constexpr std::uint32_t get_child_mask() const noexcept;
    };

    //  Typed access helpers for slot metadata storage.
    [[nodiscard]] Slot* meta_slots() noexcept { return static_cast<Slot*>(m_meta_slot_array.data()); }
    [[nodiscard]] const Slot* meta_slots() const noexcept { return static_cast<const Slot*>(m_meta_slot_array.data()); }

private:

    //  Private AVL management functions

    std::int32_t avl_single_rotate(const std::int32_t slot_index, const std::int32_t heavy_side) noexcept;

    std::int32_t avl_double_rotate(const std::int32_t slot_index, const std::int32_t heavy_side) noexcept;

    //  Insert a slot into the lexed AVL subset.
    //
    //  key_index is forwarded as the source operand to the derived comparator.
    //  It may be -1 or a slot index.
    void avl_insert(const std::int32_t slot_index, const std::int32_t key_index) noexcept;

    //  Insert a slot into the lexed AVL subset using its own key.
    void avl_insert(const std::int32_t slot_index) noexcept;

    //  Remove a slot from the lexed AVL subset.
    void avl_remove(const std::int32_t slot_index) noexcept;

private:

    //  Private implementation functions.

    //  Capacity growth recommendation.
    static inline std::uint32_t apply_growth_policy(const std::uint32_t capacity) noexcept;

    //  Tree validation helpers.
    //
    //  Return subtree height on success, or -1 on failure.
    static inline [[nodiscard]] std::int32_t failed_validate_subtree() noexcept;
    [[nodiscard]] std::int32_t private_validate_subtree(const std::int32_t slot_index, const LexCheck lex_check = LexCheck::None) const noexcept;

    //  Integrity-check helpers.
    static inline [[nodiscard]] bool failed_integrity_check() noexcept;
    [[nodiscard]] bool private_integrity_check() const noexcept;

    //  Resize implementation after precondition validation.
    [[nodiscard]] bool private_resize(const std::uint32_t requested_capacity) noexcept;

    //  Acquire implementation with optional reservation.
    //
    //  May optionally require key uniqueness and may optionally reserve capacity first.
    [[nodiscard]] std::int32_t private_acquire(const std::int32_t slot_index, const bool lex, const bool require_unique, const bool allow_reserve) noexcept;

    //  Lexed in-order navigation helpers.
    [[nodiscard]] std::int32_t private_prev_lexed(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::int32_t private_next_lexed(const std::int32_t slot_index) const noexcept;

    //  Duplicate-key helper queries.
    [[nodiscard]] bool private_has_duplicate_key(const std::int32_t slot_index = -1) const noexcept;
    [[nodiscard]] bool private_has_duplicate_key_in_lexed(const std::int32_t slot_index = -1) const noexcept;
    [[nodiscard]] bool private_has_duplicate_key_in_loose(const std::int32_t slot_index = -1) const noexcept;

    //  Implementation of sort_and_pack().
    //
    //  Reorders payload into canonical packed order and rebuilds metadata.
    void private_sort_and_compact(const bool use_external_payload = false) noexcept;

    //  Build a balanced lexed subtree over an inclusive slot-index range.
    [[nodiscard]] std::int32_t build_balanced_subtree(const std::int32_t lower_index, const std::int32_t upper_index, const std::int32_t parent_index) noexcept;

    //  Convert the lexed tree into an ordered circular bi-directional list.
    //
    //  Uses Slot::child_index[] as prev/next links.
    //  Returns the list head, or -1 if there are no lexed slots.
    [[nodiscard]] std::int32_t lexed_to_list() noexcept;

    //  Scan an inclusive slot-index range and build a bi-directional list of slots
    //  in the specified category.
    //  The caller is responsible for correcting any orphaned list members.
    //  Returns the list head, or -1 if no matching slots are found.
    [[nodiscard]] std::int32_t state_to_list(const std::int32_t lower_index, const std::int32_t upper_index, const SlotState state) noexcept;

    //  Convert an inclusive slot-index range to a bi-directional list.
    //  The caller must ensure these slots are not currently managed.
    //  Returns the list head, or -1 if the range is empty or invalid.
    [[nodiscard]] std::int32_t range_to_list(const std::int32_t lower_index, const std::int32_t upper_index, const SlotState state) noexcept;

    //  Concatenate two bi-directional lists by appending list2 to list1.
    //  Returns the head of the combined list.
    [[nodiscard]] std::int32_t combine_lists(const std::int32_t list1_head_index, const std::int32_t list2_head_index) noexcept;

    //  Write sequential ordinals into parent_index over a list traversal.
    void set_list_ordinals(const std::int32_t list_index, const std::uint32_t list_count, const std::int32_t ordinal_start) noexcept;

    //  Append a slot-index range to the loose or empty list.
    //  The caller must ensure these slots are not currently managed.
    void append_range_to_loose_list(const std::int32_t lower_index, const std::int32_t upper_index) noexcept;
    void append_range_to_empty_list(const std::int32_t lower_index, const std::int32_t upper_index) noexcept;

    //  Internal helpers for the move_to_* functions.
    void attach_to_lexed(const std::int32_t slot_index, const std::int32_t key_index) noexcept;
    void attach_to_lexed(const std::int32_t slot_index) noexcept;
    void attach_to_loose(const std::int32_t slot_index) noexcept;
    void attach_to_empty(const std::int32_t slot_index) noexcept;
    void remove_from_lexed(const std::int32_t slot_index) noexcept;
    void remove_from_loose(const std::int32_t slot_index) noexcept;
    void remove_from_empty(const std::int32_t slot_index) noexcept;

    //  Move a slot to the specified metadata category if not already a member.
    void move_to_lexed_tree(const std::int32_t slot_index, const std::int32_t key_index) noexcept;
    void move_to_lexed_tree(const std::int32_t slot_index) noexcept;
    void move_to_loose_list(const std::int32_t slot_index) noexcept;
    void move_to_empty_list(const std::int32_t slot_index) noexcept;

    //  Convert a slot index to full-domain rank.
    //
    //  Rank is defined by traversal order: lexed, then loose, then empty.
    //  Returns -1 if the slot is invalid.
    [[nodiscard]] std::int32_t convert_to_rank_index(const std::int32_t slot_index) const noexcept;

    //  Locate a slot by full-domain rank.
    //
    //  Valid rank domain is [0, capacity()).
    //  Returns the corresponding slot index, or -1 if rank_index is out of range.
    [[nodiscard]] std::int32_t locate_by_rank_index(const std::int32_t rank_index) const noexcept;

    //  Search the lexed tree using the current staged query key via the derived comparator.
    //
    //  key_index may be -1 to indicate the staged query key.
    //  Returns a lexed slot index, or -1 if no matching/bound slot exists.
    [[nodiscard]] std::int32_t locate_any_equal(const std::int32_t key_index = -1) const noexcept;
    [[nodiscard]] std::int32_t locate_first_equal(const std::int32_t key_index = -1) const noexcept;
    [[nodiscard]] std::int32_t locate_first_greater(const std::int32_t key_index = -1) const noexcept;
    [[nodiscard]] std::int32_t locate_first_greater_equal(const std::int32_t key_index = -1) const noexcept;
    [[nodiscard]] std::int32_t locate_last_equal(const std::int32_t key_index = -1) const noexcept;
    [[nodiscard]] std::int32_t locate_last_less(const std::int32_t key_index = -1) const noexcept;
    [[nodiscard]] std::int32_t locate_last_less_equal(const std::int32_t key_index = -1) const noexcept;

    //  Scan for the lowest/highest occupied slot index in the metadata array.
    [[nodiscard]] std::int32_t min_occupied_index() const noexcept;
    [[nodiscard]] std::int32_t max_occupied_index() const noexcept;

    //  Tree-shape diagnostics over metadata only.
    [[nodiscard]] std::uint32_t subtree_height(const std::int32_t slot_index) const noexcept;
    [[nodiscard]] std::uint32_t subtree_weight(const std::int32_t slot_index) const noexcept;

    //  These functions should only be called on construction or after shutdown().
    bool move_from(TOrderedSlots& src) noexcept;
    bool copy_from(const TOrderedSlots& src) noexcept;
    void set_empty() noexcept;

private:

    //  Private data.
    std::uint32_t m_capacity = 0u;          //  allocated slot count
    std::uint32_t m_peak_usage = 0u;        //  peak occupied slot count (loose + lexed)
    std::int32_t  m_peak_index = -1;        //  peak occupied slot index
    std::int32_t  m_high_index = -1;        //  highest currently occupied index
    std::uint32_t m_lexed_count = 0u;       //  count of slots in the lexed tree
    std::uint32_t m_loose_count = 0u;       //  count of slots in the loose list
    std::uint32_t m_empty_count = 0u;       //  count of slots in the empty list
    std::int32_t  m_lexed_tree_root = -1;   //  index of the lexed slot tree root (or -1)
    std::int32_t  m_loose_list_head = -1;   //  index of the loose slot list head (or -1)
    std::int32_t  m_empty_list_head = -1;   //  index of the empty slot list head (or -1)

    memory::CMemoryToken m_meta_slot_array{ sizeof(Slot), memory::t_default_align<Slot>() };  //  slot meta data array

    //  Constants
    static constexpr std::uint32_t k_capacity_limit =
        static_cast<std::uint32_t>(std::min(memory::t_max_elements<Slot>(), static_cast<std::size_t>(std::numeric_limits<TIndex>::max() + 1u)));

    static constexpr std::int32_t k_index_limit = static_cast<std::int32_t>(k_capacity_limit - 1u);

private:

    //  Private static_assert section.

    //  Verify that Slot is trivially copyable (it should be, so just defending against compiler variants)
    static_assert(std::is_trivially_copyable_v<Slot>,
        "TOrderedSlots: Slot must be trivially copyable.");

    static_assert((sizeof(Slot) <= 0xffffu),
        "TOrderedSlots: Slot metadata stride exceeds the memory token stride field.");

    //  Enforce std::size_t has at least 32 bits
    static_assert((sizeof(std::size_t) >= sizeof(std::uint32_t)),
        "TOrderedSlots: std::size_t must be at least 32 bits.");

    //  Enforce signed integer types so that negative sentinels and sign-based comparisons behave correctly
    static_assert(std::is_signed_v<TIndex> && std::is_signed_v<TMeta>,
        "TOrderedSlots: TIndex and TMeta must be signed integer types.");

    //  Enforce 2:1 index-to-metadata size ratio for predictable Slot layout and packing
    static_assert(((sizeof(TMeta) * 2) == sizeof(TIndex)),
        "TOrderedSlots: sizeof(TMeta) must be exactly half of sizeof(TIndex).");

    //  Enforce the only supported type pairs
    static_assert((
        (std::is_same_v<TIndex, std::int32_t> && std::is_same_v<TMeta, std::int16_t>) ||
        (std::is_same_v<TIndex, std::int16_t> && std::is_same_v<TMeta, std::int8_t>)),
        "TOrderedSlots: Supported type pairs are (std::int32_t,std::int16_t) and (std::int16_t,std::int8_t).");

};

//==============================================================================
//  TOrderedSlots<TSlotBacking, TIndex, TMeta> out of class function bodies
//==============================================================================

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline TOrderedSlots<TSlotBacking, TIndex, TMeta>::TOrderedSlots(TOrderedSlots&& src) noexcept : TSlotBacking(std::move(src))
{
    set_empty();
    (void)move_from(src);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline TOrderedSlots<TSlotBacking, TIndex, TMeta>::TOrderedSlots(const TOrderedSlots& src) noexcept : TSlotBacking(src)
{
    set_empty();
    (void)copy_from(src);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
constexpr bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::Slot::is_occupied() const noexcept
{
    const SlotState state = get_slot_state();
    return (state == SlotState::is_loose_slot) || (state == SlotState::is_lexed_slot);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
constexpr std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::Slot::get_child_mask() const noexcept
{
    return
        ((static_cast<std::uint32_t>(child_index[0]) >> 31) & 1u) |
        ((static_cast<std::uint32_t>(child_index[1]) >> 30) & 2u);
}

//! Protected function bodies

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline TOrderedSlots<TSlotBacking, TIndex, TMeta>& TOrderedSlots<TSlotBacking, TIndex, TMeta>::operator=(TOrderedSlots&& src) noexcept
{
    if (this != &src)
    {
        (void)shutdown();
        slot_backing() = std::move(src.slot_backing());
        (void)move_from(src);
    }
    return *this;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline TOrderedSlots<TSlotBacking, TIndex, TMeta>& TOrderedSlots<TSlotBacking, TIndex, TMeta>::operator=(const TOrderedSlots& src) noexcept
{
    if (this != &src)
    {
        (void)shutdown();
        slot_backing() = src.slot_backing();
        (void)copy_from(src);
    }
    return *this;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::take(TOrderedSlots& src) noexcept
{
    if (!shutdown())
    {
        return false;
    }
    slot_backing() = std::move(src.slot_backing());
    return move_from(src);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::clone(const TOrderedSlots& src) noexcept
{
    if (!shutdown())
    {
        return false;
    }
    slot_backing() = src.slot_backing();
    return copy_from(src);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::is_initialised() const noexcept
{
    return meta_slots() != nullptr;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::is_empty() const noexcept
{
    return occupied_count() == 0u;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::capacity() const noexcept
{
    return m_capacity;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::minimum_safe_capacity() const noexcept
{
    return static_cast<std::uint32_t>(m_high_index) + 1u;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::peak_usage() const noexcept
{
    return m_peak_usage;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::peak_index() const noexcept
{
    return m_peak_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::high_index() const noexcept
{
    return m_high_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::lexed_count() const noexcept
{
    return m_lexed_count;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::loose_count() const noexcept
{
    return m_loose_count;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::empty_count() const noexcept
{
    return m_empty_count;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::occupied_count() const noexcept
{
    return m_lexed_count + m_loose_count;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::clear() noexcept
{
    if (is_safe())
    {
        m_peak_usage = 0;
        m_peak_index = -1;
        m_high_index = -1;
        m_lexed_count = 0;
        m_loose_count = 0;
        m_empty_count = m_capacity;
        m_lexed_tree_root = -1;
        m_loose_list_head = -1;
        m_empty_list_head = range_to_list(0, static_cast<std::int32_t>(m_empty_count - 1), SlotState::is_empty_slot);
        return true;
    }
    return false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::shutdown() noexcept
{
    if (is_safe(true))
    {
        m_meta_slot_array.deallocate();
        set_empty();
        return true;
    }
    return false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::initialise(const std::uint32_t capacity) noexcept
{
    return shutdown() ? private_resize(capacity) : false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::safe_resize(const std::uint32_t requested_capacity) noexcept
{
    return is_safe(true) ? private_resize(requested_capacity) : false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::reserve_empty(const std::uint32_t slot_count) noexcept
{
    bool reserved = false;
    if (is_safe(true))
    {
        if (m_empty_count >= slot_count)
        {
            reserved = true;
        }
        else
        {
            std::uint32_t slot_limit = k_capacity_limit - m_lexed_count - m_loose_count;
            if (slot_limit >= slot_count)
            {
                std::uint32_t minimum_capacity = m_lexed_count + m_loose_count + slot_count;
                std::uint32_t reserve_capacity = slot_backing().on_reserve_empty(minimum_capacity, apply_growth_policy(minimum_capacity));
                if (reserve_capacity >= minimum_capacity)
                {
                    reserved = private_resize(reserve_capacity);
                }
            }
        }
    }
    return reserved;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::shrink_to_fit() noexcept
{
    return (is_safe() && (m_high_index >= 0)) ? private_resize(static_cast<std::uint32_t>(m_high_index) + 1u) : false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::acquire(const std::int32_t slot_index, const bool lex, const bool require_unique) noexcept
{
    return is_safe() ? private_acquire(slot_index, lex, require_unique, false) : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::reserve_and_acquire(const std::int32_t slot_index, const bool lex, const bool require_unique) noexcept
{
    return is_safe() ? private_acquire(slot_index, lex, require_unique, true) : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::erase(const std::int32_t slot_index) noexcept
{
    if (is_occupied(slot_index))
    {
        move_to_empty_list(slot_index);
        if (m_high_index == slot_index)
        {
            if ((m_lexed_count + m_loose_count) == 0)
            {
                m_high_index = -1;
            }
            else
            {
                const Slot* const meta = meta_slots();
                for (--m_high_index; !meta[m_high_index].is_occupied(); --m_high_index) {}
            }
        }
        return true;
    }
    return false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::is_occupied(const std::int32_t slot_index) const noexcept
{
    return is_safe_slot(slot_index) && meta_slots()[slot_index].is_occupied();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::is_safe_slot(const std::int32_t slot_index) const noexcept
{
    return is_safe() && (static_cast<std::uint32_t>(slot_index) < m_capacity);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::is_lexed_slot(const std::int32_t slot_index) const noexcept
{
    return is_safe_slot(slot_index) && meta_slots()[slot_index].is_lexed_slot();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::is_loose_slot(const std::int32_t slot_index) const noexcept
{
    return is_safe_slot(slot_index) && meta_slots()[slot_index].is_loose_slot();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::is_empty_slot(const std::int32_t slot_index) const noexcept
{
    return is_safe_slot(slot_index) && meta_slots()[slot_index].is_empty_slot();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::first_lexed() const noexcept
{
    std::int32_t first_index = -1;
    if (is_safe())
    {
        const Slot* const meta = meta_slots();
        for (std::int32_t scan_index = m_lexed_tree_root; scan_index >= 0; scan_index = meta[first_index].child_index[0]) first_index = scan_index;
    }
    return first_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::last_lexed() const noexcept
{
    std::int32_t last_index = -1;
    if (is_safe())
    {
        const Slot* const meta = meta_slots();
        for (std::int32_t scan_index = m_lexed_tree_root; scan_index >= 0; scan_index = meta[last_index].child_index[1]) last_index = scan_index;
    }
    return last_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::prev_lexed(const std::int32_t slot_index) const noexcept
{
    return is_lexed_slot(slot_index) ? private_prev_lexed(slot_index) : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::next_lexed(const std::int32_t slot_index) const noexcept
{
    return is_lexed_slot(slot_index) ? private_next_lexed(slot_index) : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::first_loose() const noexcept
{
    return is_safe() ? m_loose_list_head : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::last_loose() const noexcept
{
    return (is_safe() && (m_loose_list_head != -1)) ? meta_slots()[m_loose_list_head].child_index[0] : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::prev_loose(const std::int32_t slot_index) const noexcept
{
    if (is_loose_slot(slot_index) && (slot_index != m_loose_list_head))
    {
        return meta_slots()[slot_index].child_index[0];
    }
    return -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::next_loose(const std::int32_t slot_index) const noexcept
{
    if (is_loose_slot(slot_index))
    {
        const std::int32_t next_index = meta_slots()[slot_index].child_index[1];
        if (next_index != m_loose_list_head)
        {
            return next_index;
        }
    }
    return -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::first_empty() const noexcept
{
    return is_safe() ? m_empty_list_head : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::last_empty() const noexcept
{
    return (is_safe() && (m_empty_list_head != -1)) ? meta_slots()[m_empty_list_head].child_index[0] : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::prev_empty(const std::int32_t slot_index) const noexcept
{
    if (is_empty_slot(slot_index) && (slot_index != m_empty_list_head))
    {
        return meta_slots()[slot_index].child_index[0];
    }
    return -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::next_empty(const std::int32_t slot_index) const noexcept
{
    if (is_empty_slot(slot_index))
    {
        const std::int32_t next_index = meta_slots()[slot_index].child_index[1];
        if (next_index != m_empty_list_head)
        {
            return next_index;
        }
    }
    return -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::has_duplicate_key(const std::int32_t slot_index) const noexcept
{
    return is_safe() ? private_has_duplicate_key(slot_index) : false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::has_duplicate_key_in_lexed(const std::int32_t slot_index) const noexcept
{
    return is_safe() ? private_has_duplicate_key_in_lexed(slot_index) : false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::has_duplicate_key_in_loose(const std::int32_t slot_index) const noexcept
{
    return is_safe() ? private_has_duplicate_key_in_loose(slot_index) : false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::lex(const std::int32_t slot_index) noexcept
{
    if (is_loose_slot(slot_index))
    {
        move_to_lexed_tree(slot_index);
        return true;
    }
    return false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::unlex(const std::int32_t slot_index) noexcept
{
    if (is_lexed_slot(slot_index))
    {
        move_to_loose_list(slot_index);
        return true;
    }
    return false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::relex(const std::int32_t slot_index) noexcept
{
    if (is_lexed_slot(slot_index))
    {
        avl_remove(slot_index);
        Slot& slot = meta_slots()[slot_index];
        slot.parent_index = -1;
        slot.child_index[0] = slot.child_index[1] = -1;
        slot.balance_factor = 0;
        avl_insert(slot_index);
        return true;
    }
    return false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::lex_all() noexcept
{
    if (is_safe() && (m_loose_count != 0))
    {
        Slot* const meta = meta_slots();
        std::int32_t slot_index = m_loose_list_head;
        meta[meta[slot_index].child_index[0]].child_index[1] = -1;
        while (slot_index != -1)
        {
            Slot& slot = meta[slot_index];
            std::int32_t next_index = slot.child_index[1];
            slot.child_index[0] = slot.child_index[1] = -1;
            slot.set_is_lexed_slot();
            avl_insert(slot_index);
            slot_index = next_index;
        }
        m_loose_list_head = -1;
        m_lexed_count += m_loose_count;
        m_loose_count = 0;
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::unlex_all() noexcept
{
    if (is_safe() && (m_lexed_count != 0))
    {
        Slot* const meta = meta_slots();
        std::int32_t lexed_list_head = lexed_to_list();
        std::int32_t slot_index = lexed_list_head;
        for (std::uint32_t slot_count = m_lexed_count; slot_count != 0; --slot_count)
        {
            Slot& slot = meta[slot_index];
            slot.set_is_loose_slot();
            slot_index = slot.child_index[1];
        }
        m_loose_list_head = combine_lists(m_loose_list_head, lexed_list_head);
        m_lexed_tree_root = -1;
        m_loose_count += m_lexed_count;
        m_lexed_count = 0;
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::relex_all() noexcept
{
    if (is_safe() && (m_lexed_count != 0))
    {
        Slot* const meta = meta_slots();
        std::int32_t lexed_list_head = lexed_to_list();
        m_lexed_tree_root = -1;
        std::int32_t slot_index = lexed_list_head;
        for (std::uint32_t slot_count = m_lexed_count; slot_count != 0; --slot_count)
        {
            Slot& slot = meta[slot_index];
            std::int32_t next_index = slot.child_index[1];
            slot.child_index[0] = slot.child_index[1] = -1;
            avl_insert(slot_index);
            slot_index = next_index;
        }
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
[[nodiscard]] RankMap TOrderedSlots<TSlotBacking, TIndex, TMeta>::build_rank_map() const noexcept
{
    RankMap rank_map;
    if (is_safe() && (m_capacity != 0u))
    {
        if (rank_map.allocate(static_cast<std::size_t>(m_capacity)))
        {
            (void)rank_map.set_size(static_cast<std::size_t>(m_capacity));
            RankMapEntry* const map = rank_map.data();
            const Slot* const meta = meta_slots();
            std::int32_t rank_index = 0;
            if (m_lexed_count != 0)
            {
                std::int32_t slot_index = -1;
                for (std::int32_t left_index = m_lexed_tree_root; left_index >= 0; left_index = meta[slot_index = left_index].child_index[0]) {}
                while (slot_index >= 0)
                {
                    map[rank_index].rank_to_slot = slot_index;
                    map[slot_index].slot_to_rank = rank_index;
                    std::int32_t from_index = meta[slot_index].child_index[1];
                    if (from_index < 0)
                    {
                        for (from_index = slot_index; (((slot_index = meta[from_index].parent_index) >= 0) && (meta[slot_index].child_index[0] != from_index)); from_index = slot_index) {}
                    }
                    else
                    {
                        for (slot_index = from_index; ((from_index = meta[slot_index].child_index[0]) >= 0); slot_index = from_index) {}
                    }
                    ++rank_index;
                }
            }
            if (m_loose_count != 0)
            {
                std::int32_t slot_index = m_loose_list_head;
                for (std::uint32_t loose_count = m_loose_count; loose_count != 0; --loose_count)
                {
                    map[rank_index].rank_to_slot = slot_index;
                    map[slot_index].slot_to_rank = rank_index;
                    slot_index = meta[slot_index].child_index[1];
                    ++rank_index;
                }
            }
            if (m_empty_count != 0)
            {
                std::int32_t slot_index = m_empty_list_head;
                for (std::uint32_t empty_count = m_empty_count; empty_count != 0; --empty_count)
                {
                    map[rank_index].rank_to_slot = slot_index;
                    map[slot_index].slot_to_rank = rank_index;
                    slot_index = meta[slot_index].child_index[1];
                    ++rank_index;
                }
            }
            MV_ASSERT(rank_index == static_cast<std::int32_t>(m_capacity));
        }
    }
    return rank_map;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::sort_and_pack(const bool use_external_payload) noexcept
{
    if (is_safe())
    {
        private_sort_and_compact(use_external_payload);
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::rebuild_loose_in_index_order() noexcept
{
    if (is_safe() && (m_loose_count != 0))
    {
        m_loose_list_head = state_to_list(0, m_high_index, SlotState::is_loose_slot);
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::rebuild_empty_in_index_order() noexcept
{
    if (is_safe() && (m_empty_count != 0))
    {
        m_empty_list_head = combine_lists(
            state_to_list(0, m_high_index, SlotState::is_empty_slot),
            range_to_list((m_high_index + 1), static_cast<std::int32_t>(m_capacity - 1u), SlotState::is_empty_slot));
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::rank_index_of(const std::int32_t slot_index) const noexcept
{
    return is_safe_slot(slot_index) ? convert_to_rank_index(slot_index) : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::find_by_rank_index(const std::int32_t rank_index) const noexcept
{
    return is_safe() ? locate_by_rank_index(rank_index) : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::find_any_equal() const noexcept
{
    return is_safe() ? locate_any_equal() : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::find_first_equal() const noexcept
{
    return is_safe() ? locate_first_equal() : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::find_first_greater() const noexcept
{
    return is_safe() ? locate_first_greater() : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::find_first_greater_equal() const noexcept
{
    return is_safe() ? locate_first_greater_equal() : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::find_last_equal() const noexcept
{
    return is_safe() ? locate_last_equal() : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::find_last_less() const noexcept
{
    return is_safe() ? locate_last_less() : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::find_last_less_equal() const noexcept
{
    return is_safe() ? locate_last_less_equal() : -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::lower_bound_by_lex() const noexcept
{
    return find_first_greater_equal();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::upper_bound_by_lex() const noexcept
{
    return find_first_greater();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::tree_height() const noexcept
{
    return is_safe() ? subtree_height(m_lexed_tree_root) : 0;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::tree_weight() const noexcept
{
    return is_safe() ? subtree_weight(m_lexed_tree_root) : 0;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::validate_tree(const LexCheck lex_check) const noexcept
{
    bool valid = false;
    if (is_safe())
    {
        if (m_lexed_count == 0)
        {
            valid = m_lexed_tree_root == -1;
        }
        else
        {
            valid = private_validate_subtree(m_lexed_tree_root, lex_check) > 0;
        }
    }
    return valid;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::check_integrity() const noexcept
{
    return is_safe() ? private_integrity_check() : false;
}

//! Private function bodies

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::is_safe(const bool allow_null) const noexcept
{
    return allow_null || (meta_slots() != nullptr);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline TSlotBacking& TOrderedSlots<TSlotBacking, TIndex, TMeta>::slot_backing() noexcept
{
    return static_cast<TSlotBacking&>(*this);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline const TSlotBacking& TOrderedSlots<TSlotBacking, TIndex, TMeta>::slot_backing() const noexcept
{
    return static_cast<const TSlotBacking&>(*this);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::avl_single_rotate(const std::int32_t slot_index, const std::int32_t heavy_side) noexcept
{
    Slot* const meta = meta_slots();

    const std::int32_t light_side = heavy_side ^ 1;

    Slot& slot = meta[slot_index];
    const std::int32_t parent_index = slot.parent_index;

    const std::int32_t child_index = slot.child_index[heavy_side];
    Slot& child = meta[child_index];

    const std::int32_t light_child_index = child.child_index[light_side];

    slot.child_index[heavy_side] = light_child_index;
    if (light_child_index >= 0)
    {
        meta[light_child_index].parent_index = slot_index;
    }

    child.child_index[light_side] = slot_index;
    slot.parent_index = child_index;

    child.parent_index = parent_index;
    if (parent_index >= 0)
    {
        Slot& parent = meta[parent_index];
        const std::int32_t parent_side = (parent.child_index[1] == slot_index) ? 1 : 0;
        parent.child_index[parent_side] = child_index;
    }
    else
    {
        m_lexed_tree_root = child_index;
    }

    return child_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::avl_double_rotate(const std::int32_t slot_index, const std::int32_t heavy_side) noexcept
{
    Slot* const meta = meta_slots();

    const std::int32_t light_side = heavy_side ^ 1;

    Slot& slot = meta[slot_index];
    const std::int32_t parent_index = slot.parent_index;

    const std::int32_t child_index = slot.child_index[heavy_side];
    Slot& child = meta[child_index];

    const std::int32_t grandchild_index = child.child_index[light_side];
    Slot& grandchild = meta[grandchild_index];

    const std::int32_t heavy_grandchild_index = grandchild.child_index[heavy_side];
    const std::int32_t light_grandchild_index = grandchild.child_index[light_side];

    //  detach grandchild's heavy subtree and attach it as child's light subtree.
    child.child_index[light_side] = heavy_grandchild_index;
    if (heavy_grandchild_index >= 0)
    {
        meta[heavy_grandchild_index].parent_index = child_index;
    }

    //  detach grandchild's light subtree and attach it as slot's heavy subtree.
    slot.child_index[heavy_side] = light_grandchild_index;
    if (light_grandchild_index >= 0)
    {
        meta[light_grandchild_index].parent_index = slot_index;
    }

    //  promote grandchild above child and slot.
    grandchild.child_index[heavy_side] = child_index;
    child.parent_index = grandchild_index;

    grandchild.child_index[light_side] = slot_index;
    slot.parent_index = grandchild_index;

    //  attach promoted subtree to old parent (or become root).
    grandchild.parent_index = parent_index;
    if (parent_index >= 0)
    {
        Slot& parent = meta[parent_index];
        const std::int32_t parent_side = (parent.child_index[1] == slot_index) ? 1 : 0;
        parent.child_index[parent_side] = grandchild_index;
    }
    else
    {
        m_lexed_tree_root = grandchild_index;
    }

    //  balance-factor updates.
    const std::int32_t grandchild_bf = grandchild.balance_factor;
    grandchild.balance_factor = 0;

    if (grandchild_bf == 1)
    {
        if (heavy_side == 1)
        {
            slot.balance_factor = -1;
            child.balance_factor = 0;
        }
        else
        {
            slot.balance_factor = 0;
            child.balance_factor = -1;
        }
    }
    else if (grandchild_bf == -1)
    {
        if (heavy_side == 1)
        {
            slot.balance_factor = 0;
            child.balance_factor = 1;
        }
        else
        {
            slot.balance_factor = 1;
            child.balance_factor = 0;
        }
    }
    else
    {
        slot.balance_factor = 0;
        child.balance_factor = 0;
    }

    return grandchild_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::avl_insert(const std::int32_t slot_index, const std::int32_t key_index) noexcept
{
    Slot* const meta = meta_slots();
    Slot& slot = meta[slot_index];
    slot.parent_index = -1;
    slot.child_index[0] = -1;
    slot.child_index[1] = -1;
    slot.balance_factor = 0;
    slot.set_is_lexed_slot();

    if (m_lexed_tree_root < 0)
    {
        m_lexed_tree_root = slot_index;
    }
    else
    {
        std::int32_t walk_index = -1;
        std::int32_t walk_side = 0;

        for (std::int32_t scan_index = m_lexed_tree_root; scan_index >= 0; scan_index = meta[walk_index].child_index[walk_side])
        {
            walk_index = scan_index;
            walk_side = (slot_backing().on_compare_keys(key_index, walk_index) >= 0) ? 1 : 0;
        }
        meta[walk_index].child_index[walk_side] = slot_index;
        slot.parent_index = walk_index;

        //  walk_side: 0/1, delta: -1/+1
        std::int32_t delta = (walk_side << 1) - 1;
        while (walk_index >= 0)
        {
            Slot& walk = meta[walk_index];
            walk.balance_factor += delta;
            if (walk.balance_factor == delta)
            {   //  walk.balance_factor is 1 or -1
                const std::int32_t walk_parent_index = walk.parent_index;
                delta = ((walk_parent_index >= 0) && (meta[walk_parent_index].child_index[1] == walk_index)) ? 1 : -1;
                walk_index = walk_parent_index;
            }
            else
            {   //  walk.balance_factor is 0 or 2 or -2
                if (walk.balance_factor != 0)
                {
                    const std::int32_t heavy_side = (walk.balance_factor > 0) ? 1 : 0;
                    const std::int32_t expected_child_balance_factor = (heavy_side << 1) - 1;

                    Slot& child = meta[walk.child_index[heavy_side]];
                    const std::int32_t child_balance_factor = child.balance_factor;

                    if (child_balance_factor == expected_child_balance_factor)
                    {
                        walk_index = avl_single_rotate(walk_index, heavy_side);
                        walk.balance_factor = 0;
                        meta[walk_index].balance_factor = 0;
                    }
                    else
                    {
                        (void)avl_double_rotate(walk_index, heavy_side);
                    }
                }
                break;
            }
        }
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::avl_insert(const std::int32_t slot_index) noexcept
{
    avl_insert(slot_index, slot_index);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::avl_remove(const std::int32_t slot_index) noexcept
{
    Slot* const meta = meta_slots();
    Slot& slot = meta[slot_index];

    const std::int32_t parent_index = slot.parent_index;
    const std::int32_t parent_side = ((parent_index >= 0) && (meta[parent_index].child_index[1] == slot_index)) ? 1 : 0;

    std::int32_t walk_index = -1;
    std::int32_t walk_side = 0;

    if (slot.is_lexed_stem())
    {
        std::int32_t successor_index = slot.child_index[1];
        while (meta[successor_index].child_index[0] >= 0)
        {
            successor_index = meta[successor_index].child_index[0];
        }

        Slot& successor = meta[successor_index];
        const std::int32_t successor_parent_index = successor.parent_index;
        const std::int32_t successor_right_index = successor.child_index[1];

        if (successor_parent_index != slot_index)
        {
            Slot& successor_parent = meta[successor_parent_index];
            successor_parent.child_index[0] = successor_right_index;
            if (successor_right_index >= 0)
            {
                meta[successor_right_index].parent_index = successor_parent_index;
            }

            successor.child_index[1] = slot.child_index[1];
            meta[successor.child_index[1]].parent_index = successor_index;

            walk_index = successor_parent_index;
            walk_side = 0;
        }
        else
        {
            // successor was slot.right (direct child), so right height under successor shrank
            walk_index = successor_index;
            walk_side = 1;
        }

        successor.child_index[0] = slot.child_index[0];
        meta[successor.child_index[0]].parent_index = successor_index;

        successor.parent_index = parent_index;
        if (parent_index >= 0)
        {
            meta[parent_index].child_index[parent_side] = successor_index;
        }
        else
        {
            m_lexed_tree_root = successor_index;
        }

        successor.balance_factor = slot.balance_factor;
    }
    else
    {
        const std::int32_t child_side = (slot.child_index[0] >= 0) ? 0 : 1;
        const std::int32_t child_index = slot.child_index[child_side];

        if (parent_index >= 0)
        {
            meta[parent_index].child_index[parent_side] = child_index;
        }
        else
        {
            m_lexed_tree_root = child_index;
        }

        if (child_index >= 0)
        {
            meta[child_index].parent_index = parent_index;
        }

        walk_index = parent_index;
        walk_side = parent_side;
    }

    //  walk_side: 0/1, delta: -1/+1.
    std::int32_t delta = (walk_side << 1) - 1;
    while (walk_index >= 0)
    {
        Slot& walk = meta[walk_index];

        //  subtree parent is not changed by rotation, but
        //  walk.parent_index may change, so grab it now.
        const std::int32_t walk_parent_index = walk.parent_index;

        walk.balance_factor -= delta;
        if (walk.balance_factor != 0)
        {
            if ((walk.balance_factor == 1) || (walk.balance_factor == -1))
            {
                break;
            }
    
            const std::int32_t heavy_side = (walk.balance_factor > 0) ? 1 : 0;
            const std::int32_t expected_child_balance_factor = (heavy_side << 1) - 1;
    
            Slot& child = meta[walk.child_index[heavy_side]];
            const std::int32_t child_balance_factor = child.balance_factor;
    
            if ((child_balance_factor == 0) || (child_balance_factor == expected_child_balance_factor))
            {
                const std::int32_t rebalance_factor = expected_child_balance_factor - child_balance_factor;
                walk_index = avl_single_rotate(walk_index, heavy_side);
                walk.balance_factor = rebalance_factor;
                meta[walk_index].balance_factor = -rebalance_factor;
                if (child_balance_factor == 0)
                {
                    break;
                }
            }
            else
            {
                walk_index = avl_double_rotate(walk_index, heavy_side);
                if (meta[walk_index].balance_factor != 0)
                {
                    break;
                }
            }
        }
    
        delta = ((walk_parent_index >= 0) && (meta[walk_parent_index].child_index[1] == walk_index)) ? 1 : -1;
        walk_index = walk_parent_index;
    }

    slot.parent_index = -1;
    slot.child_index[0] = -1;
    slot.child_index[1] = -1;
    slot.balance_factor = 0;
    slot.set_is_unassigned();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::apply_growth_policy(const std::uint32_t capacity) noexcept
{
    return static_cast<std::uint32_t>(memory::vector_growth_policy(static_cast<std::size_t>(capacity), static_cast<std::size_t>(k_capacity_limit)));
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::failed_validate_subtree() noexcept
{
    MV_ASSERT(false);
    return -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_validate_subtree(const std::int32_t slot_index, const LexCheck lex_check) const noexcept
{
    if (static_cast<std::uint32_t>(slot_index) >= m_capacity)
    {   //  early out on an invalid slot_index
        return failed_validate_subtree();
    }

    const Slot* const meta = meta_slots();
    const Slot& slot = meta[slot_index];

    if (!slot.is_lexed_slot())
    {   //  early out on link to a slot that is not in the tree
        return failed_validate_subtree();
    }

    if ((slot.balance_factor < -1) || (slot.balance_factor > 1))
    {   //  early out on balance out of range
        return failed_validate_subtree();
    }

    const std::int32_t index_l = slot.child_index[0];
    const std::int32_t index_r = slot.child_index[1];

    if ((static_cast<std::uint32_t>(index_l) + 1u) > m_capacity)
    {   //  early out on invalid index_l
        return failed_validate_subtree();
    }
    if ((static_cast<std::uint32_t>(index_r) + 1u) > m_capacity)
    {   //  early out on invalid index_r
        return failed_validate_subtree();
    }

    std::int32_t height_l = 0;
    if (index_l >= 0)
    {
        if (meta[index_l].parent_index != slot_index)
        {   //  early out on left child parent_index mismatch
            return failed_validate_subtree();
        }
        height_l = private_validate_subtree(index_l, lex_check);
        if (height_l < 0)
        {   //  propagate failure upwards
            return failed_validate_subtree();
        }
    }

    std::int32_t height_r = 0;
    if (index_r >= 0)
    {
        if (meta[index_r].parent_index != slot_index)
        {   //  early out on right child parent_index mismatch
            return failed_validate_subtree();
        }
        height_r = private_validate_subtree(index_r, lex_check);
        if (height_r < 0)
        {   //  propagate failure upwards
            return failed_validate_subtree();
        }
    }

    const std::int32_t balance_factor = height_r - height_l;

    if (static_cast<std::int32_t>(slot.balance_factor) != balance_factor)
    {   //  early out on mismatched balance
        return failed_validate_subtree();
    }

    if (lex_check != LexCheck::None)
    {
        const std::int32_t unique_bias = static_cast<std::int32_t>(lex_check);

        std::int32_t prev_index = private_prev_lexed(slot_index);
        if (prev_index >= 0)
        {
            if (private_next_lexed(prev_index) != slot_index)
            {
                return failed_validate_subtree();
            }
            if ((slot_backing().on_compare_keys(slot_index, prev_index) - unique_bias) < 0)
            {
                return failed_validate_subtree();
            }
        }

        std::int32_t next_index = private_next_lexed(slot_index);
        if (next_index >= 0)
        {
            if (private_prev_lexed(next_index) != slot_index)
            {
                return failed_validate_subtree();
            }
            if ((slot_backing().on_compare_keys(slot_index, next_index) + unique_bias) > 0)
            {
                return failed_validate_subtree();
            }
        }
    }

    return std::max(height_l, height_r) + 1;
}

//  This function only exists as a debug convenience to help capture integrity check failure causes.
//  It may be expanded on in the future as a potential logging site.
template<typename TSlotBacking, typename TIndex, typename TMeta>
inline memory::SMemoryAttribution TOrderedSlots<TSlotBacking, TIndex, TMeta>::memory_attribution() const noexcept
{
    return memory::observe_memory_attribution(m_meta_slot_array);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_meta_slot_array.unsafe_replace_context_without_accounting(expected_source, target);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::failed_integrity_check() noexcept
{
    MV_ASSERT(false);
    return false;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_integrity_check() const noexcept
{
    if (!m_meta_slot_array.is_relocatable() ||
        (m_meta_slot_array.stride() != sizeof(Slot)) ||
        (m_meta_slot_array.storage_alignment() != memory::t_default_align<Slot>()) ||
        (m_meta_slot_array.count() != m_capacity))
    {
        return failed_integrity_check();
    }

    const Slot* const meta = meta_slots();
    if (meta != nullptr)
    {
        if ((std::uintptr_t(meta) % alignof(Slot)) != 0u)
        {   //  basic slot array alignment check failed
            return failed_integrity_check();
        }

        if ((m_capacity == 0) || (m_capacity > k_capacity_limit) ||
            ((m_lexed_count + m_loose_count + m_empty_count) != m_capacity) ||
            (m_lexed_count > m_capacity) || (m_loose_count > m_capacity) || (m_empty_count > m_capacity))
        {   //  basic capacity integrity test failed
            return failed_integrity_check();
        }

        if (((static_cast<std::uint32_t>(m_lexed_tree_root) + 1u) > m_capacity) || ((m_lexed_count == 0u) ? (m_lexed_tree_root != -1) : (m_lexed_tree_root == -1)))
        {   //  basic lexed tree root index integrity test failed
            return failed_integrity_check();
        }

        if (((static_cast<std::uint32_t>(m_loose_list_head) + 1u) > m_capacity) || ((m_loose_count == 0u) ? (m_loose_list_head != -1) : (m_loose_list_head == -1)))
        {   //  basic loose list head index integrity test failed
            return failed_integrity_check();
        }

        if (((static_cast<std::uint32_t>(m_empty_list_head) + 1u) > m_capacity) || ((m_empty_count == 0u) ? (m_empty_list_head != -1) : (m_empty_list_head == -1)))
        {   //  basic empty list head index integrity test failed
            return failed_integrity_check();
        }

        if ((m_high_index < -1) || ((m_high_index == -1) ? ((m_lexed_count + m_loose_count) != 0u) : ((static_cast<std::uint32_t>(m_high_index) + 1u) < (m_lexed_count + m_loose_count))))
        {   //  basic high index integrity test failed
            return failed_integrity_check();
        }

        if ((m_peak_index < -1) || (m_peak_usage > k_capacity_limit) || (m_peak_usage > (static_cast<std::uint32_t>(m_peak_index) + 1u)))
        {   //  basic peak usage and peak index integrity test failed
            return failed_integrity_check();
        }

        std::uint32_t lexed_count = 0;
        std::uint32_t loose_count = 0;
        std::uint32_t empty_count = 0;
        for (std::int32_t slot_index = static_cast<std::int32_t>(m_capacity - 1u); slot_index >= 0; --slot_index)
        {   //  basic array integrity check
            const Slot& slot = meta[slot_index];
            switch (slot.get_slot_state())
            {
                case (SlotState::is_lexed_slot):
                {
                    ++lexed_count;
                    if (((static_cast<std::uint32_t>(slot.parent_index) + 1u) > m_capacity) || ((slot.parent_index == -1) && (m_lexed_tree_root != slot_index)))
                    {   //  parent index is invalid
                        return failed_integrity_check();
                    }
                    if (((static_cast<std::uint32_t>(slot.child_index[0]) + 1u) > m_capacity) || ((static_cast<std::uint32_t>(slot.child_index[1]) + 1u) > m_capacity))
                    {   //  child index is invalid
                        return failed_integrity_check();
                    }
                    if ((slot_index == slot.parent_index) || (slot_index == slot.child_index[0]) || (slot_index == slot.child_index[1]))
                    {   //  invalid: an index in this slot reference this slot
                        return failed_integrity_check();
                    }
                    if ((slot.parent_index != -1) && ((slot.parent_index == slot.child_index[0]) || (slot.parent_index == slot.child_index[1])))
                    {   //  invalid: children reference the parent slot or vice-versa
                        return failed_integrity_check();
                    }
                    if ((slot.parent_index != -1) && !meta[slot.parent_index].is_lexed_slot())
                    {   //  invalid: parent index references outside of the tree
                        return failed_integrity_check();
                    }
                    if ((slot.child_index[0] != -1) && !meta[slot.child_index[0]].is_lexed_slot())
                    {   //  invalid: child index references outside of the tree
                        return failed_integrity_check();
                    }
                    if ((slot.child_index[1] != -1) && !meta[slot.child_index[1]].is_lexed_slot())
                    {   //  invalid: child index references outside of the tree
                        return failed_integrity_check();
                    }
                    if (slot.parent_index != -1)
                    {   //  not a root node
                        const Slot& parent_slot = meta[slot.parent_index];
                        if ((slot_index != parent_slot.child_index[0]) && (slot_index != parent_slot.child_index[1]))
                        {   //  invalid: this node is not a child of its parent node
                            return failed_integrity_check();
                        }
                    }
                    if (slot.child_index[0] == slot.child_index[1])
                    {   //  only leaves can have matching child indices
                        if (slot.child_index[0] != -1)
                        {   //  invalid: not a leaf
                            return failed_integrity_check();
                        }
                        if (slot.balance_factor != 0)
                        {   //  invalid: leaf nodes must be balanced by definition
                            return failed_integrity_check();
                        }
                    }
                    else
                    {
                        if (slot.child_index[0] == -1)
                        {
                            if (slot.balance_factor != 1)
                            {   //  invalid: right child only branches must be balanced to the right
                                return failed_integrity_check();
                            }
                        }
                        else if (slot_index != meta[slot.child_index[0]].parent_index)
                        {   //  invalid: the left child is not parented to this slot
                            return failed_integrity_check();
                        }
                        if (slot.child_index[1] == -1)
                        {
                            if (slot.balance_factor != -1)
                            {   //  invalid: left child only branches must be balanced to the left
                                return failed_integrity_check();
                            }
                        }
                        else if (slot_index != meta[slot.child_index[1]].parent_index)
                        {   //  invalid: the right child is not parented to this slot
                            return failed_integrity_check();
                        }
                        if ((slot.balance_factor < -1) || (slot.balance_factor > 1))
                        {   //  balance is invalid
                            return failed_integrity_check();
                        }
                    }
                    break;
                }
                case (SlotState::is_loose_slot):
                case (SlotState::is_empty_slot):
                {
                    if (slot.is_loose_slot())
                    {
                        ++loose_count;
                    }
                    else
                    {
                        ++empty_count;
                    }
                    if ((slot.parent_index != -1) || (slot.balance_factor != 0))
                    {   //  invalid: invariants check failed
                        return failed_integrity_check();
                    }
                    if ((static_cast<std::uint32_t>(slot.child_index[0]) >= m_capacity) || (static_cast<std::uint32_t>(slot.child_index[1]) >= m_capacity))
                    {   //  loose or empty list index is invalid
                        return failed_integrity_check();
                    }
                    break;
                }
                default:
                {   //  slot state is invalid
                    return failed_integrity_check();
                }
            }
        }
        if (lexed_count != m_lexed_count)
        {   //  the lexed count is invalid
            return failed_integrity_check();
        }
        if (loose_count != m_loose_count)
        {   //  the loose count is invalid
            return failed_integrity_check();
        }
        if (empty_count != m_empty_count)
        {   //  the empty count is invalid
            return failed_integrity_check();
        }
        if (empty_count != 0)
        {   //  validate the empty list
            std::int32_t empty_index = m_empty_list_head;
            while (empty_count != 0)
            {
                const Slot& slot = meta[empty_index];
                if (!slot.is_empty_slot())
                {   //  the empty list links to a non-empty slot
                    return failed_integrity_check();
                }
                if (meta[slot.child_index[1]].child_index[0] != empty_index)
                {   //  bi-directional linkage is broken
                    return failed_integrity_check();
                }
                empty_index = slot.child_index[1];
                if ((empty_index == m_empty_list_head) && (empty_count != 1))
                {   //  found a short cycle
                    return failed_integrity_check();
                }
                --empty_count;
            }
            if (empty_index != m_empty_list_head)
            {   //  the empty list is not circular
                return failed_integrity_check();
            }
        }
        if (loose_count != 0)
        {   //  validate the loose list
            std::int32_t loose_index = m_loose_list_head;
            while (loose_count != 0)
            {
                const Slot& slot = meta[loose_index];
                if (!slot.is_loose_slot())
                {   //  the loose list links to a non-loose slot
                    return failed_integrity_check();
                }
                if (meta[slot.child_index[1]].child_index[0] != loose_index)
                {   //  bi-directional linkage is broken
                    return failed_integrity_check();
                }
                loose_index = slot.child_index[1];
                if ((loose_index == m_loose_list_head) && (loose_count != 1))
                {   //  found a short cycle
                    return failed_integrity_check();
                }
                --loose_count;
            }
            if (loose_index != m_loose_list_head)
            {   //  the loose list is not circular
                return failed_integrity_check();
            }
        }
        if (lexed_count != 0)
        {   //  validate the tree and balance

            if (meta[m_lexed_tree_root].parent_index != -1)
            {   //  the root is invalid
                return failed_integrity_check();
            }

            //  step_cap = 2*N - 1 node-visits for Euler tour.
            //  N is capped at 2^31, so 2*N-1 fits in std::uint32_t (== 0xFFFFFFFF at the max).
            std::uint32_t step_cap = (lexed_count << 1) - 1;

            //  for an AVL the theoretical depth limit is ~ 1.44 * log2(n) + 1
            //
            //  the depth_cap calculation:
            //      effectively 1.5 * (floor(log2(n)) + 1)
            //      exceeds the theoretical limit allowing some margin
            //      max depth_cap = 48 (based on max n of 2^31)
            // 
            //  depth:
            //      at root = 0
            //      incremented on descent
            //      used as an index into height_cache
            //      check must be depth < depth_cap after increment
            std::uint32_t depth_cap = 0;
            for (std::uint32_t check = lexed_count; check; check >>= 1)
            {
                ++depth_cap;
            }
            depth_cap += (depth_cap >> 1);
            std::int8_t height_cache[48];    //  max depth_cap capacity

            std::uint32_t depth = 0;
            std::int32_t height = 0;
            std::int32_t step_index = -1;
            std::int32_t from_index = -1;
            std::int32_t scan_index = m_lexed_tree_root;
            while (scan_index >= 0)
            {
                const Slot& slot = meta[scan_index];

                if (from_index == slot.parent_index)
                {   //  came from parent
                    height_cache[depth] = 0;
                    height = 0;
                }
                else if (from_index == slot.child_index[0])
                {   //  came from left child
                    height_cache[depth] = static_cast<std::int8_t>(height);
                    height = 0;
                }

                if ((from_index == slot.parent_index) && (slot.child_index[0] != -1))
                {   //  descend to left child
                    step_index = slot.child_index[0];

                    ++depth;
                    if (depth >= depth_cap)
                    {   //  too deeply nested to be a valid AVL tree and would exceed height_cache capacity
                        return failed_integrity_check();
                    }
                }
                else if ((from_index != slot.child_index[1]) && (slot.child_index[1] != -1))
                {   //  descend to right child
                    step_index = slot.child_index[1];

                    ++depth;
                    if (depth >= depth_cap)
                    {   //  too deeply nested to be a valid AVL tree and would exceed height_cache capacity
                        return failed_integrity_check();
                    }
                }
                else
                {   //  ascend to parent
                    step_index = slot.parent_index;

                    if (step_index != -1)
                    {
                        std::int32_t other_height = static_cast<std::int32_t>(height_cache[depth]);
                        std::int32_t balance_factor = height - other_height;
                        if (static_cast<std::int32_t>(slot.balance_factor) != balance_factor)
                        {   //  there is a tree balance mismatch
                            return failed_integrity_check();
                        }
                        height = std::max(height, other_height) + 1;

                        if (depth == 0)
                        {   //  we should be exiting the tree but are not
                            return failed_integrity_check();
                        }
                        --depth;
                    }

                    if (lexed_count == 0)
                    {   //  too many slots for this to be a valid tree
                        return failed_integrity_check();
                    }
                    --lexed_count;
                }

                from_index = scan_index;
                scan_index = step_index;

                if (step_cap == 0)
                {   //  too many steps for this to be a valid tree
                    return failed_integrity_check();
                }
                --step_cap;
            }
            if (lexed_count > 0)
            {   //  the tree does not contain all the lexed slots
                return failed_integrity_check();
            }
        }
    }
    else
    {
        if ((m_capacity != 0) || (m_peak_usage != 0) ||
            (m_peak_index != -1) || (m_high_index != -1) ||
            (m_lexed_count != 0) || (m_lexed_tree_root != -1) ||
            (m_loose_count != 0) || (m_loose_list_head != -1) ||
            (m_empty_count != 0) || (m_empty_list_head != -1))
        {
            return failed_integrity_check();
        }
    }
    return true;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_resize(const std::uint32_t requested_capacity) noexcept
{
    bool resized = false;
    if ((requested_capacity != 0) && (requested_capacity <= k_capacity_limit))
    {
        if (requested_capacity == m_capacity)
        {
            resized = true;
        }
        else if (meta_slots() == nullptr)
        {   //  initialisation
            if (m_meta_slot_array.allocate(static_cast<std::size_t>(requested_capacity), false))
            {
                m_capacity = m_empty_count = requested_capacity;
                m_empty_list_head = range_to_list(0, static_cast<std::int32_t>(m_empty_count - 1), SlotState::is_empty_slot);
                resized = true;
            }
        }
        else if (requested_capacity >= minimum_safe_capacity())
        {
            if (m_meta_slot_array.reallocate(static_cast<std::size_t>(requested_capacity), static_cast<std::size_t>(std::min(requested_capacity, m_capacity)), false))
            {
                if (requested_capacity > m_capacity)
                {   //  grow
                    m_empty_list_head = combine_lists(m_empty_list_head, range_to_list(static_cast<std::int32_t>(m_capacity), static_cast<std::int32_t>(requested_capacity - 1), SlotState::is_empty_slot));
                    m_empty_count += requested_capacity - m_capacity;
                    m_capacity = requested_capacity;
                }
                else
                {   //  shrink
                    m_capacity = requested_capacity;
                    m_empty_count = m_capacity - m_lexed_count - m_loose_count;
                    m_empty_list_head = (m_empty_count != 0) ? state_to_list(0, static_cast<std::int32_t>(m_capacity - 1u), SlotState::is_empty_slot) : -1;
                }
                resized = true;
            }
        }
    }
    return resized;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_acquire(const std::int32_t slot_index, const bool lex, const bool require_unique, const bool allow_reserve) noexcept
{
    std::int32_t acquired_index = -1;
    if ((static_cast<std::uint32_t>(slot_index) + 1u) <= k_capacity_limit)
    {
        if (!require_unique || !private_has_duplicate_key(slot_index))
        {
            if (slot_index == -1)
            {
                acquired_index = m_empty_list_head;
                if (allow_reserve && (acquired_index == -1) && (m_capacity < k_capacity_limit))
                {   //  need to reserve
                    std::uint32_t minimum_capacity = m_capacity + 1u;
                    std::uint32_t reserve_capacity = slot_backing().on_reserve_empty(minimum_capacity, apply_growth_policy(minimum_capacity));
                    if (reserve_capacity >= minimum_capacity)
                    {
                        if (private_resize(reserve_capacity))
                        {
                            acquired_index = m_empty_list_head;
                        }
                    }
                }
            }
            else if (static_cast<std::uint32_t>(slot_index) >= m_capacity)
            {   //  need to reserve
                if (allow_reserve)
                {
                    std::uint32_t minimum_capacity = static_cast<std::uint32_t>(slot_index) + 1u;
                    std::uint32_t reserve_capacity = slot_backing().on_reserve_empty(minimum_capacity, minimum_capacity);
                    if (reserve_capacity >= minimum_capacity)
                    {
                        if (private_resize(reserve_capacity))
                        {
                            acquired_index = slot_index;
                        }
                    }
                }
            }
            else if ((static_cast<std::uint32_t>(slot_index) < m_capacity) && meta_slots()[slot_index].is_empty_slot())
            {
                acquired_index = slot_index;
            }
            if (acquired_index >= 0)
            {
                if (!lex)
                {
                    move_to_loose_list(acquired_index);
                }
                else
                {
                    move_to_lexed_tree(acquired_index, slot_index);
                }
                const std::uint32_t occupied_count = m_lexed_count + m_loose_count;
                if (m_peak_usage < occupied_count)
                {
                    m_peak_usage = occupied_count;
                }
                if (m_high_index < acquired_index)
                {
                    m_high_index = acquired_index;
                    if (m_peak_index < acquired_index)
                    {
                        m_peak_index = acquired_index;
                    }
                }
            }
        }
    }
    return acquired_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_prev_lexed(const std::int32_t slot_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t prev_index = -1;
    std::int32_t from_index = meta[slot_index].child_index[0];
    if (from_index < 0)
    {
        for (from_index = slot_index; (((prev_index = meta[from_index].parent_index) >= 0) && (meta[prev_index].child_index[1] != from_index)); from_index = prev_index) {}
    }
    else
    {
        for (prev_index = from_index; ((from_index = meta[prev_index].child_index[1]) >= 0); prev_index = from_index) {}
    }
    return prev_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_next_lexed(const std::int32_t slot_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t next_index = -1;
    std::int32_t from_index = meta[slot_index].child_index[1];
    if (from_index < 0)
    {
        for (from_index = slot_index; (((next_index = meta[from_index].parent_index) >= 0) && (meta[next_index].child_index[0] != from_index)); from_index = next_index) {}
    }
    else
    {
        for (next_index = from_index; ((from_index = meta[next_index].child_index[0]) >= 0); next_index = from_index) {}
    }
    return next_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_has_duplicate_key(const std::int32_t slot_index) const noexcept
{
    return private_has_duplicate_key_in_lexed(slot_index) || private_has_duplicate_key_in_loose(slot_index);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_has_duplicate_key_in_lexed(const std::int32_t slot_index) const noexcept
{
    const Slot* const meta = meta_slots();
    bool has_duplicate = false;
    std::int32_t lexed_index = -1;
    std::int32_t check_index = m_lexed_tree_root;
    while (check_index >= 0)
    {   //  find the first instance by lex of a matching slot
        std::int32_t relationship = slot_backing().on_compare_keys(slot_index, check_index);
        if (relationship == 0)
        {
            lexed_index = check_index;
        }
        check_index = meta[check_index].child_index[(relationship <= 0) ? 0 : 1];
    }
    if (lexed_index >= 0)
    {
        has_duplicate = true;
        if (lexed_index == slot_index)
        {   //  the found index needs to be excluded, find the next in-order index and see if it also matches
            check_index = meta[lexed_index].child_index[1];
            if (check_index < 0)
            {
                for (check_index = lexed_index; (((lexed_index = meta[check_index].parent_index) >= 0) && (meta[lexed_index].child_index[0] != check_index)); check_index = lexed_index) {}
            }
            else
            {
                for (lexed_index = check_index; ((check_index = meta[lexed_index].child_index[0]) >= 0); lexed_index = check_index) {}
            }
            if ((lexed_index < 0) || (slot_backing().on_compare_keys(slot_index, lexed_index) != 0))
            {   //  there is no non-excluded match
                has_duplicate = false;
            }
        }
    }
    return has_duplicate;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_has_duplicate_key_in_loose(const std::int32_t slot_index) const noexcept
{
    const Slot* const meta = meta_slots();
    bool has_duplicate = false;
    std::int32_t loose_index = m_loose_list_head;
    for (std::uint32_t loose_count = m_loose_count; loose_count != 0; --loose_count)
    {
        if (loose_index != slot_index)
        {
            std::int32_t relationship = slot_backing().on_compare_keys(slot_index, loose_index);
            if (relationship == 0)
            {
                has_duplicate = true;
                break;
            }
        }
        loose_index = meta[loose_index].child_index[1];
    }
    return has_duplicate;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::private_sort_and_compact(const bool use_external_payload) noexcept
{
    if (m_capacity != 0u)
    {
        Slot* const meta = meta_slots();
        std::int32_t list_head = combine_lists(lexed_to_list(), combine_lists(m_loose_list_head, m_empty_list_head));
        if (use_external_payload)
        {
            std::int32_t slot_index = list_head;
            std::int32_t rank_index = 0;
            for (std::uint32_t count = m_capacity; count != 0; --count)
            {
                slot_backing().on_move_payload(slot_index, rank_index);
                slot_index = meta[slot_index].child_index[1];
                ++rank_index;
            }
        }
        else
        {
            std::uint32_t remaining = m_capacity;
            set_list_ordinals(list_head, m_capacity, 0);
            while (remaining)
            {
                std::int32_t cycle_start = list_head;

                std::int32_t slot_index = -1;
                std::int32_t rank_index = cycle_start;

                do
                {
                    Slot& rank_slot = meta[rank_index];

                    list_head = rank_slot.child_index[1];

                    meta[rank_slot.child_index[0]].child_index[1] = rank_slot.child_index[1];
                    meta[rank_slot.child_index[1]].child_index[0] = rank_slot.child_index[0];
                    --remaining;

                    rank_slot.child_index[1] = static_cast<TIndex>(slot_index);

                    slot_index = rank_index;
                    rank_index = rank_slot.parent_index;

                } while (rank_index != cycle_start);

                if (meta[slot_index].child_index[1] >= 0)
                {   //  process a multi-slot cycle

                    slot_backing().on_move_payload(slot_index, -1);

                    while ((rank_index = slot_index) >= 0)
                    {
                        slot_index = meta[rank_index].child_index[1];
                        slot_backing().on_move_payload(slot_index, rank_index);
                    }
                }
            }
        }

        const std::int32_t lexed_lower_index = 0;
        const std::int32_t lexed_upper_index = lexed_lower_index + static_cast<std::int32_t>(m_lexed_count - 1);

        const std::int32_t loose_lower_index = lexed_lower_index + static_cast<std::int32_t>(m_lexed_count);
        const std::int32_t loose_upper_index = loose_lower_index + static_cast<std::int32_t>(m_loose_count - 1);

        const std::int32_t empty_lower_index = loose_lower_index + static_cast<std::int32_t>(m_loose_count);
        const std::int32_t empty_upper_index = empty_lower_index + static_cast<std::int32_t>(m_empty_count - 1);

        m_lexed_tree_root = build_balanced_subtree(lexed_lower_index, lexed_upper_index, -1);
        m_loose_list_head = range_to_list(loose_lower_index, loose_upper_index, SlotState::is_loose_slot);
        m_empty_list_head = range_to_list(empty_lower_index, empty_upper_index, SlotState::is_empty_slot);

        m_high_index = static_cast<std::int32_t>(m_lexed_count + m_loose_count - 1u);
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::build_balanced_subtree(const std::int32_t lower_index, const std::int32_t upper_index, const std::int32_t parent_index) noexcept
{
    std::int32_t split_index = -1;
    if (lower_index <= upper_index)
    {
        split_index = static_cast<std::int32_t>((static_cast<std::uint32_t>(lower_index) + static_cast<std::uint32_t>(upper_index)) >> 1);
        std::int32_t balance_factor = 0;
        for (std::int32_t children = upper_index - split_index; children; children >>= 1) ++balance_factor;
        for (std::int32_t children = split_index - lower_index; children; children >>= 1) --balance_factor;

        Slot& slot = meta_slots()[split_index];
        slot.parent_index = static_cast<TIndex>(parent_index);
        slot.child_index[0] = static_cast<TIndex>(build_balanced_subtree(lower_index, (split_index - 1), split_index));
        slot.child_index[1] = static_cast<TIndex>(build_balanced_subtree((split_index + 1), upper_index, split_index));
        slot.balance_factor = static_cast<TMeta>(balance_factor);
        slot.set_is_lexed_slot();
    }
    return split_index;
}

//  Convert the lexed tree structure into a ordered circular bi-directional list.
//  using Slot::child_index[] as prev/next links. Returns the list head slot index.
//
//  Note: This does not move payload items.
template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::lexed_to_list() noexcept
{
    Slot* const meta = meta_slots();
    std::int32_t list_index = -1;
    std::int32_t scan_index = -1;
    for (list_index = m_lexed_tree_root; list_index >= 0; list_index = meta[scan_index = list_index].child_index[1]) {}
    if (scan_index >= 0)
    {
        std::int32_t from_index = -1;
        while (scan_index >= 0)
        {
            Slot& slot = meta[scan_index];
            slot.child_index[1] = static_cast<TIndex>(list_index);
            list_index = scan_index;
            from_index = slot.child_index[0];
            if (from_index < 0)
            {
                for (from_index = scan_index; ((scan_index = meta[from_index].parent_index) >= 0) && (meta[scan_index].child_index[1] != from_index); from_index = scan_index) {}
            }
            else
            {
                for (scan_index = from_index; (from_index = meta[scan_index].child_index[1]) >= 0; scan_index = from_index) {}
            }
        }
        from_index = -1;
        scan_index = list_index;
        while (scan_index >= 0)
        {
            Slot& slot = meta[scan_index];
            slot.parent_index = -1;
            slot.child_index[0] = static_cast<TIndex>(from_index);
            slot.balance_factor = 0;
            from_index = scan_index;
            scan_index = slot.child_index[1];
        }
        meta[list_index].child_index[0] = static_cast<TIndex>(from_index);
        meta[from_index].child_index[1] = static_cast<TIndex>(list_index);
    }
    return list_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::state_to_list(const std::int32_t lower_index, const std::int32_t upper_index, const SlotState state) noexcept
{
    std::int32_t head_index = -1;
    if ((lower_index <= upper_index) && (lower_index >= 0))
    {
        Slot* const meta = meta_slots();
        std::int32_t prev_index = -1;
        std::int32_t next_index = -1;
        for (std::int32_t scan_index = upper_index; scan_index >= lower_index; --scan_index)
        {   //  scan backwards creating a singly linked forward list
            Slot& slot = meta[scan_index];
            if (slot.get_slot_state() == state)
            {
                slot.child_index[1] = next_index;
                next_index = scan_index;
            }
        }
        head_index = next_index;
        if (head_index != -1)
        {
            while (next_index != -1)
            {   //  scan the singly linked list patching it up to a bi-directional list
                Slot& slot = meta[next_index];
                slot.child_index[0] = prev_index;
                prev_index = next_index;
                next_index = slot.child_index[1];
            }

            //  fix up the list to be circular
            meta[head_index].child_index[0] = prev_index;
            meta[prev_index].child_index[1] = head_index;
        }
    }
    return head_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::range_to_list(const std::int32_t lower_index, const std::int32_t upper_index, SlotState state) noexcept
{
    if ((lower_index <= upper_index) && (lower_index >= 0))
    {
        Slot* const meta = meta_slots();
        for (std::int32_t scan_index = lower_index; scan_index <= upper_index; ++scan_index)
        {   //  scan the range creating new list members
            Slot& slot = meta[scan_index];
            slot.parent_index = -1;
            slot.child_index[0] = static_cast<TIndex>(scan_index - 1);
            slot.child_index[1] = static_cast<TIndex>(scan_index + 1);
            slot.balance_factor = 0;
            slot.set_slot_state(state);
        }

        //  fix up the list to be circular
        meta[upper_index].child_index[1] = static_cast<TIndex>(lower_index);
        meta[lower_index].child_index[0] = static_cast<TIndex>(upper_index);
        return lower_index;
    }
    return -1;
}

//  Convert a range of slot indices to a bi-directional list (slot metadata only).
template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::combine_lists(const std::int32_t list1_head_index, const std::int32_t list2_head_index) noexcept
{
    if (list1_head_index < 0)
    {
        return list2_head_index;
    }
    if (list2_head_index >= 0)
    {
        Slot* const meta = meta_slots();
        std::int32_t list1_tail_index = meta[list1_head_index].child_index[0];
        std::int32_t list2_tail_index = meta[list2_head_index].child_index[0];
        meta[list1_head_index].child_index[0] = static_cast<TIndex>(list2_tail_index);
        meta[list1_tail_index].child_index[1] = static_cast<TIndex>(list2_head_index);
        meta[list2_head_index].child_index[0] = static_cast<TIndex>(list1_tail_index);
        meta[list2_tail_index].child_index[1] = static_cast<TIndex>(list1_head_index);
    }
    return list1_head_index;
}

//  Set the parent_index of slots in a list to be an ordinal index
template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::set_list_ordinals(const std::int32_t list_index, const std::uint32_t list_count, const std::int32_t ordinal_start) noexcept
{
    Slot* const meta = meta_slots();
    std::int32_t ordinal_index = ordinal_start;
    std::int32_t slot_index = list_index;
    for (std::uint32_t slot_count = list_count; slot_count > 0; --slot_count)
    {
        Slot& slot = meta[slot_index];
        slot.parent_index = ordinal_index;
        slot_index = slot.child_index[1];
        ++ordinal_index;
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::append_range_to_loose_list(const std::int32_t lower_index, const std::int32_t upper_index) noexcept
{
    m_loose_count += static_cast<std::uint32_t>(upper_index - lower_index) + 1u;
    m_loose_list_head = combine_lists(m_loose_list_head, range_to_list(lower_index, upper_index, SlotState::is_loose_slot));
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::append_range_to_empty_list(const std::int32_t lower_index, const std::int32_t upper_index) noexcept
{
    m_empty_count += static_cast<std::uint32_t>(upper_index - lower_index) + 1u;
    m_empty_list_head = combine_lists(m_empty_list_head, range_to_list(lower_index, upper_index, SlotState::is_empty_slot));
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::attach_to_lexed(const std::int32_t slot_index, const std::int32_t key_index) noexcept
{
    Slot& slot = meta_slots()[slot_index];
    slot.parent_index = -1;
    slot.balance_factor = 0;
    slot.child_index[0] = slot.child_index[1] = -1;
    slot.set_is_lexed_slot();
    avl_insert(slot_index, key_index);
    ++m_lexed_count;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::attach_to_lexed(const std::int32_t slot_index) noexcept
{
    attach_to_lexed(slot_index, slot_index);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::attach_to_loose(const std::int32_t slot_index) noexcept
{
    Slot* const meta = meta_slots();
    Slot& slot = meta[slot_index];
    slot.parent_index = -1;
    slot.balance_factor = 0;
    slot.set_is_loose_slot();
    if (m_loose_list_head == -1)
    {
        slot.child_index[0] = slot.child_index[1] = slot_index;
    }
    else
    {
        slot.child_index[1] = m_loose_list_head;
        slot.child_index[0] = meta[m_loose_list_head].child_index[0];
        meta[slot.child_index[0]].child_index[1] = slot_index;
        meta[slot.child_index[1]].child_index[0] = slot_index;
    }
    m_loose_list_head = slot_index;
    ++m_loose_count;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::attach_to_empty(const std::int32_t slot_index) noexcept
{
    Slot* const meta = meta_slots();
    Slot& slot = meta[slot_index];
    slot.parent_index = -1;
    slot.balance_factor = 0;
    slot.set_is_empty_slot();
    if (m_empty_list_head == -1)
    {
        slot.child_index[0] = slot.child_index[1] = slot_index;
    }
    else
    {
        slot.child_index[1] = m_empty_list_head;
        slot.child_index[0] = meta[m_empty_list_head].child_index[0];
        meta[slot.child_index[0]].child_index[1] = slot_index;
        meta[slot.child_index[1]].child_index[0] = slot_index;
    }
    m_empty_list_head = slot_index;
    ++m_empty_count;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::remove_from_lexed(const std::int32_t slot_index) noexcept
{
    Slot& slot = meta_slots()[slot_index];
    --m_lexed_count;
    avl_remove(slot_index);
    slot.parent_index = -1;
    slot.child_index[0] = slot.child_index[1] = -1;
    slot.balance_factor = 0;
    slot.set_is_unassigned();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::remove_from_loose(const std::int32_t slot_index) noexcept
{
    Slot* const meta = meta_slots();
    Slot& slot = meta[slot_index];
    --m_loose_count;
    if (m_loose_list_head == slot_index)
    {
        m_loose_list_head = (m_loose_count == 0) ? -1 : slot.child_index[1];
    }
    if (m_loose_count != 0)
    {
        meta[slot.child_index[0]].child_index[1] = slot.child_index[1];
        meta[slot.child_index[1]].child_index[0] = slot.child_index[0];
    }
    slot.parent_index = -1;
    slot.child_index[0] = slot.child_index[1] = -1;
    slot.balance_factor = 0;
    slot.set_is_unassigned();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::remove_from_empty(const std::int32_t slot_index) noexcept
{
    Slot* const meta = meta_slots();
    Slot& slot = meta[slot_index];
    --m_empty_count;
    if (m_empty_list_head == slot_index)
    {
        m_empty_list_head = (m_empty_count == 0) ? -1 : slot.child_index[1];
    }
    if (m_empty_count != 0)
    {
        meta[slot.child_index[0]].child_index[1] = slot.child_index[1];
        meta[slot.child_index[1]].child_index[0] = slot.child_index[0];
    }
    slot.parent_index = -1;
    slot.child_index[0] = slot.child_index[1] = -1;
    slot.balance_factor = 0;
    slot.set_is_unassigned();
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::move_to_lexed_tree(const std::int32_t slot_index, const std::int32_t key_index) noexcept
{
    switch (meta_slots()[slot_index].get_slot_state())
    {
        case (SlotState::is_loose_slot):
        {
            remove_from_loose(slot_index);
            attach_to_lexed(slot_index, key_index);
            break;
        }
        case (SlotState::is_empty_slot):
        {
            remove_from_empty(slot_index);
            attach_to_lexed(slot_index, key_index);
            break;
        }
        default:
        {
            break;
        }
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::move_to_lexed_tree(const std::int32_t slot_index) noexcept
{
    move_to_lexed_tree(slot_index, slot_index);
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::move_to_loose_list(const std::int32_t slot_index) noexcept
{
    switch (meta_slots()[slot_index].get_slot_state())
    {
        case (SlotState::is_lexed_slot):
        {
            remove_from_lexed(slot_index);
            attach_to_loose(slot_index);
            break;
        }
        case (SlotState::is_empty_slot):
        {
            remove_from_empty(slot_index);
            attach_to_loose(slot_index);
            break;
        }
        default:
        {
            break;
        }
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::move_to_empty_list(const std::int32_t slot_index) noexcept
{
    switch (meta_slots()[slot_index].get_slot_state())
    {
        case (SlotState::is_lexed_slot):
        {
            remove_from_lexed(slot_index);
            attach_to_empty(slot_index);
            break;
        }
        case (SlotState::is_loose_slot):
        {
            remove_from_loose(slot_index);
            attach_to_empty(slot_index);
            break;
        }
        default:
        {
            break;
        }
    }
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::convert_to_rank_index(const std::int32_t slot_index) const noexcept
{
    std::int32_t rank_index = -1;
    const Slot* const meta = meta_slots();
    const SlotState state = meta[slot_index].get_slot_state();
    if (state == SlotState::is_lexed_slot)
    {
        std::int32_t scan_index = slot_index;
        while (scan_index >= 0)
        {
            ++rank_index;
            std::int32_t from_index = meta[scan_index].child_index[0];
            if (from_index < 0)
            {
                for (from_index = scan_index; (((scan_index = meta[from_index].parent_index) >= 0) && (meta[scan_index].child_index[1] != from_index)); from_index = scan_index) {}
            }
            else
            {
                for (scan_index = from_index; ((from_index = meta[scan_index].child_index[1]) >= 0); scan_index = from_index) {}
            }
        }
    }
    else if (state == SlotState::is_loose_slot)
    {
        rank_index = static_cast<std::int32_t>(m_lexed_count);
        for (std::int32_t scan_index = m_loose_list_head; scan_index != slot_index; scan_index = meta[scan_index].child_index[1])
        {
            ++rank_index;
        }
    }
    else if (state == SlotState::is_empty_slot)
    {
        rank_index = static_cast<std::int32_t>(m_lexed_count + m_loose_count);
        for (std::int32_t scan_index = m_empty_list_head; scan_index != slot_index; scan_index = meta[scan_index].child_index[1])
        {
            ++rank_index;
        }
    }
    return rank_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::locate_by_rank_index(const std::int32_t rank_index) const noexcept
{
    std::int32_t slot_index = -1;
    if (rank_index >= 0)
    {
        const Slot* const meta = meta_slots();
        std::uint32_t search_count = static_cast<std::uint32_t>(rank_index);
        if (search_count < m_lexed_count)
        {
            std::uint32_t prev_side = 0;
            if ((search_count >> 1) > (m_lexed_count >> 1))
            {   //  search backwards from end
                prev_side = 1;
                search_count = m_lexed_count - search_count - 1u;
            }
            std::uint32_t next_side = prev_side ^ 1u;
            for (std::int32_t scan_index = m_lexed_tree_root; scan_index >= 0; scan_index = meta[slot_index = scan_index].child_index[prev_side]) {}
            while (search_count)
            {
                std::int32_t from_index = meta[slot_index].child_index[next_side];
                if (from_index < 0)
                {
                    for (from_index = slot_index; (((slot_index = meta[from_index].parent_index) >= 0) && (meta[slot_index].child_index[prev_side] != from_index)); from_index = slot_index) {}
                }
                else
                {
                    for (slot_index = from_index; ((from_index = meta[slot_index].child_index[prev_side]) >= 0); slot_index = from_index) {}
                }
                --search_count;
            }
        }
        else if ((search_count -= m_lexed_count) < m_loose_count)
        {
            slot_index = m_loose_list_head;
            std::uint32_t side = 1u;
            if (search_count > (m_loose_count >> 1))
            {
                side = 0u;
                search_count = (m_loose_count - search_count);
            }
            while (search_count != 0)
            {
                slot_index = meta[slot_index].child_index[side];
                --search_count;
            }
        }
        else if ((search_count -= m_loose_count) < m_empty_count)
        {
            slot_index = m_empty_list_head;
            std::uint32_t side = 1u;
            if (search_count > (m_empty_count >> 1))
            {
                side = 0u;
                search_count = (m_empty_count - search_count);
            }
            while (search_count != 0)
            {
                slot_index = meta[slot_index].child_index[side];
                --search_count;
            }
        }
    }
    return slot_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::locate_any_equal(const std::int32_t key_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t found_index = m_lexed_tree_root;
    while (found_index >= 0)
    {
        std::int32_t relationship = slot_backing().on_compare_keys(key_index, found_index);
        if (relationship == 0)
        {
            break;
        }
        found_index = meta[found_index].child_index[(relationship < 0) ? 0 : 1];
    }
    return found_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::locate_first_equal(const std::int32_t key_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t found_index = -1;
    std::int32_t check_index = m_lexed_tree_root;
    while (check_index >= 0)
    {
        std::int32_t relationship = slot_backing().on_compare_keys(key_index, check_index);
        if (relationship == 0)
        {
            found_index = check_index;
        }
        check_index = meta[check_index].child_index[(relationship <= 0) ? 0 : 1];
    }
    return found_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::locate_first_greater(const std::int32_t key_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t found_index = -1;
    std::int32_t check_index = m_lexed_tree_root;
    while (check_index >= 0)
    {
        std::int32_t relationship = slot_backing().on_compare_keys(key_index, check_index);
        if (relationship < 0)
        {
            found_index = check_index;
        }
        check_index = meta[check_index].child_index[(relationship < 0) ? 0 : 1];
    }
    return found_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::locate_first_greater_equal(const std::int32_t key_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t found_index = -1;
    std::int32_t check_index = m_lexed_tree_root;
    while (check_index >= 0)
    {
        std::int32_t relationship = slot_backing().on_compare_keys(key_index, check_index);
        if (relationship <= 0)
        {
            found_index = check_index;
        }
        check_index = meta[check_index].child_index[(relationship <= 0) ? 0 : 1];
    }
    return found_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::locate_last_equal(const std::int32_t key_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t found_index = -1;
    std::int32_t check_index = m_lexed_tree_root;
    while (check_index >= 0)
    {
        std::int32_t relationship = slot_backing().on_compare_keys(key_index, check_index);
        if (relationship == 0)
        {
            found_index = check_index;
        }
        check_index = meta[check_index].child_index[(relationship >= 0) ? 1 : 0];
    }
    return found_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::locate_last_less(const std::int32_t key_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t found_index = -1;
    std::int32_t check_index = m_lexed_tree_root;
    while (check_index >= 0)
    {
        std::int32_t relationship = slot_backing().on_compare_keys(key_index, check_index);
        if (relationship > 0)
        {
            found_index = check_index;
        }
        check_index = meta[check_index].child_index[(relationship > 0) ? 1 : 0];
    }
    return found_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::locate_last_less_equal(const std::int32_t key_index) const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t found_index = -1;
    std::int32_t check_index = m_lexed_tree_root;
    while (check_index >= 0)
    {
        std::int32_t relationship = slot_backing().on_compare_keys(key_index, check_index);
        if (relationship >= 0)
        {
            found_index = check_index;
        }
        check_index = meta[check_index].child_index[(relationship >= 0) ? 1 : 0];
    }
    return found_index;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::min_occupied_index() const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t slot_index = 0;
    for (std::uint32_t slot_count = m_capacity; slot_count > 0; --slot_count)
    {
        const Slot& slot = meta[slot_index];
        if (!slot.is_empty_slot())
        {
            return static_cast<std::int32_t>(slot_index);
        }
        ++slot_index;
    }
    return -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::int32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::max_occupied_index() const noexcept
{
    const Slot* const meta = meta_slots();
    std::int32_t slot_index = static_cast<std::int32_t>(m_capacity - 1u);
    for (std::uint32_t slot_count = m_capacity; slot_count > 0; --slot_count)
    {
        const Slot& slot = meta[slot_index];
        if (!slot.is_empty_slot())
        {
            return static_cast<std::int32_t>(slot_index);
        }
        --slot_index;
    }
    return -1;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::subtree_height(const std::int32_t slot_index) const noexcept
{
    if (slot_index >= 0)
    {
        Slot& slot = meta_slots()[slot_index];
        return std::max(subtree_height(slot.child_index[0]), subtree_height(slot.child_index[1])) + 1u;
    }
    return 0;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline std::uint32_t TOrderedSlots<TSlotBacking, TIndex, TMeta>::subtree_weight(const std::int32_t slot_index) const noexcept
{
    if (slot_index >= 0)
    {
        Slot& slot = meta_slots()[slot_index];
        return subtree_weight(slot.child_index[0]) + subtree_weight(slot.child_index[1]) + 1;
    }
    return 0;
}

//  This function should only be called on construction or after a call to shutdown().
template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::move_from(TOrderedSlots& src) noexcept
{
    m_capacity = src.m_capacity;
    m_peak_usage = src.m_peak_usage;
    m_peak_index = src.m_peak_index;
    m_high_index = src.m_high_index;
    m_lexed_count = src.m_lexed_count;
    m_loose_count = src.m_loose_count;
    m_empty_count = src.m_empty_count;
    m_lexed_tree_root = src.m_lexed_tree_root;
    m_loose_list_head = src.m_loose_list_head;
    m_empty_list_head = src.m_empty_list_head;
    m_meta_slot_array = std::move(src.m_meta_slot_array);
    src.set_empty();
    return true;
}

//  This function should only be called on construction or after a call to shutdown().
template<typename TSlotBacking, typename TIndex, typename TMeta>
inline bool TOrderedSlots<TSlotBacking, TIndex, TMeta>::copy_from(const TOrderedSlots& src) noexcept
{
    if (src.is_initialised())
    {
        if (!m_meta_slot_array.clone(src.m_meta_slot_array))
        {
            return false;
        }

        m_capacity = src.m_capacity;
        m_peak_usage = src.m_peak_usage;
        m_peak_index = src.m_peak_index;
        m_high_index = src.m_high_index;
        m_lexed_count = src.m_lexed_count;
        m_loose_count = src.m_loose_count;
        m_empty_count = src.m_empty_count;
        m_lexed_tree_root = src.m_lexed_tree_root;
        m_loose_list_head = src.m_loose_list_head;
        m_empty_list_head = src.m_empty_list_head;
    }
    else
    {
        set_empty();
    }
    return true;
}

template<typename TSlotBacking, typename TIndex, typename TMeta>
inline void TOrderedSlots<TSlotBacking, TIndex, TMeta>::set_empty() noexcept
{
    m_capacity = 0;
    m_peak_usage = 0;
    m_peak_index = -1;
    m_high_index = -1;
    m_lexed_count = 0;
    m_loose_count = 0;
    m_empty_count = 0;
    m_lexed_tree_root = -1;
    m_loose_list_head = -1;
    m_empty_list_head = -1;
    m_meta_slot_array.deallocate();
}

}   //  namespace slots

#endif  //  TORDERED_SLOTS_HPP_INCLUDED
