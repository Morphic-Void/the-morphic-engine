
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    service_borrows.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Nov 26
//
//  Host owns these records. Providers own every descriptor and resource they name.

#pragma once

#include "containers/TUnorderedCollection.hpp"
#include "services/service_borrow.hpp"
#include "runtime/client_record.hpp"

namespace host
{

class CServiceBorrows
{
public:
    CServiceBorrows() noexcept = default;
    CServiceBorrows(const CServiceBorrows&) = delete;
    CServiceBorrows& operator=(const CServiceBorrows&) = delete;
    CServiceBorrows(CServiceBorrows&&) = delete;
    CServiceBorrows& operator=(CServiceBorrows&&) = delete;
    static constexpr std::uint32_t k_capacity = 256u;

    [[nodiscard]] services::EServiceStatus prepare(SClientRecord& provider, SClientRecord& client,
        const services::SServiceBorrowOffer& offer, const std::int32_t slot,
        const std::uint16_t opcode, const bool dispatched, services::SServiceBorrow& borrow) noexcept;
    void dispatched(services::SServiceBorrow borrow) noexcept;
    [[nodiscard]] bool publication_settled(services::SServiceBorrow borrow) const noexcept;
    [[nodiscard]] services::EServiceStatus begin_retirement(SClientRecord& provider, services::SServiceBorrow borrow) noexcept;
    [[nodiscard]] services::EServiceStatus finish_retirement(SClientRecord& provider, services::SServiceBorrow borrow) noexcept;
    void release(SClientRecord& client, const services::ReleaseServiceBorrow& request) noexcept;
    void quiescent(SClientRecord& client, const services::ServiceBorrowQuiescent& request) noexcept;
    void cancel(services::SServiceBorrow borrow) noexcept;
    void cancel_request(SClientRecord& client, const services::CancelServiceRequest& request) noexcept;
    void retire_client(SClientRecord& client, const bool execution_quiescent) noexcept;
    void stop_provider(SClientRecord& provider) noexcept;
    void progress() noexcept;
    [[nodiscard]] bool route_idle(services::SServiceRoute route) const noexcept;
    [[nodiscard]] bool provider_retired(const SClientRecord& provider) const noexcept;

private:
    struct SRecord
    {
        services::SServiceBorrow token;
        services::SServiceBorrowOffer offer;
        SClientRecord* provider{ nullptr };
        SClientRecord* client{ nullptr };
        std::int32_t slot{ -1 };
        std::uint16_t opcode{ 0u };
        services::EServiceQuiescence quiescence{ services::EServiceQuiescence::none };
        bool dispatched{ false };
        bool settled{ false };
        bool published{ false };
        bool closing{ false };
        bool client_notified{ false };
        bool provider_notified{ false };
        bool quiescence_notified{ false };
        bool finished{ false };
    };

    [[nodiscard]] SRecord* find(services::SServiceBorrow borrow) noexcept;
    [[nodiscard]] const SRecord* find(services::SServiceBorrow borrow) const noexcept;
    static void close(SRecord& record) noexcept;
    static bool notify(SRecord& record, SClientRecord& recipient, services::EServiceBorrowControl control) noexcept;

    TUnorderedCollection<SRecord> m_records;
    std::uint64_t m_next{ 1u };
    std::uint32_t m_count{ 0u };
};

}   //  namespace host
