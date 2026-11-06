
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    15 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  The main host runtime and process-facing entry point.

#pragma once

#ifndef HOST_HPP_INCLUDED
#define HOST_HPP_INCLUDED

#include "filesystem/filesystem_messages.hpp"
#include "module/module_messages.hpp"
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::int32_t, std::uint8_t

#include "runtime/asset_service.hpp"
#include "runtime/batch_pool.hpp"
#include "runtime/module_service.hpp"
#include "runtime/pumped_client.hpp"
#include "runtime/host_maintenance.hpp"
#include "runtime/direct_connections.hpp"
#include "runtime/host_cadence.hpp"
#include "platform/threading/owner_wait.hpp"
#include "runtime/worker_policy.hpp"
#include "containers/TInstance.hpp"
#include "containers/TUnorderedCollection.hpp"
#include "debug/service.hpp"
#include "module/bound_module.hpp"
#include "executive/module/binding/executive_binding.hpp"
#include "platform/system/performance_counter.hpp"
#include "threading/CThreadPackage.hpp"

namespace host
{

class CHost final
{
public:
    explicit CHost(memory::CMemoryAllocator& allocator) noexcept;
    CHost(const CHost&) = delete;
    CHost& operator=(const CHost&) = delete;
    CHost(CHost&&) = delete;
    CHost& operator=(CHost&&) = delete;
    ~CHost() noexcept;

    [[nodiscard]] int execute(const char* const log_tag, const char* const executive_file,
        const char* const log_directory, const std::uint32_t worker_count,
        const std::uint32_t batch_runner_count) noexcept;
    [[nodiscard]] bool retains_state() const noexcept { return m_retain_runtime; }

private:
    friend class CHostMaintenance;
    enum class EPhase : std::uint8_t
    {
        starting = 0,
        running,
        stopping_executive,
        replacing_executive,
        shutting_down,
        complete
    };

    //  Scheduling estimate for the default composition, not a service capacity.
    static constexpr std::uint32_t k_expected_service_threads = 2u;

    void initialise_debug_service(const char* const log_tag, const char* const log_directory) noexcept;
    [[nodiscard]] bool initialise_runtime(const char* const executive_file) noexcept;
    [[nodiscard]] bool start_threads() noexcept;
    [[nodiscard]] bool workers_failed() noexcept;
    [[nodiscard]] bool start_executive() noexcept;
    [[nodiscard]] bool start_module_thread() noexcept;
    [[nodiscard]] bool register_module_client() noexcept;
    void receive_startup_message(SClientRecord& client) noexcept;
    void start_pending_module() noexcept;
    void pump_clients() noexcept;
    void drain_pumped_client(CPumpedClient& pump, SClientRecord& client) noexcept;
    [[nodiscard]] SClientRecord* client_for(const threading::CThreadPackage& package) noexcept;
    [[nodiscard]] SClientRecord* module_client(const module_ids::id_type module) noexcept;
    [[nodiscard]] bool stop_module_thread(const mount_point_ids::id_type mount) noexcept;
    void receive_request(threading::CErasedOwnerMsg& message, SClientRecord& executive) noexcept;
    void receive_message(const threading::CErasedPodMsg& message, SClientRecord& client) noexcept;
    void request_refresh(threading::CErasedOwnerMsg& message, SClientRecord& executive) noexcept;
    void dispatch_refresh() noexcept;
    void complete_scan(threading::CErasedOwnerMsg& message) noexcept;
    void reply_refresh(SClientRecord& client, const std::int32_t slot, const filesystem::EFilesystemStatus status) noexcept;
    [[nodiscard]] bool filesystem_idle() const noexcept { return !m_initial_scan && (m_refresh_count == 0u); }
    void advance_lifecycle(const threading::EThreadRunState executive_state) noexcept;
    void executive_failure(const modules::EModuleStatus status) noexcept;
    void run() noexcept;
    void run_turn() noexcept;
    void wait_for_work() noexcept;
    void fail_wait(const platform::threading::WaitError error) noexcept;
    void begin_emergency_shutdown() noexcept;
    void apply_host_stop() noexcept;
    void check_shutdown_deadline() noexcept;
    [[nodiscard]] bool shutdown() noexcept;
    [[nodiscard]] bool shutdown_threads() noexcept;
    [[nodiscard]] bool shutdown_debug_service() noexcept;

    [[nodiscard]] threading::CThreadPackage* module_thread_package(const module_ids::id_type module) noexcept;
    [[nodiscard]] threading::CThreadPackage* worker_package(const std::uint32_t index) noexcept;

    //  All producer ownership is declared later and is retired first.
    platform::threading::COwnerWait m_owner_wait;
    platform::threading::WaitError m_wait_setup_error;
    CHostCadence m_cadence;
    bool m_wait_failed{ false }, m_turn_pending{ false }, m_native_stop_requested{ false };
    std::uint64_t m_wait_count{ 0u }, m_turn_count{ 0u };
    TInstance<debug_system::CDebugServiceState> m_debug_service_owner;
    debug_system::CDebugServiceState* m_debug_service{ nullptr };
    bool m_debug_service_installed{ false };
    bool m_debug_service_started{ false };

    TUnorderedCollection<threading::CThreadPackage> m_thread_packages;
    CAssetService m_asset_service;
    CBatchPool m_batch_pool;
    filesystem_image::CImage m_filesystem;
    struct SRefresh
    {
        CErasedOwner owner;
        SClientRecord* client{ nullptr };
        std::int32_t slot{ -1 };
    };
    static constexpr std::size_t k_refresh_capacity = 16u;
    SRefresh m_refresh_queue[k_refresh_capacity];
    filesystem_image::SRootScan m_root_scan;
    std::size_t m_refresh_head{ 0u };
    std::size_t m_refresh_count{ 0u };
    std::uint64_t m_scan_serial{ 0u };
    bool m_initial_scan{ false };
    bool m_scan_in_flight{ false };
    bool m_filesystem_failed{ false };
    bool m_runtime_failed{ false };
    bool m_batch_contract_failed{ false };
    platform::system::CPerfCountConversion m_perf_count_conversion;

    //  Self-replacement owns its request until the outgoing Executive is joined.
    CModuleService m_module_service;
    CErasedOwner m_executive_request;
    EPhase m_phase{ EPhase::starting };
    struct SModuleThread
    {
        std::int32_t slot{ -1 };
        threading::EThreadRunState observed_state{ threading::EThreadRunState::Empty };
    };
    SModuleThread m_module_threads[module_ids::k_count];
    TInstance<SClientRecord> m_clients[module_ids::k_count];
    SServiceProvider m_providers[module_ids::k_count];
    TInstance<CPumpedClient> m_pumps[module_ids::k_count];
    CServiceRouter m_service_router;
    CHostMaintenance m_maintenance;
    CDirectConnections m_direct_connections;
    std::uint64_t m_next_client{ 1u };
    bool m_retain_runtime{ false };
    bool m_emergency_shutdown{ false };
    bool m_host_stop_applied{ false };
    bool m_work_admission_closed{ false };
    bool m_workers_unavailable{ false };
    bool m_worker_failure_drained{ false };
    threading::CWaitDeadline m_shutdown_deadline;
    bool m_shutdown_timed{ false };
    bool m_shutdown_reported{ false };
#if defined(MV_HOST_RUNTIME_TESTS)
    friend struct SHostRuntimeTestAccess;
    friend struct SHostMaintenanceTestAccess;
    friend struct SPlatformHostTestAccess;
    bool m_test_reject_initial_scan{ false };
    std::uint32_t m_test_clock_failure{ 0u };
    void* m_test_state{ nullptr };
    void (*m_test_after_worker_drain)(CHost&, threading::CThreadPackage&){ nullptr };
    void (*m_test_after_worker_failure)(CHost&){ nullptr };
    void (*m_test_before_client_start)(threading::CThreadPackage&){ nullptr };
#endif
    std::int32_t m_worker_slots[k_max_worker_count]{ -1, -1 };
    std::uint32_t m_worker_count{ 0u };
    std::uint32_t m_batch_runner_count{ 0u };
};

int host(const char* const log_tag, const char* const executive_file,
    const char* const log_directory, const std::uint32_t worker_count,
    const std::uint32_t batch_runner_count) noexcept;

}   //  namespace host

#endif  //  HOST_HPP_INCLUDED
