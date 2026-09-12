
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    DocumentParser_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    8 Sep 26

#include "tests/test_suites/DocumentParser_test_suite.hpp"

#include <charconv>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "data_model/baked_document.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "data_model/live_document.hpp"
#include "text/text_linter.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/test_scopes.hpp"

namespace document_parser_tests
{

using tests::TTestContext;

//  Helpers have file-local linkage; the namespace groups this suite's names.
//  Grammar and construction tests opt into every supported feature.
static CDocumentParseReport parse(const std::string& text, CLiveDocument& destination)
{
    return document_parser::parse(CStringView{ text.data(), text.size() }, destination, { document_policy::k_all_supported });
}

static std::string write(TTestContext& ctx, const CLiveDocument& document, const EDocumentWriteMode mode = EDocumentWriteMode::morphic)
{
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(document, block));
    CDocumentWriteOptions options;
    options.mode = mode;
    options.pretty_print = false;
    options.trailing_line_ending = false;
    options.escape_non_ascii = true;
    const auto result = document_writer::write(block.document(), options);
    TEST_EXPECT(ctx, result.report.succeeded());
    return result.report.succeeded() ? std::string(reinterpret_cast<const char*>(result.output.data()), result.report.logical_text_byte_size) : std::string{};
}

static CNodeKey member(const CLiveDocument& document, const CNodeKey object, const CStringView& name)
{
    for (CNodeKey child = document.first_child(object); child.is_valid(); child = document.next_sibling(child))
    {
        if (document.name(child) == name)
        {
            return child;
        }
    }
    return {};
}

static void test_findings_and_policy_contract(TTestContext& ctx)
{
    static_assert(sizeof(EDocumentFinding) == sizeof(std::uint32_t));
    static_assert(document_finding_bit(EDocumentFinding::reserved_undefined_cp1252_byte) ==
        text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte));
    constexpr std::uint32_t groups[]{ document_findings::k_source,
        document_findings::k_relaxed, document_findings::k_morphic, document_findings::k_semantic };
    std::uint32_t combined = 0u;
    for (const std::uint32_t group : groups)
    {
        TEST_EXPECT(ctx, (combined & group) == 0u);
        std::uint32_t range = group;
        while ((range != 0u) && ((range & 1u) == 0u))
        {
            range >>= 1u;
        }
        TEST_EXPECT(ctx, (range != 0u) && ((range & (range + 1u)) == 0u));
        combined |= group;
    }
    TEST_EXPECT(ctx, combined == document_findings::k_all);
    TEST_EXPECT(ctx, document_findings::k_relaxed == (EDocumentFinding::comments |
        EDocumentFinding::unquoted_names | EDocumentFinding::unquoted_strings | EDocumentFinding::single_quotes |
        EDocumentFinding::trailing_commas | EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::raw_quoted_controls |
        EDocumentFinding::name_collision_extension | EDocumentFinding::implicit_body));
    TEST_EXPECT(ctx, document_findings::k_morphic == (EDocumentFinding::explicit_plus |
        EDocumentFinding::binary | EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix));
    TEST_EXPECT(ctx, document_findings::k_semantic == (EDocumentFinding::empty_member_name |
        EDocumentFinding::singleton_normalization | EDocumentFinding::logical_nul));
    TEST_EXPECT(ctx, document_findings::k_source ==
        (k_text_source_encoding_features | k_text_source_observations |
            text_source_finding_bit(ETextSourceFinding::utf8_attempt_failed)));
    TEST_EXPECT(ctx, (document_findings::k_all & text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte)) == 0u);
    TEST_EXPECT(ctx, !CDocumentPolicyResult{}.accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(0u).accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(0u, { document_policy::k_ascii }).accepted());

    constexpr std::uint32_t default_features = EDocumentFinding::non_ascii_utf8 |
        EDocumentFinding::modified_nul | EDocumentFinding::cesu8_pair | EDocumentFinding::cp1252 |
        EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal | EDocumentFinding::binary |
        EDocumentFinding::alternate_hexadecimal_prefix;
    TEST_EXPECT(ctx, CDocumentParseOptions{}.allowed_features == default_features);
    const auto default_result = document_policy::evaluate(document_findings::k_all);
    TEST_EXPECT(ctx, default_result.status == EDocumentPolicyStatus::rejected);
    TEST_EXPECT(ctx, default_result.disallowed_features == document_findings::k_relaxed);
    TEST_EXPECT(ctx, document_policy::evaluate(document_findings::k_all, { document_policy::k_all_supported }).accepted());

    //  Every permission can be selected individually; every other bit is an
    //  invalid policy option, including known informational findings.
    for (unsigned index = 0u; index < 32u; ++index)
    {
        const std::uint32_t bit = 1u << index;
        const auto selected = document_policy::evaluate(bit, { bit });
        const auto excluded = document_policy::evaluate(bit, { document_policy::k_ascii });
        if ((bit & document_findings::k_acceptance_features) != 0u)
        {
            TEST_EXPECT(ctx, selected.accepted());
            TEST_EXPECT(ctx, selected.unknown_policy_bits == 0u);
            TEST_EXPECT(ctx, excluded.status == EDocumentPolicyStatus::rejected);
            TEST_EXPECT(ctx, excluded.disallowed_features == bit);
            const auto default_bit = document_policy::evaluate(bit);
            TEST_EXPECT(ctx, default_bit.accepted() == ((default_features & bit) != 0u));
        }
        else
        {
            TEST_EXPECT(ctx, selected.status == EDocumentPolicyStatus::invalid_options);
            TEST_EXPECT(ctx, selected.unknown_policy_bits == bit);
            TEST_EXPECT(ctx, !selected.accepted());
            //  This evaluates feature permissions only, not processing success.
            TEST_EXPECT(ctx, excluded.accepted());
        }
    }

    const EDocumentFinding compatibility_forms[]{ EDocumentFinding::modified_nul, EDocumentFinding::cesu8_pair };
    for (const EDocumentFinding form : compatibility_forms)
    {
        const auto result = document_policy::evaluate(EDocumentFinding::non_ascii_utf8 | form,
            { document_finding_bit(form) });
        TEST_EXPECT(ctx, result.accepted());
        TEST_EXPECT(ctx, result.effective_allowed_features == (EDocumentFinding::non_ascii_utf8 | form));
        TEST_EXPECT(ctx, document_policy::evaluate(document_findings::k_relaxed, { document_finding_bit(form) }).disallowed_features ==
            document_findings::k_relaxed);
    }
    TEST_EXPECT(ctx, document_policy::evaluate(document_policy::k_modified_utf8,
        { document_policy::k_modified_utf8 }).accepted());
    const auto narrower = document_policy::evaluate(EDocumentFinding::hexadecimal | EDocumentFinding::comments,
        { document_finding_bit(EDocumentFinding::hexadecimal) });
    TEST_EXPECT(ctx, narrower.disallowed_features == document_finding_bit(EDocumentFinding::comments));
    const auto invalid = document_policy::evaluate(default_features, { default_features | (1u << 31) });
    TEST_EXPECT(ctx, invalid.status == EDocumentPolicyStatus::invalid_options);
    TEST_EXPECT(ctx, invalid.unknown_policy_bits == (1u << 31));

    //  Either hexadecimal spelling needs the base permission; # needs both.
    const auto alternate_hex = EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix;
    TEST_EXPECT(ctx, document_policy::evaluate(document_finding_bit(EDocumentFinding::hexadecimal)).accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(alternate_hex).accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(alternate_hex,
        { document_finding_bit(EDocumentFinding::hexadecimal) }).disallowed_features ==
        document_finding_bit(EDocumentFinding::alternate_hexadecimal_prefix));
    TEST_EXPECT(ctx, document_policy::evaluate(alternate_hex, { alternate_hex }).accepted());
    TEST_EXPECT(ctx, document_policy::evaluate(alternate_hex,
        { document_finding_bit(EDocumentFinding::alternate_hexadecimal_prefix) }).disallowed_features ==
            document_finding_bit(EDocumentFinding::hexadecimal));

    //  The same permission admits either implicit body; root kind belongs to the document.
    const auto implicit_body = document_finding_bit(EDocumentFinding::implicit_body);
    TEST_EXPECT(ctx, document_policy::evaluate(implicit_body).disallowed_features == implicit_body);
    TEST_EXPECT(ctx, document_policy::evaluate(implicit_body, { implicit_body }).accepted());
}

static void test_late_policy_acceptance(TTestContext& ctx)
{
    struct CCase { const char* source; std::uint32_t disallowed; };
    const CCase cases[]{
        { "", 0u }, { "{}", 0u }, { "[]", 0u }, { "42", 0u }, { "\"text\"", 0u },
        { "{\"\":\"\\u0000\\u00e9\"}", 0u }, { "[{\"x\":1}]", 0u },
        { "[+1,0b10,0x10,#10]", 0u },
        { "/*comment*/{}", document_finding_bit(EDocumentFinding::comments) },
        { "{name:1}", document_finding_bit(EDocumentFinding::unquoted_names) },
        { "[text]", document_finding_bit(EDocumentFinding::unquoted_strings) },
        { "['text']", document_finding_bit(EDocumentFinding::single_quotes) },
        { "[1,]", document_finding_bit(EDocumentFinding::trailing_commas) },
        { "[\"raw\nline\"]", document_finding_bit(EDocumentFinding::raw_quoted_line_breaks) },
        { "[\"raw\tcontrol\"]", document_finding_bit(EDocumentFinding::raw_quoted_controls) },
        { "{\"x\":1,\"x\":2}", document_finding_bit(EDocumentFinding::name_collision_extension) },
        { "\"x\":1", document_finding_bit(EDocumentFinding::implicit_body) },
        { "1,2", document_finding_bit(EDocumentFinding::implicit_body) },
        { ";comment only", document_finding_bit(EDocumentFinding::comments) },
        { "/*all*/ 'x':word,'x':+0x10,", EDocumentFinding::comments | EDocumentFinding::single_quotes |
            EDocumentFinding::unquoted_strings | EDocumentFinding::name_collision_extension |
            EDocumentFinding::trailing_commas | EDocumentFinding::implicit_body }
    };
    for (const auto& item : cases)
    {
        for (unsigned entry = 0u; entry < ((item.source[0] == '\0') ? 2u : 3u); ++entry)
        {
            CLiveDocument destination;
            TEST_EXPECT(ctx, parse("{\"keep\":7}", destination).succeeded());
            const CNodeKey keep = destination.first_child(destination.root());
            const std::uint64_t allocation_size = destination.memory_allocation_size();
            const CStringView source{ item.source };
            const CByteConstView bytes{ source.string(), source.length() };
            CDocumentParseReport report = (entry == 0u) ? document_parser::parse(source, destination) :
                ((entry == 1u) ? document_parser::ingest(source, destination) : document_parser::ingest(bytes, destination));
            TEST_CASE_EXPECT_TRUE(ctx, item.source, report.succeeded() == (item.disallowed == 0u));
            TEST_EXPECT(ctx, report.structure.succeeded() && report.parser_examined && report.construction_completed);
            TEST_EXPECT(ctx, report.linter_examined == (entry != 0u));
            TEST_EXPECT(ctx, report.policy.disallowed_features == item.disallowed && report.policy.unknown_policy_bits == 0u);
            TEST_EXPECT(ctx, report.policy.effective_allowed_features == document_policy::k_default);
            TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
            TEST_EXPECT(ctx, !report.structure_start.available && !report.failure_point.available);
            if (item.disallowed != 0u)
            {
                TEST_EXPECT(ctx, report.status == EDocumentParseStatus::policy_rejected);
                TEST_EXPECT(ctx, report.policy.status == EDocumentPolicyStatus::rejected);
                TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
                TEST_EXPECT(ctx, destination.memory_allocation_size() == allocation_size);
                TEST_EXPECT(ctx, write(ctx, destination) == "{\"keep\":7}");
            }
            else
            {
                TEST_EXPECT(ctx, report.status == EDocumentParseStatus::success && report.policy.accepted());
            }
            const std::uint32_t findings = report.findings;
            const CDocumentParseOptions options{ document_policy::k_all_supported };
            report = (entry == 0u) ? document_parser::parse(source, destination, options) :
                ((entry == 1u) ? document_parser::ingest(source, destination, options) : document_parser::ingest(bytes, destination, options));
            TEST_EXPECT(ctx, report.succeeded() && report.policy.accepted() && report.findings == findings);
            TEST_EXPECT(ctx, destination.is_complete() && destination.check_integrity());
            const auto narrower = document_policy::evaluate(report.findings);
            TEST_EXPECT(ctx, narrower.disallowed_features == item.disallowed);
        }
    }
    CLiveDocument destination;
    auto report = document_parser::parse(CStringView{ "{\"x\":1,\"x\":2}" }, destination);
    TEST_EXPECT(ctx, report.status == EDocumentParseStatus::policy_rejected && !destination.is_ready());
    TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 1u);
    TEST_EXPECT(ctx, report.construction_completed && report.policy.disallowed_features ==
        document_finding_bit(EDocumentFinding::name_collision_extension));
    report = document_parser::parse(CStringView{ "[{\"x\":1}]" }, destination, { document_policy::k_ascii });
    TEST_EXPECT(ctx, report.succeeded() && report.interpretations.singleton_objects_unwrapped == 1u);
    TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::singleton_normalization));
    //  Invalid options are diagnosed after processing too, including known
    //  informational bits. No policy outcome invents a terminal failure.
    for (const std::uint32_t bit : { 1u << 31, document_finding_bit(EDocumentFinding::logical_nul) })
    {
        report = document_parser::ingest(CStringView{ "/*comment*/{}" }, destination, { bit });
        TEST_EXPECT(ctx, report.status == EDocumentParseStatus::invalid_options && report.construction_completed);
        TEST_EXPECT(ctx, report.policy.status == EDocumentPolicyStatus::invalid_options && report.policy.unknown_policy_bits == bit);
        TEST_EXPECT(ctx, report.failure.reason == EDocumentFailureReason::none && !report.failure_point.available);
        TEST_EXPECT(ctx, write(ctx, destination) == "[{\"x\":1}]");
    }
    //  The source can alias the destination on either acceptance or rejection.
    TEST_EXPECT(ctx, parse("{\"s\":\"/*comment*/[]\"}", destination).succeeded());
    const CStringView alias = destination.string_value(destination.first_child(destination.root()));
    report = document_parser::parse(alias, destination);
    TEST_EXPECT(ctx, report.status == EDocumentParseStatus::policy_rejected);
    TEST_EXPECT(ctx, destination.string_value(destination.first_child(destination.root())) == alias);
    report = document_parser::parse(alias, destination, { document_policy::k_all_supported });
    TEST_EXPECT(ctx, report.succeeded() && write(ctx, destination) == "[]");
}

static void test_policy_processing_precedence(TTestContext& ctx)
{
    struct CCase { const char* source; EDocumentFailureStage stage; EDocumentFailureReason reason; };
    const CCase cases[]{
        { "\x81", EDocumentFailureStage::linter, EDocumentFailureReason::undefined_cp1252_byte },
        { "\xef\xbb\xbf\xff", EDocumentFailureStage::linter, EDocumentFailureReason::utf8_decode },
        { "/*comment*/{a:[1}", EDocumentFailureStage::structure, EDocumentFailureReason::mismatched_delimiter },
        { "{a:1e9999,bad:}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_value },
        { "/*comment*/[+1e9999]", EDocumentFailureStage::parser, EDocumentFailureReason::numeric_out_of_range },
        { "{\"a\":1,\"a\":2,\"b\":18446744073709551616}", EDocumentFailureStage::parser, EDocumentFailureReason::numeric_out_of_range },
        { "/*comment*/{\"$morphic\":{}}", EDocumentFailureStage::parser, EDocumentFailureReason::invalid_root_value }
    };
    CLiveDocument destination;
    TEST_EXPECT(ctx, parse("{\"keep\":7}", destination).succeeded());
    for (const auto& item : cases)
    {
        for (const std::uint32_t permissions : { document_policy::k_ascii, 1u << 31, document_policy::k_all_supported })
        {
            const auto report = document_parser::ingest(CStringView{ item.source }, destination, { permissions });
            TEST_CASE_EXPECT_TRUE(ctx, item.source, report.failure.stage == item.stage && report.failure.reason == item.reason);
            TEST_EXPECT(ctx, !report.succeeded() && !report.construction_completed);
            TEST_EXPECT(ctx, report.policy.status == EDocumentPolicyStatus::unexamined);
            TEST_EXPECT(ctx, report.policy.disallowed_features == 0u && report.policy.unknown_policy_bits == 0u);
            TEST_EXPECT(ctx, report.failure_point.available);
            TEST_EXPECT(ctx, write(ctx, destination) == "{\"keep\":7}");
        }
    }
}

static void test_policy_source_acceptance(TTestContext& ctx)
{
    struct CCase { const char* source; std::uint32_t encoding; const char* output; };
    const CCase cases[]{
        { "{\"s\":\"\\u00e9\\u0000\"}", 0u, "{\"s\":\"\\u00e9\\u0000\"}" },
        { "{\"s\":\"\xc3\xa9\"}", document_finding_bit(EDocumentFinding::non_ascii_utf8), "{\"s\":\"\\u00e9\"}" },
        { "{\"s\":\"\xc0\x80\"}", document_finding_bit(EDocumentFinding::modified_nul), "{\"s\":\"\\u0000\"}" },
        { "{\"s\":\"\xed\xa0\x80\xed\xb0\x80\"}", document_finding_bit(EDocumentFinding::cesu8_pair), "{\"s\":\"\\ud800\\udc00\"}" },
        { "{\"s\":\"\x80\"}", document_finding_bit(EDocumentFinding::cp1252), "{\"s\":\"\\u20ac\"}" }
    };
    for (const auto& item : cases)
    {
        CLiveDocument destination;
        TEST_EXPECT(ctx, parse("{\"keep\":7}", destination).succeeded());
        auto report = document_parser::ingest(CStringView{ item.source }, destination, { document_policy::k_ascii });
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.succeeded() == (item.encoding == 0u));
        TEST_EXPECT(ctx, report.linter.success && report.construction_completed);
        TEST_EXPECT(ctx, report.policy.disallowed_features == (report.findings & document_findings::k_encoding_features));
        if (item.encoding != 0u)
        {
            TEST_EXPECT(ctx, report.status == EDocumentParseStatus::policy_rejected && report.policy.disallowed_features != 0u);
            TEST_EXPECT(ctx, write(ctx, destination) == "{\"keep\":7}");
        }
        report = document_parser::ingest(CStringView{ item.source }, destination, { item.encoding });
        TEST_EXPECT(ctx, report.succeeded() && report.policy.accepted());
        TEST_CASE_EXPECT_TRUE(ctx, item.output, write(ctx, destination, EDocumentWriteMode::strict_json) == item.output);
        report = document_parser::ingest(CStringView{ item.source }, destination);
        TEST_EXPECT(ctx, report.succeeded() && report.policy.accepted());
        if (item.encoding == document_finding_bit(EDocumentFinding::cp1252))
        {
            report = document_parser::ingest(CStringView{ item.source }, destination, { document_policy::k_utf8 });
            TEST_EXPECT(ctx, report.status == EDocumentParseStatus::policy_rejected);
            TEST_EXPECT(ctx, report.policy.disallowed_features == item.encoding);
            TEST_EXPECT(ctx, report.linter.recovered_as_cp1252 && report.linter.utf8_attempt_errors != 0u);
            TEST_EXPECT(ctx, !report.linter.first_failure.present && report.failure.reason == EDocumentFailureReason::none);
        }
    }
    CLiveDocument destination;
    const char evidence[]{ '\xef', '\xbb', '\xbf', '{', '"', 's', '"', ':', '"', 0, '"', '}', 0, 0 };
    const auto report = document_parser::ingest(CStringView{ evidence, sizeof(evidence) }, destination, { document_policy::k_ascii });
    TEST_EXPECT(ctx, report.succeeded() && report.policy.accepted());
    const std::uint32_t observations = EDocumentFinding::leading_bom | EDocumentFinding::stripped_utf8_bom |
        EDocumentFinding::stripped_terminal_zeros | EDocumentFinding::literal_source_nul | EDocumentFinding::logical_nul;
    TEST_EXPECT(ctx, (report.findings & observations) == observations);
    TEST_EXPECT(ctx, write(ctx, destination) == "{\"s\":\"\\u0000\"}");
    //  Numeric feature permissions are independent and both hexadecimal
    //  permissions are required for the alternate spelling.
    const auto hexadecimal = document_finding_bit(EDocumentFinding::hexadecimal);
    auto numeric = document_parser::ingest(CStringView{ "[0x10,#10,+1,0b1]" }, destination, { hexadecimal });
    TEST_EXPECT(ctx, numeric.status == EDocumentParseStatus::policy_rejected);
    TEST_EXPECT(ctx, numeric.policy.disallowed_features == (EDocumentFinding::alternate_hexadecimal_prefix |
        EDocumentFinding::explicit_plus | EDocumentFinding::binary));
    numeric = document_parser::ingest(CStringView{ "[0x10]" }, destination, { hexadecimal });
    TEST_EXPECT(ctx, numeric.succeeded());
}

static void test_policy_source_provenance(TTestContext& ctx)
{
    const auto escaped = text_linter::lint(CStringView{ "{\"text\":\"\\u00e9\\u0000\"}" });
    TEST_EXPECT(ctx, escaped.report.success);
    TEST_EXPECT(ctx, (escaped.report.source_findings & k_text_source_encoding_features) == 0u);
    TEST_EXPECT(ctx, document_policy::evaluate(document_findings::from_source(escaped.report.source_findings),
        { document_policy::k_ascii }).accepted());

    const std::uint8_t cp1252[]{ 0x80u };
    const auto converted = text_linter::lint(CByteConstView{ cp1252, sizeof(cp1252) });
    TEST_EXPECT(ctx, converted.report.success && converted.report.recovered_as_cp1252);
    const auto converted_findings = document_findings::from_source(converted.report.source_findings);
    const auto rejected = document_policy::evaluate(converted_findings, { document_policy::k_utf8 });
    TEST_EXPECT(ctx, rejected.disallowed_features == document_finding_bit(EDocumentFinding::cp1252));
    TEST_EXPECT(ctx, document_policy::evaluate(converted_findings).accepted());
    TEST_EXPECT(ctx, (converted_findings & document_finding_bit(EDocumentFinding::utf8_attempt_failed)) != 0u);
    TEST_EXPECT(ctx, !converted.report.first_failure.present);

    const std::uint8_t undefined[]{ 0x81u };
    const auto failed = text_linter::lint(CByteConstView{ undefined, sizeof(undefined) });
    TEST_EXPECT(ctx, !failed.report.success && failed.report.first_failure.present);
    TEST_EXPECT(ctx, failed.report.first_failure.reason == ETextLintFailure::undefined_cp1252_byte);
    const auto failed_findings = document_findings::from_source(failed.report.source_findings);
    TEST_EXPECT(ctx, (failed.report.source_findings & text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte)) != 0u);
    TEST_EXPECT(ctx, (failed_findings & text_source_finding_bit(ETextSourceFinding::undefined_cp1252_byte)) == 0u);
    TEST_EXPECT(ctx, (failed_findings & document_finding_bit(EDocumentFinding::utf8_attempt_failed)) != 0u);

    const std::uint8_t nul[]{ 'a', 0u, 'b' };
    const auto literal = text_linter::lint(CByteConstView{ nul, sizeof(nul) });
    TEST_EXPECT(ctx, literal.report.success);
    TEST_EXPECT(ctx, (literal.report.source_findings & document_finding_bit(EDocumentFinding::literal_source_nul)) != 0u);
    const auto literal_findings = document_findings::from_source(literal.report.source_findings);
    TEST_EXPECT(ctx, document_policy::evaluate(literal_findings, { document_policy::k_ascii }).accepted());
    const auto logical = document_policy::evaluate(literal_findings | EDocumentFinding::logical_nul,
        { document_policy::k_ascii });
    TEST_EXPECT(ctx, logical.accepted());
}

static void test_composed_findings(TTestContext& ctx)
{
    CLiveDocument destination;
    TEST_EXPECT(ctx, destination.initialise());
    const CNodeKey keep = destination.create_null(CStringView{ "keep" });
    TEST_EXPECT(ctx, destination.append_child(destination.root(), keep).succeeded());
    const auto failed = parse("a:1,a:2,b:[{n:1}],bad:18446744073709551616", destination);
    TEST_EXPECT(ctx, failed.status == EDocumentParseStatus::numeric_out_of_range);
    TEST_EXPECT(ctx, failed.structure.succeeded() && failed.parser_examined && !failed.construction_completed);
    TEST_EXPECT(ctx, failed.findings == (EDocumentFinding::unquoted_names | EDocumentFinding::implicit_body |
        EDocumentFinding::name_collision_extension | EDocumentFinding::singleton_normalization));
    TEST_EXPECT(ctx, failed.interpretations.duplicate_members_recovered == 0u && failed.interpretations.singleton_objects_unwrapped == 0u);
    TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep && destination.check_integrity());

    const std::uint8_t cp1252[]{ '{', 's', ':', '\'', 0x80u, '\'', ',', 'n', ':', '}' };
    const auto malformed = document_parser::ingest(CByteConstView{ cp1252, sizeof(cp1252) }, destination);
    TEST_EXPECT(ctx, malformed.status == EDocumentParseStatus::structural_failure);
    TEST_EXPECT(ctx, malformed.linter_examined && malformed.linter.success && !malformed.parser_examined && !malformed.construction_completed);
    TEST_EXPECT(ctx, malformed.findings == (EDocumentFinding::cp1252 | EDocumentFinding::utf8_attempt_failed |
        EDocumentFinding::unquoted_names | EDocumentFinding::single_quotes));
    TEST_EXPECT(ctx, malformed.structure.failure.reason == EDocumentFailureReason::missing_value);
    TEST_EXPECT(ctx, malformed.failure.stage == EDocumentFailureStage::structure && malformed.failure.reason == EDocumentFailureReason::missing_value);
    TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep && destination.check_integrity());

    const std::uint8_t undefined[]{ 0x81u };
    const auto undecodable = document_parser::ingest(CByteConstView{ undefined, sizeof(undefined) }, destination);
    TEST_EXPECT(ctx, undecodable.status == EDocumentParseStatus::linter_failure);
    TEST_EXPECT(ctx, undecodable.linter_examined && !undecodable.linter.success && !undecodable.parser_examined && !undecodable.construction_completed);
    TEST_EXPECT(ctx, undecodable.structure.status == EDocumentStructureStatus::unexamined);
    TEST_EXPECT(ctx, undecodable.findings == document_findings::from_source(undecodable.linter.source_findings));
    TEST_EXPECT(ctx, (undecodable.findings & document_finding_bit(EDocumentFinding::reserved_undefined_cp1252_byte)) == 0u);
    TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);

    const auto empty_name = parse("{\"\":1}", destination);
    TEST_EXPECT(ctx, empty_name.succeeded());
    TEST_EXPECT(ctx, empty_name.findings == document_finding_bit(EDocumentFinding::empty_member_name));
    TEST_EXPECT(ctx, empty_name.structure.succeeded() && empty_name.parser_examined && empty_name.construction_completed);

    const auto success = document_parser::ingest(CStringView{ "{\"s\":\"\\u00e9\\u0000\"}" }, destination);
    TEST_EXPECT(ctx, success.succeeded() && success.linter_examined && success.linter.success);
    TEST_EXPECT(ctx, success.structure.succeeded() && success.parser_examined && success.construction_completed);
    TEST_EXPECT(ctx, success.findings == document_finding_bit(EDocumentFinding::logical_nul));
}

static void test_shared_failures(TTestContext& ctx)
{
    CDocumentParseReport report;
    TEST_EXPECT(ctx, report.status == EDocumentParseStatus::unexamined && !report.succeeded());
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
    CLiveDocument destination;
    TEST_EXPECT(ctx, parse("{\"keep\":7}", destination).succeeded());
    const CNodeKey keep = destination.first_child(destination.root());
    struct CCase
    {
        const char* source;
        EDocumentFailureStage stage;
        EDocumentFailureReason reason;
    };
    const CCase cases[]{
        { "{:1}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_name },
        { "{\"a\" 1}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_colon },
        { "{\"a\":}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_value },
        { "[1 2]", EDocumentFailureStage::structure, EDocumentFailureReason::missing_separator },
        { "[1}", EDocumentFailureStage::structure, EDocumentFailureReason::mismatched_delimiter },
        { "[1", EDocumentFailureStage::structure, EDocumentFailureReason::unexpected_end },
        { "[] true", EDocumentFailureStage::structure, EDocumentFailureReason::trailing_content },
        { "/*", EDocumentFailureStage::structure, EDocumentFailureReason::unterminated_comment },
        { "\"unfinished", EDocumentFailureStage::structure, EDocumentFailureReason::unterminated_string },
        { "\"\\q\"", EDocumentFailureStage::structure, EDocumentFailureReason::invalid_escape },
        { "\"\\uD800\"", EDocumentFailureStage::structure, EDocumentFailureReason::invalid_surrogate_pair },
        { "{\"a\\nb\":1}", EDocumentFailureStage::structure, EDocumentFailureReason::newline_in_name },
        { "{n:1e9999,bad:}", EDocumentFailureStage::structure, EDocumentFailureReason::missing_value },
        { "18446744073709551616", EDocumentFailureStage::parser, EDocumentFailureReason::numeric_out_of_range },
        { "n:1e9999,r:{$morphic:{v:2,type:'recovered-array',values:[]}}", EDocumentFailureStage::parser, EDocumentFailureReason::numeric_out_of_range },
        { "$morphic:{}", EDocumentFailureStage::parser, EDocumentFailureReason::invalid_root_value },
        { "r:{$morphic:{v:2,type:'recovered-array',values:[]}}", EDocumentFailureStage::parser, EDocumentFailureReason::construction_failed }
    };
    for (const auto& item : cases)
    {
        report = parse(item.source, destination);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, !report.succeeded());
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.failure.stage, item.stage);
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.failure.reason, item.reason);
        TEST_EXPECT(ctx, report.structure_start.available && report.failure_point.available);
        TEST_EXPECT(ctx, !report.construction_completed && destination.first_child(destination.root()) == keep);
        if (item.stage == EDocumentFailureStage::structure)
        {
            TEST_EXPECT(ctx, !report.parser_examined && report.structure.status == EDocumentStructureStatus::failed);
            TEST_EXPECT(ctx, report.failure.stage == report.structure.failure.stage && report.failure.reason == report.structure.failure.reason);
        }
        else
        {
            TEST_EXPECT(ctx, report.parser_examined && report.structure.succeeded());
            TEST_EXPECT(ctx, report.structure.failure.stage == EDocumentFailureStage::none && report.structure.failure.reason == EDocumentFailureReason::none);
        }
    }
    report = document_parser::parse(CStringView{}, destination);
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::structure && report.failure.reason == EDocumentFailureReason::invalid_input_view);
    TEST_EXPECT(ctx, !report.parser_examined && !report.structure_start.available && !report.failure_point.available);
    const CCase decoding[]{
        { "\x81", EDocumentFailureStage::linter, EDocumentFailureReason::undefined_cp1252_byte },
        { "\xef\xbb\xbf\xff", EDocumentFailureStage::linter, EDocumentFailureReason::utf8_decode }
    };
    for (const auto& item : decoding)
    {
        report = document_parser::ingest(CStringView{ item.source }, destination);
        TEST_EXPECT(ctx, report.status == EDocumentParseStatus::linter_failure && report.failure.stage == item.stage);
        TEST_EXPECT(ctx, report.failure.reason == item.reason && report.linter.first_failure.present);
        TEST_EXPECT(ctx, !report.parser_examined && !report.construction_completed && !report.structure_start.available);
        TEST_EXPECT(ctx, report.structure.status == EDocumentStructureStatus::unexamined);
        TEST_EXPECT(ctx, report.structure.failure.stage == EDocumentFailureStage::none && report.structure.failure.reason == EDocumentFailureReason::none);
        TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
    }
    report = document_parser::ingest(CStringView{}, destination);
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::linter && report.failure.reason == EDocumentFailureReason::invalid_input_view);
    TEST_EXPECT(ctx, report.structure.status == EDocumentStructureStatus::unexamined);
    report = document_parser::ingest(CStringView{ "{\"s\":\"\x80\"}" }, destination);
    TEST_EXPECT(ctx, report.succeeded() && report.linter.recovered_as_cp1252 && report.linter.utf8_attempt_errors != 0u);
    TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
    TEST_EXPECT(ctx, !report.linter.first_failure.present && report.structure.failure.reason == EDocumentFailureReason::none);
    report = parse("[]", destination);
    TEST_EXPECT(ctx, report.succeeded() && report.failure.stage == EDocumentFailureStage::none && report.failure.reason == EDocumentFailureReason::none);
}

static void test_unquoted_strings(TTestContext& ctx)
{
    struct CCase
    {
        const char* source;
        const char* value;
    };
    const CCase cases[]{
        { "hello", "hello" }, { "123abc", "123abc" }, { "1e+", "1e+" },
        { "01", "01" }, { "-01", "-01" }, { ".5", ".5" }, { "1.", "1." },
        { "+", "+" }, { "--1", "--1" }, { "+0x", "+0x" }, { "#", "#" },
        { "0b2", "0b2" }, { "0x1g", "0x1g" }, { "1e99999x", "1e99999x" },
        { "NaN", "NaN" }, { "Infinity", "Infinity" }, { "True", "True" },
        { "tr\\u0075e", "true" }, { "\\u0031", "1" }, { "\\u002B0x10", "+0x10" },
        { "a\\u002Cb", "a,b" }, { "a\\u0020b", "a b" }, { "a\\u003Ab", "a:b" },
        { "\\u007B\\u005B\\u005D\\u007D", "{[]}" }, { "\\\"quoted\\\"", "\"quoted\"" },
        { "don't", "don't" }, { "path//part", "path//part" }, { "path/*part*/", "path/*part*/" },
        { "/", "/" }, { "\\/\\/text", "//text" }, { "\xc3\xa9", "\xc3\xa9" },
        { "a\\nb", "a\nb" }, { "alpha;beta", "alpha;beta" }
    };
    CLiveDocument document;
    for (const auto& item : cases)
    {
        const auto report = parse(std::string("{\"s\":") + item.source + "}", document);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.succeeded());
        TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::unquoted_strings));
        const CNodeKey value = member(document, document.root(), CStringView{ "s" });
        TEST_EXPECT(ctx, document.value_type(value) == ELiveValueType::string);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, document.string_value(value) == CStringView{ item.value });
    }
    const CCase names[]{ { "0x10", "0x10" }, { "+#F", "+#F" }, { "0b10", "0b10" },
        { "1e99999", "1e99999" }, { "true", "true" }, { "tr\\u0075e", "true" },
        { "\\u0031", "1" }, { "a\\u003Ab", "a:b" }, { "\xc3\xa9", "\xc3\xa9" },
        { "name/*text*/", "name/*text*/" }, { "alpha;beta", "alpha;beta" } };
    for (const auto& item : names)
    {
        const auto report = parse(std::string("{") + item.source + ":null}", document);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.succeeded());
        TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::unquoted_names));
        const CNodeKey value = member(document, document.root(), CStringView{ item.value });
        TEST_EXPECT(ctx, value.is_valid() && document.value_type(value) == ELiveValueType::null_value);
    }
    const auto typed = parse(R"({"n":+#F,"b":true,"z":null,"f":1e2})", document);
    TEST_EXPECT(ctx, typed.succeeded());
    TEST_EXPECT(ctx, typed.findings == (EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal |
        EDocumentFinding::alternate_hexadecimal_prefix));
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "n" })) == ELiveValueType::integer);
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "b" })) == ELiveValueType::boolean);
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "f" })) == ELiveValueType::floating_point);

    const auto comments = parse(R"({"s":abc/*literal*/,"n":1 /*comment*/})", document);
    TEST_EXPECT(ctx, comments.succeeded());
    TEST_EXPECT(ctx, comments.findings == (EDocumentFinding::unquoted_strings | EDocumentFinding::comments));
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "s" })) == CStringView{ "abc/*literal*/" });
    const char* const semicolon_comments[]{ ";head\n{\"s\":\"value\"}", "{\"s\":;note\n\"value\"}",
        "{\"s\":;\"'{}[]:,/* \\q\n\"value\"};tail" };
    for (const char* source : semicolon_comments)
    {
        const auto report = parse(source, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, report.succeeded());
        TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::comments));
        TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "s" })) == CStringView{ "value" });
    }
    const char* const comment_only[]{ ";", ";note", ";\"'{}[]:,/* \\q\n;tail" };
    for (const char* source : comment_only)
    {
        const auto report = parse(source, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, report.succeeded());
        TEST_EXPECT(ctx, report.findings == document_finding_bit(EDocumentFinding::comments));
        TEST_EXPECT(ctx, !document.first_child(document.root()).is_valid());
    }
    const auto quotes = parse(R"({"s":'say "hello", don\'t'})", document);
    TEST_EXPECT(ctx, quotes.succeeded() && quotes.findings == document_finding_bit(EDocumentFinding::single_quotes));
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "s" })) == CStringView{ "say \"hello\", don't" });

    const CNodeKey keep = document.first_child(document.root());
    const char* const malformed[]{ R"({"s":abc"tail"})", R"({"s":abc[]})", R"({"s":abc{}})",
        R"({"s":abc def})", R"({"s":abc\q})", R"({"s":abc\,def})", R"({"s":abc\ def})",
        R"({"s":abc\x41})", R"({"s":abc\'def})", R"({"s":'abc\"def'})",
        R"({"s":abc\uD800})", R"({"s":abc\uDC00})", R"({"s":abc\u12})",
        "{\"s\":;note\n}", "{\"s\":;note" };
    for (const char* source : malformed)
    {
        const auto report = parse(source, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, report.status == EDocumentParseStatus::structural_failure);
        TEST_EXPECT(ctx, !report.parser_examined && !report.construction_completed);
        TEST_EXPECT(ctx, document.first_child(document.root()) == keep && document.check_integrity());
    }
    const std::string nul = std::string("{s:a") + '\0' + "b}";
    const auto embedded = parse(nul, document);
    TEST_EXPECT(ctx, embedded.succeeded());
    TEST_EXPECT(ctx, embedded.findings == (EDocumentFinding::unquoted_names |
        EDocumentFinding::unquoted_strings | EDocumentFinding::logical_nul));
    const std::uint8_t canonical[]{ 'a', 0xc0u, 0x80u, 'b' };
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "s" })) ==
        (CStringView{ canonical, sizeof(canonical) }));
}

static void test_construction_and_features(TTestContext& ctx)
{
    CLiveDocument document;
    const auto report = parse("/*start*/ n:+0X7f, a:[null,true,false,1.5,'text',{},[],{x:2},], o:{b:-#80},", document);
    TEST_EXPECT(ctx, report.succeeded() && report.structure.succeeded());
    TEST_EXPECT(ctx, report.structure.findings == (EDocumentFinding::comments | EDocumentFinding::single_quotes |
        EDocumentFinding::unquoted_names | EDocumentFinding::trailing_commas | EDocumentFinding::implicit_body |
        EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix));
    TEST_EXPECT(ctx, document.is_ready() && document.is_complete() && document.check_integrity());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":+0x7f,\"a\":[null,true,false,1.5,\"text\",{},[],{\"x\":2}],\"o\":{\"b\":-#80}}");
    const CNodeKey array = member(document, document.root(), CStringView{ "a" });
    TEST_EXPECT(ctx, document.child_count(array) == 8u);
    const CNodeKey singleton = document.last_child(array);
    TEST_EXPECT(ctx, document.value_type(singleton) == ELiveValueType::integer && document.is_object_entry(singleton));
    TEST_EXPECT(ctx, document.name(singleton) == CStringView{ "x" });
    TEST_EXPECT(ctx, report.interpretations.singleton_objects_unwrapped == 1u);
    //  Strict syntax needs neither relaxation nor numeric-extension flags.
    const auto strict = parse("{\"true\":false,\"z\":null,\"a\":[1,2]}", document);
    TEST_EXPECT(ctx, strict.succeeded());
    TEST_EXPECT(ctx, strict.structure.findings == 0u);
    TEST_EXPECT(ctx, write(ctx, document) == "{\"true\":false,\"z\":null,\"a\":[1,2]}");
}

static void test_strings_and_ingestion(TTestContext& ctx)
{
    //  Escaped names and values require independent scratch lifetimes.
    const std::string text = "{\"n\\u0000\\u00e9\":\"\\u0000\\uD834\\uDD1E\\n\\t\\\\\\\"\\/\",e:'',s:'can\\'t',a:'raw\nline',b:'\\b\\f\\r'}";
    CLiveDocument document;
    const auto report = parse(text, document);
    TEST_EXPECT(ctx, report.succeeded());
    const std::uint8_t name[]{ 'n', 0xc0u, 0x80u, 0xc3u, 0xa9u };
    const CNodeKey value = member(document, document.root(), CStringView{ name, sizeof(name) });
    TEST_EXPECT(ctx, value.is_valid());
    const std::uint8_t expected[]{ 0xc0u, 0x80u, 0xf0u, 0x9du, 0x84u, 0x9eu, '\n', '\t', '\\', '"', '/' };
    TEST_EXPECT(ctx, document.string_value(value) == (CStringView{ expected, sizeof(expected) }));
    TEST_EXPECT(ctx, document.string_value(member(document, document.root(), CStringView{ "e" })).length() == 0u);
    TEST_EXPECT(ctx, document.check_integrity());

    const std::uint8_t cp1252[]{ '{', 'n', ':', '"', 0xe9u, 0u, '\r', '\n', '"', '}' };
    const auto linted = text_linter::lint(CByteConstView{ cp1252, sizeof(cp1252) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, linted.report.success && linted.report.recovered_as_cp1252);
    TEST_EXPECT(ctx, linted.report.embedded_nul_count == 1u && linted.report.normalised_line_endings == text_line_ending_bit(ETextLineEnding::crlf));
    const auto ingested = document_parser::parse(CStringView{ linted.output.data(), linted.report.logical_text_byte_size }, document, { document_policy::k_all_supported });
    TEST_EXPECT(ctx, ingested.succeeded());
    TEST_EXPECT(ctx, ingested.structure.findings == (EDocumentFinding::unquoted_names |
        EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::logical_nul));
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":\"\\u00e9\\u0000\n\"}");

    const std::uint8_t modified[]{ '{', 'n', ':', '"', 0xc0u, 0x80u, '"', '}' };
    const auto normalized = text_linter::lint(CByteConstView{ modified, sizeof(modified) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, normalized.report.success && normalized.report.modified_utf8_nul_count == 1u);
    TEST_EXPECT(ctx, document_parser::parse(CStringView{ normalized.output.data(), normalized.report.logical_text_byte_size }, document, { document_policy::k_all_supported }).succeeded());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"n\":\"\\u0000\"}");

    const std::uint8_t offset_input[]{ 0xefu, 0xbbu, 0xbfu, '{', 'a', ':', '"', 0xc3u, 0xa9u, '"', ',', 'n', ':', '1', ' ', '2', '}', 0u };
    const auto shifted = text_linter::lint(CByteConstView{ offset_input, sizeof(offset_input) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, shifted.report.success && shifted.report.leading_utf8_bom_stripped);
    const auto failure = document_parser::parse(CStringView{ shifted.output.data(), shifted.report.logical_text_byte_size }, document);
    TEST_EXPECT(ctx, failure.status == EDocumentParseStatus::structural_failure);
    TEST_EXPECT(ctx, failure.failure_point.code_point_column_1_based == 12u && failure.structure.failure_point.code_point_column_1_based == 12u);

    const std::uint8_t cp1252_error[]{ '{', 'a', ':', '"', 0xe9u, '"', ',', 'n', ':', '1', ' ', '2', '}' };
    const auto expanded = text_linter::lint(CByteConstView{ cp1252_error, sizeof(cp1252_error) }, k_document_text_lint_line_endings);
    TEST_EXPECT(ctx, expanded.report.success && expanded.report.recovered_as_cp1252);
    const auto expanded_failure = document_parser::parse(CStringView{ expanded.output.data(), expanded.report.logical_text_byte_size }, document);
    TEST_EXPECT(ctx, expanded_failure.status == EDocumentParseStatus::structural_failure);
    TEST_EXPECT(ctx, expanded_failure.failure_point.code_point_column_1_based == 12u && expanded_failure.structure.failure_point.code_point_column_1_based == 12u);
}

static void test_empty_names(TTestContext& ctx)
{
    struct CCase
    {
        const char* source;
        ELiveValueType type;
    };
    const CCase cases[]{ { "null", ELiveValueType::null_value }, { "true", ELiveValueType::boolean },
        { "1", ELiveValueType::integer }, { "1.5", ELiveValueType::floating_point }, { "\"\"", ELiveValueType::string },
        { "{}", ELiveValueType::object }, { "[]", ELiveValueType::array } };
    CLiveDocument document;
    for (const auto& item : cases)
    {
        for (const char quote : { '"', '\'' })
        {
            const std::string source = std::string("{") + quote + quote + ":" + item.source + "}";
            const auto report = parse(source, document);
            TEST_CASE_EXPECT_TRUE(ctx, source.c_str(), report.succeeded());
            const std::uint32_t findings = document_finding_bit(EDocumentFinding::empty_member_name) |
                ((quote == '\'') ? document_finding_bit(EDocumentFinding::single_quotes) : 0u);
            TEST_EXPECT(ctx, report.findings == findings);
            const CNodeKey node = document.object_child(document.root(), CStringView{ "" });
            TEST_EXPECT(ctx, node.is_valid() && document.is_object_entry(node));
            TEST_EXPECT(ctx, document.value_type(node) == item.type);
            TEST_EXPECT(ctx, !document.name(node).empty() && document.name(node).length() == 0u);
            TEST_EXPECT(ctx, !document.object_child(document.root(), CStringView{}).is_valid());
            TEST_EXPECT(ctx, write(ctx, document, EDocumentWriteMode::strict_json) == std::string("{\"\":") + item.source + "}");
            CBakedDocumentBlock block;
            TEST_EXPECT(ctx, document_translation::bake(document, block));
            CLiveDocument promoted;
            TEST_EXPECT(ctx, document_translation::promote(block.document(), promoted));
            const CNodeKey copy = promoted.object_child(promoted.root(), CStringView{ "" });
            TEST_EXPECT(ctx, copy.is_valid() && promoted.is_object_entry(copy) && promoted.value_type(copy) == item.type);
            TEST_EXPECT(ctx, promoted.name(copy).length() == 0u && !promoted.name(copy).empty());
        }
    }
    const auto nested = parse("{\"a\":[{\"\":\"line\nbreak\"}]}", document);
    TEST_EXPECT(ctx, nested.succeeded());
    TEST_EXPECT(ctx, nested.findings == (EDocumentFinding::empty_member_name |
        EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::singleton_normalization));
    const CNodeKey child = document.first_child(document.object_child(document.root(), CStringView{ "a" }));
    TEST_EXPECT(ctx, document.is_object_entry(child) && !document.name(child).empty() && document.name(child).length() == 0u);
    TEST_EXPECT(ctx, document.string_value(child) == CStringView{ "line\nbreak" } && document.suppresses_newline_escaping(child));
    TEST_EXPECT(ctx, write(ctx, document) == "{\"a\":[{\"\":\"line\nbreak\"}]}");
}

static void test_newline_metadata(TTestContext& ctx)
{
    CLiveDocument document;
    const auto report = parse("{\"literal\":\"a\nb\",\"escaped\":\"a\\nb\",\"mixed\":'a\nb\\nc',\"plain\":\"\\\\n\",\"unquoted\":a\\nb}", document);
    TEST_EXPECT(ctx, report.succeeded());
    TEST_EXPECT(ctx, report.findings == (EDocumentFinding::raw_quoted_line_breaks |
        EDocumentFinding::single_quotes | EDocumentFinding::unquoted_strings));
    const CNodeKey literal = member(document, document.root(), CStringView{ "literal" });
    const CNodeKey escaped = member(document, document.root(), CStringView{ "escaped" });
    const CNodeKey unquoted = member(document, document.root(), CStringView{ "unquoted" });
    TEST_EXPECT(ctx, document.string_value_id(literal) == document.string_value_id(escaped));
    TEST_EXPECT(ctx, document.string_value_id(literal) == document.string_value_id(unquoted));
    TEST_EXPECT(ctx, document.suppresses_newline_escaping(literal));
    TEST_EXPECT(ctx, !document.suppresses_newline_escaping(escaped) && !document.suppresses_newline_escaping(unquoted));
    TEST_EXPECT(ctx, document.suppresses_newline_escaping(member(document, document.root(), CStringView{ "mixed" })));
    TEST_EXPECT(ctx, !document.suppresses_newline_escaping(member(document, document.root(), CStringView{ "plain" })));
    CBakedDocumentBlock block;
    TEST_EXPECT(ctx, document_translation::bake(document, block));
    CLiveDocument promoted;
    TEST_EXPECT(ctx, document_translation::promote(block.document(), promoted));
    TEST_EXPECT(ctx, promoted.suppresses_newline_escaping(member(promoted, promoted.root(), CStringView{ "literal" })));
    TEST_EXPECT(ctx, !promoted.suppresses_newline_escaping(member(promoted, promoted.root(), CStringView{ "escaped" })));
    const std::string strict = write(ctx, document, EDocumentWriteMode::strict_json);
    TEST_EXPECT(ctx, strict == "{\"literal\":\"a\\nb\",\"escaped\":\"a\\nb\",\"mixed\":\"a\\nb\\nc\",\"plain\":\"\\\\n\",\"unquoted\":\"a\\nb\"}");
    CLiveDocument reparsed;
    TEST_EXPECT(ctx, parse(strict, reparsed).succeeded());
    TEST_EXPECT(ctx, !reparsed.suppresses_newline_escaping(member(reparsed, reparsed.root(), CStringView{ "literal" })));
    TEST_EXPECT(ctx, document.suppresses_newline_escaping(literal));
    const std::string morphic = write(ctx, promoted);
    TEST_EXPECT(ctx, morphic == "{\"literal\":\"a\nb\",\"escaped\":\"a\\nb\",\"mixed\":\"a\nb\nc\",\"plain\":\"\\\\n\",\"unquoted\":\"a\\nb\"}");
    TEST_EXPECT(ctx, parse(morphic, reparsed).succeeded());
    TEST_EXPECT(ctx, reparsed.suppresses_newline_escaping(member(reparsed, reparsed.root(), CStringView{ "literal" })));
    TEST_EXPECT(ctx, !reparsed.suppresses_newline_escaping(member(reparsed, reparsed.root(), CStringView{ "escaped" })));

    const char* const breaks[]{ "\n", "\r", "\r\n", "\n\r", "\v", "\f", "\xc2\x85", "\xe2\x80\xa8", "\xe2\x80\xa9" };
    for (const char* line_break : breaks)
    {
        const std::string source = std::string("{\"s\":\"a") + line_break + "b\"}";
        const auto ingested = document_parser::ingest(CStringView{ source.data(), source.size() }, document, { document_policy::k_all_supported });
        TEST_EXPECT(ctx, ingested.succeeded());
        const CNodeKey value = document.first_child(document.root());
        TEST_EXPECT(ctx, document.string_value(value) == CStringView{ "a\nb" } && document.suppresses_newline_escaping(value));
        TEST_EXPECT(ctx, (ingested.findings & document_finding_bit(EDocumentFinding::raw_quoted_line_breaks)) != 0u);

        const std::string invalid = std::string("{\"a") + line_break + "b\":1}";
        const auto failed = document_parser::ingest(CStringView{ invalid.data(), invalid.size() }, document);
        TEST_EXPECT(ctx, failed.status == EDocumentParseStatus::structural_failure);
        TEST_EXPECT(ctx, failed.structure.failure.reason == EDocumentFailureReason::newline_in_name);
        TEST_EXPECT(ctx, !failed.parser_examined && !failed.construction_completed);
        TEST_EXPECT(ctx, document.first_child(document.root()) == value && document.suppresses_newline_escaping(value));
    }
}

static void test_root_inference(TTestContext& ctx)
{
    struct CCase
    {
        const char* source;
        const char* output;
        std::uint32_t findings;
    };
    const CCase cases[]{
        { "", "{}", 0u }, { " \n\t", "{}", 0u },
        { ";empty", "{}", document_finding_bit(EDocumentFinding::comments) },
        { "{}", "{}", 0u }, { "[]", "[]", 0u },
        { "[1,true,null]", "[1,true,null]", 0u },
        { "{\"a\":1}", "{\"a\":1}", 0u },
        { "\"a\":1", "{\"a\":1}", document_finding_bit(EDocumentFinding::implicit_body) },
        { "123:1", "{\"123\":1}", EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "0x10:1", "{\"0x10\":1}", EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "\"\":1", "{\"\":1}", EDocumentFinding::implicit_body | EDocumentFinding::empty_member_name },
        { "42", "[42]", 0u }, { "1.5", "[1.5]", 0u }, { "true", "[true]", 0u },
        { "false", "[false]", 0u }, { "null", "[null]", 0u }, { "\"hello\"", "[\"hello\"]", 0u },
        { "1,true,\"hello\"", "[1,true,\"hello\"]", document_finding_bit(EDocumentFinding::implicit_body) },
        { "1,", "[1]", document_finding_bit(EDocumentFinding::trailing_commas) },
        { "+#F", "[+#f]", EDocumentFinding::explicit_plus | EDocumentFinding::hexadecimal | EDocumentFinding::alternate_hexadecimal_prefix },
        { "a\\u003Ab", "[\"a:b\"]", document_finding_bit(EDocumentFinding::unquoted_strings) },
        { "a\\u003Ab:1", "{\"a:b\":1}", EDocumentFinding::implicit_body | EDocumentFinding::unquoted_names },
        { "\"a\" /* : ignored */ :1", "{\"a\":1}", EDocumentFinding::implicit_body | EDocumentFinding::comments },
        { "\"a\" /* : ignored */", "[\"a\"]", document_finding_bit(EDocumentFinding::comments) },
        { "\"a\";ignored\n:1", "{\"a\":1}", EDocumentFinding::implicit_body | EDocumentFinding::comments },
        { "\"line\nbreak\"", "[\"line\nbreak\"]", document_finding_bit(EDocumentFinding::raw_quoted_line_breaks) }
    };
    CLiveDocument document;
    for (const auto& item : cases)
    {
        const auto report = parse(item.source, document);
        TEST_CASE_EXPECT_TRUE(ctx, item.source, report.succeeded() && report.construction_completed);
        TEST_CASE_EXPECT_EQ(ctx, item.source, report.findings, item.findings);
        const ELiveValueType root_type = (*item.output == '[') ? ELiveValueType::array : ELiveValueType::object;
        TEST_EXPECT(ctx, document.value_type(document.root()) == root_type);
        TEST_EXPECT(ctx, !document.is_object_entry(document.root()) && document.name(document.root()).empty());
        TEST_EXPECT(ctx, !document.parent(document.root()).is_valid() && document.check_integrity());
        TEST_CASE_EXPECT_TRUE(ctx, item.source, write(ctx, document) == item.output);
        CBakedDocumentBlock block;
        TEST_EXPECT(ctx, document_translation::bake(document, block));
        CLiveDocument promoted;
        TEST_EXPECT(ctx, document_translation::promote(block.document(), promoted));
        TEST_EXPECT(ctx, promoted.value_type(promoted.root()) == root_type);
        TEST_EXPECT(ctx, write(ctx, promoted) == item.output);
        CLiveDocument reparsed;
        TEST_EXPECT(ctx, parse(item.output, reparsed).succeeded());
        TEST_EXPECT(ctx, reparsed.value_type(reparsed.root()) == root_type);
        TEST_EXPECT(ctx, write(ctx, reparsed) == item.output);
        if (item.findings == 0u)
        {
            TEST_EXPECT(ctx, document_policy::evaluate(report.findings).accepted());
        }
    }
    const auto normalized = parse("0,{\"\":\"line\nbreak\"},{},[]", document);
    TEST_EXPECT(ctx, normalized.succeeded());
    TEST_EXPECT(ctx, normalized.findings == (EDocumentFinding::implicit_body | EDocumentFinding::empty_member_name |
        EDocumentFinding::raw_quoted_line_breaks | EDocumentFinding::singleton_normalization));
    const CNodeKey named = document.next_sibling(document.first_child(document.root()));
    TEST_EXPECT(ctx, document.is_object_entry(named) && !document.name(named).empty() && document.name(named).length() == 0u);
    TEST_EXPECT(ctx, document.suppresses_newline_escaping(named));
    TEST_EXPECT(ctx, write(ctx, document) == "[0,{\"\":\"line\nbreak\"},{},[]]");
    const auto explicit_array = parse("[{\"x\":1},{},{\"x\":1,\"y\":2}]", document);
    TEST_EXPECT(ctx, explicit_array.succeeded() && explicit_array.findings == document_finding_bit(EDocumentFinding::singleton_normalization));
    TEST_EXPECT(ctx, document.is_object_entry(document.first_child(document.root())));
    TEST_EXPECT(ctx, write(ctx, document) == "[{\"x\":1},{},{\"x\":1,\"y\":2}]");

    const CNodeKey keep = document.first_child(document.root());
    const std::uint64_t allocation_size = document.memory_allocation_size();
    const char* const malformed[]{ "1 2", "[1}", "[1", "[] true", "{} ,1", "1,a:2", "\"a\":1,2", "[a:1]", "1,,2", "," };
    for (const char* source : malformed)
    {
        const auto failed = parse(source, document);
        TEST_CASE_EXPECT_TRUE(ctx, source, failed.status == EDocumentParseStatus::structural_failure);
        TEST_EXPECT(ctx, !failed.parser_examined && !failed.construction_completed);
        TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::array && document.first_child(document.root()) == keep);
        TEST_EXPECT(ctx, document.memory_allocation_size() == allocation_size && document.check_integrity());
    }
    const auto range = parse("18446744073709551616", document);
    TEST_EXPECT(ctx, range.status == EDocumentParseStatus::numeric_out_of_range);
    TEST_EXPECT(ctx, range.structure.succeeded() && range.parser_examined && !range.construction_completed);
    TEST_EXPECT(ctx, range.findings == 0u && range.failure_point.code_point_column_1_based == 1u);
    TEST_EXPECT(ctx, document.first_child(document.root()) == keep && document.value_type(document.root()) == ELiveValueType::array);
    const auto ingested = document_parser::ingest(CStringView{ "\xef\xbb\xbf;head\r\n[1,true]" }, document, { document_policy::k_all_supported });
    TEST_EXPECT(ctx, ingested.succeeded() && ingested.linter_examined && ingested.linter.success);
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::array && write(ctx, document) == "[1,true]");
    const auto nul = parse(std::string(1u, '\0'), document);
    TEST_EXPECT(ctx, nul.succeeded() && nul.findings == (EDocumentFinding::logical_nul | EDocumentFinding::unquoted_strings));
    const std::uint8_t canonical_nul[]{ 0xc0u, 0x80u };
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::array);
    TEST_EXPECT(ctx, document.string_value(document.first_child(document.root())) == (CStringView{ canonical_nul, sizeof(canonical_nul) }));
}

static void test_integer_metadata(TTestContext& ctx)
{
    struct CCase
    {
        const char* spelling;
        bool signed_value;
        std::uint64_t magnitude;
        bool negative;
        EIntegerNotation notation;
        EIntegerPrefix prefix;
    };
    const CCase cases[]{
        { "0", false, 0u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+0", true, 0u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-0", true, 0u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+127", true, 127u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+128", true, 128u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-128", true, 128u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-129", true, 129u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "255", false, 255u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "256", false, 256u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+32767", true, 32767u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-32769", true, 32769u, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "65536", false, 65536u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+2147483648", true, 2147483648u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "4294967296", false, 4294967296u, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "+9223372036854775807", true, 9223372036854775807ull, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "-9223372036854775808", true, 9223372036854775808ull, true, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "18446744073709551615", false, 18446744073709551615ull, false, EIntegerNotation::decimal, EIntegerPrefix::standard },
        { "0XFF", false, 255u, false, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "-#80", true, 128u, true, EIntegerNotation::hexadecimal, EIntegerPrefix::alternate },
        { "+#7FFF", true, 32767u, false, EIntegerNotation::hexadecimal, EIntegerPrefix::alternate },
        { "-0x8000000000000000", true, 9223372036854775808ull, true, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "0xffffffffffffffff", false, 18446744073709551615ull, false, EIntegerNotation::hexadecimal, EIntegerPrefix::standard },
        { "+0B100000000", true, 256u, false, EIntegerNotation::binary, EIntegerPrefix::standard } };
    for (const auto& item : cases)
    {
        CLiveDocument document;
        TEST_CASE_EXPECT_TRUE(ctx, item.spelling, parse(std::string("n:") + item.spelling, document).succeeded());
        const CNodeKey node = document.first_child(document.root());
        CIntegerMetadata metadata;
        TEST_EXPECT(ctx, document.integer_metadata(node, metadata));
        TEST_EXPECT(ctx, metadata.domain == (item.signed_value ? EIntegerDomain::signed_value : EIntegerDomain::unsigned_value));
        TEST_EXPECT(ctx, metadata.notation == item.notation && metadata.prefix == item.prefix);
        if (item.signed_value)
        {
            const std::int64_t expected = item.negative ? ((item.magnitude == 9223372036854775808ull) ? std::numeric_limits<std::int64_t>::min() :
                -static_cast<std::int64_t>(item.magnitude)) : static_cast<std::int64_t>(item.magnitude);
            std::int64_t value = 0;
            TEST_EXPECT(ctx, document.signed_integer_value(node, value) && value == expected);
            TEST_EXPECT(ctx, metadata.width == live_signed_integer_smallest_width(expected));
        }
        else
        {
            std::uint64_t value = 0u;
            TEST_EXPECT(ctx, document.unsigned_integer_value(node, value) && value == item.magnitude);
            TEST_EXPECT(ctx, metadata.width == live_unsigned_integer_smallest_width(item.magnitude));
        }
    }
}

static void expect_float(TTestContext& ctx, const char* spelling, const double expected)
{
    CLiveDocument document;
    TEST_CASE_EXPECT_TRUE(ctx, spelling, parse(std::string("f:") + spelling, document).succeeded());
    double value = 0.0;
    TEST_EXPECT(ctx, document.floating_point_value(document.first_child(document.root()), value));
    TEST_EXPECT(ctx, live_floating_point_bits(value) == live_floating_point_bits(expected));
}

static void test_floats(TTestContext& ctx)
{
    expect_float(ctx, "-0.0", -0.0);
    expect_float(ctx, "+0.0", 0.0);
    expect_float(ctx, "0e999999999999999999999999999999", 0.0);
    expect_float(ctx, "0.1", 0.1);
    expect_float(ctx, "+1.2345678901234567", 1.2345678901234567);
    expect_float(ctx, "5e-324", std::numeric_limits<double>::denorm_min());
    expect_float(ctx, "2.2250738585072014e-308", std::numeric_limits<double>::min());
    expect_float(ctx, "1.7976931348623157e308", std::numeric_limits<double>::max());
    std::uint64_t bits = 0xa0761d6478bd642full;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        bits ^= bits << 13u;
        bits ^= bits >> 7u;
        bits ^= bits << 17u;
        const double value = live_floating_point_from_bits(bits);
        if (!live_floating_point_is_finite(value))
        {
            continue;
        }
        char digits[64];
        const auto converted = std::to_chars(digits, digits + sizeof(digits), value);
        TEST_EXPECT(ctx, converted.ec == std::errc{});
        if (converted.ec != std::errc{})
        {
            continue;
        }
        std::string spelling(digits, converted.ptr);
        if (spelling.find_first_of(".e") == std::string::npos)
        {
            spelling += ".0";
        }
        expect_float(ctx, spelling.c_str(), value);
    }
}

static void test_failure_publication(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, parse("keep:7", document).succeeded());
    const CNodeKey root = document.root();
    const CNodeKey keep = document.first_child(root);
    const std::uint64_t allocation_size = document.memory_allocation_size();
    struct CCase
    {
        const char* text;
        EDocumentParseStatus status;
        std::size_t offset;
        bool structural_success;
    };
    const CCase cases[]{
        { "{n:1 2}", EDocumentParseStatus::structural_failure, 5u, false },
        { "n:18446744073709551616", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "n:+9223372036854775808", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "n:-9223372036854775809", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "n:1e99999", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "n:1e-99999", EDocumentParseStatus::numeric_out_of_range, 2u, true },
        { "good:1,n:0x10000000000000000", EDocumentParseStatus::numeric_out_of_range, 9u, true },
        { "good:1,'':", EDocumentParseStatus::structural_failure, 10u, false },
        { "{\"a\\u2028b\":1}", EDocumentParseStatus::structural_failure, 3u, false },
        { "a\\nb:1", EDocumentParseStatus::structural_failure, 1u, false },
        { "$morphic:{v:1,type:'recovered-array',values:[]}", EDocumentParseStatus::invalid_root_value, 0u, true },
        { "'\\u0024morphic':null", EDocumentParseStatus::invalid_root_value, 0u, true } };
    for (const auto& item : cases)
    {
        const auto report = parse(item.text, document);
        TEST_CASE_EXPECT_EQ(ctx, item.text, report.status, item.status);
        TEST_EXPECT(ctx, report.failure_point.available && report.failure_point.code_point_column_1_based == item.offset + 1u);
        TEST_EXPECT(ctx, report.structure.succeeded() == item.structural_success);
        TEST_EXPECT(ctx, document.root() == root && document.first_child(root) == keep);
        TEST_EXPECT(ctx, document.memory_allocation_size() == allocation_size);
        TEST_EXPECT(ctx, document.check_integrity());
        TEST_EXPECT(ctx, write(ctx, document) == "{\"keep\":7}");
    }
    TEST_EXPECT(ctx, document_parser::parse(CStringView{}, document).status == EDocumentParseStatus::invalid_input_view);
    TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    TEST_EXPECT(ctx, parse("$morphicx:1,s:'$morphic'", document).succeeded());
    TEST_EXPECT(ctx, parse("", document).succeeded());
    TEST_EXPECT(ctx, document.is_ready() && document.value_count() == 1u && document.child_count(document.root()) == 0u);
    //  Publication must happen after all reads, even when source aliases the
    //  destination's old string storage.
    TEST_EXPECT(ctx, parse("source:'new_value:42'", document).succeeded());
    const CStringView alias = document.string_value(document.first_child(document.root()));
    TEST_EXPECT(ctx, document_parser::parse(alias, document, { document_policy::k_all_supported }).succeeded());
    TEST_EXPECT(ctx, write(ctx, document) == "{\"new_value\":42}");
}

static std::string recovery(const std::string& values)
{
    return "{\"$morphic\":{\"v\":1,\"type\":\"recovered-array\",\"values\":[" + values + "]}}";
}

static void expect_no_interpretations(TTestContext& ctx, const CDocumentParseReport& report)
{
    TEST_EXPECT(ctx, report.interpretations.recovered_arrays_decoded == 0u);
    TEST_EXPECT(ctx, report.interpretations.reserved_names_unescaped == 0u);
    TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 0u);
    TEST_EXPECT(ctx, report.interpretations.singleton_objects_unwrapped == 0u);
}

static void test_recovery_and_collisions(TTestContext& ctx)
{
    CLiveDocument document;
    const std::string nested = recovery("false");
    const std::string text = "first:0,item:1,middle:2,item:'two',item:null,item:" + nested + ",last:3";
    const auto report = parse(text, document);
    TEST_EXPECT(ctx, report.succeeded());
    TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 3u);
    TEST_EXPECT(ctx, report.interpretations.recovered_arrays_decoded == 1u);
    TEST_EXPECT(ctx, document.check_integrity() && document.is_complete());
    const CNodeKey item = member(document, document.root(), CStringView{ "item" });
    TEST_EXPECT(ctx, document.value_type(item) == ELiveValueType::recovered_array && document.child_count(item) == 4u);
    TEST_EXPECT(ctx, document.value_type(document.last_child(item)) == ELiveValueType::recovered_array);
    TEST_EXPECT(ctx, write(ctx, document) == "{\"first\":0,\"item\":" + recovery("1,\"two\",null," + nested) + ",\"middle\":2,\"last\":3}");
    for (CNodeKey child = document.first_child(item); child.is_valid(); child = document.next_sibling(child))
    {
        TEST_EXPECT(ctx, !document.is_object_entry(child));
    }

    for (const std::string& values : { std::string{}, std::string("1"), std::string("1,2") })
    {
        const auto extended = parse("r:" + recovery(values) + ",r:{a:1,a:2},r:" + nested, document);
        TEST_EXPECT(ctx, extended.succeeded());
        TEST_EXPECT(ctx, extended.interpretations.duplicate_members_recovered == 3u);
        TEST_EXPECT(ctx, extended.interpretations.recovered_arrays_decoded == 2u);
        const std::string prefix = values.empty() ? std::string{} : values + ",";
        TEST_EXPECT(ctx, write(ctx, document) == "{\"r\":" + recovery(prefix + "{\"a\":" + recovery("1,2") + "}," + nested) + "}");
        TEST_EXPECT(ctx, document.check_integrity());
    }

    //  All six metadata field orders, decoded control names, relaxed version
    //  spellings, and a singleton competitor that must remain anonymous.
    const char* fields[]{ "'\\u0076':+0x01", "type:'recovered-\\u0061rray'", "values:[{n:1}]" };
    const unsigned orders[][3]{ {0u,1u,2u}, {0u,2u,1u}, {1u,0u,2u}, {1u,2u,0u}, {2u,0u,1u}, {2u,1u,0u} };
    for (const auto& order : orders)
    {
        const auto decoded = parse(std::string("r:{'\\u0024morphic':{/*metadata*/") + fields[order[0]] + "," + fields[order[1]] + "," + fields[order[2]] + ",}}", document);
        TEST_EXPECT(ctx, decoded.succeeded());
        TEST_EXPECT(ctx, decoded.interpretations.recovered_arrays_decoded == 1u && decoded.interpretations.singleton_objects_unwrapped == 0u);
        const CNodeKey r = document.first_child(document.root());
        const CNodeKey competitor = document.first_child(r);
        TEST_EXPECT(ctx, document.value_type(r) == ELiveValueType::recovered_array && document.child_count(r) == 1u);
        TEST_EXPECT(ctx, document.value_type(competitor) == ELiveValueType::object && !document.is_object_entry(competitor));
        TEST_EXPECT(ctx, write(ctx, document) == "{\"r\":" + recovery("{\"n\":1}") + "}");
    }
    const auto aliases = parse("'a\\u0000':1,'a" + std::string(1u, '\0') + "':2,'$$morphic':3,'\\u0024$morphic':4,$$$morphic:5,$morphicx:6,s:'$$morphic'", document);
    TEST_EXPECT(ctx, aliases.succeeded());
    TEST_EXPECT(ctx, aliases.interpretations.duplicate_members_recovered == 2u && aliases.interpretations.reserved_names_unescaped == 3u);
    TEST_EXPECT(ctx, write(ctx, document) == "{\"a\\u0000\":" + recovery("1,2") + ",\"$$morphic\":" + recovery("3,4") + ",\"$$$morphic\":5,\"$morphicx\":6,\"s\":\"$$morphic\"}");
    for (const char* version : { "1", "+1", "0X0001", "#01", "+#1", "0b001", "+0B1" })
    {
        const auto decoded = parse(std::string("r:{$morphic:{v:") + version + ",type:'recovered-array',values:[]}}", document);
        TEST_CASE_EXPECT_TRUE(ctx, version, decoded.succeeded());
        TEST_EXPECT(ctx, decoded.interpretations.recovered_arrays_decoded == 1u);
        TEST_EXPECT(ctx, write(ctx, document) == "{\"r\":" + recovery("") + "}");
    }
}

static void test_singleton_contexts(TTestContext& ctx)
{
    CLiveDocument document;
    const std::string text = "a:[{n:null},{b:true},{s:'text'},{i:+1},{f:-0.0},{o:{}},{a:[]},{r:" + recovery("") + "},"
        "{d:1,d:2},{},{x:1,y:2}," + recovery("{n:1},[{a:2}]," + recovery("{b:3}")) + "],o:{one:1}";
    const auto report = parse(text, document);
    TEST_EXPECT(ctx, report.succeeded());
    TEST_EXPECT(ctx, report.interpretations.singleton_objects_unwrapped == 10u);
    TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 1u && report.interpretations.recovered_arrays_decoded == 3u);
    const CNodeKey array = member(document, document.root(), CStringView{ "a" });
    const ELiveValueType types[]{ ELiveValueType::null_value, ELiveValueType::boolean, ELiveValueType::string,
        ELiveValueType::integer, ELiveValueType::floating_point, ELiveValueType::object, ELiveValueType::array,
        ELiveValueType::recovered_array, ELiveValueType::recovered_array, ELiveValueType::object,
        ELiveValueType::object, ELiveValueType::recovered_array };
    CNodeKey child = document.first_child(array);
    for (unsigned i = 0u; i < 12u; ++i)
    {
        TEST_EXPECT(ctx, document.value_type(child) == types[i]);
        TEST_EXPECT(ctx, document.is_object_entry(child) == (i < 9u));
        child = document.next_sibling(child);
    }
    TEST_EXPECT(ctx, !child.is_valid());
    const CNodeKey transport = document.last_child(array);
    TEST_EXPECT(ctx, document.value_type(document.first_child(transport)) == ELiveValueType::object);
    TEST_EXPECT(ctx, document.value_type(document.first_child(document.last_child(transport))) == ELiveValueType::object);
    TEST_EXPECT(ctx, document.value_type(member(document, document.root(), CStringView{ "o" })) == ELiveValueType::object);
    TEST_EXPECT(ctx, document.value_type(document.root()) == ELiveValueType::object && document.check_integrity());
}

static void test_protocol_failures(TTestContext& ctx)
{
    struct CCase
    {
        const char* text;
        EDocumentParseStatus status;
    };
    const CCase cases[]{
        { "r:{$morphic:null}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:[]}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array'}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,v:1,type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array','\\u0074ype':'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{values:[],values:[],v:1,type:'recovered-array'}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array',values:[],extra:0}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{'':0}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{$$morphic:0}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{extra:0,$morphic:{v:1,type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array',values:[]},extra:0}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array',values:[]},$morphic:{}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1.0,type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:true,type:'recovered-array',values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:2,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:0,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:#00,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:0b11,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:-1,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:18446744073709551616,type:'recovered-array',values:[]}}", EDocumentParseStatus::unsupported_recovery_version },
        { "r:{$morphic:{v:1,type:null,values:[]}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'array',values:[]}}", EDocumentParseStatus::unsupported_recovery_type },
        { "r:{$morphic:{v:1,type:'recovered-array\\u0000',values:[]}}", EDocumentParseStatus::unsupported_recovery_type },
        { "r:{$morphic:{v:1,type:'recovered-array',values:{}}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "r:{$morphic:{v:1,type:'recovered-array',values:null}}", EDocumentParseStatus::malformed_recovery_wrapper },
        { "{$morphic:{v:1,type:'recovered-array',values:[]}}", EDocumentParseStatus::invalid_root_value } };
    CLiveDocument document;
    TEST_EXPECT(ctx, parse("keep:7", document).succeeded());
    const CNodeKey keep = document.first_child(document.root());
    const std::uint64_t allocation_size = document.memory_allocation_size();
    for (const auto& item : cases)
    {
        const auto report = parse(item.text, document);
        TEST_CASE_EXPECT_EQ(ctx, item.text, report.status, item.status);
        TEST_EXPECT(ctx, report.structure.succeeded());
        TEST_EXPECT(ctx, report.failure_point.available && report.failure_point.code_point_column_1_based <= std::strlen(item.text));
        expect_no_interpretations(ctx, report);
        TEST_EXPECT(ctx, document.first_child(document.root()) == keep && document.memory_allocation_size() == allocation_size);
        TEST_EXPECT(ctx, document.check_integrity() && write(ctx, document) == "{\"keep\":7}");
    }
    const std::string prefix = "$$morphic:1,a:[{n:1}],d:1,d:2,r:" + recovery("") + ",bad:";
    const auto late = parse(prefix + "{$morphic:{v:2,type:'recovered-array',values:[]}}", document);
    TEST_EXPECT(ctx, late.status == EDocumentParseStatus::unsupported_recovery_version);
    TEST_EXPECT(ctx, late.failure_point.code_point_column_1_based == prefix.size() + std::strlen("{$morphic:{v:") + 1u);
    expect_no_interpretations(ctx, late);
    TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    const auto duplicate = parse("r:{$morphic:{v:1,v:1,type:'recovered-array',values:[]}}", document);
    TEST_EXPECT(ctx, duplicate.failure_point.code_point_column_1_based == std::strlen("r:{$morphic:{v:1,") + 1u);
    //  User-authored protocol lookalikes are ordinary data when escaped.
    const auto data = parse("$$morphic:{v:2,type:'array',values:[],values:[1]},$$$morphic:3", document);
    TEST_EXPECT(ctx, data.succeeded() && data.interpretations.recovered_arrays_decoded == 0u);
    TEST_EXPECT(ctx, data.interpretations.reserved_names_unescaped == 2u && data.interpretations.duplicate_members_recovered == 1u);
}

static void attach(TTestContext& ctx, CLiveDocument& document, const CNodeKey parent, const CNodeKey child)
{
    TEST_EXPECT(ctx, child.is_valid());
    TEST_EXPECT(ctx, document.append_child(parent, child).succeeded());
}

static void expect_same_semantics(TTestContext& ctx, const CLiveDocument& source, const CLiveDocument& parsed, const bool strict)
{
    struct CPair
    {
        CNodeKey source;
        CNodeKey parsed;
    };
    std::vector<CPair> pending{ { source.root(), parsed.root() } };
    while (!pending.empty())
    {
        CPair pair = pending.back();
        pending.pop_back();
        if ((source.value_type(source.parent(pair.source)) == ELiveValueType::array) &&
            (source.value_type(pair.source) == ELiveValueType::object) && !source.is_object_entry(pair.source) &&
            (source.child_count(pair.source) == 1u))
        {
            pair.source = source.first_child(pair.source);
        }
        const ELiveValueType original_type = source.value_type(pair.source);
        const ELiveValueType type = (original_type == ELiveValueType::empty) ? ELiveValueType::null_value : original_type;
        TEST_EXPECT(ctx, parsed.value_type(pair.parsed) == type);
        TEST_EXPECT(ctx, source.name(pair.source) == parsed.name(pair.parsed));
        TEST_EXPECT(ctx, source.is_object_entry(pair.source) == parsed.is_object_entry(pair.parsed));
        switch (type)
        {
            case ELiveValueType::null_value:
            {
                break;
            }
            case ELiveValueType::boolean:
            {
                bool before = false;
                bool after = false;
                TEST_EXPECT(ctx, source.boolean_value(pair.source, before) && parsed.boolean_value(pair.parsed, after) && before == after);
                break;
            }
            case ELiveValueType::integer:
            {
                CIntegerMetadata before;
                CIntegerMetadata after;
                TEST_EXPECT(ctx, source.integer_metadata(pair.source, before) && parsed.integer_metadata(pair.parsed, after));
                std::int64_t signed_value = 0;
                std::uint64_t unsigned_value = 0u;
                if (before.domain == EIntegerDomain::signed_value)
                {
                    TEST_EXPECT(ctx, source.signed_integer_value(pair.source, signed_value));
                }
                else
                {
                    TEST_EXPECT(ctx, source.unsigned_integer_value(pair.source, unsigned_value));
                }
                if (strict)
                {
                    before.notation = EIntegerNotation::decimal;
                    before.prefix = EIntegerPrefix::standard;
                    if ((before.domain == EIntegerDomain::signed_value) && (signed_value >= 0))
                    {
                        before.domain = EIntegerDomain::unsigned_value;
                        unsigned_value = static_cast<std::uint64_t>(signed_value);
                    }
                }
                TEST_EXPECT(ctx, before.domain == after.domain && before.notation == after.notation && before.prefix == after.prefix);
                if (before.domain == EIntegerDomain::signed_value)
                {
                    std::int64_t value = 0;
                    TEST_EXPECT(ctx, parsed.signed_integer_value(pair.parsed, value) && value == signed_value);
                    TEST_EXPECT(ctx, after.width == live_signed_integer_smallest_width(signed_value));
                }
                else
                {
                    std::uint64_t value = 0u;
                    TEST_EXPECT(ctx, parsed.unsigned_integer_value(pair.parsed, value) && value == unsigned_value);
                    TEST_EXPECT(ctx, after.width == live_unsigned_integer_smallest_width(unsigned_value));
                }
                break;
            }
            case ELiveValueType::floating_point:
            {
                double before = 0.0;
                double after = 0.0;
                TEST_EXPECT(ctx, source.floating_point_value(pair.source, before) && parsed.floating_point_value(pair.parsed, after));
                TEST_EXPECT(ctx, live_floating_point_bits(before) == live_floating_point_bits(after));
                break;
            }
            case ELiveValueType::string:
            {
                TEST_EXPECT(ctx, source.string_value(pair.source) == parsed.string_value(pair.parsed));
                break;
            }
            case ELiveValueType::object:
            case ELiveValueType::array:
            case ELiveValueType::recovered_array:
            {
                TEST_EXPECT(ctx, source.child_count(pair.source) == parsed.child_count(pair.parsed));
                CNodeKey before = source.first_child(pair.source);
                CNodeKey after = parsed.first_child(pair.parsed);
                while (before.is_valid() && after.is_valid())
                {
                    pending.push_back(CPair{ before, after });
                    before = source.next_sibling(before);
                    after = parsed.next_sibling(after);
                }
                TEST_EXPECT(ctx, !before.is_valid() && !after.is_valid());
                break;
            }
            default:
            {
                TEST_EXPECT(ctx, false);
                break;
            }
        }
    }
}

static void test_round_trips(TTestContext& ctx)
{
    CLiveDocument source;
    TEST_EXPECT(ctx, source.initialise());
    const CNodeKey array = source.create_array(CStringView{ "items" });
    attach(ctx, source, source.root(), array);
    attach(ctx, source, array, source.create_empty(CStringView{ "placeholder" }));
    attach(ctx, source, array, source.create_null(CStringView{ "null" }));
    attach(ctx, source, array, source.create_boolean(true, CStringView{ "bool" }));
    attach(ctx, source, array, source.create_signed_integer(7, CStringView{ "signed" }));
    attach(ctx, source, array, source.create_signed_integer(std::numeric_limits<std::int64_t>::min(), CStringView{ "minimum" }));
    attach(ctx, source, array, source.create_unsigned_integer(std::numeric_limits<std::uint64_t>::max(), CStringView{ "maximum" }));
    CIntegerMetadata hexadecimal;
    hexadecimal.domain = EIntegerDomain::signed_value;
    hexadecimal.notation = EIntegerNotation::hexadecimal;
    hexadecimal.prefix = EIntegerPrefix::alternate;
    hexadecimal.width = live_signed_integer_smallest_width(-128);
    attach(ctx, source, array, source.create_signed_integer(-128, hexadecimal, CStringView{ "hex" }));
    CIntegerMetadata binary;
    binary.domain = EIntegerDomain::unsigned_value;
    binary.notation = EIntegerNotation::binary;
    binary.width = live_unsigned_integer_smallest_width(256u);
    attach(ctx, source, array, source.create_unsigned_integer(256u, binary, CStringView{ "binary" }));
    attach(ctx, source, array, source.create_floating_point(-0.0, CStringView{ "float" }));
    const std::uint8_t unicode[]{ 'x', 0u, 0xc3u, 0xa9u, 0xf0u, 0x9du, 0x84u, 0x9eu, '\n' };
    attach(ctx, source, array, source.create_string(CStringView{ unicode, sizeof(unicode) }, CStringView{ unicode, sizeof(unicode) - 1u }));
    attach(ctx, source, array, source.create_string(CStringView{ "" }, CStringView{ "empty" }));
    attach(ctx, source, array, source.create_array(CStringView{ "array" }));
    const CNodeKey object = source.create_object(CStringView{ "object" });
    attach(ctx, source, array, object);
    attach(ctx, source, object, source.create_null(CStringView{ "$morphic" }));
    const CNodeKey redundant = source.create_object();
    attach(ctx, source, array, redundant);
    attach(ctx, source, redundant, source.create_boolean(false, CStringView{ "singleton" }));
    attach(ctx, source, array, source.create_object());
    const CNodeKey multi = source.create_object();
    attach(ctx, source, array, multi);
    attach(ctx, source, multi, source.create_null(CStringView{ "a" }));
    attach(ctx, source, multi, source.create_null(CStringView{ "b" }));

    for (unsigned count = 0u; count < 4u; ++count)
    {
        const std::string name = std::string(count + 1u, '$') + "morphic";
        const CNodeKey recovered = source.create_recovered_array(CStringView{ name.data(), name.size() });
        attach(ctx, source, array, recovered);
        for (unsigned i = 0u; i < count; ++i)
        {
            const CNodeKey competitor = source.create_object();
            attach(ctx, source, recovered, competitor);
            attach(ctx, source, competitor, source.create_unsigned_integer(i, CStringView{ "n" }));
        }
    }
    const CNodeKey recovery_array = source.create_recovered_array();
    attach(ctx, source, array, recovery_array);
    attach(ctx, source, recovery_array, source.create_recovered_array());
    const CNodeKey competitor_array = source.create_array();
    attach(ctx, source, recovery_array, competitor_array);
    attach(ctx, source, competitor_array, source.create_recovered_array(CStringView{ "nested" }));
    const CNodeKey lookalike = source.create_object(CStringView{ "$morphic" });
    attach(ctx, source, source.root(), lookalike);
    attach(ctx, source, lookalike, source.create_unsigned_integer(2u, CStringView{ "v" }));
    attach(ctx, source, lookalike, source.create_string(CStringView{ "array" }, CStringView{ "type" }));
    attach(ctx, source, lookalike, source.create_array(CStringView{ "values" }));

    const CNodeKey numbers = source.create_array(CStringView{ "numbers" });
    attach(ctx, source, source.root(), numbers);
    for (const double value : { std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(), std::numeric_limits<double>::max() })
    {
        attach(ctx, source, numbers, source.create_floating_point(value));
    }
    std::uint64_t bits = 0xa0761d6478bd642full;
    for (unsigned i = 0u; i < 256u; ++i)
    {
        bits ^= bits << 13u;
        bits ^= bits >> 7u;
        bits ^= bits << 17u;
        const double value = live_floating_point_from_bits(bits);
        if (live_floating_point_is_finite(value))
        {
            attach(ctx, source, numbers, source.create_floating_point(value));
        }
    }
    TEST_EXPECT(ctx, source.check_integrity());
    CBakedDocumentBlock baked;
    TEST_EXPECT(ctx, document_translation::bake(source, baked));
    for (unsigned variant = 0u; variant < 8u; ++variant)
    {
        CDocumentWriteOptions options;
        const bool strict = (variant & 1u) != 0u;
        options.mode = strict ? EDocumentWriteMode::strict_json : EDocumentWriteMode::morphic;
        options.escape_non_ascii = (variant & 2u) != 0u;
        options.pretty_print = (variant & 4u) != 0u;
        const auto written = document_writer::write(baked.document(), options);
        TEST_EXPECT(ctx, written.report.succeeded());
        const auto linted = text_linter::lint(CByteConstView{ written.output.data(), written.output.size() }, k_document_text_lint_line_endings);
        TEST_EXPECT(ctx, linted.report.success && linted.report.normalised_line_endings == linted.report.encountered_line_endings);
        CLiveDocument parsed;
        const auto report = document_parser::parse(CStringView{ linted.output.data(), linted.report.logical_text_byte_size }, parsed, { document_policy::k_all_supported });
        TEST_EXPECT(ctx, report.succeeded());
        if (!report.succeeded())
        {
            continue;
        }
        TEST_EXPECT(ctx, parsed.check_integrity() && parsed.is_complete());
        TEST_EXPECT(ctx, report.interpretations.recovered_arrays_decoded == written.report.recovered_arrays_written);
        TEST_EXPECT(ctx, report.interpretations.reserved_names_unescaped == written.report.reserved_property_names_escaped);
        TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 0u && report.interpretations.singleton_objects_unwrapped > 0u);
        TEST_EXPECT(ctx, (report.structure.findings & document_findings::k_relaxed) == 0u);
        TEST_EXPECT(ctx, ((report.structure.findings & document_findings::k_morphic) == 0u) == strict);
        expect_same_semantics(ctx, source, parsed, strict);
        CBakedDocumentBlock rebaked;
        TEST_EXPECT(ctx, document_translation::bake(parsed, rebaked));
        const auto rewritten = document_writer::write(rebaked.document(), options);
        TEST_EXPECT(ctx, rewritten.report.succeeded());
        TEST_EXPECT(ctx, rewritten.report.logical_text_byte_size == written.report.logical_text_byte_size);
        if (rewritten.report.logical_text_byte_size == written.report.logical_text_byte_size)
        {
            TEST_EXPECT(ctx, std::memcmp(rewritten.output.data(), written.output.data(), written.report.logical_text_byte_size) == 0);
        }
    }
}

struct CAllocatorState
{
    std::size_t attempts{ 0u };
    std::size_t fail_on{ 0u };
    bool failed{ false };
};

static void* MV_STD_ABI_CALL allocate(void* const state, const std::size_t alignment, const std::size_t size) noexcept
{
    auto& fixture = *static_cast<CAllocatorState*>(state);
    if (fixture.attempts++ == fixture.fail_on)
    {
        fixture.failed = true;
        return nullptr;
    }
    return tests::allocate_test_memory(nullptr, alignment, size);
}

static void test_policy_allocation(TTestContext& ctx)
{
    for (const std::uint32_t permissions : { document_policy::k_default, 1u << 31 })
    {
        CLiveDocument destination;
        TEST_EXPECT(ctx, parse("[7]", destination).succeeded());
        const CNodeKey keep = destination.first_child(destination.root());
        const std::uint64_t allocation_size = destination.memory_allocation_size();
        bool completed = false;
        for (std::size_t fail_on = 0u; (fail_on < 256u) && !completed; ++fail_on)
        {
            CAllocatorState fixture{ 0u, fail_on, false };
            memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            {
                tests::TMemoryContextScope scope{ &context };
                const auto report = document_parser::ingest(CStringView{ "/*comment*/{\"a\":\"\\u00e9\",\"a\":[1,2]}" },
                    destination, { permissions });
                completed = report.construction_completed;
                TEST_EXPECT(ctx, !report.succeeded());
                if (completed)
                {
                    TEST_EXPECT(ctx, !fixture.failed && report.failure.reason == EDocumentFailureReason::none);
                    TEST_EXPECT(ctx, report.policy.status == ((permissions == document_policy::k_default) ?
                        EDocumentPolicyStatus::rejected : EDocumentPolicyStatus::invalid_options));
                    TEST_EXPECT(ctx, report.interpretations.duplicate_members_recovered == 1u);
                }
                else
                {
                    TEST_EXPECT(ctx, fixture.failed && report.policy.status == EDocumentPolicyStatus::unexamined);
                    TEST_EXPECT(ctx, report.failure.reason == EDocumentFailureReason::allocation_failed ||
                        report.failure.reason == EDocumentFailureReason::construction_failed);
                }
                TEST_EXPECT(ctx, destination.value_type(destination.root()) == ELiveValueType::array);
                TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
                TEST_EXPECT(ctx, destination.memory_allocation_size() == allocation_size && destination.check_integrity());
            }
            //  Rejected temporary documents and linter output must release all
            //  attributed storage, even after construction completed.
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_EXPECT(ctx, completed);
    }
}

static void test_root_allocation(TTestContext& ctx)
{
    const char* const sources[]{ "[]", "42", "\"\":1", "[\"a\\nb\",{\"\":\"x\n y\"},[1]]", "1,{\"x\":2},[3]" };
    for (const char* source : sources)
    {
        CLiveDocument destination;
        TEST_EXPECT(ctx, parse("[7]", destination).succeeded());
        const CNodeKey keep = destination.first_child(destination.root());
        const std::uint64_t allocation_size = destination.memory_allocation_size();
        bool completed = false;
        for (std::size_t fail_on = 0u; (fail_on < 256u) && !completed; ++fail_on)
        {
            CAllocatorState fixture{ 0u, fail_on, false };
            memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
            memory::CMemoryContext context{ allocator };
            {
                tests::TMemoryContextScope scope{ &context };
                const auto report = parse(source, destination);
                completed = report.succeeded();
                if (completed)
                {
                    TEST_EXPECT(ctx, !fixture.failed && report.construction_completed && destination.check_integrity());
                    destination.deallocate();
                }
                else
                {
                    TEST_EXPECT(ctx, fixture.failed && !report.construction_completed);
                    TEST_EXPECT(ctx, (report.status == EDocumentParseStatus::structural_failure) ||
                        (report.status == EDocumentParseStatus::allocation_failed) || (report.status == EDocumentParseStatus::construction_failed));
                    TEST_EXPECT(ctx, report.failure.stage == (report.parser_examined ? EDocumentFailureStage::parser : EDocumentFailureStage::structure));
                    TEST_EXPECT(ctx, report.failure.reason == ((report.status == EDocumentParseStatus::construction_failed) ?
                        EDocumentFailureReason::construction_failed : EDocumentFailureReason::allocation_failed));
                    TEST_EXPECT(ctx, destination.value_type(destination.root()) == ELiveValueType::array);
                    TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
                    TEST_EXPECT(ctx, destination.memory_allocation_size() == allocation_size && destination.check_integrity());
                }
            }
            TEST_EXPECT(ctx, context.is_attribution_empty());
        }
        TEST_CASE_EXPECT_TRUE(ctx, source, completed);
    }
}

static void test_depth_and_allocation(TTestContext& ctx)
{
    CLiveDocument destination;
    TEST_EXPECT(ctx, parse("keep:7", destination).succeeded());
    const CNodeKey keep = destination.first_child(destination.root());
    const std::string text = "'':'literal\nbreak',u\\u0020name:unquoted\\u0020value,'n\\u0000':'\\u0000\\u00e9',a:[{s:'\\uD834\\uDD1E'},+0x80,{},[],{d:1,d:2}],"
        "r:{$morphic:{values:[{n:1},[{a:2}]," + recovery("false") + "],type:'recovered-\\u0061rray',v:1}},"
        "r:{x:1,x:2},r:" + recovery("") + ",$$morphic:0,'\\u0024$morphic':1,b:'last'";
    bool completed = false;
    for (std::size_t fail_on = 0u; (fail_on < 256u) && !completed; ++fail_on)
    {
        CAllocatorState fixture{ 0u, fail_on, false };
        memory::CMemoryAllocator allocator{ &fixture, &allocate, &tests::deallocate_test_memory };
        memory::CMemoryContext context{ allocator };
        {
            tests::TMemoryContextScope scope{ &context };
            const auto report = parse(text, destination);
            completed = report.succeeded();
            if (completed)
            {
                TEST_EXPECT(ctx, !fixture.failed);
                TEST_EXPECT(ctx, destination.check_integrity());
                destination.deallocate();
            }
            else
            {
                TEST_EXPECT(ctx, fixture.failed);
                TEST_EXPECT(ctx, (report.status == EDocumentParseStatus::structural_failure) ||
                    (report.status == EDocumentParseStatus::allocation_failed) || (report.status == EDocumentParseStatus::construction_failed));
                TEST_EXPECT(ctx, report.failure.stage == (report.parser_examined ? EDocumentFailureStage::parser : EDocumentFailureStage::structure));
                TEST_EXPECT(ctx, report.failure.reason == ((report.status == EDocumentParseStatus::construction_failed) ?
                    EDocumentFailureReason::construction_failed : EDocumentFailureReason::allocation_failed));
                TEST_EXPECT(ctx, destination.first_child(destination.root()) == keep);
                expect_no_interpretations(ctx, report);
            }
        }
        TEST_EXPECT(ctx, context.is_attribution_empty());
    }
    TEST_EXPECT(ctx, completed);
    constexpr std::size_t depth = 2048u;
    const std::string deep = "a:" + std::string(depth, '[') + "null" + std::string(depth, ']');
    const auto report = parse(deep, destination);
    TEST_EXPECT(ctx, report.succeeded() && report.parser_examined && report.construction_completed);
    TEST_EXPECT(ctx, destination.check_integrity());
    TEST_EXPECT(ctx, destination.value_count() == depth + 2u);
    const std::string expected = "{\"a\":" + std::string(depth, '[') + "null" + std::string(depth, ']') + "}";
    TEST_EXPECT(ctx, write(ctx, destination) == expected);

    constexpr std::size_t recovery_depth = 512u;
    std::string recovered = "null";
    for (std::size_t i = 0u; i < recovery_depth; ++i)
    {
        recovered = recovery(recovered);
    }
    const auto nested = parse("r:" + recovered, destination);
    TEST_EXPECT(ctx, nested.succeeded() && nested.interpretations.recovered_arrays_decoded == recovery_depth);
    TEST_EXPECT(ctx, destination.value_count() == recovery_depth + 2u && destination.check_integrity());
    TEST_EXPECT(ctx, write(ctx, destination) == "{\"r\":" + recovered + "}");
}

static void test_composed_linter_diagnostics(TTestContext& ctx)
{
    CLiveDocument document;
    TEST_EXPECT(ctx, parse("keep:7", document).succeeded());
    const CNodeKey keep = document.first_child(document.root());
    CDocumentParseReport report = parse("{a:[1}", document);
    TEST_EXPECT(ctx, report.structure_start.available && report.failure_point.available);
    TEST_EXPECT(ctx, report.structure_start.code_point_column_1_based == 4u);
    TEST_EXPECT(ctx, report.failure_point.code_point_column_1_based == 6u);
    const std::string failures[]{ "\x81", "\xef\xbb\xbf\xff",
        "\xef\xbb\xbf\r\n\t\xc3\xa9\xcc\x81\xed\xa0\x80\xed\xb0\x80\xff",
        "\x93\r\n\xe9\t\x81" };
    for (const auto& source : failures)
    {
        report = document_parser::ingest(CByteConstView{ reinterpret_cast<const std::uint8_t*>(source.data()), source.size() }, document);
        TEST_EXPECT(ctx, report.status == EDocumentParseStatus::linter_failure);
        TEST_EXPECT(ctx, report.linter_examined && !report.linter.success);
        TEST_EXPECT(ctx, report.structure.status == EDocumentStructureStatus::unexamined);
        TEST_EXPECT(ctx, !report.structure_start.available);
        TEST_EXPECT(ctx, !report.structure.structure_start.available && !report.structure.failure_point.available);
        TEST_EXPECT(ctx, report.failure_point.available == report.linter.first_failure.location.available);
        TEST_EXPECT(ctx, report.failure_point.line_1_based == report.linter.first_failure.location.line_1_based);
        TEST_EXPECT(ctx, report.failure_point.code_point_column_1_based == report.linter.first_failure.location.code_point_column_1_based);
        TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    }
    report = document_parser::ingest(CByteConstView{}, document);
    TEST_EXPECT(ctx, report.status == EDocumentParseStatus::linter_failure);
    TEST_EXPECT(ctx, !report.structure_start.available && !report.failure_point.available);
    TEST_EXPECT(ctx, report.linter.first_failure.before_output);
    TEST_EXPECT(ctx, report.linter.first_failure.reason == ETextLintFailure::invalid_input_view);
    TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    report = document_parser::ingest(CStringView{}, document);
    TEST_EXPECT(ctx, report.status == EDocumentParseStatus::linter_failure);
    TEST_EXPECT(ctx, report.linter.first_failure.reason == ETextLintFailure::invalid_input_view);
    TEST_EXPECT(ctx, report.linter.first_failure.before_output && !report.failure_point.available);
    TEST_EXPECT(ctx, !report.structure_start.available && report.structure.status == EDocumentStructureStatus::unexamined);
    TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    const std::uint8_t source[]{ '{', '"', 's', '"', ':', '"', 0xedu, 0xa0u, 0x80u,
        0xedu, 0xb0u, 0x80u, '\r', '\n', 'x', '"', '}' };
    tests::TAllocatorFixture fixture;
    fixture.reject_allocation = true;
    memory::CMemoryAllocator allocator{ &fixture, &tests::allocate_test_memory, &tests::deallocate_test_memory };
    memory::CMemoryContext context{ allocator };
    {
        tests::TMemoryContextScope scope{ &context };
        report = document_parser::ingest(CByteConstView{ source, sizeof(source) }, document, { document_policy::k_all_supported });
        TEST_EXPECT(ctx, report.status == EDocumentParseStatus::linter_failure);
        TEST_EXPECT(ctx, report.linter.first_failure.reason == ETextLintFailure::allocation_failed);
        TEST_EXPECT(ctx, report.failure.stage == EDocumentFailureStage::linter && report.failure.reason == EDocumentFailureReason::allocation_failed);
        TEST_EXPECT(ctx, report.linter.first_failure.before_output);
        TEST_EXPECT(ctx, !report.structure_start.available && report.failure_point.available);
        TEST_EXPECT(ctx, report.structure.status == EDocumentStructureStatus::unexamined);
        TEST_EXPECT(ctx, document.first_child(document.root()) == keep);
    }
    TEST_EXPECT(ctx, context.is_attribution_empty());
    report = document_parser::ingest(CByteConstView{ source, sizeof(source) }, document, { document_policy::k_all_supported });
    TEST_EXPECT(ctx, report.succeeded() && report.linter.success && report.structure.succeeded());
    TEST_EXPECT(ctx, report.linter.cesu8_pair_count == 1u);
    TEST_EXPECT(ctx, !report.structure_start.available && !report.failure_point.available);
    const std::uint8_t expected[]{ 0xf0u, 0x90u, 0x80u, 0x80u, '\n', 'x' };
    TEST_EXPECT(ctx, document.string_value(document.first_child(document.root())) == (CStringView{ expected, sizeof(expected) }));

    const char alias_source[] = "{\"alias\":1}";
    TEST_EXPECT(ctx, parse("s:'{\"alias\":1}'", document).succeeded());
    const CStringView alias = document.string_value(document.first_child(document.root()));
    TEST_EXPECT(ctx, document_parser::ingest(CByteConstView{ alias.string(), alias.length() }, document).succeeded());
    TEST_EXPECT(ctx, write(ctx, document) == alias_source);
    report = document_parser::ingest(CStringView{ "", 0u }, document);
    TEST_EXPECT(ctx, report.succeeded() && report.linter.success && report.linter.input_is_empty);
    TEST_EXPECT(ctx, document.value_count() == 1u && document.check_integrity());
    TEST_EXPECT(ctx, !report.failure_point.available && !report.structure_start.available);
}

}   //  namespace document_parser_tests

int run_document_parser_tests()
{
    tests::TTestContext ctx;
    document_parser_tests::test_findings_and_policy_contract(ctx);
    document_parser_tests::test_late_policy_acceptance(ctx);
    document_parser_tests::test_policy_processing_precedence(ctx);
    document_parser_tests::test_policy_source_acceptance(ctx);
    document_parser_tests::test_composed_findings(ctx);
    document_parser_tests::test_shared_failures(ctx);
    document_parser_tests::test_policy_source_provenance(ctx);
    document_parser_tests::test_construction_and_features(ctx);
    document_parser_tests::test_unquoted_strings(ctx);
    document_parser_tests::test_strings_and_ingestion(ctx);
    document_parser_tests::test_empty_names(ctx);
    document_parser_tests::test_newline_metadata(ctx);
    document_parser_tests::test_root_inference(ctx);
    document_parser_tests::test_integer_metadata(ctx);
    document_parser_tests::test_floats(ctx);
    document_parser_tests::test_failure_publication(ctx);
    document_parser_tests::test_recovery_and_collisions(ctx);
    document_parser_tests::test_singleton_contexts(ctx);
    document_parser_tests::test_protocol_failures(ctx);
    document_parser_tests::test_round_trips(ctx);
    document_parser_tests::test_depth_and_allocation(ctx);
    document_parser_tests::test_root_allocation(ctx);
    document_parser_tests::test_policy_allocation(ctx);
    document_parser_tests::test_composed_linter_diagnostics(ctx);
    std::cout << "DocumentParser: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return (ctx.failed == 0) ? 0 : 1;
}
