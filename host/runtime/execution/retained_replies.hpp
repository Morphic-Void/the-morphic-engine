
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    retained_replies.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Nov 26
//
//  Dispatch-local fallback reservations, retained acceptance and output ordinals.

#pragma once

#include "host/runtime/execution/module_pipes.hpp"

namespace host
{

enum class EReplyWrite : std::uint8_t { published, retained, busy, invalid, unavailable, overflow, lost };

class CRetainedReplies
{
public:
    explicit CRetainedReplies(memory::CMemoryContext& context,
        const std::uint32_t limit = k_module_fifo_limit) noexcept
        : m_tail{ context, (limit ? (limit - 1u) : 0u) }, m_limit{ limit } {}

    //  Logical reply credits belong to SClientRecord, not this storage. A scope
    //  may reserve space only for responses produced during the current dispatch.
    class CDispatch
    {
    public:
        CDispatch(CRetainedReplies& replies, const std::uint32_t count, const bool admission_allowed = true) noexcept
            : m_replies{ replies }, m_reserved{ replies.reserve(count, admission_allowed) } {}
        ~CDispatch() noexcept
        {
            if (m_reserved)
            {
                m_replies.m_reserved = 0u;
                m_replies.m_dispatch = false;
            }
        }
        CDispatch(const CDispatch&) = delete;
        CDispatch& operator=(const CDispatch&) = delete;
        [[nodiscard]] explicit operator bool() const noexcept { return m_reserved; }

    private:
        CRetainedReplies& m_replies;
        const bool m_reserved;
    };

    [[nodiscard]] std::uint32_t size() const noexcept { return m_tail.size() + (m_front_live ? 1u : 0u); }
    [[nodiscard]] std::uint32_t reservations() const noexcept { return m_reserved; }
    [[nodiscard]] bool idle() const noexcept { return !m_front_live && !m_reserved; }
    [[nodiscard]] std::uint64_t issued() const noexcept { return m_issued; }
    [[nodiscard]] std::uint64_t published() const noexcept { return m_published; }
    [[nodiscard]] bool available(const std::uint32_t count) const noexcept
    {
        return count <= (m_limit - size() - m_reserved);
    }

    //  Producer-owned operations retain their own result on busy. No fallback
    //  entry or order position is acquired by a declined physical publication.
    [[nodiscard]] EReplyWrite publish(CModulePodPipe& pipe, const threading::CErasedPodMsg& message) noexcept
    {
        if (!pipe.admits(message))
        {
            return EReplyWrite::invalid;
        }
        if (!pipe.ready()) return EReplyWrite::unavailable;
        if (m_front_live) return EReplyWrite::busy;
        if (m_issued == UINT64_MAX)
        {
            return EReplyWrite::overflow;
        }
        const EReplyWrite result = write(pipe, message);
        if (result == EReplyWrite::published)
        {
            ++m_issued;
            ++m_published;
        }
        return result;
    }

    //  The caller transfers one already-owed immediate reply. A successful return
    //  owns the obligation even when it has not reached the physical pipe yet.
    [[nodiscard]] EReplyWrite retain(CModulePodPipe& pipe, const threading::CErasedPodMsg& message) noexcept
    {
        if (!m_dispatch || !m_reserved)
        {
            return EReplyWrite::lost;
        }
        if (!pipe.admits(message))
        {
            return EReplyWrite::invalid;
        }
        if (!pipe.ready()) return EReplyWrite::unavailable;
        if (m_issued == UINT64_MAX)
        {
            return EReplyWrite::overflow;
        }
        if (!m_front_live)
        {
            const EReplyWrite result = write(pipe, message);
            if (result == EReplyWrite::published)
            {
                --m_reserved;
                ++m_issued;
                ++m_published;
                return result;
            }
            if (result != EReplyWrite::busy) return result;
            m_front = message;
            m_front_live = true;
        }
        else if (!m_tail.push(message)) return EReplyWrite::lost;
        --m_reserved;
        ++m_issued;
        return EReplyWrite::retained;
    }

    [[nodiscard]] EReplyWrite flush(CModulePodPipe& pipe, const std::uint32_t allowance,
        std::uint32_t& published) noexcept
    {
        published = 0u;
        while (m_front_live && (published < allowance))
        {
            const EReplyWrite result = write(pipe, m_front);
            if (result != EReplyWrite::published)
            {
                return result;
            }
            ++published;
            ++m_published;
            if (m_tail.size())
            {
                if (!m_tail.pop(m_front))
                {
                    return EReplyWrite::lost;
                }
            }
            else m_front_live = false;
        }
        return m_front_live ? EReplyWrite::busy : EReplyWrite::published;
    }

    //  Only after recipient execution quiescence and operation-specific
    //  undeliverable settlement. Issued ordinals remain historical frontiers.
    void cancel_after_quiescence() noexcept
    {
        threading::CErasedPodMsg message;
        while (m_tail.pop(message)) {}
        m_front_live = false;
    }

private:
    [[nodiscard]] bool reserve(const std::uint32_t count, const bool admission_allowed) noexcept
    {
        if (m_dispatch || (count && !admission_allowed) || !available(count))
        {
            return false;
        }
        const std::uint32_t tail_needed = (count && !m_front_live) ? (count - 1u) : count;
        if (!m_tail.ensure_free(tail_needed)) return false;
        m_reserved = count;
        m_dispatch = true;
        return true;
    }
    [[nodiscard]] static EReplyWrite write(CModulePodPipe& pipe, const threading::CErasedPodMsg& message) noexcept
    {
        switch (pipe.post(message))
        {
            case EModulePipePost::published: return EReplyWrite::published;
            case EModulePipePost::full: return EReplyWrite::busy;
            case EModulePipePost::invalid: return EReplyWrite::invalid;
            case EModulePipePost::not_ready: return EReplyWrite::unavailable;
        }
        return EReplyWrite::unavailable;
    }

    TModuleFifo<threading::CErasedPodMsg> m_tail;
    const std::uint32_t m_limit;
    threading::CErasedPodMsg m_front;
    std::uint32_t m_reserved{ 0u };
    std::uint64_t m_issued{ 0u }, m_published{ 0u };
    bool m_front_live{ false }, m_dispatch{ false };
};

}   //  namespace host
