
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_borrow.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Provider-owned views and execution-independent, cooperative borrow retirement.

#pragma once

#include <type_traits>
#include "services/service_protocol.hpp"

namespace services
{

struct SServiceBorrow { std::uint64_t value{ 0u }; };

struct SServiceView
{
    const void* descriptor{ nullptr };
    std::uint32_t size{ 0u };
    std::uint32_t version{ 0u };
};

struct SServiceBorrowOffer
{
    SServiceRoute route;
    SServiceRequestId request;
    SServiceView view;
};

//  The prepared ServiceReply payload. Domain identities belong to the descriptor.
struct SPreparedServiceBorrow
{
    SServiceBorrow borrow;
    SServiceView view;
};

enum class EServiceBorrowControl : std::uint32_t { closing = 0u, consumer_quiescent, retired };
enum class EServiceQuiescence : std::uint32_t
{
    none = 0u, never_published, relinquished, execution_ended
};

//  No immediate reply credit is required by these idempotent cleanup messages.
//  Submission success means queued, not that provider retirement has completed.
struct ReleaseServiceBorrow { SServiceRoute route; SServiceBorrow borrow; };
struct ServiceBorrowQuiescent { SServiceRoute route; SServiceBorrow borrow; };
//  Sent only after receipt acknowledgment. Before that, the request may still
//  be in an ordinary outbound queue that cleanup admission can overtake.
struct CancelServiceRequest { SServiceRoute route; SServiceRequestId request; };

//  Host-to-client/provider only, independent of the acquisition's async slot.
//  execution_ended proves registered CPU access ended, not external/native cleanup.
struct ServiceBorrowControl
{
    SServiceRoute route;
    SServiceRequestId request;
    SServiceBorrow borrow;
    EServiceBorrowControl control{ EServiceBorrowControl::closing };
    EServiceQuiescence quiescence{ EServiceQuiescence::none };
};

//  An accepted provider operation must still complete after cancellation. Its
//  late result drains work; it cannot publish a new view to the cancelled client.
struct ServiceRequestCancelled { SServiceRoute route; SServiceRequestId request; };

static_assert(sizeof(SPreparedServiceBorrow) <= sizeof(ServiceReply::payload));
static_assert(std::is_trivially_copyable_v<SPreparedServiceBorrow>);
static_assert(sizeof(ServiceBorrowControl) <= 48u);

}   //  namespace services

MV_REGISTER_SYSTEM_TYPE(services::ReleaseServiceBorrow, system_type_ids::release_service_borrow);
MV_REGISTER_SYSTEM_TYPE(services::ServiceBorrowQuiescent, system_type_ids::service_borrow_quiescent);
MV_REGISTER_SYSTEM_TYPE(services::CancelServiceRequest, system_type_ids::cancel_service_request);
MV_REGISTER_SYSTEM_TYPE(services::ServiceBorrowControl, system_type_ids::service_borrow_control);
MV_REGISTER_SYSTEM_TYPE(services::ServiceRequestCancelled, system_type_ids::service_request_cancelled);
