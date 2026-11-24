
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Module-owned Input Host acceptance state, driven by common invocations.

#pragma once

#include "tests/input_host/source_execution.hpp"
#include "tests/input_host/borrow_execution.hpp"
#include "tests/input_host/control.hpp"
#include "tests/input_host/schema.hpp"
#include "tests/support/input_schema.hpp"
#include "tests/environment/test_paths.hpp"
#include "framework/module/module_messages.hpp"
#include "framework/platform/input_reader.hpp"
#include "framework/platform/controllers.hpp"
#include "containers/TInstance.hpp"
#include "debug/macros.hpp"

namespace input_host
{

class CInputExecution
{
public:
    explicit CInputExecution(const modules::SExecutionContext& context, const bool secondary = false) noexcept;
    [[nodiscard]] modules::EExecutionStatus handle(const modules::SInvocation& invocation, modules::SExecutionProgress& progress) noexcept;
    [[nodiscard]] bool complete() const noexcept;

private:
    friend struct SInputExecutionTestAccess;
    using SRoute = services::SServiceRoute;
    using EStatus = services::EServiceStatus;
    using ECommand = platform::input::ECommand;
    using SInstallation = platform::input::SInstallationToken;
    struct SReadTrace
    {
        std::uint64_t provider{}, connection{}, generation{}, sequence{}, read{}, applied{};
        std::uint32_t stream{}, outcome{};
    };
    struct SPipe
    {
        TInstance<platform::input::CInputReader> reader;
        const platform::input::SInputEndpointV4* view{};
        platform::input::SBoundaryReady boundary;
        platform::input::SInputEvent retained;
        SReadTrace retained_trace;
        services::ServiceRequest boundary_request;
        std::uint64_t pending{}, applied{}, cancellations{}, stale{}, last_sequence{};
        float values[256][2]{};
        bool has_retained{}, drained{}, boundary_prepared{}, boundary_acknowledged{};
    };
    struct SWindow
    {
        std::uint64_t begin{}, end{}, offered{}, admitted{}, late_max{};
        std::uint64_t wait_begin{}, wait_end{}, round_begin{}, round_end{};
        std::uint32_t rate{}, controllers{};
        std::uint64_t expected[k_pipe_count]{}, awake_begin{}, awake_end{};
    };
    enum class EStage { setup, exercise, scenario, freeze, report, cleanup, secondary_notify, secondary_wait, self_stop, done };
    enum class ESetup
    {
        load, input_route, window_route, control_route, control_acquire, desktop_install, controller_install,
        window_acquire, window_notification, window_ready, catalogue_acquire, catalogue_notification,
        catalogue_ready, pipe_acquire, pipe_notification, pipe_ready, active
    };
    enum class ECleanup { pipes, source, secondary_barrier, configurations, borrows, routes, unload, finished };
    enum class EOperation { none, module, open, command, close, self_stop };
    struct SOperation
    {
        EOperation kind{ EOperation::none };
        threading::CErasedOwnerMsg owner;
        threading::CErasedPodMsg pod;
        services::ServiceRequest request;
        services::ServiceReply result;
        services::OpenServiceResult opened;
        module_ids::id_type module{};
        EStatus expected{ EStatus::success };
        platform::threading::CWaitDeadline deadline;
        bool sent{}, acknowledged{}, done{};
    };

    static bool option(const char* const name, const char* const expected) noexcept;
    bool receive(const threading::CErasedPodMsg& message) noexcept;
    void service() noexcept;
    void advance_setup() noexcept;
    void advance_exercise() noexcept;
    void advance_scenario() noexcept;
    void source_cleanup() noexcept;
    void acquisition_cleanup() noexcept;
    void history_freshness() noexcept;
    void ingress_loss() noexcept;
    void work_pressure() noexcept;
    void shutdown_blocked() noexcept;
    void load() noexcept;
    void pressure() noexcept;
    void transitions() noexcept;
    void second_consumer() noexcept;
    void advance_secondary() noexcept;
    void native_pass() noexcept;
    void next_scenario(const unsigned step) noexcept;
    void advance_cleanup() noexcept;
    void advance_report() noexcept;
    bool write_report();
    bool active() const noexcept;
    bool feed(SFeed frame) noexcept;
    void close_reader(const unsigned index) noexcept;
    void close_all() noexcept;
    void begin_cleanup() noexcept;
    void next_setup(const ESetup stage) noexcept;
    void next_stage(const EStage stage) noexcept;
    void arm_stage(const std::uint64_t milliseconds = 10000u) noexcept;
    void poll() noexcept;
    void fail(const bool unsafe = false) noexcept;
    void deadline_expired() noexcept;
    bool begin_module(const modules::EModuleAction action, const module_ids::id_type module) noexcept;
    bool begin_open(const services::SServiceContract contract) noexcept;
    bool begin_command(const SRoute route, const std::uint16_t opcode, const void* const payload,
        const std::uint16_t size, const EStatus expected = EStatus::success) noexcept;
    template<class T> bool command(const ECommand opcode, const T& payload, const EStatus expected = EStatus::success) noexcept
    {
        return begin_command(input_route, static_cast<std::uint16_t>(opcode), &payload, sizeof(payload), expected);
    }
    bool begin_acquire(const unsigned index, const SRoute route, const std::uint16_t opcode,
        const void* const payload, const std::uint16_t size, const EStatus expected = EStatus::success) noexcept;
    void publish_operation() noexcept;
    bool publication_pending() const noexcept;
    void clear_operation() noexcept;
    void capture_installation() noexcept;

    const modules::CCommunication communication;
    const modules::SModuleInstance instance;
    const services::SClientToken endpoint;
    CSourceExecution source;
    CBorrowExecution borrows;
    platform::system::CPerfCountConversion conversion;
    SOperation operation;
    SRoute input_route, window_route, control_route;
    SShared* shared{};
    services::SServiceBorrow control_borrow;
    platform::windows::SWindowToken window;
    std::uintptr_t native_window{};
    SInstallation installations[2];
    SPipe pipes[k_pipe_count];
    TPodVector<SReadTrace> reads;
    SWindow windows[3];
    SWindow* measuring{};
    platform::input::SInputSnapshot report_states[k_pipe_count];
    platform::threading::CWaitDeadline stage_deadline, settle_deadline;
    std::uint64_t next_request{}, next_observation{}, intended{}, wait_requests{}, service_passes{};
    std::uint64_t next_wake{ UINT64_MAX };
    std::uint32_t read_count{}, trace_overflow{}, pause_mask{}, window_count{}, index{}, exercise_step{}, report_index{};
    unsigned source_installation{};
    unsigned scenario_step{}, scenario_count{};
    std::uint64_t scenario_observation{}, scenario_loss{}, scenario_applied{}, scenario_generation{};
    struct SLoad
    {
        std::uint64_t initial[k_pipe_count]{}, expected[k_pipe_count]{};
        std::uint64_t begin{}, end{}, tick{}, pattern{};
        unsigned pass{};
    } load_state;
    platform::threading::CWaitDeadline native_deadline;
    std::uint64_t native_display{};
    bool native_held[3]{};
    EStage stage{ EStage::setup };
    ESetup setup{ ESetup::load };
    ECleanup cleanup{ ECleanup::pipes };
    bool secondary{}, native{}, ordinary{}, stopping{}, cleaning{}, failed{}, unsafe{}, reported{}, passed{};
    bool framework_shutdown{}, shutdown_requested{}, freeze_timeout_cleanup{};
    bool reacquiring{};
    bool blocked_shutdown{};
    bool secondary_barrier_released{}, secondary_notified{};
    bool runnable{}, source_active{}, source_captured{}, stage_started{}, settling{}, loaded{}, report_frozen{};
};

}   //  namespace input_host
