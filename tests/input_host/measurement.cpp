
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    measurement.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    10 Nov 26
//
//  Frozen trace correlation, expected-output checks and latency reporting.

#include "tests/input_host/consumer.hpp"

namespace input_host
{
static void metric(const std::uint32_t window, const char* const name, std::uint64_t* const samples,
    const std::uint32_t count)
{
    if (!count) return;
    std::sort(samples, (samples + count));
    const auto percentile = [&](const std::uint32_t percent)
    {
        return samples[((static_cast<std::uint64_t>(count) * percent + 99u) / 100u) - 1u];
    };
    std::fprintf(stdout, "METRIC,%u,%s,%u,%llu,%llu,%llu,%llu\n", window, name, count,
        static_cast<unsigned long long>(percentile(50u)), static_cast<unsigned long long>(percentile(95u)),
        static_cast<unsigned long long>(percentile(99u)), static_cast<unsigned long long>(samples[count - 1u]));
}
bool CConsumer::report()
{
    if (!shared) return true;
    if (!transact(control_route, 2u, nullptr, 0u) ||
        !until([&] { return !shared || shared->frozen.load(std::memory_order_acquire); }) || !shared)
    {
        return false;
    }
    MV_LOG(info, "Input Host: traces {} reads {} pumps {} waits {} rounds {}", shared->trace_count, read_count, shared->pump_count, waits, rounds);
    std::fprintf(stdout, "DIAGNOSTIC,%u,%llu,%u,%u,%u\n", shared->failed.load() ? 1u : 0u,
        static_cast<unsigned long long>(shared->clock_errors.load()), shared->trace_overflow, shared->pump_overflow, trace_overflow);
    std::fprintf(stdout, "COUNTER,%llu\n", static_cast<unsigned long long>(thread.perf_count_conversion().query_ticks_per_second()));
    std::fprintf(stdout, "SOURCE,%llu,%llu\n", static_cast<unsigned long long>(shared->feed_loss.load()),
        static_cast<unsigned long long>(shared->history_loss.load()));
    for (std::uint32_t index = 0u; index < k_pipe_count; ++index)
    {
        SInputSnapshot state;
        if (pipes[index].view && (pipes[index].view->state->read(state) != EStateRead::success))
        {
            return false;
        }
        std::fprintf(stdout, "PIPE,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", index,
            shared->high_water[index][0].load(), shared->high_water[index][1].load(),
            static_cast<unsigned long long>(shared->publications[index][0].load()),
            static_cast<unsigned long long>(shared->publications[index][1].load()),
            static_cast<unsigned long long>(pipes[index].applied), static_cast<unsigned long long>(pipes[index].cancellations),
            static_cast<unsigned long long>(state.action_loss), static_cast<unsigned long long>(state.text_loss),
            static_cast<unsigned long long>(state.ingress_loss));
    }
    std::fflush(stdout);
    if (shared->failed.load() || shared->clock_errors.load() || shared->trace_overflow || shared->pump_overflow || trace_overflow)
    {
        return false;
    }
    TPodVector<STrace> publications;
    if (!publications.resize(shared->trace_count))
    {
        return false;
    }
    if (shared->trace_count) std::memcpy(publications.data(), shared->traces.data(), (shared->trace_count * sizeof(STrace)));
    const auto less = [](const auto& left, const auto& right)
    {
        if (left.provider != right.provider)
        {
            return left.provider < right.provider;
        }
        if (left.connection != right.connection)
        {
            return left.connection < right.connection;
        }
        if (left.stream != right.stream)
        {
            return left.stream < right.stream;
        }
        if (left.generation != right.generation)
        {
            return left.generation < right.generation;
        }
        return left.sequence < right.sequence;
    };
    if (publications.size()) std::sort(publications.data(), (publications.data() + publications.size()), less);
    if (read_count) std::sort(reads.data(), (reads.data() + read_count), less);
    if (!window_count)
    {
        window_count = 1u;
        windows[0].end = UINT64_MAX;
    }
    const char* const names[]{ "offered_ingress", "ingress_publication", "publication_read", "read_applied", "ingress_applied", "offered_applied" };
    TPodVector<std::uint64_t> samples[6];
    for (TPodVector<std::uint64_t>& values : samples)
    {
        if (!values.resize(read_count)) return false;
    }
    TPodVector<std::uint64_t> pump_work, pump_gap;
    if (!pump_work.resize(shared->pump_count) || !pump_gap.resize(shared->pump_count))
    {
        return false;
    }
    for (std::uint32_t window_index = 0u; window_index < window_count; ++window_index)
    {
        const SWindow& window_sample = windows[window_index];
        std::uint32_t counts[6]{};
        std::uint64_t applied[k_pipe_count]{};
        std::uint32_t missing = 0u, invalid = 0u, excluded = 0u, stale = 0u;
        for (std::uint32_t index = 0u; index < read_count; ++index)
        {
            const SReadTrace& read = reads[index];
            const STrace* const begin = publications.data();
            const STrace* const end = begin + publications.size();
            const STrace* const source = std::lower_bound(begin, end, read, less);
            if ((source == end) || less(read, *source) || less(*source, read))
            {
                if ((read.read >= window_sample.begin) && (read.read < window_sample.end)) ++missing;
                continue;
            }
            const std::uint64_t cohort = source->offered ? source->offered : source->ingress;
            if ((cohort < window_sample.begin) || (cohort >= window_sample.end))
            {
                continue;
            }
            if (!source->observation || source->boundary)
            {
                ++excluded;
                continue;
            }
            if (read.outcome)
            {
                ++stale;
                continue;
            }
            for (std::uint32_t pipe = 0u; pipe < k_pipe_count; ++pipe)
            {
                if (pipes[pipe].view && (pipes[pipe].view->connection.value == read.connection)) ++applied[pipe];
            }
            const std::uint64_t starts[]{ source->offered, source->ingress, source->publication, read.read, source->ingress, source->offered };
            const std::uint64_t ends[]{ source->ingress, source->publication, read.read, read.applied, read.applied, read.applied };
            for (std::uint32_t stage = 0u; stage < 6u; ++stage)
            {
                if (!starts[stage] && ((stage == 0u) || (stage == 5u)))
                {
                    continue;
                }
                std::uint64_t delta = 0u;
                if (!starts[stage] || !ends[stage] || (ends[stage] < starts[stage]) ||
                    !thread.perf_count_conversion().to_nanoseconds_u64((ends[stage] - starts[stage]), delta))
                {
                    ++invalid;
                    continue;
                }
                samples[stage][counts[stage]++] = delta;
            }
        }
        if (window_sample.rate)
        {
            for (std::uint32_t pipe = 0u; pipe <= window_sample.controllers; ++pipe)
            {
                if (!window_sample.expected[pipe]) ++missing;
            }
            for (std::uint32_t pipe = 0u; pipe < k_pipe_count; ++pipe)
            {
                std::fprintf(stdout, "EXPECTED,%u,%u,%llu,%llu\n", window_index, pipe,
                    static_cast<unsigned long long>(window_sample.expected[pipe]), static_cast<unsigned long long>(applied[pipe]));
                if (shared->tracing && (applied[pipe] != window_sample.expected[pipe])) ++missing;
            }
            for (std::size_t index = 0u; index < publications.size(); ++index)
            {
                const STrace& publication = publications[index];
                if (!publication.observation || publication.boundary || (publication.offered < window_sample.begin) ||
                    (publication.offered >= window_sample.end))
                {
                    continue;
                }
                const SReadTrace* const begin = reads.data();
                const SReadTrace* const end = begin + read_count;
                const SReadTrace* const read = std::lower_bound(begin, end, publication, less);
                if ((read == end) || less(publication, *read) || less(*read, publication) || read->outcome) ++missing;
            }
        }
        std::uint32_t pump_count = 0u, gap_count = 0u;
        for (std::uint32_t index = 0u; index < shared->pump_count; ++index)
        {
            const SPumpTrace& pump = shared->pumps[index];
            if ((pump.begin < window_sample.begin) || (pump.begin >= window_sample.end))
            {
                continue;
            }
            std::uint64_t duration = 0u;
            if ((pump.end < pump.begin) || !thread.perf_count_conversion().to_nanoseconds_u64((pump.end - pump.begin), duration)) ++invalid;
            else pump_work[pump_count++] = duration;
            if (index && (shared->pumps[index - 1u].begin >= window_sample.begin))
            {
                const std::uint64_t previous = shared->pumps[index - 1u].begin;
                if ((pump.begin < previous) || !thread.perf_count_conversion().to_nanoseconds_u64((pump.begin - previous), duration)) ++invalid;
                else pump_gap[gap_count++] = duration;
            }
        }
        std::uint64_t duration = 0u, lateness = 0u;
        if (window_sample.rate && ((window_sample.end < window_sample.begin) ||
            !thread.perf_count_conversion().to_nanoseconds_u64((window_sample.end - window_sample.begin), duration) ||
            !thread.perf_count_conversion().to_nanoseconds_u64(window_sample.late_max, lateness))) ++invalid;
        if (window_sample.rate)
        {
            constexpr std::uint64_t k_clock_tolerance_ns = 100000000u;
            const std::uint64_t awake = window_sample.awake_end - window_sample.awake_begin;
            const std::uint64_t difference = (awake > duration) ? (awake - duration) : (duration - awake);
            if ((window_sample.awake_end < window_sample.awake_begin) || (difference > k_clock_tolerance_ns)) ++invalid;
        }
        std::fprintf(stdout, "WINDOW,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u\n", window_index,
            window_sample.rate, window_sample.controllers, static_cast<unsigned long long>(duration),
            static_cast<unsigned long long>(window_sample.offered), static_cast<unsigned long long>(window_sample.admitted),
            static_cast<unsigned long long>(lateness), static_cast<unsigned long long>(window_sample.wait_end - window_sample.wait_begin),
            static_cast<unsigned long long>(window_sample.round_end - window_sample.round_begin), missing, invalid, excluded, stale);
        if (missing || invalid)
        {
            return false;
        }
        for (std::uint32_t stage = 0u; stage < 6u; ++stage) metric(window_index, names[stage], samples[stage].data(), counts[stage]);
        metric(window_index, "pump_work", pump_work.data(), pump_count);
        metric(window_index, "pump_gap", pump_gap.data(), gap_count);
    }
    std::fflush(stdout);
    return true;
}
}
