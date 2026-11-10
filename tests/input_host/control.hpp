
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    control.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Nov 26
//
//  Borrowed acceptance-only feeds and frozen traces shared across real modules.

#pragma once

#include <atomic>
#include "framework/platform/input.hpp"
#include "containers/TPodVector.hpp"
#include "platform/system/performance_counter.hpp"

namespace input_host
{
inline constexpr services::SServiceContract k_contract{ 0x7ffeu, 1u };
inline constexpr std::uint32_t k_feed_capacity = 1u << 12u;
inline constexpr std::uint32_t k_history_capacity = 1u << 10u;
inline constexpr std::uint32_t k_history_mask = k_history_capacity - 1u;
inline constexpr std::uint32_t k_trace_capacity = 1u << 19u;
inline constexpr std::uint32_t k_pipe_count = 5u;
enum class EFeed : std::uint32_t { controller, key, text, focus, modal, arrival, history_loss, freeze };
struct SFeed
{
    std::uint64_t session{}, instance{}, observation{}, offered{};
    EFeed kind{};
    std::uint32_t device{}, value{}, repeats{ 1u };
    float left_x{}, left_y{}, right_x{}, right_y{}, left_trigger{}, right_trigger{};
};
struct STrace
{
    std::uint64_t provider{}, connection{}, generation{}, sequence{};
    std::uint64_t observation{}, offered{}, ingress{}, publication{};
    std::uint32_t stream{}, occupancy{}, boundary{}, reserved{};
};
struct SPumpTrace { std::uint64_t begin{}, end{}; };
struct SLatest
{
    std::atomic<std::uint64_t> revision{}, instance{}, observation{}, offered{}, loss{};
    std::atomic<std::uint32_t> buttons{};
    std::atomic<float> axes[6]{};
    void publish(const SFeed& frame) noexcept
    {
        const std::uint64_t next = revision.load() + 2u;
        revision.store(next - 1u);
        instance.store(frame.instance);
        observation.store(frame.observation);
        offered.store(frame.offered);
        buttons.store(frame.value);
        axes[0].store(frame.left_x);
        axes[1].store(frame.left_y);
        axes[2].store(frame.right_x);
        axes[3].store(frame.right_y);
        axes[4].store(frame.left_trigger);
        axes[5].store(frame.right_trigger);
        revision.store(next);
    }
    bool read(SFeed& frame) const noexcept
    {
        for (std::uint32_t attempt = 0u; attempt < 4u; ++attempt)
        {
            const std::uint64_t before = revision.load();
            if (before & 1u) continue;
            SFeed candidate;
            candidate.instance = instance.load();
            candidate.observation = observation.load();
            candidate.offered = offered.load();
            candidate.value = buttons.load();
            candidate.left_x = axes[0].load();
            candidate.left_y = axes[1].load();
            candidate.right_x = axes[2].load();
            candidate.right_y = axes[3].load();
            candidate.left_trigger = axes[4].load();
            candidate.right_trigger = axes[5].load();
            if (before == revision.load())
            {
                frame = candidate;
                return true;
            }
        }
        return false;
    }
};
struct SShared
{
    ::threading::transports::TRing<SFeed> feed;
    ::threading::transports::TRingProducerEndpoint<SFeed> producer{ feed };
    ::threading::transports::TRingConsumerEndpoint<SFeed> consumer{ feed };
    TPodVector<STrace> traces;
    TPodVector<SPumpTrace> pumps;
    SLatest latest[4];
    std::atomic<std::uint64_t> device_instance[4]{};
    std::atomic<bool> closing{}, frozen{}, failed{};
    std::atomic<std::uint64_t> consumed{}, drained{}, feed_loss{}, history_loss{}, clock_errors{};
    std::atomic<std::uint32_t> occupancy[k_pipe_count][2]{};
    std::atomic<std::uint32_t> high_water[k_pipe_count][2]{};
    std::atomic<std::uint64_t> connection[k_pipe_count]{};
    std::atomic<std::uint64_t> ready_connection[k_pipe_count]{};
    std::atomic<std::uint64_t> publications[k_pipe_count][2]{};
    //  Host is the sole writer. Consumer reads these only after frozen/acquire.
    std::uint32_t trace_count{}, pump_count{}, trace_overflow{}, pump_overflow{};
    bool tracing{};
};
struct SView
{
    modules::SModuleInstance provider, consumer;
    SShared* shared{};
};
inline bool sample(std::uint64_t& ticks) noexcept
{
    platform::system::CPerfCounter counter;
    if (!counter.update()) return false;
    ticks = counter.query_value();
    return true;
}
}
