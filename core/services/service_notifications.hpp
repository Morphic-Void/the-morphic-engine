
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_notifications.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  One exact-consumer registration pinned through its borrow's final retirement.

#pragma once

#include "services/service_borrow.hpp"
#include "threading/CNotification.hpp"

namespace services
{

struct SNotificationRegistration { std::uint64_t value{ 0u }; };
struct RegisterServiceNotification { SServiceRoute route; SServiceBorrow borrow; };
struct ServiceNotificationRegistered
{
    SServiceRoute route;
    SServiceBorrow borrow;
    SNotificationRegistration registration;
    threading::SNotificationReader reader;
    EServiceStatus status{ EServiceStatus::invalid_request };
};

static_assert(sizeof(ServiceNotificationRegistered) <= 48u);
static_assert(std::is_trivially_copyable_v<ServiceNotificationRegistered>);

}   //  namespace services

MV_REGISTER_SYSTEM_TYPE(services::RegisterServiceNotification, system_type_ids::register_service_notification);
MV_REGISTER_SYSTEM_TYPE(services::ServiceNotificationRegistered, system_type_ids::service_notification_registered);
