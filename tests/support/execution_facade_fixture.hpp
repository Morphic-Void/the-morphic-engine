
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    execution_facade_fixture.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    24 Nov 26
//
//  Bounded POD capture and explicit module-operation validation for fixture tests.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/communications/module_tests.hpp"

namespace tests
{

template<unsigned Capacity>
struct TPodSubmissionCapture
{
    modules::ESubmission result{ modules::ESubmission::busy };
    threading::CErasedPodMsg posts[Capacity];
    unsigned count{}, pod_attempts{};
    bool valid{ true };

    modules::ESubmission capture(const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        ++pod_attempts;
        valid &= destination.kind == modules::SDestination::EKind::host;
        if (modules::accepted(result))
        {
            if (count == Capacity) valid = false;
            else posts[count++] = message;
        }
        return result;
    }

    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie,
        const modules::SDestination& destination, const threading::CErasedPodMsg& message) noexcept
    {
        return static_cast<TPodSubmissionCapture*>(cookie)->capture(destination, message);
    }
};

struct SLifecycleExecutiveFacade
{
    modules::ESubmission result{ modules::ESubmission::busy };
    modules::ESubmission pod_result{ modules::ESubmission::busy };
    unsigned attempts{ 0u }, accepted{ 0u };
    unsigned pods{ 0u }, connections{ 0u }, quiescences{ 0u };
    std::int32_t pod_slots[3]{};
    modules::EModuleAction action{};
    module_ids::id_type module{};
    std::int32_t correlation{ -1 };
    bool valid{ true };
    static modules::ESubmission MV_STD_ABI_CALL pod(void* const cookie, const modules::SDestination& destination,
        const threading::CErasedPodMsg& message) noexcept
    {
        SLifecycleExecutiveFacade& self = *static_cast<SLifecycleExecutiveFacade*>(cookie);
        modules::RunTestsRequest request;
        self.valid &= (destination.kind == modules::SDestination::EKind::connection) &&
            (destination.token.value == 17u) && message.copy_payload_to(request) && (request.module == module_ids::audio);
        if (modules::accepted(self.pod_result))
        {
            if (self.pods < 3u) self.pod_slots[self.pods] = message.query_async_slot();
            else self.valid = false;
            ++self.pods;
        }
        return self.pod_result;
    }
    static modules::ESubmission MV_STD_ABI_CALL intent(void* const cookie, const modules::SExecutionIntent& request) noexcept
    {
        SLifecycleExecutiveFacade& self = *static_cast<SLifecycleExecutiveFacade*>(cookie);
        if (request.kind == modules::SExecutionIntent::EKind::connect)
        {
            ++self.connections;
            self.valid &= (request.correlation == 50) && (request.first == system_ids::executive) &&
                (request.second == modules::test_connections::address(module_ids::audio));
        }
        else if (request.kind == modules::SExecutionIntent::EKind::connection_quiescent)
        {
            ++self.quiescences;
            self.valid &= request.connection.value == 17u;
        }
        else self.valid &= request.kind == modules::SExecutionIntent::EKind::participate;
        return modules::ESubmission::accepted;
    }
    static modules::ESubmission MV_STD_ABI_CALL owner(void* const cookie, const modules::SDestination& destination,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SLifecycleExecutiveFacade& self = *static_cast<SLifecycleExecutiveFacade*>(cookie);
        ++self.attempts;
        const modules::ModuleRequest* const request = message.owner().payload<modules::ModuleRequest>();
        self.valid &= (destination.kind == modules::SDestination::EKind::host) && request;
        if (!request) return modules::ESubmission::invalid;
        if (modules::accepted(self.result))
        {
            ++self.accepted;
            self.module = request->module;
            self.action = request->action;
            self.correlation = message.query_async_slot();
            message = {};
        }
        return self.result;
    }
};

}   // namespace tests
