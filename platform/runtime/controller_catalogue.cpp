
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    controller_catalogue.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    09 Nov 26
//
//  Nonexclusive controller catalogue borrows and revision-checked bounded queries.

#include <cstring>
#include "runtime/platform_runtime.hpp"

namespace platform::runtime
{

using namespace input;
using services::EServiceStatus;

static bool same_owner(const services::SDispatchedCommand& first, const services::SDispatchedCommand& second) noexcept
{
    return (first.client.value == second.client.value) && (first.requester.value == second.requester.value) &&
        (first.command.route.value == second.command.route.value);
}

void CPlatformRuntime::catalogue_changed() noexcept
{
    if (m_catalogue.revision == UINT64_MAX)
    {
        native_failure(UINT32_MAX);
        return;
    }
    ++m_catalogue.revision;
    if (!m_catalogue_publication.publish(m_catalogue)) native_failure(UINT32_MAX);
    for (TInstance<SCatalogueUse>& owned : m_catalogues)
    {
        if (owned && owned->ready && !owned->closing && !owned->notification.signal())
        {
            owned->closing = true;
            complete(owned->completion, EServiceStatus::delivery_failed);
        }
    }
}

bool CPlatformRuntime::catalogue_command(const services::SDispatchedCommand& dispatched, const std::int32_t completion) noexcept
{
    const services::ServiceRequest& command = dispatched.command;
    const ECommand opcode = static_cast<ECommand>(command.opcode);
    EServiceStatus status = EServiceStatus::invalid_request;
    if (opcode == ECommand::acquire_catalogue)
    {
        if (command.payload_size)
        {
            complete(completion, status);
            return true;
        }
        for (TInstance<SCatalogueUse>& owned : m_catalogues)
        {
            if (owned) continue;
            if (!owned.emplace(m_clock))
            {
                complete(completion, EServiceStatus::allocation_failed);
                return true;
            }
            SCatalogueUse& use = *owned;
            use.acquisition = dispatched;
            use.completion = completion;
            use.view.provider = m_context.instance;
            use.view.consumer = dispatched.requester;
            use.view.state = &m_catalogue_publication;
            const modules::EBudgetStatus budget = use.budget.start(m_context.execution->restriction, UINT64_C(5000000000));
            if ((budget != modules::EBudgetStatus::running) && (budget != modules::EBudgetStatus::restricted))
            {
                use.closing = true;
                complete(completion, EServiceStatus::native_failure);
            }
            start_controllers();
            return true;
        }
        complete(completion, EServiceStatus::busy);
        return true;
    }
    if (command.payload_size != sizeof(SCatalogueReady))
    {
        complete(completion, EServiceStatus::invalid_request);
        return true;
    }
    services::SServiceBorrow borrow;
    std::memcpy(&borrow, command.payload, sizeof(borrow));
    SCatalogueUse* use = nullptr;
    for (TInstance<SCatalogueUse>& owned : m_catalogues)
    {
        if (owned && borrow.value && (owned->borrow.value == borrow.value)) use = &*owned;
    }
    if (!use) status = EServiceStatus::stale;
    else if (!same_owner(use->acquisition, dispatched)) status = EServiceStatus::invalid_request;
    else if (use->closing) status = EServiceStatus::closing;
    else if (opcode == ECommand::catalogue_ready)
    {
        SCatalogueReady request;
        std::memcpy(&request, command.payload, sizeof(request));
        if (request.acquisition.value != use->acquisition.command.request.value) status = EServiceStatus::stale;
        else if (use->ready)
        {
            status = (request.registration.value == use->registration.value) ? EServiceStatus::success : EServiceStatus::stale;
        }
        else
        {
            const modules::EBudgetStatus budget = use->budget.observe().status;
            if (budget == modules::EBudgetStatus::retry) status = EServiceStatus::busy;
            else if ((budget == modules::EBudgetStatus::expired) || (budget == modules::EBudgetStatus::failed))
            {
                use->closing = true;
                status = (budget == modules::EBudgetStatus::expired) ? EServiceStatus::timed_out : EServiceStatus::native_failure;
                complete(use->completion, status);
            }
            else
            {
                status = m_context.services->bind_borrow_notification(m_context.service_context,
                    use->borrow, request.registration, &use->notification);
                if (status == EServiceStatus::success)
                {
                    use->registration = request.registration;
                    use->ready = true;
                    if (!use->notification.signal())
                    {
                        use->closing = true;
                        status = EServiceStatus::delivery_failed;
                    }
                    complete(use->completion, status);
                }
            }
        }
    }
    else if (!use->ready) status = EServiceStatus::busy;
    else if (opcode == ECommand::enumerate_device)
    {
        SEnumerateDevice request;
        std::memcpy(&request, command.payload, sizeof(request));
        if (request.reserved || (request.cursor > k_device_capacity)) status = EServiceStatus::invalid_request;
        else if (request.revision != m_catalogue.revision) status = EServiceStatus::stale;
        else
        {
            SDeviceResult result;
            result.revision = m_catalogue.revision;
            result.next_cursor = k_device_capacity;
            result.flags = k_device_end;
            for (std::uint32_t slot = request.cursor; slot < k_device_capacity; ++slot)
            {
                const SDevice& device = m_devices[slot];
                if (!device.token) continue;
                result.device = device.token;
                result.next_cursor = slot + 1u;
                result.flags = device.info.eligible ? k_device_eligible : 0u;
                for (const TInstance<CInputConnection>& connection : m_inputs)
                {
                    if (connection && (connection->snapshot().device == device.token)) result.flags |= k_device_claimed;
                }
                break;
            }
            m_completions[completion].reply.payload_size = sizeof(result);
            std::memcpy(m_completions[completion].reply.payload, &result, sizeof(result));
            status = EServiceStatus::success;
        }
    }
    else if (opcode == ECommand::device_metadata)
    {
        SDeviceMetadata request;
        std::memcpy(&request, command.payload, sizeof(request));
        status = EServiceStatus::stale;
        if (request.reserved || (request.page >= 8u)) status = EServiceStatus::invalid_request;
        else
        {
            for (const SDevice& device : m_devices)
            {
                if (!device.token || (device.token != request.device))
                {
                    continue;
                }
                SDeviceMetadataResult result;
                result.device = device.token;
                result.vendor = device.info.vendor;
                result.product = device.info.product;
                result.page = request.page;
                std::memcpy(result.name, (device.info.name + (request.page << 3u)), sizeof(result.name));
                m_completions[completion].reply.payload_size = sizeof(result);
                std::memcpy(m_completions[completion].reply.payload, &result, sizeof(result));
                status = EServiceStatus::success;
                break;
            }
        }
    }
    complete(completion, status);
    return true;
}

void CPlatformRuntime::progress_catalogues() noexcept
{
    for (TInstance<SCatalogueUse>& owned : m_catalogues)
    {
        if (!owned) continue;
        SCatalogueUse& use = *owned;
        if (m_stopping) use.closing = true;
        if (!use.ready && !use.closing)
        {
            const modules::EBudgetStatus budget = use.budget.observe().status;
            if ((budget == modules::EBudgetStatus::expired) || (budget == modules::EBudgetStatus::failed))
            {
                use.closing = true;
                complete(use.completion, ((budget == modules::EBudgetStatus::expired) ? EServiceStatus::timed_out : EServiceStatus::native_failure));
            }
            else if (!use.borrow.value && (budget != modules::EBudgetStatus::retry))
            {
                const services::SServiceBorrowOffer offer{ use.acquisition.command.route, use.acquisition.command.request,
                    { &use.view, sizeof(use.view), 1u } };
                const EServiceStatus status = m_context.services->prepare_borrow(m_context.service_context, &offer, &use.borrow);
                if ((status != EServiceStatus::success) && (status != EServiceStatus::busy))
                {
                    use.closing = true;
                    complete(use.completion, status);
                }
            }
        }
        if (!use.closing) continue;
        complete(use.completion, EServiceStatus::closing);
        use.notification = {};
        if (!use.retirement_started && use.borrow.value)
        {
            const EServiceStatus status = m_context.services->begin_borrow_retirement(m_context.service_context, use.borrow);
            if (status == EServiceStatus::success) use.retirement_started = true;
            else if (status != EServiceStatus::busy) native_failure(static_cast<std::uint32_t>(status));
        }
        const bool quiescent = !use.borrow.value || (use.quiescence == services::EServiceQuiescence::never_published) ||
            (use.quiescence == services::EServiceQuiescence::relinquished) || (use.quiescence == services::EServiceQuiescence::execution_ended);
        if (quiescent && (use.completion < 0) && !m_native_depth && !m_callback_depth && !m_modal_depth[0] && !m_modal_depth[1])
        {
            if (!use.borrow.value || (m_context.services->finish_borrow_retirement(m_context.service_context, use.borrow) == EServiceStatus::success))
            {
                owned.reset();
            }
        }
    }
}

}   //  namespace platform::runtime
