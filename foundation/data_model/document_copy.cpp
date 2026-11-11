
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_copy.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Stabilise names before document storage can move.

#include "data_model/document_copy.hpp"

#include <cstring>

namespace document_translation
{

bool detail::CDocumentConstruction::append(CLiveDocument& target, const CNodeKey parent, const CNodeKey child) noexcept
{
    if (!child)
    {
        return false;
    }
    if (target.append_constructed_child(parent, child).succeeded())
    {
        return true;
    }
    if (target.is_detached(child)) (void)target.erase(child);
    return false;
}

CPropertyNameId detail::CDocumentConstruction::name(CLiveDocument& target, const CStringView& text) noexcept
{
    if (!target.is_ready())
    {
        return {};
    }
    CLiveDocument::SPreparedString prepared;
    CPropertyNameId id;
    return target.prepare_string(text, prepared) && target.intern_property_name(prepared, id) ? id : CPropertyNameId{};
}

bool detail::CDocumentConstruction::initialise_strings(CLiveDocument& target, const bool names, const std::size_t count, const std::size_t bytes) noexcept
{
    CStableStrings& strings = names ? target.m_property_names : target.m_string_values;
    if (!target.is_ready() || (strings.memory_attribution().source_state != memory::EMemorySourceState::empty))
    {
        return false;
    }
    return count ? strings.initialise(count, bytes) : (bytes == 0u);
}

//  Imported text has already been admitted by a separate source document.
//  Same-document copying retains its IDs and never borrows growing string views.
CPropertyNameId detail::CDocumentConstruction::import_name(CLiveDocument& target, const CStringView& text) noexcept
{
    if (!target.is_ready())
    {
        return {};
    }
    CLiveDocument::SPreparedString prepared;
    prepared.bytes = text.string();
    prepared.size = text.length();
    CPropertyNameId id;
    return target.intern_property_name(prepared, id) ? id : CPropertyNameId{};
}

CStringValueId detail::CDocumentConstruction::import_string(CLiveDocument& target, const CStringView& text) noexcept
{
    if (!target.is_ready())
    {
        return {};
    }
    CLiveDocument::SPreparedString prepared;
    prepared.bytes = text.string();
    prepared.size = text.length();
    CStringValueId id;
    return target.intern_string_value(prepared, id) ? id : CStringValueId{};
}

CNodeKey detail::CDocumentConstruction::create(CLiveDocument& target, const ELiveValueType kind,
    const std::uint64_t bits, const CIntegerMetadata metadata, const CPropertyNameId name,
    const bool object_entry) noexcept
{
    return target.create_value(kind, bits, metadata, name, object_entry);
}

//==============================================================================
//  Name stabilisation
//==============================================================================

[[nodiscard]] bool stabilise_document_name(const CStringView& source, CByteBuffer& storage, CStringView& copied) noexcept
{
    if (source.empty())
    {
        return false;
    }
    if (source.length() == 0u)
    {
        copied = CStringView{ "" };
        return true;
    }
    if (!storage.allocate(source.length(), 1u) || !storage.set_size(source.length()))
    {
        return false;
    }
    std::memcpy(storage.data(), source.string(), source.length());
    copied = CStringView{ storage.data(), source.length() };
    return true;
}

}   // namespace document_translation
