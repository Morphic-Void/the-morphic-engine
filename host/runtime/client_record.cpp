
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    client_record.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Exact-instance execution state, reply admission and bounded inbox delivery.

#include <algorithm>
#include <cstring>

#include "runtime/client_record.hpp"

namespace host
{

SClientRecord::SClientRecord(const module_ids::id_type module, const std::uint64_t token,
    threading::CThreadPackage* const package) noexcept
    : instance{ token }, identity{ token }, module{ module }, thread{ package }
    , execution{ (package != nullptr) ? EClientExecution::threaded : EClientExecution::passive }
{}

module_ids::id_type SClientRecord::module_id() const noexcept
{
    return module;
}

void SClientRecord::request_exit() noexcept
{
    stop_requested = true;
    if (thread != nullptr)
    {
        thread->request_exit();
    }
}

threading::CBatchClient* SClientRecord::batch_client() const noexcept
{
    return (thread != nullptr) ? thread->batch_client() : nullptr;
}

threading::EThreadRunState SClientRecord::query_state() const noexcept
{
    if (thread != nullptr)
    {
        return thread->query_state();
    }
    if ((phase == EClientPhase::stopped) || (phase == EClientPhase::retired))
    {
        return threading::EThreadRunState::Exited;
    }
    return threading::EThreadRunState::Running;
}

bool SClientRecord::terminal() const noexcept
{
    const auto state = query_state();
    return (state == threading::EThreadRunState::Exited) || (state == threading::EThreadRunState::Failed) ||
        ((thread != nullptr) && (state == threading::EThreadRunState::Empty));
}

bool SClientRecord::reserve_replies(const std::uint32_t count) noexcept
{
    if (thread != nullptr)
    {
        if (count > UINT32_MAX - reserved_replies)
        {
            return false;
        }
    }
    else if (count > k_reply_capacity - std::min(k_reply_capacity, message_count + reserved_replies))
    {
        return false;
    }
    reserved_replies += count;
    return true;
}

bool SClientRecord::reserve_control_replies(const std::uint32_t count) noexcept
{
    if (count > k_control_capacity - std::min(k_control_capacity, control_count + reserved_control_replies))
    {
        return false;
    }
    reserved_control_replies += count;
    return true;
}

bool SClientRecord::replies_idle() const noexcept
{
    return (reserved_replies == 0u) && (reserved_control_replies == 0u);
}

void SClientRecord::cancel_reservation(const std::uint32_t count) noexcept
{
    MV_ASSERT(count <= reserved_replies);
    reserved_replies -= count;
}

void SClientRecord::cancel_control_reservation(const std::uint32_t count) noexcept
{
    MV_ASSERT(count <= reserved_control_replies);
    reserved_control_replies -= count;
}

bool SClientRecord::post(const threading::CErasedPodMsg& message) noexcept
{
    if (terminal() || (phase == EClientPhase::retired))
    {
        return false;
    }
    if (thread != nullptr)
    {
        return thread->post(message);
    }
    if (control_count + reserved_control_replies >= k_control_capacity)
    {
        return false;
    }
    control_messages[control_count++] = message;
    return true;
}

bool SClientRecord::post_reply(const threading::CErasedPodMsg& message) noexcept
{
    if (terminal() || (phase == EClientPhase::retired))
    {
        return false;
    }
    if (thread != nullptr)
    {
        return thread->post(message);
    }
    if (message_count + reserved_replies >= k_reply_capacity)
    {
        return false;
    }
    messages[message_count++] = message;
    return true;
}

bool SClientRecord::reply(const threading::CErasedPodMsg& message) noexcept
{
    if (reserved_replies != 0u)
    {
        --reserved_replies;
    }
    return terminal() || post_reply(message);
}

bool SClientRecord::reply_control(const threading::CErasedPodMsg& message) noexcept
{
    if (reserved_control_replies == 0u)
    {   //  Existing threaded/test callers.
        return reply(message);
    }
    --reserved_control_replies;
    return terminal() || post(message);
}

bool SClientRecord::retry_reply(const threading::CErasedPodMsg& message) noexcept
{
    const bool reserved = reserved_replies != 0u;
    if (reply(message))
    {
        return true;
    }
    if (reserved)
    {
        ++reserved_replies;
    }
    return false;
}

EReplyPublication SClientRecord::publish_reply(const threading::CErasedPodMsg& message) noexcept
{
    MV_ASSERT(reserved_replies != 0u);
    if (terminal())
    {
        cancel_reservation(1u);
        return EReplyPublication::cancelled;
    }
    cancel_reservation(1u);
    if (post_reply(message)) return EReplyPublication::queued;
    if (terminal()) return EReplyPublication::cancelled;
    ++reserved_replies;
    return EReplyPublication::busy;
}

bool SClientRecord::execution_quiescent() const noexcept
{
    if (!terminal_before_drain || !terminal() || !incoming_drained) return false;
    const auto* const batch = batch_client();
    return (batch == nullptr) || ((batch->outstanding() == 0u) && (batch->return_publishers() == 0u));
}

void SClientRecord::flush() noexcept
{
    if (terminal())
    {
        message_count = control_count = 0u;
        return;
    }
    if (thread == nullptr)
    {
        return;
    }
    std::uint32_t consumed = 0u;
    while ((consumed < message_count) && thread->post(messages[consumed]))
    {
        ++consumed;
    }
    consume(consumed);
}

std::uint32_t SClientRecord::copy_messages(threading::CErasedPodMsg* output, const std::uint32_t capacity, std::uint32_t& controls) const noexcept
{
    controls = std::min(control_count, capacity);
    const auto ordinary = std::min(message_count, capacity - controls);
    std::memcpy(output, control_messages, controls * sizeof(*output));
    std::memcpy(output + controls, messages, ordinary * sizeof(*output));
    return controls + ordinary;
}

void SClientRecord::consume_messages(const std::uint32_t count, const std::uint32_t offered_controls) noexcept
{
    const auto controls = std::min(count, offered_controls);
    MV_ASSERT(controls <= control_count);
    control_count -= controls;
    if (control_count != 0u)
    {
        std::memmove(control_messages, control_messages + controls, control_count * sizeof(control_messages[0]));
    }
    consume(count - controls);
}

void SClientRecord::consume(const std::uint32_t count) noexcept
{
    MV_ASSERT(count <= message_count);
    message_count -= count;
    if (message_count != 0u)
    {
        std::memmove(messages, messages + count, message_count * sizeof(messages[0]));
    }
}

}   //  namespace host
