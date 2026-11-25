
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    module_execution.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Versioned execution callbacks and a bound, nonrecursive communication facade.

#pragma once

#include <type_traits>
#include <utility>

#include "framework/module/module_binding.hpp"
#include "framework/module/host_execution_context.hpp"
#include "framework/communications/connection_types.hpp"
#include "framework/threading/messages/CErasedPodMsg.hpp"
#include "framework/threading/messages/CErasedOwnerMsg.hpp"
#include "foundation/memory/memory_view.hpp"
#include "foundation/platform/threading/thread_priority.hpp"

namespace threading { class CBatchClient; }

namespace modules
{

enum class EExecutionStatus : std::uint32_t { success, invalid_argument, unsupported_version, failed };
enum class EExecutionStop : std::uint32_t { requested, replacement, shutdown, creation_failure, execution_failure };
enum class ESubmission : std::uint32_t
{
    accepted, busy, closed, no_pipe, invalid, transport_failed, published_wake_failed
};

[[nodiscard]] inline bool accepted(const ESubmission status) noexcept
{
    return (status == ESubmission::accepted) || (status == ESubmission::published_wake_failed);
}

struct SDestination
{
    enum class EKind : std::uint32_t { host, self, peer, connection };
    EKind kind{ EKind::host };
    system_ids::id_type peer{};
    connections::SToken token; //  Exact connection, or an optional expected peer connection.
};

struct SExecutionV1;
struct SChildExecution
{
    //  Descriptor and context remain live through the correlated creation outcome.
    //  The created execution retains context according to its module's contract.
    const SExecutionV1* descriptor{ nullptr };
    void* context{ nullptr };
    thread_ids::id_type thread{};
    platform::threading::EThreadPriority priority{ platform::threading::EThreadPriority::Normal };
    std::uint32_t designated_service{ 0u };
};

struct SExecutionIntent
{
    enum class EKind : std::uint32_t
    {
        participate, connect, consent, close_connection, connection_quiescent,
        create_child, stop_child, close_provider_admission, stop_module, fail
    };
    EKind kind{ EKind::participate };
    std::int32_t correlation{ -1 };
    connections::SToken connection;
    connections::EStatus decision{ connections::EStatus::rejected };
    system_ids::id_type first{}, second{};
    connections::SBulkConfiguration bulk;
    std::uint32_t connection_version{ 1u };
    services::SClientToken endpoint;
    SChildExecution child;
};

struct SConnectionQuery
{
    connections::SToken token;
    system_ids::id_type peer{};
    std::uint32_t active{ 0u }, closing{ 0u };
};

//  The immutable table has Host lifetime; only the cookie is endpoint-specific.
//  Every entry compares cookie with the currently executing context before
//  dereferencing it. A copied stale facade cannot dereference a reclaimed table.
//  This fixed table crosses the DLL-to-Host boundary: modules do not import Host
//  functions, and their Framework code belongs to their own image. Host-internal
//  routing uses ordinary direct calls. This is not registration or discovery.
//  Intents share the outbound facade; they expose no private transport records.
struct SCommunicationV1
{
    ESubmission(MV_STD_ABI_CALL* pod)(void* const, const SDestination&, const threading::CErasedPodMsg&) noexcept{ nullptr };
    ESubmission(MV_STD_ABI_CALL* owner)(void* const, const SDestination&, threading::CErasedOwnerMsg&&) noexcept{ nullptr };
    ESubmission(MV_STD_ABI_CALL* intent)(void* const, const SExecutionIntent&) noexcept{ nullptr };
    SConnectionQuery(MV_STD_ABI_CALL* query)(void* const, const SDestination&) noexcept{ nullptr };
    memory::CMemoryView(MV_STD_ABI_CALL* producer_data)(void* const, const connections::SToken) noexcept{ nullptr };
    connections::EBulkPublish(MV_STD_ABI_CALL* publish_bulk)(void* const, const connections::SToken) noexcept{ nullptr };
    bool(MV_STD_ABI_CALL* acquire_bulk)(void* const, const connections::SToken) noexcept{ nullptr };
    memory::CMemoryConstView(MV_STD_ABI_CALL* consumer_data)(void* const, const connections::SToken) noexcept{ nullptr };
};

class CCommunication
{
public:
    CCommunication() noexcept = default;
    CCommunication(const SCommunicationV1& functions, void* const context) noexcept
        : m_functions{ &functions }, m_context{ context } {}

    [[nodiscard]] ESubmission submit(const SDestination& destination, const threading::CErasedPodMsg& message) const noexcept
    {
        return (m_functions && m_functions->pod) ? m_functions->pod(m_context, destination, message) : ESubmission::invalid;
    }
    [[nodiscard]] ESubmission submit(const SDestination& destination, threading::CErasedOwnerMsg&& message) const noexcept
    {
        return (m_functions && m_functions->owner) ? m_functions->owner(m_context, destination, std::move(message)) : ESubmission::invalid;
    }
    [[nodiscard]] ESubmission submit(const SExecutionIntent& intent) const noexcept
    {
        return (m_functions && m_functions->intent) ? m_functions->intent(m_context, intent) : ESubmission::invalid;
    }
    [[nodiscard]] SConnectionQuery query(const SDestination& destination) const noexcept
    {
        return (m_functions && m_functions->query) ? m_functions->query(m_context, destination) : SConnectionQuery{};
    }
    [[nodiscard]] memory::CMemoryView producer_data(const connections::SToken token) const noexcept
    {
        return (m_functions && m_functions->producer_data) ? m_functions->producer_data(m_context, token) : memory::CMemoryView{};
    }
    [[nodiscard]] connections::EBulkPublish publish_bulk(const connections::SToken token) const noexcept
    {
        return (m_functions && m_functions->publish_bulk) ? m_functions->publish_bulk(m_context, token) : connections::EBulkPublish::inactive;
    }
    [[nodiscard]] bool discard_and_acquire(const connections::SToken token) const noexcept
    {
        return m_functions && m_functions->acquire_bulk && m_functions->acquire_bulk(m_context, token);
    }
    [[nodiscard]] memory::CMemoryConstView consumer_data(const connections::SToken token) const noexcept
    {
        return (m_functions && m_functions->consumer_data) ? m_functions->consumer_data(m_context, token) : memory::CMemoryConstView{};
    }

private:
    const SCommunicationV1* m_functions{ nullptr };
    void* m_context{ nullptr };
};

struct SExecutionContext
{
    std::uint32_t size{ sizeof(SExecutionContext) }, version{ 1u };
    SModuleInstance instance;
    services::SClientToken endpoint;
    system_ids::id_type address{};
    const CCommunication* communication{ nullptr };
    const SHostExecutionContext* execution{ nullptr };
    threading::CBatchClient* batch{ nullptr };
    platform::threading::SWakeEndpoint owner_wake;
    void* creation_context{ nullptr };
};

enum class EExecutionEvent : std::uint32_t
{
    start, stop, service_async, message, participation, connection_offered,
    connection_available, connection_closing, connection_retired, connection_result,
    child_created, child_ready, child_ended, child_retired, child_stop_result, provider_admission_closed
};

struct SExecutionNotification
{
    connections::SToken connection;
    services::SClientToken first, second;
    system_ids::id_type peer{};
    connections::SBulkConfiguration bulk;
    connections::EStatus connection_status{ connections::EStatus::invalid_request };
    services::EServiceStatus status{ services::EServiceStatus::invalid_request };
    std::uint32_t connection_version{ 1u };
    std::int32_t correlation{ -1 };
    EExecutionStop stop_reason{ EExecutionStop::requested };
};

struct SInvocation
{
    std::uint32_t size{ sizeof(SInvocation) }, version{ 1u };
    EExecutionEvent event{ EExecutionEvent::service_async };
    connections::SSource source;
    const threading::CErasedPodMsg* message{ nullptr }; //  Only for event == message; expires on return.
    SExecutionNotification notification;
    std::uint64_t time_budget_ns{ 0u };
};

struct SExecutionProgress
{
    std::uint32_t ready{ 0u }; //  Monotonic; never reverses stopping or failure.
    std::uint32_t work_pending{ 0u }; //  Another asynchronous invocation can progress now.
    std::uint32_t direct_input{ 1u };
    std::uint32_t work_complete{ 0u }; //  Domain cleanup complete; not permission to retire resources.
    std::uint32_t native_events_active{ 0u }, stop_requested{ 0u };
    std::uint32_t error_domain{ 0u }, error_code{ 0u };
    std::uint64_t next_wake_ns{ UINT64_MAX }; //  Maximum delay from this callback's return.
};

inline constexpr char k_execution_symbol_name[]{ "morphic_module_execution_v1" };
inline constexpr std::uint32_t k_execution_native_events = 1u;

struct SExecutionV1
{
    std::uint32_t size{ sizeof(SExecutionV1) }, version{ 1u }, flags{ 0u };
    //  Framework's TExecutionInstance<T> supplies single-allocation opaque
    //  storage. Its handles must be accessed with get(), not cast to T*.
    //  Handle layout is private to the creating module; Host only carries it.
    EExecutionStatus(MV_STD_ABI_CALL* create)(const SExecutionContext* const, void** const) noexcept{ nullptr };
    //  Success means this invocation supplied valid progress. A failed return
    //  latches a fatal endpoint failure and its progress is not consumed. Later
    //  stop/retirement invocations must return success with valid cleanup progress
    //  when they can do so; this does not recover the module from its latched
    //  failure. Repeating a sticky failed status would hide cleanup completion.
    EExecutionStatus(MV_STD_ABI_CALL* handle)(void* const, const SInvocation* const, SExecutionProgress* const) noexcept{ nullptr };
    //  A failed destroy retains the object and its DLL. Host never invokes it
    //  again or resumes domain callbacks; failure does not reopen execution.
    EExecutionStatus(MV_STD_ABI_CALL* destroy)(void* const) noexcept{ nullptr };
    //  A null callback set selects the Host standard coordinator. Its optional
    //  child descriptor remains immutable through the module binding lifetime.
    const SChildExecution* initial_child{ nullptr };

    [[nodiscard]] bool is_valid() const noexcept
    {
        if ((size != sizeof(SExecutionV1)) || (version != 1u) || ((flags & ~k_execution_native_events) != 0u))
        {
            return false;
        }
        const bool callbacks = create && handle && destroy;
        const bool coordinator = !create && !handle && !destroy && (flags == 0u);
        return (callbacks && !initial_child) || coordinator;
    }
};

using FQueryExecution = EBindingResult(MV_STD_ABI_CALL*)(const std::uint32_t, SExecutionV1* const) noexcept;

static_assert(std::is_standard_layout_v<SExecutionV1> && std::is_trivially_copyable_v<SExecutionV1>);
static_assert(std::is_standard_layout_v<SExecutionContext> && std::is_trivially_copyable_v<SExecutionContext>);
static_assert(std::is_standard_layout_v<SInvocation> && std::is_trivially_copyable_v<SInvocation>);
static_assert(std::is_standard_layout_v<SExecutionIntent> && std::is_trivially_copyable_v<SExecutionIntent>);

}   //  namespace modules
