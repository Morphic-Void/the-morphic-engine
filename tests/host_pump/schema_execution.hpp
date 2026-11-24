
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_execution.hpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    23 Nov 26
//
//  Retained schema operation, acquisition cleanup and value-only replacement handoff.

#pragma once

#include "framework/module/module_execution.hpp"
#include "framework/assets/schema_messages.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "data_model/document_parser.hpp"
#include "schema/data_block_view.hpp"
#include "platform/filesystem/file.hpp"
#include "platform/threading/wait_deadline.hpp"
#include "debug/macros.hpp"
#include <cstring>

namespace pump_tests
{

class CSchemaExecution
{
public:
    CSchemaExecution(const modules::CCommunication& communication, const bool replacement,
        const char* const handoff_path = "declaration-id.bin") noexcept
        : m_communication{ communication }, m_replacement{ replacement }, m_handoff_path{ handoff_path } {}
    bool start() noexcept
    {
        if (m_active) return false;
        m_active = true;
        m_owner = threading::make_owner_message<SchemaProcessRequest>(290);
        SchemaProcessRequest* const request = m_owner.owner().payload<SchemaProcessRequest>();
        if (!request || (m_deadline.set_after_milliseconds(10000u).code != platform::threading::EWaitError::none))
        {
            m_process_done = true;
            fail();
            return false;
        }
        request->request_id = { 1u };
        static constexpr char declarations[] = R"({"types":{"structures":{"Row":{"members":[{"label":{"type":{"kind":"text"}}}]},"Report":{"members":[{"rows":{"type":{"kind":"span","element":"Row"}}}]}}}})";
        static constexpr char instances[] = R"({"instances":{"Report":{"main":{"declaration":{"rows":{"collection":"all","first":1,"count":1}}}}},"data":{"Row":{"all":{"data":[{"label":"Alpha"},{"label":"Zulu"}]}}}})";
        if (m_replacement)
        {
            const CByteBuffer handoff = platform::filesystem::loadFile(m_handoff_path);
            if (handoff.size() != sizeof(m_declarations))
            {
                m_process_done = true;
                fail();
                return false;
            }
            std::memcpy(&m_declarations, handoff.data(), sizeof(m_declarations));
            if (!m_declarations)
            {
                m_process_done = true;
                fail();
                return false;
            }
            request->operation = schema::ESchemaAssembly::compose_roles;
            request->output_kind = schema::ESchemaOutput::runtime_data_block;
            request->input_count = 2u;
            request->types_source = 0;
            request->instances_source = request->data_source = 1;
            request->inputs.slots[0].kind = ESchemaInput::retained_document;
            request->inputs.slots[0].asset = m_declarations;
        }
        else
        {
            request->output_kind = schema::ESchemaOutput::schema_document;
            request->input_count = 1u;
        }
        SSchemaProcessInput& authored = request->inputs.slots[m_replacement ? 1u : 0u];
        authored.kind = ESchemaInput::owned_document;
        const char* const text = m_replacement ? instances : declarations;
        const std::size_t size = m_replacement ? (sizeof(instances) - 1u) : (sizeof(declarations) - 1u);
        if (!document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(text), size }, authored.document).accepted())
        {
            m_process_done = true;
            fail();
            return false;
        }
        return true;
    }
    void stop() noexcept { m_stopping = true; }
    bool failed() const noexcept { return m_failed; }
    bool ready() const noexcept
    {
        return m_active && !m_failed && ((m_replacement && domain_complete()) || m_handoff_ready);
    }
    bool domain_complete() const noexcept
    {
        return !m_active || (!m_unsafe && m_process_done && !m_acquisition && !m_output && !m_declarations);
    }
    void replacement_accepted() noexcept
    {
        // Called only after the self-replacement intent entered Host transport.
        // The file holds a copied value identity; no view crosses this boundary.
        if (!m_replacement && m_handoff_ready && !m_stopping && !m_failed && !m_unsafe &&
            m_output && !m_output_sent && !m_acquisition)
        {
            m_output = {};
        }
        else unsafe();
    }
    bool owns(const std::int32_t slot) const noexcept { return m_active && (slot >= 290) && (slot <= 293); }
    void receive(const threading::CErasedPodMsg& message) noexcept
    {
        if (m_unsafe) return;
        const std::int32_t slot = message.query_async_slot();
        if (slot == 290)
        {
            SchemaProcessAccepted accepted;
            SchemaProcessResult result;
            if (!m_submitted || m_process_done)
            {
                unsafe();
                return;
            }
            if (message.copy_payload_to(accepted))
            {
                if (m_accepted || (accepted.contract_version != 1u) || (accepted.request_id.value != 1u)) unsafe();
                else m_accepted = true;
                return;
            }
            if (!message.copy_payload_to(result) || (result.request_id.value != 1u) ||
                (result.contract_version != 1u) || result.reserved || (result.status > ESchemaProcessStatus::delivery_failed))
            {
                unsafe();
                return;
            }
            m_process_done = true;
            if (result.status != ESchemaProcessStatus::success)
            {
                if (!(m_stopping && (result.status == ESchemaProcessStatus::cancelled))) fail();
                return;
            }
            if (!m_accepted || !result.success.asset || !result.success.acquisition || !result.success.descriptor)
            {
                unsafe();
                return;
            }
            m_output = result.success.asset;
            m_acquisition = result.success.acquisition;
            // Every descriptor/byte reader is scoped to this invocation. The
            // named release can safely wait under pressure using only value IDs.
            if (!m_stopping && !validate(result)) fail();
            return;
        }
        if (slot == 292)
        {
            AssetReleaseResult result;
            if (!m_release_sent || !m_acquisition || !message.copy_payload_to(result) ||
                (result.acquisition != m_acquisition) || (result.status != EAssetStatus::success))
            {
                unsafe();
                return;
            }
            m_acquisition = {};
            return;
        }
        if ((slot == 291) || (slot == 293))
        {
            CAssetId& asset = (slot == 291) ? m_declarations : m_output;
            const bool sent = (slot == 291) ? m_declarations_sent : m_output_sent;
            AssetDisposeResult result;
            if (!sent || !asset || !message.copy_payload_to(result) || (result.asset != asset) ||
                (result.status != EAssetStatus::success))
            {
                unsafe();
                return;
            }
            asset = {};
            return;
        }
        unsafe();
    }
    void advance(bool& runnable, std::uint64_t& next_wake) noexcept
    {
        if (!m_active || m_unsafe || domain_complete()) return;
        if (m_stopping && !m_submitted)
        {
            m_owner = {};
            m_process_done = true;
        }
        std::uint64_t remaining = 0u;
        if ((m_deadline.remaining_nanoseconds(remaining).code != platform::threading::EWaitError::none) || !remaining)
        {
            unsafe();
            return;
        }
        if (remaining < next_wake) next_wake = remaining;
        // One named publication per turn; accepted operations always await their
        // corresponding result before any dependent release/disposal begins.
        if (!m_submitted && !m_process_done)
        {
            const modules::ESubmission result = m_communication.submit({}, std::move(m_owner));
            if (modules::accepted(result)) m_submitted = true;
            else if (result != modules::ESubmission::busy)
            {
                m_process_done = true;
                fail();
                runnable = static_cast<bool>(m_declarations);
            }
            return;
        }
        if (!m_process_done)
        {
            if (m_stopping && m_accepted && !m_cancel_sent)
            {
                post(threading::make_pod_message(-1, SchemaProcessCancel{ { 1u } }), m_cancel_sent);
            }
            return;
        }
        if (m_acquisition)
        {
            if (!m_release_sent) post(threading::make_pod_message(292, AssetReleaseRequest{ m_acquisition }), m_release_sent);
            return;
        }
        if (!m_replacement && !m_stopping && !m_failed && m_output)
        {
            if (!m_handoff_ready)
            {
                if (!platform::filesystem::saveFile(m_handoff_path,
                    CByteConstView{ reinterpret_cast<const std::uint8_t*>(&m_output), sizeof(m_output) }))
                {
                    fail();
                    runnable = true;
                    return;
                }
                m_handoff_ready = true;
            }
            return;
        }
        if (m_declarations)
        {
            if (!m_declarations_sent) post(threading::make_pod_message(291, AssetDisposeRequest{ m_declarations }), m_declarations_sent);
            return;
        }
        if (m_output && !m_output_sent) post(threading::make_pod_message(293, AssetDisposeRequest{ m_output }), m_output_sent);
    }

private:
    const modules::CCommunication& m_communication;
    const bool m_replacement;
    const char* const m_handoff_path;
    threading::CErasedOwnerMsg m_owner;
    CAssetId m_declarations, m_output;
    CAssetAcquisitionId m_acquisition;
    platform::threading::CWaitDeadline m_deadline;
    bool m_active{}, m_submitted{}, m_accepted{}, m_process_done{}, m_cancel_sent{}, m_release_sent{};
    bool m_declarations_sent{}, m_output_sent{}, m_handoff_ready{}, m_stopping{}, m_failed{}, m_unsafe{};
    void fail() noexcept { m_failed = m_stopping = true; m_owner = {}; }
    void unsafe() noexcept { fail(); m_unsafe = true; }
    void post(const threading::CErasedPodMsg& message, bool& sent) noexcept
    {
        const modules::ESubmission result = m_communication.submit({}, message);
        if (modules::accepted(result)) sent = true;
        else if (result != modules::ESubmission::busy) unsafe();
    }
    bool validate(const SchemaProcessResult& result) noexcept
    {
        const SSchemaOutputV1& output = *result.success.descriptor;
        const schema::ESchemaOutput expected = m_replacement ? schema::ESchemaOutput::runtime_data_block : schema::ESchemaOutput::schema_document;
        if ((result.output_kind != expected) || (output.size != sizeof(output)) || (output.version != 1u) || output.reserved ||
            (output.kind != expected) || !output.bytes.is_ready())
        {
            return false;
        }
        if (!m_replacement) return true;
        schema::CBakedDataBlock block;
        schema::SDataBlockDiagnostic error;
        schema::CDataBlockView view;
        schema::SDataOperationDiagnostic diagnostic;
        schema::CDataValueView value, field;
        schema::SDataRangeView span;
        schema::SDataTextView label;
        const bool valid = block.attach_asset(m_output, output.bytes, error) && view.bind(block, {}, diagnostic) &&
            view.value(view.find_instance(CStringView{ "Report" }, CStringView{ "main" }), 0u, value) &&
            view.member(value, CStringView{ "rows" }, field) && view.span(field, span) &&
            (span.first == 1u) && (span.count == 1u) && view.element(span, 0u, value) &&
            view.member(value, CStringView{ "label" }, field) && view.text(field, label) &&
            (label.stored_text == CStringView{ "Zulu" }) && view.value(span.collection, 0u, value) &&
            view.member(value, CStringView{ "label" }, field) && view.text(field, label) &&
            (label.stored_text == CStringView{ "Alpha" }) && (output.bytes.align() >= 128u) &&
            (block.file_bytes().size() == output.bytes.size()) && block.bulk_payload_view().is_ready();
        if (valid) MV_LOG(info, "Replacement composed retained declarations and read remapped text and span");
        return valid;
    }
};

}   // namespace pump_tests
