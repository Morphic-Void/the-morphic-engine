
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    provider_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Nov 26
//
//  Asynchronous provider operations on the ordinary endpoint POD lanes.

#pragma once

#include "framework/services/service_notifications.hpp"

namespace services
{

struct ServiceCommand
{
    std::uint64_t operation{ 0u };
    const SDispatchedCommand* descriptor{ nullptr };
};
struct CompleteServiceRequest { ServiceReply reply; };
struct CommitServiceResource { ServiceReply reply; };
struct PrepareServiceBorrow { SServiceBorrowOffer offer; };
struct BeginServiceBorrowRetirement { SServiceBorrow borrow; };
struct FinishServiceBorrowRetirement { SServiceBorrow borrow; };
struct BindServiceNotification { SServiceBorrow borrow; SNotificationRegistration registration; };

//  The envelope slot correlates each operation, including complete/commit.
struct ProviderOperationResult
{
    SServiceBorrow borrow;
    threading::SNotificationEndpoint notification;
    EServiceStatus status{ EServiceStatus::invalid_request };
};

static_assert(sizeof(CompleteServiceRequest) <= 48u);
static_assert(sizeof(CommitServiceResource) <= 48u);
static_assert(sizeof(PrepareServiceBorrow) <= 48u);
static_assert(sizeof(ProviderOperationResult) <= 48u);

}   //  namespace services

MV_REGISTER_SYSTEM_TYPE(services::ServiceCommand, system_type_ids::service_command);
MV_REGISTER_SYSTEM_TYPE(services::CompleteServiceRequest, system_type_ids::complete_service_request);
MV_REGISTER_SYSTEM_TYPE(services::CommitServiceResource, system_type_ids::commit_service_resource);
MV_REGISTER_SYSTEM_TYPE(services::PrepareServiceBorrow, system_type_ids::prepare_service_borrow);
MV_REGISTER_SYSTEM_TYPE(services::BeginServiceBorrowRetirement, system_type_ids::begin_service_borrow_retirement);
MV_REGISTER_SYSTEM_TYPE(services::FinishServiceBorrowRetirement, system_type_ids::finish_service_borrow_retirement);
MV_REGISTER_SYSTEM_TYPE(services::BindServiceNotification, system_type_ids::bind_service_notification);
MV_REGISTER_SYSTEM_TYPE(services::ProviderOperationResult, system_type_ids::provider_operation_result);
