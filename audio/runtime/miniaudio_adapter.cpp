
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    miniaudio_adapter.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Pinned miniaudio implementation, compiled once into Audio only.

#include "module/module_messages.hpp"
#include "miniaudio_adapter.hpp"
#include "vendor_allocator.hpp"
#include <new>

#define MA_NO_NULL
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "external/miniaudio/miniaudio.h"

namespace audio
{
#include "miniaudio_context.inl"

struct CMiniaudioOutput::SState
{
    CToneTest tone;
    ma_log log{};
    ma_context context{};
    SContextLifetime context_lifetime;
    ma_device device{};
    bool log_ready{ false };
    bool device_ready{ false };
    bool started{ false };
    std::atomic<std::uint32_t> stopping{ 0u };
    memory::CMemoryContext* memory{ nullptr };
    std::atomic<std::uint32_t> allocation_failed{ 0u };
    std::atomic<std::uint32_t> terminal_notification{ 0u };
    void* probe_allocation{ nullptr };

    static void* allocate(std::size_t bytes, void* context) noexcept
    {
        auto& state = *static_cast<SState*>(context);
        void* result = vendor_allocate(bytes, state.memory);
        if (!result && bytes != 0u) state.allocation_failed.store(1u, std::memory_order_release);
        return result;
    }
    static void* reallocate(void* block, std::size_t bytes, void* context) noexcept
    {
        auto& state = *static_cast<SState*>(context);
        void* result = vendor_reallocate(block, bytes, state.memory);
        if (!result && bytes != 0u) state.allocation_failed.store(1u, std::memory_order_release);
        return result;
    }
    static void free(void* block, void* context) noexcept
    {
        vendor_free(block, static_cast<SState*>(context)->memory);
    }

    //  Device-free backend used only by the private acquisition-failure tests.
    //  Production leaves this callback unset and uses the native backend order.
    static ma_result cleanup_guard_uninit(ma_context* context) noexcept
    {
        auto& state = *static_cast<SState*>(context->pUserData);
        ma_free(state.probe_allocation, &context->allocationCallbacks);
        state.probe_allocation = nullptr;
        return MA_SUCCESS;
    }
    static ma_result cleanup_guard(ma_context* context, const ma_context_config*, ma_backend_callbacks* callbacks) noexcept
    {
        auto& state = *static_cast<SState*>(context->pUserData);
        state.probe_allocation = ma_malloc(32u, &context->allocationCallbacks);
        if (!state.probe_allocation) return MA_OUT_OF_MEMORY;
        callbacks->onContextUninit = &cleanup_guard_uninit;
        return MA_SUCCESS;
    }

    static void data(ma_device* device, void* output, const void*, ma_uint32 frames) noexcept
    {
        static_cast<SState*>(device->pUserData)->tone.fill(static_cast<float*>(output), frames);
    }
    static void notification(const ma_device_notification* notice) noexcept
    {
        auto& state = *static_cast<SState*>(notice->pDevice->pUserData);
        if ((notice->type == ma_device_notification_type_interruption_began) ||
            (notice->type == ma_device_notification_type_rerouted) ||
            ((notice->type == ma_device_notification_type_stopped) && (state.stopping.load(std::memory_order_acquire) == 0u)))
        {
            state.terminal_notification.store(static_cast<std::uint32_t>(notice->type) + 1u, std::memory_order_release);
            state.tone.interrupt();
        }
    }
};

CMiniaudioOutput::~CMiniaudioOutput() noexcept { (void)close(); }

modules::ETestRunStatus CMiniaudioOutput::open() noexcept
{
    return open(false);
}

modules::ETestRunStatus CMiniaudioOutput::open(const bool cleanup_guard_only, SContextProbe* const probe) noexcept
{
    if (m_state != nullptr) return modules::ETestRunStatus::failed;
    void* storage = m_memory.allocate(alignof(SState), sizeof(SState));
    if (storage == nullptr) return modules::ETestRunStatus::failed;
    m_state = ::new (storage) SState{};
    m_state->memory = &m_memory;
    m_state->context_lifetime.probe = probe;
    auto config = ma_context_config_init();
    config.pUserData = m_state;
    config.allocationCallbacks = { m_state, &SState::allocate, &SState::reallocate, &SState::free };
    if (ma_log_init(&config.allocationCallbacks, &m_state->log) != MA_SUCCESS) return modules::ETestRunStatus::failed;
    m_state->log_ready = true;
    config.pLog = &m_state->log;
    config.custom.onContextInit = cleanup_guard_only ? &SState::cleanup_guard : nullptr;
    //  Default backend order, with the null backend compiled out. The owned
    //  log uses these allocation callbacks; no engine log hook runs
    //  on a vendor thread. Native OS/driver allocations retain native ownership.
    const ma_backend guard = ma_backend_custom;
    ma_result result = initialise_context(cleanup_guard_only ? &guard : nullptr, cleanup_guard_only ? 1u : 0u,
        &config, &m_state->context, m_state->context_lifetime);
    if (result != MA_SUCCESS)
    {
        MV_LOG(info, "Audio: Context initialisation returned {}", static_cast<int>(result));
        return modules::ETestRunStatus::failed;
    }
    if (m_state->context.backend == ma_backend_custom)
    {
        MV_LOG(info, "Audio: Device-free context probe completed; no playback backend selected");
        return modules::ETestRunStatus::failed;
    }
    ma_uint32 playback_count = 0u;
    result = ma_context_get_devices(&m_state->context, nullptr, &playback_count, nullptr, nullptr);
    if ((result != MA_SUCCESS) || (m_state->allocation_failed.load(std::memory_order_acquire) != 0u))
    {
        MV_LOG(info, "Audio: Device enumeration failed ({})", static_cast<int>(result));
        return modules::ETestRunStatus::failed;
    }
    if (playback_count == 0u)
    {
        MV_LOG(info, "Audio: No real playback device enumerated");
        return modules::ETestRunStatus::unavailable;
    }
    auto device_config = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format = ma_format_f32;
    device_config.playback.channels = 2u;
    device_config.sampleRate = 48000u;
    device_config.dataCallback = &SState::data;
    device_config.notificationCallback = &SState::notification;
    device_config.pUserData = m_state;
    result = ma_device_init(&m_state->context, &device_config, &m_state->device);
    if (result != MA_SUCCESS)
    {
        MV_LOG(info, "Audio: Device initialisation returned {}", static_cast<int>(result));
        return ((result == MA_NO_DEVICE) && (m_state->allocation_failed.load(std::memory_order_acquire) == 0u)) ?
            modules::ETestRunStatus::unavailable : modules::ETestRunStatus::failed;
    }
    m_state->device_ready = true;
    if (!m_state->tone.reset(m_state->device.sampleRate)) return modules::ETestRunStatus::failed;
    MV_LOG_RICH(info, "Audio: Real backend %s, output %s, callback rate %u", ma_get_backend_name(m_state->context.backend),
        m_state->device.playback.name, m_state->device.sampleRate);
    return modules::ETestRunStatus::success;
}

bool CMiniaudioOutput::start() noexcept
{
    const ma_result result = ma_device_start(&m_state->device);
    m_state->started = (result == MA_SUCCESS);
    if (!m_state->started) MV_LOG(error, "Audio: Device start failed ({})", static_cast<int>(result));
    return m_state->started;
}

bool CMiniaudioOutput::close() noexcept
{
    if (m_state == nullptr) return true;
    m_state->stopping.store(1u, std::memory_order_release);
    m_state->tone.cancel();
    bool success = true;
    if (m_state->device_ready)
    {
        if (m_state->started)
        {
            const ma_result result = ma_device_stop(&m_state->device);
            success = result == MA_SUCCESS;
            if (!success) MV_LOG(error, "Audio: Device stop failed ({})", static_cast<int>(result));
        }
        //  uninit joins vendor execution even when start/stop failed.
        ma_device_uninit(&m_state->device);
    }
    const ma_result released = release_context(m_state->context, m_state->context_lifetime);
    if (released != MA_SUCCESS)
    {
        MV_LOG(error, "Audio: Context teardown failed ({})", static_cast<int>(released));
        success = false;
    }
    if (m_state->log_ready) ma_log_uninit(&m_state->log);
    if (m_state->allocation_failed.load(std::memory_order_acquire) != 0u)
    {
        MV_LOG(error, "Audio: Vendor allocation failed");
        success = false;
    }
    const auto terminal = m_state->terminal_notification.load(std::memory_order_acquire);
    if (terminal != 0u)
    {
        MV_LOG(error, "Audio: Unexpected device notification ({})", terminal - 1u);
        success = false;
    }
    m_state->~SState();
    m_memory.deallocate(alignof(SState), sizeof(SState), m_state);
    m_state = nullptr;
    return success;
}

SProgress CMiniaudioOutput::observe() const noexcept
{
    if (m_state == nullptr) return {};
    auto result = m_state->tone.observe();
    result.interrupted = result.interrupted || (m_state->allocation_failed.load(std::memory_order_acquire) != 0u);
    return result;
}
}   //  namespace audio
