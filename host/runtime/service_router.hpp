
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_router.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Nov 26
//
//  Bounded provider-scoped POD operations. Host retains every accepted correlation.

#pragma once

#include "services/service_protocol.hpp"
#include "runtime/service_provider.hpp"
#include "runtime/service_borrows.hpp"

namespace host
{

class CServiceRouter
{
public:
    void open(const services::OpenServiceRequest& request, const std::int32_t slot,
        SClientRecord& client, const SServiceProvider* provider) noexcept;
    void close(const services::CloseServiceRequest& request, const std::int32_t slot, SClientRecord& client) noexcept;
    void request(const services::ServiceRequest& request, const std::int32_t slot, SClientRecord& client) noexcept;
    [[nodiscard]] services::EServiceStatus complete(SClientRecord& provider, const services::ServiceReply& reply) noexcept;
    [[nodiscard]] services::EServiceStatus prepare_borrow(SClientRecord& provider,
        const services::SServiceBorrowOffer& offer, services::SServiceBorrow& borrow) noexcept;
    [[nodiscard]] services::EServiceStatus begin_borrow_retirement(SClientRecord& provider, services::SServiceBorrow borrow) noexcept;
    [[nodiscard]] services::EServiceStatus finish_borrow_retirement(SClientRecord& provider, services::SServiceBorrow borrow) noexcept;
    void release_borrow(SClientRecord& client, const services::ReleaseServiceBorrow& request) noexcept;
    void quiesce_borrow(SClientRecord& client, const services::ServiceBorrowQuiescent& request) noexcept;
    void cancel_request(SClientRecord& client, const services::CancelServiceRequest& request) noexcept;
    [[nodiscard]] bool provider_borrows_retired(const SClientRecord& provider) const noexcept;
    [[nodiscard]] std::uint32_t prepare_batch(SClientRecord& provider,
        services::SDispatchedCommand* commands, const std::uint32_t capacity) noexcept;
    [[nodiscard]] bool finish_batch(SClientRecord& provider, const services::SDispatchedCommand* commands,
        const std::uint32_t offered, const std::uint32_t consumed) noexcept;
    void progress() noexcept;
    void stop_provider(SClientRecord& provider, const bool quiescent) noexcept;
    void retire_client(SClientRecord& client) noexcept;
    [[nodiscard]] bool client_idle(const SClientRecord& client) const noexcept;
    [[nodiscard]] bool provider_in_use(const SClientRecord& provider) const noexcept;
    [[nodiscard]] bool failed() const noexcept { return m_failed; }

private:
    struct SRoute
    {
        services::SServiceRoute token;
        SClientRecord* client{ nullptr };
        SClientRecord* provider{ nullptr };
        services::SServiceContract contract;
        std::uint64_t last_request{ 0u };
        bool closing{ false };
    };
    struct SOperation
    {
        SRoute* route{ nullptr };
        services::ServiceRequest request;
        services::ServiceReply reply;
        std::uint64_t order{ 0u };
        std::int32_t slot{ -1 };
        bool offered{ false };
        bool delivered{ false };
        bool completed{ false };
        bool cancelled{ false };
        bool cancellation_notified{ false };
        bool terminal_sent{ false };
        services::SServiceBorrow borrow;
    };

    [[nodiscard]] SRoute* find_route(const services::SServiceRoute token) noexcept;
    [[nodiscard]] SOperation* find_operation(SRoute& route, const services::SServiceRequestId request) noexcept;
    [[nodiscard]] bool route_idle(const SRoute& route) const noexcept;
    void fail(SOperation& operation, const services::EServiceStatus status) noexcept;
    void cancel(SOperation& operation) noexcept;

    SRoute m_routes[64];
    SOperation m_operations[256];
    CServiceBorrows m_borrows;
    std::uint64_t m_next_route{ 1u };
    std::uint64_t m_next_order{ 1u };
    bool m_failed{ false };
};

}   //  namespace host
