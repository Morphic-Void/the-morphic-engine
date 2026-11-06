
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    input_configuration.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Resumable validation and independent immutable input configuration storage.

#pragma once

#include "runtime/input_connection.hpp"
#include "schema/prepared_schema.hpp"
#include "containers/TInstance.hpp"
#include "containers/TUnorderedCollection.hpp"

namespace platform::runtime
{

//  Small individually owned pages bound growth copies, initialisation and cleanup.
template<typename T>
class TConfigurationPages
{
    struct SPage { T entries[128]{}; };
public:
    [[nodiscard]] bool push(const T& value) noexcept
    {
        if ((m_count % 128u) == 0u)
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
        (*m_pages.get_object(m_order[m_count / 128u]))->entries[m_count % 128u] = value;
        ++m_count;
        return true;
    }
    [[nodiscard]] std::uint32_t size() const noexcept { return m_count; }
    [[nodiscard]] T& at(const std::uint32_t index) noexcept
    { return (*m_pages.get_object(m_order[index / 128u]))->entries[index % 128u]; }
    [[nodiscard]] const T& at(const std::uint32_t index) const noexcept
    { return (*m_pages.get_object(m_order[index / 128u]))->entries[index % 128u]; }
    [[nodiscard]] bool clear_page() noexcept
    {
        std::int32_t slot;
        if (m_order.pop_back(slot))
        {
            (void)m_pages.erase(slot);
            m_count = static_cast<std::uint32_t>(m_order.size()) * 128u;
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

enum class EConfigurationTable : std::uint16_t
{
    header, action, context, membership, profile, binding, source, modifier, analog, policy, shortcut, shortcut_modifier, count
};

struct SConfigurationRow
{
    std::uint32_t values[8]{};
    char name[128]{};
};

struct SConfigurationTable
{
    TConfigurationPages<SConfigurationRow> rows;
    TConfigurationPages<std::uint32_t> order[2];
    schema::CTypesIndex type;
    std::uint32_t read_order{};
    [[nodiscard]] const SConfigurationRow& sorted(const std::uint32_t index) const noexcept
    { return rows.at(order[read_order].at(index)); }
};

enum class EConfigurationBuild { building, ready, failed };

class CInputConfiguration
{
public:
    [[nodiscard]] bool begin(const schema::SPreparedSchemaV1* const prepared) noexcept;
    [[nodiscard]] EConfigurationBuild step() noexcept;
    [[nodiscard]] std::uint32_t step_cost() const noexcept;
    [[nodiscard]] std::uint32_t step_bytes() const noexcept;
    [[nodiscard]] bool clear_step() noexcept;
    void detach() noexcept { m_view = schema::CPreparedSchemaView{}; }
    void publish(const input::SInstallationToken token) noexcept;
    [[nodiscard]] const SInputInstallation* profile(const std::uint32_t id, const input::EConnection kind) const noexcept;
    [[nodiscard]] const SConfigurationTable& table(const EConfigurationTable type) const noexcept
    { return m_tables[static_cast<std::uint32_t>(type)]; }
    [[nodiscard]] const SConfigurationRow& binding_at(const std::uint32_t ordinal) const noexcept
    { return table(EConfigurationTable::binding).rows.at(m_binding_order[m_binding_read].at(ordinal)); }
    [[nodiscard]] input::SInstallationResult diagnostic() const noexcept { return m_error; }
    [[nodiscard]] bool ready() const noexcept { return m_stage == EStage::ready; }
private:
    enum class EStage { enumerations, layouts, inventory, instances, bulk, sorting, duplicates, references, grouping, conflicts, profiles, ready, failed };
    [[nodiscard]] bool layout() noexcept;
    [[nodiscard]] bool inventory_step() noexcept;
    [[nodiscard]] bool copy(const schema::CTypesIndex type, const CStringView name,
        const std::uint8_t* const bytes, const std::size_t size, const bool bulk) noexcept;
    [[nodiscard]] bool sort_step() noexcept;
    [[nodiscard]] bool group_step() noexcept;
    [[nodiscard]] bool reference_step() noexcept;
    [[nodiscard]] bool profile_step() noexcept;
    [[nodiscard]] const SConfigurationRow* find(const EConfigurationTable table, const std::uint32_t id) const noexcept;
    [[nodiscard]] bool contains(const EConfigurationTable table, const std::uint32_t first, const std::uint32_t second) const noexcept;
    [[nodiscard]] bool fail(const input::EConfigurationError reason, const std::uint32_t definition = 0u,
        const std::uint32_t field = 0u) noexcept;
    [[nodiscard]] static std::uint64_t key(const SConfigurationRow& row, const std::uint32_t table) noexcept;
    schema::CPreparedSchemaView m_view;
    SConfigurationTable m_tables[12];
    TConfigurationPages<std::uint32_t> m_binding_order[2];
    std::uint32_t m_binding_read{};
    TUnorderedCollection<SInputInstallation> m_profiles;
    input::SInstallationResult m_error;
    EStage m_stage{ EStage::failed };
    std::uint32_t m_table{}, m_cursor{}, m_row{}, m_total_relationships{};
    std::uint32_t m_inventory[12]{};
    std::uint32_t m_copy_stage{}, m_copy_index{};
    std::uint32_t m_width{ 1u }, m_begin{}, m_left{}, m_right{}, m_end{}, m_output{};
    std::uint32_t m_profile_stage{}, m_profile_cursor{}, m_profile_binding{};
    std::int32_t m_profile_slot{ -1 };
    bool m_profile_actions[1024]{};
    std::uint32_t m_cleanup{};
};

}   // namespace platform::runtime
