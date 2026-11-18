
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_reference_fixture.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Standalone Foundation schema round trip, portable artifacts and phase timings.

#include "schema/schema.hpp"
#include "schema/data_copy.hpp"
#include "schema/reference_types.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_writer.hpp"
#include "memory/memory_context.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <type_traits>

static_assert(sizeof(schema::STextReference) == 4u);
static_assert(alignof(schema::STextReference) == 4u);
static_assert(sizeof(schema::SSpanReference) == 12u);
static_assert(alignof(schema::SSpanReference) == 4u);
static_assert(std::is_same_v<decltype(schema::SSpanReference::count), std::uint32_t>);

struct SAllocationCounts { std::size_t current{}, peak{}, allocations{}; };
struct SAllocationHeader { std::size_t bytes{}, prefix{}, alignment{}; };

static void* MV_STD_ABI_CALL allocate(void* const context, const std::size_t alignment, const std::size_t bytes) noexcept
{
    SAllocationCounts& counts = *static_cast<SAllocationCounts*>(context);
    const std::size_t aligned = std::max(alignment, alignof(SAllocationHeader));
    const std::size_t prefix = (sizeof(SAllocationHeader) + (aligned - 1u)) & ~(aligned - 1u);
    if (bytes > (SIZE_MAX - prefix)) return nullptr;
    std::uint8_t* const base = static_cast<std::uint8_t*>(::operator new[]((bytes + prefix), std::align_val_t{ aligned }, std::nothrow));
    if (!base) return nullptr;
    const SAllocationHeader header{ bytes, prefix, aligned };
    std::uint8_t* const result = base + prefix;
    std::memcpy((result - sizeof(header)), &header, sizeof(header));
    counts.current += bytes;
    counts.peak = std::max(counts.peak, counts.current);
    ++counts.allocations;
    return result;
}

static bool MV_STD_ABI_CALL deallocate(void* const context, const std::size_t alignment, void* const pointer) noexcept
{
    (void)alignment;
    if (!pointer) return false;
    SAllocationHeader header;
    std::memcpy(&header, (static_cast<std::uint8_t*>(pointer) - sizeof(header)), sizeof(header));
    static_cast<SAllocationCounts*>(context)->current -= header.bytes;
    ::operator delete[]((static_cast<std::uint8_t*>(pointer) - header.prefix), std::align_val_t{ header.alignment });
    return true;
}

static std::uint64_t micros(const std::chrono::steady_clock::time_point start) noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}

static bool write_bytes(const std::filesystem::path& path, const void* const data, const std::size_t size)
{
    std::ofstream output(path, (std::ios::binary | std::ios::trunc));
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return output.good();
}

static std::string authored_report(const unsigned count)
{
    std::string result = R"({"types":{"structures":{
        "Mode":{"members":[{"label":{"type":{"kind":"text"}}},{"width":{"type":"u32"}},{"height":{"type":"u32"}},{"refreshNumerator":{"type":"u32"}},{"refreshDenominator":{"type":"u32"}}]},
        "Display":{"members":[{"name":{"type":{"kind":"text"}}},{"modes":{"type":{"kind":"span","element":"Mode"}}}]},
        "Adapter":{"members":[{"name":{"type":{"kind":"text"}}},{"displays":{"type":{"kind":"span","element":"Display"}}}]},
        "Report":{"members":[{"title":{"type":{"kind":"text"},"default":"Portable report"}},{"note":{"type":{"kind":"text","nullable":true}}},{"adapters":{"type":{"kind":"span","element":"Adapter"}}}]}
    }},"instances":{"Report":{"current":{"declaration":{"note":"Exact \u00e9 e\u0301 \u0000 text","adapters":{"collection":"adapters","first":0,"count":2}}}}},
    "data":{"Mode":{"modes":{"data":[)";
    for (unsigned index = 0u; index < count; ++index)
    {
        if (index) result += ',';
        result += "{\"label\":\"Mode " + std::to_string(index) + "\",\"width\":1920,\"height\":1080,\"refreshNumerator\":60000,\"refreshDenominator\":1001}";
    }
    result += R"(]}},"Display":{"displays":{"data":[{"name":"Display A","modes":{"collection":"modes","first":0,"count":)";
    result += std::to_string(count);
    result += R"(}},{"name":"Display B","modes":{"collection":"modes","first":0,"count":)";
    result += std::to_string(count);
    result += R"(}}]}},"Adapter":{"adapters":{"data":[{"name":"Adapter A","displays":{"collection":"displays","first":0,"count":2}},{"name":"Adapter B","displays":{"collection":"displays","first":1,"count":1}}]}}}})";
    return result;
}

static bool inspect(const schema::CDataBlockView& view)
{
    schema::CDataValueView root, field, adapter, display, mode;
    schema::SDataTextView text;
    schema::SDataRangeView adapters, displays, modes;
    return view.value(view.find_instance(CStringView{ "Report" }, CStringView{ "current" }), 0u, root) &&
        view.member(root, CStringView{ "title" }, field) && view.text(field, text) &&
        (text.stored_text == CStringView{ "Portable report" }) &&
        view.member(root, CStringView{ "adapters" }, field) && view.span(field, adapters) &&
        view.element(adapters, 0u, adapter) && view.member(adapter, CStringView{ "displays" }, field) &&
        view.span(field, displays) && view.element(displays, 1u, display) &&
        view.member(display, CStringView{ "modes" }, field) && view.span(field, modes) &&
        view.element(modes, (modes.count - 1u), mode) && view.member(mode, CStringView{ "label" }, field) &&
        view.text(field, text) && text.stored_text.length();
}

static bool round_trip(const std::filesystem::path& directory, const unsigned count, const unsigned iteration,
    SAllocationCounts& allocations)
{
    allocations.peak = allocations.current;
    std::uint64_t parse_us{}, resolve_us{}, bake_us{}, admission_us{}, copy_us{}, source_retire_us{}, export_us{}, import_us{}, retire_us{};
    std::size_t file_size{};
    std::size_t bind_allocations{}, bind_retained_bytes{}, bind_peak_bytes{};
    auto started = std::chrono::steady_clock::now();
    {
        const std::string json = authored_report(count);
        CLiveDocument input;
        schema::CSchema authored;
        schema::SSchemaDiagnostic schema_error;
        if (!document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(json.data()), json.size(), 1u }, input).accepted() ||
            !authored.try_adopt(std::move(input), schema_error))
        {
            return false;
        }
        parse_us = micros(started);
        started = std::chrono::steady_clock::now();
        schema::CResolvedTypes types;
        schema::SDiagnostic type_error;
        if (!types.resolve(authored.types_query(), type_error))
        {
            return false;
        }
        resolve_us = micros(started);
        started = std::chrono::steady_clock::now();
        schema::CBakedDataBlock source;
        if (!authored.bake(source, schema::SDataOperationLimits{}, schema_error))
        {
            std::cerr << "Bake failed reason=" << static_cast<unsigned>(schema_error.reason)
                << " operation=" << static_cast<unsigned>(schema_error.operation.reason)
                << " limit=" << static_cast<unsigned>(schema_error.operation.limit) << '\n';
            return false;
        }
        bake_us = micros(started);
        file_size = source.file_bytes().size();
        started = std::chrono::steady_clock::now();
        schema::CDataBlockView view;
        schema::SDataOperationDiagnostic error;
        //  Isolate the view's operation and metadata from already-owned source
        //  storage. Restore the whole-round-trip peak after measuring admission.
        const std::size_t before_bind_bytes = allocations.current;
        const std::size_t before_bind_allocations = allocations.allocations;
        const std::size_t prior_peak = allocations.peak;
        allocations.peak = before_bind_bytes;
        if (!view.bind(source, {}, error))
        {
            return false;
        }
        bind_allocations = allocations.allocations - before_bind_allocations;
        bind_retained_bytes = allocations.current - before_bind_bytes;
        bind_peak_bytes = allocations.peak - before_bind_bytes;
        allocations.peak = std::max(prior_peak, allocations.peak);
        if (!inspect(view)) return false;
        admission_us = micros(started);
        started = std::chrono::steady_clock::now();
        schema::CBakedDataBlock copied;
        if (!schema::repack_data(view, {}, copied, error))
        {
            std::cerr << "Copy failed reason=" << static_cast<unsigned>(error.reason) << " limit=" << static_cast<unsigned>(error.limit) << '\n';
            return false;
        }
        copy_us = micros(started);
        //  The copied result is usable after its source owner and authoring input retire.
        started = std::chrono::steady_clock::now();
        view.clear();
        source.clear();
        authored.clear();
        types.clear();
        source_retire_us = micros(started);
        schema::CDataBlockView independent;
        if (!independent.bind(copied, {}, error) || !inspect(independent))
        {
            return false;
        }
        started = std::chrono::steady_clock::now();
        CBakedDocumentBlock exported;
        if (!schema::export_data(independent, {}, exported, error))
        {
            return false;
        }
        export_us = micros(started);
        started = std::chrono::steady_clock::now();
        schema::CSchema restored;
        schema::CBakedDataBlock rebuilt;
        schema::CDataBlockView reread;
        if (!restored.copy_from(schema::CSchemaDocumentQuery{ exported.document() }, schema_error) ||
            !restored.bake(rebuilt, schema_error) || !reread.bind(rebuilt, {}, error) || !inspect(reread))
        {
            return false;
        }
        import_us = micros(started);
        if (!iteration)
        {
            const std::string stem = "report-" + std::to_string(count);
            CDocumentWriteOptions options;
            options.mode = EDocumentWriteMode::strict_json;
            const CDocumentWriteResult written = document_writer::write(exported.document(), options);
            CByteBuffer header;
            if (!written.report.succeeded() || !schema::generate_cpp(*independent.types(), CStringView{ "report_fixture" }, header, type_error))
            {
                return false;
            }
            if (!write_bytes((directory / (stem + ".bsd")), copied.file_bytes().data(), copied.file_bytes().size()) ||
                !write_bytes((directory / (stem + ".authored.json")), json.data(), json.size()) ||
                !write_bytes((directory / (stem + ".inspection.json")), written.output.data(), written.report.logical_text_byte_size) ||
                !write_bytes((directory / (stem + ".hpp")), header.data(), (header.size() - 1u)))
            {
                return false;
            }
            const std::string layout = "#include \"" + stem + ".hpp\"\n#include <cstddef>\n#include <type_traits>\n"
                "static_assert(sizeof(report_fixture::Mode) == 20);\nstatic_assert(alignof(report_fixture::Mode) == 4);\n"
                "static_assert(sizeof(report_fixture::Display) == 16);\nstatic_assert(sizeof(report_fixture::Adapter) == 16);\n"
                "static_assert(sizeof(report_fixture::Report) == 20);\nstatic_assert(offsetof(report_fixture::Report, adapters) == 8);\n"
                "static_assert(sizeof(schema::STextReference) == 4);\nstatic_assert(sizeof(schema::SSpanReference) == 12);\n"
                "static_assert(alignof(schema::SSpanReference) == 4);\n"
                "static_assert(std::is_same_v<decltype(schema::SSpanReference::count), std::uint32_t>);\n";
            if (!write_bytes((directory / (stem + ".layout.cpp")), layout.data(), layout.size()))
            {
                return false;
            }
        }
        started = std::chrono::steady_clock::now();
    }
    retire_us = micros(started);
    std::cout << count << ',' << iteration << ',' << file_size << ',' << parse_us << ',' << resolve_us << ',' << bake_us << ','
        << admission_us << ',' << copy_us << ',' << source_retire_us << ',' << export_us << ',' << import_us << ',' << retire_us << ',' << allocations.peak << ','
        << bind_allocations << ',' << bind_retained_bytes << ',' << bind_peak_bytes << '\n';
    return allocations.current == 0u;
}

static bool read_fixture(const std::filesystem::path& path, const std::filesystem::path& inspection, const bool schema_fixture)
{
    std::ifstream input(path, (std::ios::binary | std::ios::ate));
    const std::streamoff size = input.tellg();
    if ((size <= 0) || (size > (INT64_C(16) << 20u)))
    {
        return false;
    }
    input.seekg(0);
    CByteBuffer bytes;
    if (!bytes.allocate(static_cast<std::size_t>(size), 128u) || !bytes.set_size(static_cast<std::size_t>(size)))
    {
        return false;
    }
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input.good()) return false;
    CBakedDocumentBlock generic;
    schema::CBakedDataBlock block;
    schema::SDataBlockDiagnostic storage_error;
    schema::SDataOperationDiagnostic error;
    schema::CDataBlockView view;
    CBakedDocumentBlock exported;
    schema::SDataOperationLimits inspection_limits;
    //  Full JSON reconstruction is an offline inspection, after report publication.
    if (!schema_fixture) inspection_limits.work_credits = UINT64_C(1) << 26u;
    if (!generic.adopt_buffer(std::move(bytes)) || !block.adopt_local(std::move(generic), storage_error) ||
        !view.bind(block, {}, error) || (schema_fixture && !inspect(view)) || !schema::export_data(view, inspection_limits, exported, error))
    {
        std::cerr << "Inspection failed, storage " << static_cast<unsigned>(storage_error.reason)
            << ", operation " << static_cast<unsigned>(error.reason) << ", limit " << static_cast<unsigned>(error.limit)
            << ", requested " << error.requested << ", allowed " << error.allowed << ", entry " << error.entry << ", path " << error.path << '\n';
        return false;
    }
    CDocumentWriteOptions options;
    options.mode = EDocumentWriteMode::strict_json;
    const CDocumentWriteResult written = document_writer::write(exported.document(), options);
    return written.report.succeeded() && write_bytes(inspection, written.output.data(), written.report.logical_text_byte_size);
}

static bool write_platform_layout(const std::filesystem::path& output)
{
    static constexpr char declarations[] =
#include "../framework/platform/report_types.inl"
    ;
    CLiveDocument document;
    schema::CResolvedTypes types;
    schema::SDiagnostic diagnostic;
    CByteBuffer header;
    const CByteConstView bytes{ reinterpret_cast<const std::uint8_t*>(declarations), sizeof(declarations) - 1u, 1u };
    if (!document_parser::parse(bytes, document).accepted() || !types.resolve(document, diagnostic) ||
        !schema::generate_cpp(types, CStringView{ "platform_report_v1" }, header, diagnostic))
    {
        std::cerr << "Platform report layout generation failed, schema reason "
            << static_cast<unsigned>(diagnostic.reason) << '\n';
        return false;
    }
    std::string result(reinterpret_cast<const char*>(header.data()), header.size() - 1u);
    result += "\n#include <cstddef>\n\nnamespace platform_report_v1\n{\n";
    for (std::uint32_t i = 0u; i < types.definition_count(); ++i)
    {
        const schema::CTypesIndex index = types.definition_at(i);
        schema::SType type;
        if (!types.type(index, type)) return false;
        if (type.category != schema::ECategory::structure) continue;
        const CStringView name = types.name(type.name);
        const std::string record(name.cstring(), name.length());
        result += "static_assert(sizeof(" + record + ") == " + std::to_string(type.size) + "u);\n";
        result += "static_assert(alignof(" + record + ") == " + std::to_string(type.alignment) + "u);\n";
        for (std::uint32_t m = 0u; m < type.count; ++m)
        {
            schema::SMember member;
            if (!types.member(types.member_at(index, m), member)) return false;
            const CStringView field = types.name(member.name);
            result += "static_assert(offsetof(" + record + ", " + std::string(field.cstring(), field.length()) +
                ") == " + std::to_string(member.offset) + "u);\n";
        }
    }
    result += "}\n";
    return write_bytes(output, result.data(), result.size());
}

int main(const int argc, char** const argv)
{
    SAllocationCounts counts;
    memory::CMemoryAllocator allocator{ &counts, &allocate, &deallocate };
    memory::CMemoryContext memory{ allocator };
    (void)memory::set_module_memory_context(&memory);
    bool success{};
    if ((argc == 3) && (std::strcmp(argv[1], "platform-layout") == 0))
    {
        success = write_platform_layout(argv[2]);
    }
    else if ((argc == 4) && ((std::strcmp(argv[1], "read") == 0) || (std::strcmp(argv[1], "inspect-platform") == 0)))
    {
        success = read_fixture(argv[2], argv[3], std::strcmp(argv[1], "read") == 0);
    }
    else if ((argc == 2) || ((argc == 3) && ((std::strcmp(argv[2], "64") == 0) || (std::strcmp(argv[2], "1024") == 0))))
    {
        const std::filesystem::path directory{ argv[1] };
        std::filesystem::create_directories(directory);
        std::cout << "records,iteration,file_bytes,parse_us,resolve_us,bake_us,typed_admission_us,copy_us,source_retire_us,export_us,import_and_bake_us,retire_us,peak_engine_requested_bytes,view_bind_allocations,view_bind_retained_requested_bytes,view_bind_peak_requested_bytes\n";
        success = true;
        for (const unsigned count : { 64u, 1024u })
        {
            if ((argc == 3) && (count != ((std::strcmp(argv[2], "64") == 0) ? 64u : 1024u)))
            {
                continue;
            }
            for (unsigned iteration = 0u; success && (iteration < 3u); ++iteration)
            {
                success = round_trip(directory, count, iteration, counts);
            }
        }
    }
    else
    {
        std::cerr << "Usage: MorphicSchemaFixture <output-directory> [64|1024] | read <input.bsd> <inspection.json>"
            " | inspect-platform <input.bsd> <inspection.json> | platform-layout <output.inl>\n";
    }
    success = success && memory.is_attribution_empty() && !counts.current;
    (void)memory::set_module_memory_context(nullptr);
    return success ? 0 : 1;
}
