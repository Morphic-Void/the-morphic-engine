
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_compiler.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Non-throwing DXC adapter. Engine scratch and COM objects never escape.

#include "runtime/shader_compiler.hpp"
#include "debug/macros.hpp"
#include "external/SuiteUTF/include/utf_toolkit.h"
#include <charconv>

#if MV_PLATFORM_WINDOWS
#include "platform/windows_include.hpp"
#include <unknwn.h>
#include <oleauto.h>
#include <combaseapi.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <dxcapi.h>

namespace rendering::shader_compiler
{

using shader::CCompileRequest;
using shader::CShaderCompileReport;
using shader::EDebug;
using shader::EFeatures;
using shader::EOptimisation;
using shader::EStage;
using shader::EStatus;
using shader::ETarget;
using shader::SDefinition;
using shader::SEntry;
using shader::TMembers;

static DxcCreateInstanceProc s_factory{ nullptr };

struct STaskString
{
    STaskString() noexcept = default;
    STaskString(const STaskString&) = delete;
    STaskString& operator=(const STaskString&) = delete;
    char* value{ nullptr };
    ~STaskString() noexcept
    {
        if (value != nullptr)
        {
            CoTaskMemFree(value);
        }
    }
};

template<typename T>
struct TCom
{
    TCom() noexcept = default;
    TCom(const TCom&) = delete;
    TCom& operator=(const TCom&) = delete;
    T* value{ nullptr };
    ~TCom() noexcept
    {
        if (value != nullptr)
        {
            value->Release();
        }
    }
    T* operator->() const noexcept { return value; }
};

using unicode::unicode_t;
using unicode::utf::utf_text;
using unicode::utf::toolkit::IUTFTK;
using unicode::utf::toolkit::UTF_SUB_TYPE;
using unicode::utf::toolkit::cp_errors;

static bool read_utf8(utf_text& input, unicode_t& point) noexcept
{
    const auto errors = IUTFTK::getHandler(UTF_SUB_TYPE::UTF8st).read(input, point);
    return !errors.failed() && !errors.any(cp_errors::bits::IrregularForm) &&
           (point != 0u) && (point <= 0x10ffffu) && !((point >= 0xd800u) && (point <= 0xdfffu));
}

static CStringView text(const CByteBuffer& buffer) noexcept
{
    return (buffer.size() != 0u) ? CStringView{ buffer.data(), buffer.size() } : CStringView{ "" };
}

static bool append(CByteBuffer& buffer, const CStringView value) noexcept
{
    return (value.length() == 0u) || buffer.append(value.string(), value.length());
}

static bool append(CByteBuffer& buffer, const char* const value) noexcept
{
    return append(buffer, CStringView{ value });
}

static bool append_number(CByteBuffer& buffer, const std::uint64_t value) noexcept
{
    char digits[20];
    const auto result = std::to_chars(digits, digits + sizeof(digits), value);
    return (result.ec == std::errc{}) && append(buffer, CStringView{ digits, static_cast<std::size_t>(result.ptr - digits) });
}

//  All strings are appended before materialising pointers into the wide storage.
class CArguments final
{
public:
    bool add(const wchar_t* const value) noexcept
    {
        if (status != EStatus::success) return false;
        std::size_t length = 0u;
        while (value[length] != 0) ++length;
        if (!m_offsets.push_back(m_text.size()) || !m_text.push_back(value, length + 1u))
        {
            status = EStatus::allocation_failed;
            return false;
        }
        return true;
    }

    bool add(const CStringView value) noexcept
    {
        if (status != EStatus::success) return false;
        if (value.length() > UINT32_MAX)
        {
            status = EStatus::limit_exceeded;
            return false;
        }
        if (!m_offsets.push_back(m_text.size()))
        {
            status = EStatus::allocation_failed;
            return false;
        }
        utf_text input{ static_cast<std::uint32_t>(value.length()), 0u, const_cast<std::uint8_t*>(value.string()) };
        while (input.offset < input.length)
        {
            unicode_t point = 0u;
            if (!read_utf8(input, point))
            {
                status = EStatus::invalid_input;
                return false;
            }
#if MV_PLATFORM_WINDOWS
            if (point > 0xffffu)
            {
                const wchar_t pair[]{ static_cast<wchar_t>(0xd800u + ((point - 0x10000u) >> 10u)),
                    static_cast<wchar_t>(0xdc00u + ((point - 0x10000u) & 0x3ffu)) };
                if (!m_text.push_back(pair, 2u))
                {
                    status = EStatus::allocation_failed;
                    return false;
                }
                continue;
            }
#endif
            if (!m_text.push_back(static_cast<wchar_t>(point)))
            {
                status = EStatus::allocation_failed;
                return false;
            }
        }
        if (!m_text.push_back(wchar_t{ 0 }))
        {
            status = EStatus::allocation_failed;
            return false;
        }
        return true;
    }

    bool finish() noexcept
    {
        if (status != EStatus::success) return false;
        //  The final -P is used only by the diagnostic preprocessing pass.
        if (!add(L"-P") || !pointers.resize(m_offsets.size()))
        {
            status = EStatus::allocation_failed;
            return false;
        }
        for (std::size_t i = 0u; i < m_offsets.size(); ++i)
        {
            pointers[i] = m_text.data() + m_offsets[i];
        }
        return true;
    }

    EStatus status{ EStatus::success };
    TPodVector<LPCWSTR> pointers;

private:
    TPodVector<wchar_t> m_text;
    TPodVector<std::size_t> m_offsets;
};

static EStatus narrow(const wchar_t* value, CByteBuffer& output) noexcept
{
    if (value == nullptr) return EStatus::invalid_input;
    while (*value != 0)
    {
        std::uint32_t point = static_cast<std::uint32_t>(*value++);
#if MV_PLATFORM_WINDOWS
        if ((point >= 0xd800u) && (point <= 0xdbffu))
        {
            const auto low = static_cast<std::uint32_t>(*value);
            if ((low < 0xdc00u) || (low > 0xdfffu)) return EStatus::invalid_input;
            ++value;
            point = 0x10000u + ((point - 0xd800u) << 10u) + (low - 0xdc00u);
        }
#endif
        if ((point > 0x10ffffu) || ((point >= 0xd800u) && (point <= 0xdfffu))) return EStatus::invalid_input;
        std::uint8_t bytes[4];
        utf_text encoded{ sizeof(bytes), 0u, bytes };
        const auto errors = IUTFTK::getHandler(UTF_SUB_TYPE::UTF8st).write(encoded, point);
        if (errors.failed()) return EStatus::invalid_input;
        if (!output.append(bytes, encoded.offset)) return EStatus::allocation_failed;
    }
    return EStatus::success;
}

static bool separator(const char value) noexcept
{
    return (value == '/') || (value == '\\');
}

//  Validate without scratch allocation. Parent components may only pop a
//  component already inside the bundle; the result must name a member/root.
static bool valid_path(const CStringView path) noexcept
{
    if ((path.length() == 0u) || separator(path.cstring()[0])) return false;
    std::size_t depth = 0u;
    for (std::size_t begin = 0u; begin < path.length();)
    {
        std::size_t end = begin;
        while ((end < path.length()) && !separator(path.cstring()[end]))
        {
            if ((path.cstring()[end] == ':') || (path.cstring()[end] == 0)) return false;
            ++end;
        }
        const auto part = CStringView{ path.cstring() + begin, end - begin };
        if (part == CStringView{ ".." })
        {
            if (depth == 0u) return false;
            --depth;
        }
        else if ((part.length() != 0u) && !(part == CStringView{ "." })) ++depth;
        begin = end + 1u;
    }
    return depth != 0u;
}

//  DXC supplies include search candidates, including ./ and parent components.
//  Collapse within the bundle, using its ASCII case rule. No terminator is
//  needed: bundle lookup and argument conversion both accept explicit lengths.
static EStatus normalise(const CStringView path, CByteBuffer& output) noexcept
{
    if (!valid_path(path)) return EStatus::invalid_input;
    if (!output.resize(path.length())) return EStatus::allocation_failed;
    std::size_t written = 0u;
    for (std::size_t begin = 0u; begin < path.length();)
    {
        std::size_t end = begin;
        while ((end < path.length()) && !separator(path.cstring()[end])) ++end;
        const auto part = CStringView{ path.cstring() + begin, end - begin };
        if (part == CStringView{ ".." })
        {
            while ((written != 0u) && (output.data()[written - 1u] != '/')) --written;
            if (written != 0u) --written;
        }
        else if ((part.length() != 0u) && !(part == CStringView{ "." }))
        {
            if (written != 0u) output.data()[written++] = '/';
            for (std::size_t i = begin; i < end; ++i)
            {
                const char c = path.cstring()[i];
                output.data()[written++] = static_cast<std::uint8_t>(((c >= 'A') && (c <= 'Z')) ? (c + ('a' - 'A')) : c);
            }
        }
        begin = end + 1u;
    }
    (void)output.set_size(written);
    return EStatus::success;
}

static bool valid_string(const CSimpleString& value, const std::size_t limit, const bool nonempty) noexcept
{
    if ((value.length() > limit) || (value.length() > INT32_MAX) || (nonempty && (value.length() == 0u)))
    {
        return false;
    }
    utf_text input{ static_cast<std::uint32_t>(value.length()), 0u, const_cast<std::uint8_t*>(value.string()) };
    while (input.offset < input.length)
    {
        unicode_t point = 0u;
        if (!read_utf8(input, point)) return false;
    }
    return true;
}

static bool identifier(const CStringView name) noexcept
{
    if (name.length() == 0u) return false;
    for (std::size_t i = 0u; i < name.length(); ++i)
    {
        const char c = name.cstring()[i];
        if (!((c == '_') || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
                ((i != 0u) && (c >= '0') && (c <= '9'))))
        {
            return false;
        }
    }
    return true;
}

static bool definitions_valid(const TMembers<SDefinition>& values, const SLimits& limits, std::size_t& bytes) noexcept
{
    if (values.count() > limits.definitions)
    {
        return false;
    }
    for (std::uint32_t i = 0u; i < values.count(); ++i)
    {
        const auto& item = values[i];
        if (!valid_string(item.name, limits.name_bytes, true) || !identifier(item.name.view()) ||
            !valid_string(item.value, limits.definition_bytes, false) ||
            ((item.name.length() >= 10u) && (std::memcmp(item.name.string(), "MV_SHADER_", 10u) == 0)))
        {
            return false;
        }
        for (std::uint32_t j = 0u; j < i; ++j)
        {
            if (item.name == values[j].name)
            {
                return false;
            }
        }
        bytes += sizeof(SDefinition) + item.name.length() + item.value.length() + 2u;
        if (bytes > limits.request_bytes)
        {
            return false;
        }
    }
    return true;
}

EStatus validate_request(const CCompileRequest& request, const SLimits& limits) noexcept
{
    if ((request.entries.count() == 0u) || (request.entries.count() > limits.entries) ||
        (request.include_roots.count() > limits.include_roots))
    {
        return EStatus::limit_exceeded;
    }
    std::size_t bytes = sizeof(CCompileRequest);
    if (!definitions_valid(request.definitions, limits, bytes))
    {
        return EStatus::invalid_input;
    }
    for (std::uint32_t i = 0u; i < request.include_roots.count(); ++i)
    {
        const auto& root = request.include_roots[i];
        if (!valid_string(root, limits.path_bytes, true) || !valid_path(root.view()))
        {
            return EStatus::invalid_input;
        }
        bytes += sizeof(CSimpleString) + root.length() + 1u;
    }
    for (std::uint32_t i = 0u; i < request.entries.count(); ++i)
    {
        const auto& entry = request.entries[i];
        if (!valid_string(entry.source_path, limits.path_bytes, true) ||
            !valid_path(entry.source_path.view()) ||
            !valid_string(entry.entry_point, limits.name_bytes, true) || !identifier(entry.entry_point.view()) ||
            (entry.stage > EStage::compute) || (entry.options.target > ETarget::vulkan_1_3) ||
            (entry.options.optimisation > EOptimisation::level_3) || (entry.options.debug > EDebug::source_debug) ||
            (entry.options.features != EFeatures::conventional_32_bit))
        {
            return EStatus::invalid_input;
        }
        bytes += sizeof(SEntry) + entry.source_path.length() + entry.entry_point.length() + 2u;
        if (!definitions_valid(entry.definitions, limits, bytes))
        {
            return EStatus::invalid_input;
        }
        if (bytes > limits.request_bytes)
        {
            return EStatus::limit_exceeded;
        }
    }
    return EStatus::success;
}

bool initialise_compiler() noexcept
{
    if (s_factory != nullptr)
    {
        return true;
    }
#if MV_PLATFORM_WINDOWS
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768u);
    if ((length == 0u) || (length >= 32768u))
    {
        return false;
    }
    std::size_t directory = length;
    while ((directory != 0u) && (executable[directory - 1u] != L'\\') && (executable[directory - 1u] != L'/')) --directory;
    static const wchar_t library_name[]{ L"dxcompiler.dll" };
    if ((directory == 0u) || (directory + (sizeof(library_name) / sizeof(wchar_t)) > 32768u)) return false;
    std::memcpy(executable + directory, library_name, sizeof(library_name));
    HMODULE library =
        LoadLibraryExW(executable, nullptr, (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
    if (library == nullptr)
    {
        return false;
    }
    const auto factory = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(library, "DxcCreateInstance"));
    HMODULE pinned = nullptr;
    const bool ready =
        ((factory != nullptr) && (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, executable, (&pinned)) != 0));
    FreeLibrary(library);
    if (!ready)
    {
        return false;
    }
#else
    char executable[4096]{};
    const auto length = readlink("/proc/self/exe", executable, (sizeof(executable) - 1u));
    if ((length <= 0) || (static_cast<std::size_t>(length) >= (sizeof(executable) - 1u)))
    {
        return false;
    }
    std::size_t directory = static_cast<std::size_t>(length);
    while ((directory != 0u) && (executable[directory - 1u] != '/')) --directory;
    static const char library_name[]{ "libdxcompiler.so" };
    if ((directory == 0u) || (directory + sizeof(library_name) > sizeof(executable))) return false;
    std::memcpy(executable + directory, library_name, sizeof(library_name));
    void* const library = dlopen(executable, ((RTLD_NOW | RTLD_LOCAL) | RTLD_NODELETE));
    if (library == nullptr)
    {
        return false;
    }
    const auto factory = reinterpret_cast<DxcCreateInstanceProc>(dlsym(library, "DxcCreateInstance"));
    dlclose(library);
    if (factory == nullptr)
    {
        return false;
    }
#endif
    s_factory = factory;
    return true;
}

class CIncludes final : public IDxcIncludeHandler
{
public:
    CIncludes(IDxcUtils& utils, const bundle::CBundleView& bundle, const SLimits& limits) noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** const result) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR const filename, IDxcBlob** const result) noexcept override;

    bool account(const CStringView path, const void* const content, const std::size_t bytes) noexcept;

    CByteBuffer identities;
    EStatus failure{ EStatus::success };

private:
    CStableStrings m_paths;
    IDxcUtils& m_utils;
    const bundle::CBundleView& m_bundle;
    const SLimits& m_limits;
    std::size_t m_bytes{ 0u };
    ULONG m_references{ 1u };   // Stack lifetime covers the synchronous Compile call.
};

CIncludes::CIncludes(IDxcUtils& utils, const bundle::CBundleView& bundle, const SLimits& limits) noexcept
    : m_utils{ utils }, m_bundle{ bundle }, m_limits{ limits }
{
}

HRESULT STDMETHODCALLTYPE CIncludes::QueryInterface(REFIID iid, void** const result) noexcept
{
    if (result == nullptr)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if ((iid == __uuidof(IUnknown)) || (iid == __uuidof(IDxcIncludeHandler)))
    {
        *result = static_cast<IDxcIncludeHandler*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CIncludes::AddRef() noexcept { return ++m_references; }

ULONG STDMETHODCALLTYPE CIncludes::Release() noexcept { return --m_references; }

HRESULT STDMETHODCALLTYPE CIncludes::LoadSource(LPCWSTR const filename, IDxcBlob** const result) noexcept
{
    if (result == nullptr)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (failure != EStatus::success)
    {
        return (failure == EStatus::allocation_failed) ? E_OUTOFMEMORY : E_FAIL;
    }
    CByteBuffer candidate, path;
    const auto converted = narrow(filename, candidate);
    if (converted != EStatus::success)
    {
        failure = converted;
        return (failure == EStatus::allocation_failed) ? E_OUTOFMEMORY : E_INVALIDARG;
    }
    const auto normalised = normalise(text(candidate), path);
    if (normalised == EStatus::allocation_failed)
    {
        failure = EStatus::allocation_failed;
        return E_OUTOFMEMORY;
    }
    if ((normalised != EStatus::success) || (path.size() > m_limits.path_bytes))
    {
        return E_INVALIDARG;
    }
    const auto found = m_bundle.lookup(text(path));
    if (found.status != bundle::ELookupStatus::success) return E_FAIL;
    if (!account(text(path), found.bytes.data(), found.bytes.bytes()))
    {
        return (failure == EStatus::allocation_failed) ? E_OUTOFMEMORY : E_FAIL;
    }
    IDxcBlobEncoding* blob = nullptr;
    const HRESULT status = m_utils.CreateBlobFromPinned(
        found.bytes.data(), (static_cast<UINT32>(found.bytes.bytes())), DXC_CP_UTF8, (&blob));
    if (status == E_OUTOFMEMORY) failure = EStatus::allocation_failed;
    *result = blob;
    return status;
}

bool CIncludes::account(const CStringView path, const void* const content, const std::size_t bytes) noexcept
{
    if ((bytes > m_limits.file_bytes) || (bytes > UINT32_MAX))
    {
        failure = EStatus::limit_exceeded;
        return false;
    }
    if (m_paths.find_id(path.string(), path.length()) != 0u)
    {
        return true;
    }
    if ((m_bytes > m_limits.source_bytes) || (bytes > (m_limits.source_bytes - m_bytes)))
    {
        failure = EStatus::limit_exceeded;
        return false;
    }

    //  A compact provenance identifier, not a cryptographic cache key.
    std::uint64_t hash = UINT64_C(14695981039346656037);
    const auto* const data = static_cast<const std::uint8_t*>(content);
    for (std::size_t i = 0u; i < bytes; ++i)
    {
        hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    }
    if ((m_paths.append(path.string(), path.length()) == 0u) ||
        !append(identities, "source=") || !append(identities, path) || !append(identities, ";bytes=") ||
        !append_number(identities, bytes) || !append(identities, ";fnv1a64=") ||
        !append_number(identities, hash) || !append(identities, "\n"))
    {
        failure = EStatus::allocation_failed;
        return false;
    }
    m_bytes += bytes;
    return true;
}

static bool allowed_capability(const std::uint32_t value) noexcept
{
    switch (value)
    {
    case 0u:
    case 1u:
    case 2u:
    case 3u:
    case 21u:
    case 23u:
    case 24u:
    case 25u:
    case 27u:
    case 28u:
    case 29u:
    case 30u:
    case 31u:
    case 32u:
    case 33u:
    case 34u:
    case 35u:
    case 40u:
    case 42u:
    case 43u:
    case 44u:
    case 45u:
    case 46u:
    case 47u:
    case 48u:
    case 49u:
    case 50u:
    case 51u:
    case 52u:
    case 55u:
    case 56u:
        return true;
    default:
        return false;
    }
}

static EStatus inspect_spirv(const IDxcBlob& blob, CByteBuffer& requirements) noexcept
{
    //  IDxcBlob's accessors are not const in the upstream COM contract.
    auto& mutable_blob = const_cast<IDxcBlob&>(blob);
    const std::size_t bytes = mutable_blob.GetBufferSize();
    if ((bytes < 20u) || ((bytes % 4u) != 0u))
    {
        return EStatus::unsupported_feature;
    }
    const auto* const words = static_cast<const std::uint32_t*>(mutable_blob.GetBufferPointer());
    if (words[0] != 0x07230203u)
    {
        return EStatus::unsupported_feature;
    }
    for (std::size_t i = 5u; i < (bytes / 4u);)
    {
        const std::uint32_t length = words[i] >> 16u;
        const std::uint32_t opcode = words[i] & 0xffffu;
        if ((length == 0u) || (length > ((bytes / 4u) - i)))
        {
            return EStatus::unsupported_feature;
        }
        if (opcode == 17u)   // OpCapability
        {
            if ((length != 2u) || !allowed_capability(words[i + 1u]))
            {
                return EStatus::unsupported_feature;
            }
            if (!append(requirements, "capability=") || !append_number(requirements, words[i + 1u]) ||
                !append(requirements, "\n")) return EStatus::allocation_failed;
        }
        if (opcode == 10u)   // OpExtension
        {
            if (length < 2u)
            {
                return EStatus::unsupported_feature;
            }
            const char* const value = reinterpret_cast<const char*>((words + i) + 1u);
            const auto* const end = static_cast<const char*>(std::memchr(value, 0, ((length - 1u) * 4u)));
            if (end == nullptr)
            {
                return EStatus::unsupported_feature;
            }
            const CStringView extension{ value, static_cast<std::size_t>(end - value) };
            if (!(extension == CStringView{ "SPV_KHR_non_semantic_info" }) && !(extension == CStringView{ "SPV_GOOGLE_hlsl_functionality1" }) &&
                !(extension == CStringView{ "SPV_GOOGLE_user_type" }))
            {
                return EStatus::unsupported_feature;
            }
            if (!append(requirements, extension) || !append(requirements, "\n")) return EStatus::allocation_failed;
        }

        // PushConstant storage and hidden append/consume counter bindings.
        if ((opcode == 32u) && ((length < 4u) || (words[i + 2u] == 9u)))
        {
            return EStatus::unsupported_feature;
        }
        if (((opcode == 71u) || (opcode == 332u)) && (length >= 3u) && (words[i + 2u] == 5634u))
        {
            return EStatus::unsupported_feature;
        }
        i += length;
    }
    return EStatus::success;
}

static void diagnostic(CShaderCompileReport& report, const char* const bytes, const std::size_t size, const SLimits& limits) noexcept
{
    std::size_t retained = std::min(size, limits.diagnostic_bytes);
    if (retained < size)
    {
        while ((retained != 0u) && ((static_cast<unsigned char>(bytes[retained]) & 0xc0u) == 0x80u))
        {
            --retained;
        }
        report.diagnostics_truncated = true;
    }
    if (!report.diagnostics.set(bytes, retained))
    {
        report.status = EStatus::allocation_failed;
    }
}

static void failure(CShaderCompileReport& report, const EStatus status, const char* const message, const SLimits& limits) noexcept
{
    report.status = status;
    report.shader = {};
    diagnostic(report, message, std::strlen(message), limits);
}

static EStatus api_failure(const HRESULT status, const EStatus fallback = EStatus::compiler_failed,
    const EStatus include_status = EStatus::success) noexcept
{
    if ((status == E_OUTOFMEMORY) || (include_status == EStatus::allocation_failed))
    {
        return EStatus::allocation_failed;
    }
    return (include_status != EStatus::success) ? include_status : fallback;
}

void compile(const CCompileRequest& request, const std::uint32_t member, const bundle::CBundleView& sources,
    const SLimits& limits, CShaderCompileReport& report) noexcept
{
    report = {};
    report.member = member;
    if ((member >= request.entries.count()) || !sources.is_ready())
    {
        failure(report, EStatus::invalid_input, "Invalid shader member or source bundle", limits);
        return;
    }
    const auto& entry = request.entries[member];
    report.stage = entry.stage;
    report.options = entry.options;
    const auto validity = validate_request(request, limits);
    if (validity != EStatus::success)
    {
        failure(report, validity, "Invalid shader request or request limit exceeded", limits);
        return;
    }
    if (!report.source_path.set(entry.source_path.string(), entry.source_path.length()) ||
        !report.entry_point.set(entry.entry_point.string(), entry.entry_point.length()))
    {
        report.status = EStatus::allocation_failed;
        return;
    }
    if (s_factory == nullptr)
    {
        failure(report, EStatus::compiler_unavailable, "Pinned DXC compiler is unavailable", limits);
        return;
    }
    TCom<IDxcUtils> utils;
    TCom<IDxcCompiler3> compiler;
    const HRESULT utils_created = s_factory(CLSID_DxcUtils, __uuidof(IDxcUtils), (reinterpret_cast<void**>(&utils.value)));
    if (FAILED(utils_created) || (utils.value == nullptr))
    {
        failure(report, api_failure(utils_created), "DXC utility creation failed", limits);
        return;
    }
    const HRESULT compiler_created = s_factory(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), (reinterpret_cast<void**>(&compiler.value)));
    if (FAILED(compiler_created) || (compiler.value == nullptr))
    {
        failure(report, api_failure(compiler_created), "DXC compiler creation failed", limits);
        return;
    }
    TCom<IDxcVersionInfo2> version;
    UINT32 major = 0u, minor = 0u, commit_count = 0u;
    STaskString commit;
    const HRESULT version_queried = compiler->QueryInterface(__uuidof(IDxcVersionInfo2), (reinterpret_cast<void**>(&version.value)));
    if (FAILED(version_queried) || (version.value == nullptr))
    {
        failure(report, api_failure(version_queried, EStatus::compiler_unavailable), "DXC version interface unavailable", limits);
        return;
    }
    const HRESULT version_read = version->GetVersion((&major), (&minor));
    if (FAILED(version_read))
    {
        failure(report, api_failure(version_read, EStatus::compiler_unavailable), "DXC version unavailable", limits);
        return;
    }
    const HRESULT commit_read = version->GetCommitInfo((&commit_count), (&commit.value));
    if (FAILED(commit_read) || (commit.value == nullptr))
    {
        failure(report, api_failure(commit_read, EStatus::compiler_unavailable), "DXC commit information unavailable", limits);
        return;
    }
    if ((major != 1u) || (minor != 9u) || (std::strncmp(commit.value, "01b62ad4", 8u) != 0))
    {
        failure(report, EStatus::compiler_unavailable, "DXC does not match the pinned compiler revision", limits);
        return;
    }
    CIncludes includes{ (*utils.value), sources, limits };
    CByteBuffer path;
    const auto normalised = normalise(entry.source_path.view(), path);
    if (normalised != EStatus::success)
    {
        failure(report, normalised, "Could not normalise shader path", limits);
        return;
    }
    const auto source = sources.lookup(text(path));
    if (source.status != bundle::ELookupStatus::success)
    {
        failure(report, EStatus::invalid_input, "Shader source not found in bundle", limits);
        return;
    }
    if (!includes.account(text(path), source.bytes.data(), source.bytes.bytes()))
    {
        failure(report, includes.failure, "Could not account for shader source", limits);
        return;
    }
    static const wchar_t* const profiles[]{ L"vs_6_0", L"hs_6_0", L"ds_6_0", L"gs_6_0", L"ps_6_0", L"cs_6_0" };
    static const wchar_t* const targets[]{ L"-fspv-target-env=vulkan1.1", L"-fspv-target-env=vulkan1.2", L"-fspv-target-env=vulkan1.3" };
    static const wchar_t* const optimisations[]{ L"-Od", L"-O1", L"-O2", L"-O3" };

    //  Argument construction latches its first error; finish checks the batch.
    CArguments arguments;
    (void)arguments.add(text(path));
    (void)arguments.add(L"-E");
    (void)arguments.add(entry.entry_point.view());
    const wchar_t* const fixed[]{ L"-T", profiles[static_cast<unsigned int>(entry.stage)], L"-spirv", L"-HV",
        L"2021", L"-Zpr", L"-fspv-reflect", targets[static_cast<unsigned int>(entry.options.target)],
        optimisations[static_cast<unsigned int>(entry.options.optimisation)],
        L"-fspv-extension=SPV_GOOGLE_hlsl_functionality1", L"-fspv-extension=SPV_GOOGLE_user_type", L"-D",
        L"MV_SHADER_SPIRV=1", L"-D", L"MV_SHADER_SUPPORT_VERSION=1" };
    for (const auto argument : fixed) (void)arguments.add(argument);
    if (entry.options.warnings_as_errors) (void)arguments.add(L"-WX");
    if (entry.options.debug == EDebug::source_and_lines) (void)arguments.add(L"-Zi");
    if (entry.options.debug == EDebug::source_debug)
    {
        (void)arguments.add(L"-fspv-debug=vulkan-with-source");
        (void)arguments.add(L"-fspv-extension=SPV_KHR_non_semantic_info");
    }
    for (std::uint32_t i = 0u; i < request.include_roots.count(); ++i)
    {
        (void)arguments.add(L"-I");
        (void)arguments.add(request.include_roots[i].view());
    }
    CByteBuffer provenance;
    if (!append(provenance, "DXC v1.9.2609; HLSL 2021; SM 6.0; row-major; vector-relaxed std140/std430\n") ||
        !append(provenance, "compiler-commit=") || !append(provenance, commit.value) ||
        !append(provenance, ";count=") || !append_number(provenance, commit_count) ||
        !append(provenance, "\ndefine=MV_SHADER_SPIRV=1\ndefine=MV_SHADER_SUPPORT_VERSION=1\n"))
    {
        failure(report, EStatus::allocation_failed, "Could not retain compiler provenance", limits);
        return;
    }
    auto add_definition = [&](const SDefinition& definition) noexcept {
        CByteBuffer value;
        return append(value, definition.name.view()) && append(value, "=") && append(value, definition.value.view()) &&
               arguments.add(L"-D") && arguments.add(text(value)) &&
               append(provenance, "define=") && append(provenance, text(value)) && append(provenance, "\n");
    };
    for (std::uint32_t i = 0u; i < request.definitions.count(); ++i)
    {
        bool overridden = false;
        for (std::uint32_t j = 0u; j < entry.definitions.count(); ++j)
        {
            overridden = (overridden || (request.definitions[i].name == entry.definitions[j].name));
        }
        if (!overridden && !add_definition(request.definitions[i]))
        {
            failure(report, EStatus::allocation_failed, "Could not prepare shader definition", limits);
            return;
        }
    }
    for (std::uint32_t i = 0u; i < entry.definitions.count(); ++i)
    {
        if (!add_definition(entry.definitions[i]))
        {
            failure(report, EStatus::allocation_failed, "Could not prepare shader definition", limits);
            return;
        }
    }
    if (!arguments.finish())
    {
        failure(report, arguments.status, "Could not prepare compiler arguments", limits);
        return;
    }
    auto& pointers = arguments.pointers;
    const DxcBuffer input{ source.bytes.data(), source.bytes.bytes(), DXC_CP_UTF8 };
    CByteBuffer preliminary;

    //  DXC's embedded-source routes can drop successful preprocessor
    //  warnings. Preserve them through DXC's own -P action.
    if (entry.options.debug != EDebug::none)
    {
        TCom<IDxcResult> preprocessed;
        MV_LOG(info, "Rendering: DXC preprocess entering, member {}", member);
        const HRESULT called = compiler->Compile((&input), pointers.data(), (static_cast<UINT32>(pointers.size())),
            (&includes), __uuidof(IDxcResult), (reinterpret_cast<void**>(&preprocessed.value)));
        MV_LOG(info, "Rendering: DXC preprocess returned, member {}, HRESULT {}", member, static_cast<std::int32_t>(called));
        HRESULT status = E_FAIL;
        HRESULT inspected = called;
        TCom<IDxcBlobUtf8> errors;
        if (FAILED(called) || (preprocessed.value == nullptr) || FAILED(inspected = preprocessed->GetStatus(&status)) ||
            FAILED(inspected = preprocessed->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), (reinterpret_cast<void**>(&errors.value)), nullptr)))
        {
            failure(report, api_failure(inspected, EStatus::compiler_failed, includes.failure),
                "DXC preprocessing failed at its API boundary", limits);
            return;
        }
        if (errors.value != nullptr)
        {
            diagnostic(report, errors->GetStringPointer(), errors->GetStringLength(), limits);
            if (!append(preliminary, report.diagnostics.view()))
            {
                report.status = EStatus::allocation_failed;
            }
            if (report.status == EStatus::allocation_failed)
            {
                return;
            }
        }
        if (FAILED(status))
        {
            report.status = api_failure(status, EStatus::compilation_failed, includes.failure);
            if (!append(provenance, text(includes.identities)) ||
                !report.provenance.set(provenance.data(), provenance.size()))
            {
                report.status = EStatus::allocation_failed;
            }
            return;
        }
    }
    TCom<IDxcResult> result;
    MV_LOG(info, "Rendering: DXC compile entering, member {}", member);
    const HRESULT called = compiler->Compile((&input), pointers.data(), (static_cast<UINT32>(pointers.size() - 1u)),
        (&includes), __uuidof(IDxcResult), (reinterpret_cast<void**>(&result.value)));
    MV_LOG(info, "Rendering: DXC compile returned, member {}, HRESULT {}", member, (static_cast<std::int32_t>(called)));
    if (FAILED(called) || (result.value == nullptr))
    {
        failure(report, api_failure(called, EStatus::compiler_failed, includes.failure),
            "DXC invocation failed at its API boundary", limits);
        return;
    }
    HRESULT compiled = E_FAIL;
    const HRESULT status_read = result->GetStatus(&compiled);
    if (FAILED(status_read))
    {
        failure(report, api_failure(status_read, EStatus::compiler_failed, includes.failure), "DXC result status unavailable", limits);
        return;
    }
    report.status = SUCCEEDED(compiled) ? EStatus::success :
        ((compiled == E_OUTOFMEMORY) ? EStatus::allocation_failed : EStatus::compilation_failed);
    TCom<IDxcBlobUtf8> errors;
    const HRESULT diagnostics_read = result->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), (reinterpret_cast<void**>(&errors.value)), nullptr);
    if (FAILED(diagnostics_read))
    {
        failure(report, api_failure(diagnostics_read, EStatus::compiler_failed, includes.failure), "DXC diagnostics unavailable", limits);
        return;
    }
    if (errors.value != nullptr)
    {
        auto retained = std::min<std::size_t>(errors->GetStringLength(), limits.diagnostic_bytes);
        if (retained < errors->GetStringLength())
        {
            while ((retained != 0u) && ((static_cast<unsigned char>(errors->GetStringPointer()[retained]) & 0xc0u) == 0x80u))
            {
                --retained;
            }
        }
        if (!append(preliminary, CStringView{ errors->GetStringPointer(), retained }))
        {
            failure(report, EStatus::allocation_failed, "Could not retain compiler diagnostics", limits);
            return;
        }
        report.diagnostics_truncated = (report.diagnostics_truncated || (retained < errors->GetStringLength()));
    }
    diagnostic(report, text(preliminary).cstring(), preliminary.size(), limits);
    if (!append(provenance, text(includes.identities)) || !report.provenance.set(provenance.data(), provenance.size()))
    {
        report.status = EStatus::allocation_failed;
    }
    //  A later report-allocation failure takes precedence over an include limit.
    if (report.status == EStatus::allocation_failed)
    {
        return;
    }
    if (includes.failure != EStatus::success)
    {
        failure(report, includes.failure, "Include source limit or allocation failure", limits);
        return;
    }
    if (report.status != EStatus::success)
    {
        return;
    }
    TCom<IDxcBlob> object;
    const HRESULT object_read = result->GetOutput(DXC_OUT_OBJECT, __uuidof(IDxcBlob), (reinterpret_cast<void**>(&object.value)), nullptr);
    if (FAILED(object_read) || (object.value == nullptr))
    {
        failure(report, api_failure(object_read), "DXC returned no compiled artifact", limits);
        return;
    }
    if (object->GetBufferSize() > limits.artifact_bytes)
    {
        failure(report, EStatus::limit_exceeded, "Compiled shader exceeds artifact limit", limits);
        return;
    }
    CByteBuffer requirements;
    const auto inspected = inspect_spirv((*object.value), requirements);
    if (inspected != EStatus::success)
    {
        failure(report, inspected, "Could not retain or validate compiled shader requirements", limits);
        return;
    }
    if (!report.shader.requirements.set(requirements.data(), requirements.size()) || !report.shader.bytes.resize(object->GetBufferSize(), 16u))
    {
        failure(report, EStatus::allocation_failed, "Could not retain compiled shader", limits);
        return;
    }
    std::memcpy(report.shader.bytes.data(), object->GetBufferPointer(), object->GetBufferSize());
    report.shader.stage = entry.stage;
    report.shader.target = entry.options.target;
}

}   //  namespace rendering::shader_compiler
