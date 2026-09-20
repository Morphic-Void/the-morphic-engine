
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    AsyncState_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26

#include <cstdint>
#include <iostream>
#include <type_traits>

#include "system/async_state.hpp"
#include "system/transported_types.hpp"
#include "tests/test_suites/AsyncState_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace async_state_test_types
{
struct SLocalState { std::uint32_t value; };
}

MV_REGISTER_LOCAL_TYPE(
    async_state_test_types::SLocalState,
    local_type_ids::ops::encode_id(local_type_ids::ops::encode_index(1235u)));

namespace
{

using TTestContext = tests::TTestContext;

void test_state_shape_and_default_repository(TTestContext& ctx)
{
    static_assert(std::is_trivially_copyable_v<CASyncState>);
    static_assert(std::is_standard_layout_v<CASyncState>);
    static_assert(CASyncState::k_payload_size == 48u);
    static_assert(CASyncState::k_payload_align == 16u);
    static_assert(sizeof(CASyncState) == 64u);
    static_assert(alignof(CASyncState) == 16u);

    CASyncStates states;
    TEST_EXPECT(ctx, states.is_valid());
    TEST_EXPECT(ctx, states.is_empty());
    TEST_EXPECT(ctx, !states.is_ready());
    TEST_EXPECT(ctx, states.acquire<UnrecognisedMsg>() == -1);
    TEST_EXPECT(ctx, states.resolve(0) == nullptr);
    TEST_EXPECT(ctx, states.payload<UnrecognisedMsg>(0) == nullptr);
    TEST_EXPECT(ctx, states.redefine<UnrecognisedMsg>(0) == nullptr);
    TEST_EXPECT(ctx, !states.release(0));
}

void test_acquisition_redefinition_and_release(TTestContext& ctx)
{
    CASyncStates states;
    TEST_EXPECT(ctx, states.initialise(2u));
    TEST_EXPECT(ctx, states.is_ready());

    const std::int32_t first_slot = states.acquire<UnrecognisedMsg>(41u);
    const std::int32_t second_slot = states.acquire<FileSaveResult>(73u);
    TEST_EXPECT(ctx, first_slot >= 0);
    TEST_EXPECT(ctx, second_slot >= 0);
    TEST_EXPECT(ctx, first_slot != second_slot);

    CASyncState* const first_state = states.resolve(first_slot);
    TEST_EXPECT(ctx, first_state != nullptr);
    TEST_EXPECT(ctx, first_state->query_tag() == 41u);
    TEST_EXPECT(ctx, first_state->is_a<UnrecognisedMsg>());
    TEST_EXPECT(ctx, states.payload<FileSaveResult>(first_slot) == nullptr);

    UnrecognisedMsg* const initial_payload = states.payload<UnrecognisedMsg>(first_slot);
    TEST_EXPECT(ctx, initial_payload != nullptr);
    TEST_EXPECT(ctx, initial_payload->msg_id == system_type_ids::undefined);
    initial_payload->msg_id = system_type_ids::file_load_request;

    FileSaveResult* const redefined_payload = states.redefine<FileSaveResult>(first_slot);
    TEST_EXPECT(ctx, redefined_payload != nullptr);
    TEST_EXPECT(ctx, !redefined_payload->success);
    TEST_EXPECT(ctx, states.resolve(first_slot)->query_tag() == 41u);
    TEST_EXPECT(ctx, states.payload<UnrecognisedMsg>(first_slot) == nullptr);
    TEST_EXPECT(ctx, states.payload<FileSaveResult>(first_slot) == redefined_payload);

    TEST_EXPECT(ctx, states.release(first_slot));
    TEST_EXPECT(ctx, states.resolve(first_slot) == nullptr);
    TEST_EXPECT(ctx, !states.release(first_slot));
    TEST_EXPECT(ctx, states.resolve(second_slot)->query_tag() == 73u);
    TEST_EXPECT(ctx, states.check_integrity());

    TEST_EXPECT(ctx, states.release(second_slot));
    TEST_EXPECT(ctx, states.is_empty());
}

void test_reused_slot_is_reinitialised(TTestContext& ctx)
{
    CASyncStates states;
    TEST_EXPECT(ctx, states.initialise());

    const std::int32_t released_slot = states.acquire<UnrecognisedMsg>(0xffffffffu);
    TEST_EXPECT(ctx, released_slot >= 0);
    states.payload<UnrecognisedMsg>(released_slot)->msg_id = system_type_ids::asset_save_request;
    TEST_EXPECT(ctx, states.release(released_slot));

    const std::int32_t acquired_slot = states.acquire<FileSaveResult>(7u);
    TEST_EXPECT(ctx, acquired_slot >= 0);
    TEST_EXPECT(ctx, states.resolve(acquired_slot)->query_tag() == 7u);
    TEST_EXPECT(ctx, states.payload<FileSaveResult>(acquired_slot) != nullptr);
    TEST_EXPECT(ctx, !states.payload<FileSaveResult>(acquired_slot)->success);
    TEST_EXPECT(ctx, states.payload<UnrecognisedMsg>(acquired_slot) == nullptr);
}

void test_local_async_state(TTestContext& ctx)
{
    using local_type = async_state_test_types::SLocalState;
    CASyncStates states;
    TEST_EXPECT(ctx, states.initialise(1u));

    const std::int32_t slot = states.acquire<local_type>(19u);
    TEST_EXPECT(ctx, slot >= 0);
    TEST_EXPECT(ctx, states.resolve(slot) != nullptr);
    TEST_EXPECT(ctx, states.resolve(slot)->query_type_id() == k_type_id_v<local_type>);
    TEST_EXPECT(ctx, states.resolve(slot)->query_type_id().is_local());
    TEST_EXPECT(ctx, states.payload<local_type>(slot) != nullptr);
    states.payload<local_type>(slot)->value = 91u;
    TEST_EXPECT(ctx, states.payload<local_type>(slot)->value == 91u);

    FileSaveResult* const system_payload = states.redefine<FileSaveResult>(slot);
    TEST_EXPECT(ctx, system_payload != nullptr);
    TEST_EXPECT(ctx, states.resolve(slot)->query_type_id().is_system());
    TEST_EXPECT(ctx, states.resolve(slot)->query_tag() == 19u);
    TEST_EXPECT(ctx, states.payload<local_type>(slot) == nullptr);
    TEST_EXPECT(ctx, states.release(slot));
}

}   //  namespace

int run_async_state_tests()
{
    TTestContext ctx;
    test_state_shape_and_default_repository(ctx);
    test_acquisition_redefinition_and_release(ctx);
    test_reused_slot_is_reinitialised(ctx);
    test_local_async_state(ctx);

    std::cout << "AsyncState: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
