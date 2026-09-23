
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

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::int32_t, std::uint8_t

#include "host/runtime/asset_service.hpp"
#include "host/runtime/module_service.hpp"
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
    CHost() noexcept = default;
    CHost(const CHost&) = delete;
    CHost& operator=(const CHost&) = delete;
    CHost(CHost&&) = delete;
    CHost& operator=(CHost&&) = delete;
    ~CHost() noexcept;

    [[nodiscard]] int execute(const char* const log_tag, const char* const executive_file, const char* const log_directory) noexcept;

private:
    enum class EWorkerThreadID : std::uint8_t
    {
        bg_file_io = 0u,
        bg_conditioning,
        executive,
        rendering,
        count
    };

    enum class EPhase : std::uint8_t
    {
        starting = 0,
        running,
        stopping_executive,
        replacing_executive,
        shutting_down,
        complete
    };

    static constexpr std::size_t k_thread_count = static_cast<std::size_t>(EWorkerThreadID::count);

    void initialise_debug_service(const char* const log_tag, const char* const log_directory) noexcept;
    [[nodiscard]] bool initialise_runtime(const char* const executive_file) noexcept;
    [[nodiscard]] bool start_threads() noexcept;
    [[nodiscard]] bool start_executive() noexcept;
    [[nodiscard]] bool start_rendering() noexcept;
    [[nodiscard]] bool stop_rendering(const threading::EThreadRunState state) noexcept;
    void receive_request(threading::CErasedOwnerMsg& message, threading::CThreadPackage& executive) noexcept;
    void request_refresh(threading::CErasedOwnerMsg& message, threading::CThreadPackage& executive) noexcept;
    void dispatch_refresh() noexcept;
    void complete_scan(threading::CErasedOwnerMsg& message) noexcept;
    void reply_refresh(threading::CThreadPackage& client, const std::int32_t slot, const EFilesystemStatus status) noexcept;
    [[nodiscard]] bool filesystem_idle() const noexcept { return !m_initial_scan && (m_refresh_count == 0u); }
    void advance_lifecycle(const threading::EThreadRunState executive_state) noexcept;
    void executive_failure(const EModuleStatus status) noexcept;
    void run() noexcept;
    [[nodiscard]] bool shutdown() noexcept;
    void shutdown_threads() noexcept;
    void shutdown_debug_service() noexcept;

    [[nodiscard]] threading::CThreadPackage* thread_package(const EWorkerThreadID id) noexcept;

    TInstance<debug_system::CDebugServiceState> m_debug_service_owner;
    debug_system::CDebugServiceState* m_debug_service{ nullptr };
    bool m_debug_service_installed{ false };
    bool m_debug_service_started{ false };

    TUnorderedCollection<threading::CThreadPackage> m_thread_packages;
    CAssetService m_asset_service;
    filesystem_image::CImage m_filesystem;
    struct SRefresh
    {
        CErasedOwner owner;
        threading::CThreadPackage* client{ nullptr };
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
    platform::system::CPerfCountConversion m_perf_count_conversion;

    //  Self-replacement owns its request until the outgoing Executive is joined.
    CModuleService m_module_service;
    CErasedOwner m_executive_request;
    EPhase m_phase{ EPhase::starting };
    std::int32_t m_thread_slots[k_thread_count]{ -1, -1, -1, -1 };
};

int host(const char* const log_tag = nullptr, const char* const executive_file = "package:/bin/MorphicExecutive.dll",
    const char* const log_directory = "development/logical-roots/logs") noexcept;

}   //  namespace host

#endif  //  HOST_HPP_INCLUDED
