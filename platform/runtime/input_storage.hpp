
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_storage.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    08 Nov 26
//
//  Bounded pages for configuration and connection mapper storage.

#pragma once

#include "containers/TInstance.hpp"
#include "containers/TPodVector.hpp"
#include "containers/TUnorderedCollection.hpp"

namespace platform::runtime
{

inline constexpr std::uint32_t k_input_page_shift = 7u;
inline constexpr std::uint32_t k_input_page_entries = 1u << k_input_page_shift;
inline constexpr std::uint32_t k_input_page_mask = k_input_page_entries - 1u;

//  Small individually owned pages bound growth copies, initialisation and cleanup.
template<typename T>
class TConfigurationPages
{
    struct SPage { T entries[k_input_page_entries]{}; };
public:
    [[nodiscard]] bool push(const T& value) noexcept
    {
        if ((m_count & k_input_page_mask) == 0u)
        {
            if (!m_ready)
            {
                if (!m_pages.initialise()) return false;
                m_ready = true;
            }
            const std::int32_t slot = m_pages.emplace();
            if (slot < 0) return false;
            TInstance<SPage>& page = *m_pages.get_object(slot);
            if (!page.emplace() || !m_order.push_back(slot))
            {
                (void)m_pages.erase(slot);
                return false;
            }
        }
        (*m_pages.get_object(m_order[m_count >> k_input_page_shift]))->entries[m_count & k_input_page_mask] = value;
        ++m_count;
        return true;
    }
    [[nodiscard]] std::uint32_t size() const noexcept { return m_count; }
    [[nodiscard]] T& at(const std::uint32_t index) noexcept
    { return (*m_pages.get_object(m_order[index >> k_input_page_shift]))->entries[index & k_input_page_mask]; }
    [[nodiscard]] const T& at(const std::uint32_t index) const noexcept
    { return (*m_pages.get_object(m_order[index >> k_input_page_shift]))->entries[index & k_input_page_mask]; }
    [[nodiscard]] bool clear_page() noexcept
    {
        std::int32_t slot;
        if (m_order.pop_back(slot))
        {
            (void)m_pages.erase(slot);
            m_count = static_cast<std::uint32_t>(m_order.size()) << k_input_page_shift;
            return false;
        }
        m_order.deallocate();
        m_pages.deallocate();
        m_count = 0u;
        m_ready = false;
        return true;
    }
private:
    TUnorderedCollection<TInstance<SPage>> m_pages;
    TPodVector<std::int32_t> m_order;
    std::uint32_t m_count{};
    bool m_ready{ false };
};

}   // namespace platform::runtime
