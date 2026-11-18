
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    SchemaReference_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    17 Nov 26
//
//  Portable schema text/span construction and checked contextual access.

#include "tests/test_suites/SchemaReference_test_cases.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/schema_fixtures.hpp"
#include "schema/data_block_view.hpp"
#include "schema/data_copy.hpp"
#include "schema/data_block_edit.hpp"
#include "schema/values/value_codec.hpp"

#include <cstdio>

namespace
{

constexpr char k_report[] = R"({
  "types":{"structures":{
    "Mode":{"members":[{"name":{"type":{"kind":"text"}}},{"hz":{"type":"u32"}}]},
    "Report":{"members":[
      {"label":{"type":{"kind":"text"},"default":"Zebra default"}},
      {"optional":{"type":{"kind":"text","nullable":true}}},
      {"modes":{"type":{"kind":"span","element":"Mode"}}}
    ]}
  }},
  "instances":{"Report":{"report":{"declaration":{"modes":{"collection":"modes","first":0,"count":2}},
    "specialisation":{"empty":{"declaration":{"label":"","optional":{"absent":true},"modes":{"empty":true}}}}}}},
  "data":{"Mode":{"modes":{"data":[{"name":"Alpha\u0000omega","hz":60},{"name":"Display \u00e9","hz":120}]}}}
})";

void construction(tests::TTestContext& context)
{
    schema::CBakedDataBlock block;
    schema::SSchemaDiagnostic error;
    const bool built = tests::bake_schema_document(k_report, block, error);
    if (!built)
    {
        std::printf("Reference bake failure: schema=%u stage=%u types=%u block=%u operation=%u limit=%u\n",
            static_cast<unsigned>(error.reason), static_cast<unsigned>(error.stage), static_cast<unsigned>(error.types.reason),
            static_cast<unsigned>(error.block.reason), static_cast<unsigned>(error.operation.reason), static_cast<unsigned>(error.operation.limit));
    }
    TEST_EXPECT(context, built);
    if (!built) return;
    schema::CDataBlockView view;
    schema::SDataOperationDiagnostic diagnostic;
    TEST_EXPECT(context, view.bind(block, {}, diagnostic));
    if (!view.is_ready()) return;
    const schema::CDataEntryHandle report = view.find_instance(CStringView{ "Report" }, CStringView{ "report" });
    schema::CDataValueView root, field;
    schema::SDataTextView text;
    TEST_EXPECT(context, view.value(report, 0u, root));
    TEST_EXPECT(context, view.member(root, CStringView{ "label" }, field) && view.text(field, text));
    TEST_EXPECT(context, !text.absent && (text.stored_text == CStringView{ "Zebra default" }));
    TEST_EXPECT(context, view.member(root, CStringView{ "optional" }, field) && view.text(field, text) && text.absent);
    schema::SDataRangeView range;
    TEST_EXPECT(context, view.member(root, CStringView{ "modes" }, field) && view.span(field, range));
    TEST_EXPECT(context, !range.absent && (range.first == 0u) && (range.count == 2u));
    schema::CDataValueView mode;
    TEST_EXPECT(context, view.element(range, 0u, mode) && view.member(mode, CStringView{ "name" }, field));
    CByteBuffer logical;
    TEST_EXPECT(context, view.copy_text_utf8(field, logical));
    constexpr char expected[]{ 'A', 'l', 'p', 'h', 'a', '\0', 'o', 'm', 'e', 'g', 'a' };
    TEST_EXPECT(context, (logical.size() == sizeof(expected)) && (std::memcmp(logical.data(), expected, sizeof(expected)) == 0));
    const schema::CDataEntryHandle empty = view.find_instance(CStringView{ "Report" }, CStringView{ "empty" }, report);
    TEST_EXPECT(context, view.value(empty, 0u, root) && view.member(root, CStringView{ "label" }, field) && view.text(field, text));
    TEST_EXPECT(context, !text.absent && (text.stored_text.length() == 0u));
    TEST_EXPECT(context, view.member(root, CStringView{ "modes" }, field) && view.span(field, range) && !range.absent && !range.count);
    const CBakedDocument& document = block.document();
    std::uint64_t version{};
    TEST_EXPECT(context, document.unsigned_integer_value(document.object_child(document.root(), CStringView{ "version" }), version) && (version == 1u));
    TEST_EXPECT(context, document.child_count(document.object_child(document.root(), CStringView{ "retainedText" })) == 3u);
    CBakedDocumentBlock exported;
    const bool exported_ok = schema::export_data(view, {}, exported, diagnostic);
    if (!exported_ok) std::printf("Export failure: reason=%u stage=%u limit=%u entry=%s\n", static_cast<unsigned>(diagnostic.reason),
        static_cast<unsigned>(diagnostic.stage), static_cast<unsigned>(diagnostic.limit), diagnostic.entry);
    TEST_EXPECT(context, exported_ok);
    schema::CSchema imported;
    schema::CBakedDataBlock imported_block;
    TEST_EXPECT(context, exported_ok && imported.copy_from(schema::CSchemaDocumentQuery{ exported.document() }, error) &&
        imported.bake(imported_block, error));
    schema::CBakedDataBlock repacked;
    const bool repacked_ok = schema::repack_data(view, {}, repacked, diagnostic);
    if (!repacked_ok) std::printf("Repack failure: reason=%u stage=%u limit=%u entry=%s\n", static_cast<unsigned>(diagnostic.reason),
        static_cast<unsigned>(diagnostic.stage), static_cast<unsigned>(diagnostic.limit), diagnostic.entry);
    TEST_EXPECT(context, repacked_ok);
    schema::CDataBlockView compact;
    TEST_EXPECT(context, repacked_ok && compact.bind(repacked, {}, diagnostic));
    if (compact.is_ready())
    {
        TEST_EXPECT(context, compact.entry_count(schema::EDataEntryKind::instance) == 2u);
        TEST_EXPECT(context, compact.entry_count(schema::EDataEntryKind::collection) == 1u);
        const schema::CDataEntryHandle copied_report = compact.find_instance(CStringView{ "Report" }, CStringView{ "report" });
        TEST_EXPECT(context, compact.value(copied_report, 0u, root) && compact.member(root, CStringView{ "modes" }, field) && compact.span(field, range));
        TEST_EXPECT(context, range.count == 2u);
    }
    schema::CBakedDataBlock copied;
    const schema::CDataBlockView* const sources[]{ &view };
    const schema::SDataCopyRoot roots[]{ { 0u, report } };
    const bool copied_ok = schema::copy_data(sources, 1u, roots, 1u, nullptr, 0u, {}, copied, diagnostic);
    TEST_EXPECT(context, copied_ok);
    schema::CDataBlockView subset;
    TEST_EXPECT(context, copied_ok && subset.bind(copied, {}, diagnostic));
    if (subset.is_ready())
    {
        TEST_EXPECT(context, subset.entry_count(schema::EDataEntryKind::instance) == 1u);
        TEST_EXPECT(context, subset.entry_count(schema::EDataEntryKind::collection) == 1u);
    }
    schema::CDataBlockEdit edit;
    const bool began = edit.begin(view, {}, diagnostic);
    TEST_EXPECT(context, began);
    if (began)
    {
        const schema::CDataEditHandle edited_report = edit.find(CStringView{ "Report" }, schema::EDataEntryKind::instance, CStringView{ "report" });
        const schema::CDataEditHandle edited_modes = edit.find(CStringView{ "Mode" }, schema::EDataEntryKind::collection, CStringView{ "modes" });
        CLiveDocument input;
        TEST_EXPECT(context, tests::parse_document("{\"value\":\"New title\"}", input));
        const schema::SInstanceSelectionStep path[]{ { schema::EInstanceSelectionStepKind::member, CStringView{ "label" }, 0u } };
        TEST_EXPECT(context, edit.write(edited_report, 0u, path, 1u, input, input.object_child(input.root(), CStringView{ "value" }),
            schema::EInstanceDescendantPolicy::preserve, diagnostic));
        TEST_EXPECT(context, edit.rename(edited_modes, CStringView{ "renamed modes" }, diagnostic));
        schema::CBakedDataBlock edited;
        const bool committed = edit.commit(edited, diagnostic);
        if (!committed) std::printf("Edit commit failure: reason=%u stage=%u limit=%u entry=%s\n", static_cast<unsigned>(diagnostic.reason),
            static_cast<unsigned>(diagnostic.stage), static_cast<unsigned>(diagnostic.limit), diagnostic.entry);
        TEST_EXPECT(context, committed);
        TEST_EXPECT(context, committed && !edit.is_ready());
        schema::CDataBlockView changed;
        TEST_EXPECT(context, committed && changed.bind(edited, {}, diagnostic));
        if (changed.is_ready())
        {
            const schema::CDataEntryHandle edited_root = changed.find_instance(CStringView{ "Report" }, CStringView{ "report" });
            TEST_EXPECT(context, changed.value(edited_root, 0u, root) && changed.member(root, CStringView{ "label" }, field) && changed.text(field, text));
            TEST_EXPECT(context, text.stored_text == CStringView{ "New title" });
            TEST_EXPECT(context, changed.member(root, CStringView{ "modes" }, field) && changed.span(field, range));
            schema::SDataEntryInfo target;
            TEST_EXPECT(context, changed.entry(range.collection, target) && (target.name == CStringView{ "renamed modes" }));
        }
        TEST_EXPECT(context, !edit.rename(edited_modes, CStringView{ "stale" }, diagnostic));
        TEST_EXPECT(context, diagnostic.reason == schema::EDataOperationReason::stale_handle);
    }
    TEST_EXPECT(context, view.value(report, 0u, root) && view.member(root, CStringView{ "label" }, field));
    view.clear();
    TEST_EXPECT(context, !view.value(report, 0u, root));
    TEST_EXPECT(context, !view.text(field, text));
}

void checked_entry_identities(tests::TTestContext& context)
{
    schema::CBakedDataBlock block;
    schema::SSchemaDiagnostic schema_error;
    TEST_EXPECT(context, tests::bake_schema_document(k_report, block, schema_error));
    if (!block.loaded_ready()) return;
    schema::CDataBlockView view, alias;
    schema::SDataOperationDiagnostic diagnostic;
    TEST_EXPECT(context, view.bind(block, {}, diagnostic));
    TEST_EXPECT(context, alias.bind(block, {}, diagnostic));
    if (!view.is_ready() || !alias.is_ready())
    {
        return;
    }
    for (const schema::EDataEntryKind expected_kind : { schema::EDataEntryKind::instance, schema::EDataEntryKind::collection })
    {
        for (std::uint32_t expected_ordinal = 0u; expected_ordinal < view.entry_count(expected_kind); ++expected_ordinal)
        {
            schema::EDataEntryKind kind{};
            std::uint32_t ordinal = UINT32_MAX;
            const schema::CDataEntryHandle handle = view.entry_at(expected_kind, expected_ordinal);
            TEST_EXPECT(context, view.entry_identity(handle, kind, ordinal));
            TEST_EXPECT(context, (kind == expected_kind) && (ordinal == expected_ordinal));
        }
    }
    const auto rejected = [&context](const schema::CDataBlockView& owner, const schema::CDataEntryHandle handle)
    {
        schema::EDataEntryKind kind = schema::EDataEntryKind::collection;
        std::uint32_t ordinal = UINT32_MAX;
        TEST_EXPECT(context, !owner.entry_identity(handle, kind, ordinal));
        TEST_EXPECT(context, (kind == schema::EDataEntryKind::collection) && (ordinal == UINT32_MAX));
    };
    const schema::CDataEntryHandle report = view.find_instance(CStringView{ "Report" }, CStringView{ "report" });
    const schema::CDataEntryHandle alias_report = alias.find_instance(CStringView{ "Report" }, CStringView{ "report" });
    rejected(view, {});
    rejected(view, alias_report);
    rejected(alias, report);
    schema::CDataBlockView unbound;
    rejected(unbound, report);

    const schema::CDataBlockView* const sources[]{ &view, &alias };
    const schema::SDataCopyRoot valid_root[]{ { 0u, report } };
    schema::CBakedDataBlock output;
    for (const schema::CDataEntryHandle invalid : { schema::CDataEntryHandle{}, alias_report })
    {
        const schema::SDataCopyRoot invalid_root[]{ { 0u, invalid } };
        TEST_EXPECT(context, !schema::copy_data(sources, 2u, invalid_root, 1u, nullptr, 0u, {}, output, diagnostic));
        TEST_EXPECT(context, (diagnostic.reason == schema::EDataOperationReason::stale_handle) && !output.loaded_ready());
        const schema::SDataCopyName invalid_name[]{ { 0u, invalid, CStringView{ "renamed" } } };
        TEST_EXPECT(context, !schema::copy_data(sources, 2u, valid_root, 1u, invalid_name, 1u, {}, output, diagnostic));
        TEST_EXPECT(context, (diagnostic.reason == schema::EDataOperationReason::stale_handle) && !output.loaded_ready());
    }
    const schema::SDataCopyName duplicate_names[]{ { 0u, report, CStringView{ "first" } },
        { 1u, alias_report, CStringView{ "second" } } };
    TEST_EXPECT(context, !schema::copy_data(sources, 2u, valid_root, 1u, duplicate_names, 2u, {}, output, diagnostic));
    TEST_EXPECT(context, (diagnostic.reason == schema::EDataOperationReason::naming_conflict) && !output.loaded_ready());

    const schema::SDataCopyRoot alias_root[]{ { 1u, alias_report } };
    const schema::SDataCopyName rename[]{ { 0u, report, CStringView{ "renamed report" } } };
    schema::CBakedDataBlock alias_output;
    TEST_EXPECT(context, schema::copy_data(sources, 2u, alias_root, 1u, rename, 1u, {}, alias_output, diagnostic));
    schema::CDataBlockView copied;
    TEST_EXPECT(context, copied.bind(alias_output, {}, diagnostic));
    if (copied.is_ready())
    {
        TEST_EXPECT(context, (copied.entry_count(schema::EDataEntryKind::instance) == 1u));
        TEST_EXPECT(context, (copied.entry_count(schema::EDataEntryKind::collection) == 1u));
        TEST_EXPECT(context, !copied.find_instance(CStringView{ "Report" }, CStringView{ "report" }));
        const schema::CDataEntryHandle copied_report = copied.find_instance(CStringView{ "Report" }, CStringView{ "renamed report" });
        schema::CDataValueView root, field, mode;
        schema::SDataTextView text;
        TEST_EXPECT(context, copied.value(copied_report, 0u, root));
        TEST_EXPECT(context, copied.member(root, CStringView{ "label" }, field) && copied.text(field, text));
        TEST_EXPECT(context, !text.absent && (text.stored_text == CStringView{ "Zebra default" }));
        schema::SDataRangeView range;
        TEST_EXPECT(context, copied.member(root, CStringView{ "modes" }, field) && copied.span(field, range));
        TEST_EXPECT(context, !range.absent && (range.first == 0u) && (range.count == 2u));
        schema::SDataEntryInfo dependency;
        TEST_EXPECT(context, copied.entry(range.collection, dependency) && (dependency.name == CStringView{ "modes" }));
        TEST_EXPECT(context, copied.element(range, 1u, mode) && copied.member(mode, CStringView{ "name" }, field) && copied.text(field, text));
        TEST_EXPECT(context, !text.absent && (text.stored_text == CStringView{ "Display \xc3\xa9" }));
        CByteConstView bytes;
        TEST_EXPECT(context, copied.member(mode, CStringView{ "hz" }, field) && copied.bytes(field, bytes) &&
            (bytes.size() == 4u) && (schema::read_scalar_bits(bytes.data(), bytes.size()) == 120u));
    }

    view.clear();
    rejected(view, report);
    TEST_EXPECT(context, view.bind(block, {}, diagnostic));
    if (!view.is_ready()) return;
    rejected(view, report);
    TEST_EXPECT(context, !schema::copy_data(sources, 2u, valid_root, 1u, nullptr, 0u, {}, output, diagnostic));
    TEST_EXPECT(context, (diagnostic.reason == schema::EDataOperationReason::stale_handle) && !output.loaded_ready());
    schema::EDataEntryKind kind{};
    std::uint32_t ordinal = UINT32_MAX;
    TEST_EXPECT(context, view.entry_identity(view.find_instance(CStringView{ "Report" }, CStringView{ "report" }), kind, ordinal));
    TEST_EXPECT(context, (kind == schema::EDataEntryKind::instance) && (ordinal == 0u));
}

}   // namespace

void run_schema_reference_cases(tests::TTestContext& context)
{
    construction(context);
    checked_entry_identities(context);
    run_schema_reference_validation_cases(context);
}

int run_schema_reference_tests()
{
    tests::TTestContext context;
    run_schema_reference_cases(context);
    return context.report("SchemaReferences");
}
