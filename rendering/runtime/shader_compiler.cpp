
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_compiler.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Exception-enabled DXC boundary. STL scratch and COM objects never escape.

//  This translation unit is built with /EHsc. Other rendering sources retain
//  their exception-free STL configuration; all adapter scratch is local here.
#if defined(_MSC_VER)
#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif

#include "rendering/runtime/shader_compiler.hpp"
#include "debug/macros.hpp"
#include <string>
#include <vector>
#include <set>
#include <codecvt>
#include <locale>
#include <exception>
#include <stdexcept>

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

static std::string text(const CSimpleString& value)
{
    return std::string{ (value.cstring() != nullptr) ? value.cstring() : "", value.length() };
}

static std::wstring wide(const std::string& value)
{
#if MV_PLATFORM_WINDOWS
    if (value.empty())
    {
        return {};
    }
    const int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (static_cast<int>(value.size())), nullptr, 0);
    if (count <= 0)
    {
        throw std::invalid_argument("Invalid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (static_cast<int>(value.size())), result.data(), count) != count)
    {
        throw std::invalid_argument("Invalid UTF-8");
    }
    return result;
#else
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(value);
#endif
}

static std::string narrow(const wchar_t* const value)
{
#if MV_PLATFORM_WINDOWS
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0)
    {
        throw std::invalid_argument("Invalid UTF-16");
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), count, nullptr, nullptr) != count)
    {
        throw std::invalid_argument("Invalid UTF-16");
    }
    result.resize(static_cast<std::size_t>(count - 1));
    return result;
#else
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(value);
#endif
}

//  DXC supplies include search candidates, including ./ and parent components.
//  Reject external paths; collapse within the bundle, using its ASCII case rule.
static bool normalise(std::string path, std::string& output)
{
    for (char& c : path)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    if (path.empty() || (path.front() == '/') || (path.find(':') != std::string::npos) ||
        (path.find('\0') != std::string::npos))
    {
        return false;
    }
    std::vector<std::string> parts;
    std::size_t begin = 0u;
    while (begin <= path.size())
    {
        const auto end = path.find('/', begin);
        auto part = path.substr(begin, (end == std::string::npos) ? end : (end - begin));
        if (part == "..")
        {
            if (parts.empty())
            {
                return false;
            }
            parts.pop_back();
        }
        else if (!part.empty() && (part != "."))
        {
            for (char& c : part)
            {
                if ((c >= 'A') && (c <= 'Z'))
                {
                    c += 'a' - 'A';
                }
            }
            parts.push_back(std::move(part));
        }
        if (end == std::string::npos)
        {
            break;
        }
        begin = end + 1u;
    }
    output.clear();
    for (const auto& part : parts)
    {
        if (!output.empty())
        {
            output += '/';
        }
        output += part;
    }
    return !output.empty();
}

static bool valid_string(const CSimpleString& value, const std::size_t limit, const bool nonempty)
{
    if ((value.length() > limit) || (value.length() > INT32_MAX) || (nonempty && (value.length() == 0u)))
    {
        return false;
    }
    const auto input = text(value);
    if (input.find('\0') != std::string::npos)
    {
        return false;
    }
    (void)wide(input);
    return true;
}

static bool identifier(const std::string& name)
{
    if (name.empty())
    {
        return false;
    }
    for (std::size_t i = 0u; i < name.size(); ++i)
    {
        const char c = name[i];
        if (!((c == '_') || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
                ((i != 0u) && (c >= '0') && (c <= '9'))))
        {
            return false;
        }
    }
    return true;
}

static bool definitions_valid(const TMembers<SDefinition>& values, const SLimits& limits, std::size_t& bytes)
{
    if (values.count() > limits.definitions)
    {
        return false;
    }
    for (std::uint32_t i = 0u; i < values.count(); ++i)
    {
        const auto& item = values[i];
        if (!valid_string(item.name, limits.name_bytes, true) || !identifier(text(item.name)) ||
            !valid_string(item.value, limits.definition_bytes, false) ||
            (text(item.name).compare(0u, 10u, "MV_SHADER_") == 0))
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
    try
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
        std::string path;
        for (std::uint32_t i = 0u; i < request.include_roots.count(); ++i)
        {
            const auto& root = request.include_roots[i];
            if (!valid_string(root, limits.path_bytes, true) || !normalise(text(root), path))
            {
                return EStatus::invalid_input;
            }
            bytes += sizeof(CSimpleString) + root.length() + 1u;
        }
        for (std::uint32_t i = 0u; i < request.entries.count(); ++i)
        {
            const auto& entry = request.entries[i];
            if (!valid_string(entry.source_path, limits.path_bytes, true) ||
                !normalise(text(entry.source_path), path) ||
                !valid_string(entry.entry_point, limits.name_bytes, true) || !identifier(text(entry.entry_point)) ||
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
    catch (const std::bad_alloc&)
    {
        return EStatus::allocation_failed;
    }
    catch (...)
    {
        return EStatus::invalid_input;
    }
}

bool initialise_compiler() noexcept
{
    if (s_factory != nullptr)
    {
        return true;
    }
    try
    {
#if MV_PLATFORM_WINDOWS
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768u);
        if ((length == 0u) || (length >= 32768u))
        {
            return false;
        }
        std::wstring path{ executable, length };
        path.resize(path.find_last_of(L"\\/") + 1u);
        path += L"dxcompiler.dll";
        HMODULE library =
            LoadLibraryExW(path.c_str(), nullptr, (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
        if (library == nullptr)
        {
            return false;
        }
        const auto factory = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(library, "DxcCreateInstance"));
        HMODULE pinned = nullptr;
        const bool ready =
            ((factory != nullptr) && (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, path.c_str(), (&pinned)) != 0));
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
        std::string path{ executable, (static_cast<std::size_t>(length)) };
        path.resize(path.find_last_of('/') + 1u);
        path += "libdxcompiler.so";
        void* const library = dlopen(path.c_str(), ((RTLD_NOW | RTLD_LOCAL) | RTLD_NODELETE));
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
    catch (...)
    {
        return false;
    }
}

class CIncludes final : public IDxcIncludeHandler
{
public:
    CIncludes(IDxcUtils& utils, const bundle::CBundleView& bundle, const SLimits& limits)
        : m_utils{ utils }, m_bundle{ bundle }, m_limits{ limits }
    {
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** const result) override
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
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_references; }
    ULONG STDMETHODCALLTYPE Release() override { return --m_references; }
    HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR const filename, IDxcBlob** const result) override
    {
        if (result == nullptr)
        {
            return E_POINTER;
        }
        *result = nullptr;
        try
        {
            std::string path;
            if ((filename == nullptr) || !normalise(narrow(filename), path) || (path.size() > m_limits.path_bytes))
            {
                return E_INVALIDARG;
            }
            const auto found = m_bundle.lookup(CStringView{ path.c_str(), path.size() });
            if (found.status != bundle::ELookupStatus::success)
            {
                return E_FAIL;
            }
            if (!account(path, found.bytes.data(), found.bytes.bytes()))
            {
                return E_FAIL;
            }
            IDxcBlobEncoding* blob = nullptr;
            const HRESULT status = m_utils.CreateBlobFromPinned(
                found.bytes.data(), (static_cast<UINT32>(found.bytes.bytes())), DXC_CP_UTF8, (&blob));
            if (status == E_OUTOFMEMORY)
            {
                failure = EStatus::allocation_failed;
            }
            *result = blob;
            return status;
        }
        catch (const std::bad_alloc&)
        {
            failure = EStatus::allocation_failed;
            return E_OUTOFMEMORY;
        }
        catch (...)
        {
            failure = EStatus::invalid_input;
            return E_INVALIDARG;
        }
    }
    bool account(const std::string& path, const void* const content, const std::size_t bytes)
    {
        if ((bytes > m_limits.file_bytes) || (bytes > UINT32_MAX))
        {
            failure = EStatus::limit_exceeded;
            return false;
        }
        if (paths.find(path) != paths.end())
        {
            return true;
        }
        if ((m_bytes > m_limits.source_bytes) || (bytes > (m_limits.source_bytes - m_bytes)))
        {
            failure = EStatus::limit_exceeded;
            return false;
        }
        paths.insert(path);
        //  A compact provenance identifier, not a cryptographic cache key.
        std::uint64_t hash = UINT64_C(14695981039346656037);
        const auto* const data = static_cast<const std::uint8_t*>(content);
        for (std::size_t i = 0u; i < bytes; ++i)
        {
            hash = (hash ^ data[i]) * UINT64_C(1099511628211);
        }
        identities += "source=" + path + ";bytes=" + std::to_string(bytes) + ";fnv1a64=" + std::to_string(hash) + "\n";
        m_bytes += bytes;
        return true;
    }
    std::set<std::string> paths;
    std::string identities;
    EStatus failure{ EStatus::success };

private:
    IDxcUtils& m_utils;
    const bundle::CBundleView& m_bundle;
    const SLimits& m_limits;
    std::size_t m_bytes{ 0u };
    ULONG m_references{ 1u };   // Stack lifetime covers the synchronous Compile call.
};

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

static bool inspect_spirv(const IDxcBlob& blob, std::string& requirements)
{
    //  IDxcBlob's accessors are not const in the upstream COM contract.
    auto& mutable_blob = const_cast<IDxcBlob&>(blob);
    const std::size_t bytes = mutable_blob.GetBufferSize();
    if ((bytes < 20u) || ((bytes % 4u) != 0u))
    {
        return false;
    }
    const auto* const words = static_cast<const std::uint32_t*>(mutable_blob.GetBufferPointer());
    if (words[0] != 0x07230203u)
    {
        return false;
    }
    for (std::size_t i = 5u; i < (bytes / 4u);)
    {
        const std::uint32_t length = words[i] >> 16u;
        const std::uint32_t opcode = words[i] & 0xffffu;
        if ((length == 0u) || (length > ((bytes / 4u) - i)))
        {
            return false;
        }
        if (opcode == 17u)   // OpCapability
        {
            if ((length != 2u) || !allowed_capability(words[i + 1u]))
            {
                return false;
            }
            requirements += "capability=" + std::to_string(words[i + 1u]) + "\n";
        }
        if (opcode == 10u)   // OpExtension
        {
            if (length < 2u)
            {
                return false;
            }
            const char* const value = reinterpret_cast<const char*>((words + i) + 1u);
            const auto* const end = static_cast<const char*>(std::memchr(value, 0, ((length - 1u) * 4u)));
            if (end == nullptr)
            {
                return false;
            }
            const std::string extension{ value, end };
            if ((extension != "SPV_KHR_non_semantic_info") && (extension != "SPV_GOOGLE_hlsl_functionality1") &&
                (extension != "SPV_GOOGLE_user_type"))
            {
                return false;
            }
            requirements += extension + "\n";
        }
        // PushConstant storage and hidden append/consume counter bindings.
        if ((opcode == 32u) && ((length < 4u) || (words[i + 2u] == 9u)))
        {
            return false;
        }
        if (((opcode == 71u) || (opcode == 332u)) && (length >= 3u) && (words[i + 2u] == 5634u))
        {
            return false;
        }
        i += length;
    }
    return true;
}

static void diagnostic(
    CShaderCompileReport& report, const char* const bytes, const std::size_t size, const SLimits& limits) noexcept
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

static void failure(
    CShaderCompileReport& report, const EStatus status, const char* const message, const SLimits& limits) noexcept
{
    report.status = status;
    report.shader = {};
    diagnostic(report, message, std::strlen(message), limits);
}

void compile(const CCompileRequest& request, const std::uint32_t member, const bundle::CBundleView& sources,
    const SLimits& limits, CShaderCompileReport& report) noexcept
{
    report = {};
    report.member = member;
    try
    {
        if ((member >= request.entries.count()) || !sources.is_ready())
        {
            failure(report, EStatus::invalid_input, "Invalid shader member or source bundle", limits);
            return;
        }
        const auto& entry = request.entries[member];
        report.stage = entry.stage;
        report.options = entry.options;
        if (!report.source_path.set(entry.source_path.string(), entry.source_path.length()) ||
            !report.entry_point.set(entry.entry_point.string(), entry.entry_point.length()))
        {
            report.status = EStatus::allocation_failed;
            return;
        }
        const auto validity = validate_request(request, limits);
        if (validity != EStatus::success)
        {
            failure(report, validity, "Invalid shader request or request limit exceeded", limits);
            return;
        }
        if (s_factory == nullptr)
        {
            failure(report, EStatus::compiler_unavailable, "Pinned DXC compiler is unavailable", limits);
            return;
        }
        TCom<IDxcUtils> utils;
        TCom<IDxcCompiler3> compiler;
        if (FAILED(s_factory(CLSID_DxcUtils, __uuidof(IDxcUtils), (reinterpret_cast<void**>(&utils.value)))) ||
            (utils.value == nullptr) ||
            FAILED(s_factory(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), (reinterpret_cast<void**>(&compiler.value)))) ||
            (compiler.value == nullptr))
        {
            failure(report, EStatus::compiler_failed, "DXC interface creation failed", limits);
            return;
        }
        TCom<IDxcVersionInfo2> version;
        UINT32 major = 0u, minor = 0u, commit_count = 0u;
        STaskString commit;
        if (FAILED(compiler->QueryInterface(__uuidof(IDxcVersionInfo2), (reinterpret_cast<void**>(&version.value)))) ||
            (version.value == nullptr) || FAILED(version->GetVersion((&major), (&minor))) ||
            FAILED(version->GetCommitInfo((&commit_count), (&commit.value))) || (commit.value == nullptr) ||
            (major != 1u) || (minor != 9u) || (std::strncmp(commit.value, "01b62ad4", 8u) != 0))
        {
            failure(report, EStatus::compiler_unavailable, "DXC does not match the pinned compiler revision", limits);
            return;
        }
        CIncludes includes{ (*utils.value), sources, limits };
        std::string path;
        if (!normalise(text(entry.source_path), path))
        {
            failure(report, EStatus::invalid_input, "Invalid shader path", limits);
            return;
        }
        const auto source = sources.lookup(CStringView{ path.c_str(), path.size() });
        if (source.status != bundle::ELookupStatus::success)
        {
            failure(report, EStatus::invalid_input, "Shader source not found in bundle", limits);
            return;
        }
        if (!includes.account(path, source.bytes.data(), source.bytes.bytes()))
        {
            failure(report, includes.failure, "Shader source limit exceeded", limits);
            return;
        }
        static const wchar_t* const profiles[]{ L"vs_6_0", L"hs_6_0", L"ds_6_0", L"gs_6_0", L"ps_6_0", L"cs_6_0" };
        static const wchar_t* const targets[]{ L"-fspv-target-env=vulkan1.1", L"-fspv-target-env=vulkan1.2",
            L"-fspv-target-env=vulkan1.3" };
        static const wchar_t* const optimisations[]{ L"-Od", L"-O1", L"-O2", L"-O3" };
        std::vector<std::wstring> arguments{ wide(path), L"-E", wide(text(entry.entry_point)), L"-T",
            profiles[static_cast<unsigned int>(entry.stage)], L"-spirv", L"-HV", L"2021", L"-Zpr", L"-fspv-reflect",
            targets[static_cast<unsigned int>(entry.options.target)],
            optimisations[static_cast<unsigned int>(entry.options.optimisation)],
            L"-fspv-extension=SPV_GOOGLE_hlsl_functionality1", L"-fspv-extension=SPV_GOOGLE_user_type", L"-D",
            L"MV_SHADER_SPIRV=1", L"-D", L"MV_SHADER_SUPPORT_VERSION=1" };
        if (entry.options.warnings_as_errors)
        {
            arguments.push_back(L"-WX");
        }
        if (entry.options.debug == EDebug::source_and_lines)
        {
            arguments.push_back(L"-Zi");
        }
        if (entry.options.debug == EDebug::source_debug)
        {
            arguments.push_back(L"-fspv-debug=vulkan-with-source");
            arguments.push_back(L"-fspv-extension=SPV_KHR_non_semantic_info");
        }
        for (std::uint32_t i = 0u; i < request.include_roots.count(); ++i)
        {
            arguments.push_back(L"-I");
            arguments.push_back(wide(text(request.include_roots[i])));
        }
        std::string provenance = "DXC v1.9.2609; HLSL 2021; SM 6.0; row-major; vector-relaxed std140/std430\n";
        provenance +=
            "compiler-commit=" + std::string{ commit.value } + ";count=" + std::to_string(commit_count) + "\n";
        provenance += "define=MV_SHADER_SPIRV=1\ndefine=MV_SHADER_SUPPORT_VERSION=1\n";
        auto add_definition = [&](const SDefinition& definition) {
            const auto value = text(definition.name) + "=" + text(definition.value);
            arguments.push_back(L"-D");
            arguments.push_back(wide(value));
            provenance += "define=" + value + "\n";
        };
        for (std::uint32_t i = 0u; i < request.definitions.count(); ++i)
        {
            bool overridden = false;
            for (std::uint32_t j = 0u; j < entry.definitions.count(); ++j)
            {
                overridden = (overridden || (request.definitions[i].name == entry.definitions[j].name));
            }
            if (!overridden)
            {
                add_definition(request.definitions[i]);
            }
        }
        for (std::uint32_t i = 0u; i < entry.definitions.count(); ++i)
        {
            add_definition(entry.definitions[i]);
        }
        std::vector<LPCWSTR> pointers;
        for (const auto& argument : arguments)
        {
            pointers.push_back(argument.c_str());
        }
        const DxcBuffer input{ source.bytes.data(), source.bytes.bytes(), DXC_CP_UTF8 };
        std::string preliminary;
        //  DXC's embedded-source routes can drop successful preprocessor
        //  warnings. Preserve them through DXC's own -P action.
        if (entry.options.debug != EDebug::none)
        {
            pointers.push_back(L"-P");
            TCom<IDxcResult> preprocessed;
            MV_LOG(info, "Rendering: DXC preprocess entering, member {}", member);
            const HRESULT called = compiler->Compile((&input), pointers.data(), (static_cast<UINT32>(pointers.size())),
                (&includes), __uuidof(IDxcResult), (reinterpret_cast<void**>(&preprocessed.value)));
            MV_LOG(info, "Rendering: DXC preprocess returned, member {}, HRESULT {}", member,
                (static_cast<std::int32_t>(called)));
            pointers.pop_back();
            HRESULT status = E_FAIL;
            TCom<IDxcBlobUtf8> errors;
            if (FAILED(called) || (preprocessed.value == nullptr) || FAILED(preprocessed->GetStatus(&status)) ||
                FAILED(preprocessed->GetOutput(
                    DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), (reinterpret_cast<void**>(&errors.value)), nullptr)))
            {
                failure(report, EStatus::compiler_failed, "DXC preprocessing failed at its API boundary", limits);
                return;
            }
            if (errors.value != nullptr)
            {
                diagnostic(report, errors->GetStringPointer(), errors->GetStringLength(), limits);
                preliminary = text(report.diagnostics);
                if (report.status == EStatus::allocation_failed)
                {
                    return;
                }
            }
            if (FAILED(status))
            {
                report.status =
                    (includes.failure != EStatus::success)
                        ? includes.failure
                        : ((status == E_OUTOFMEMORY) ? EStatus::allocation_failed : EStatus::compilation_failed);
                provenance += includes.identities;
                if (!report.provenance.set(provenance.data(), provenance.size()))
                {
                    report.status = EStatus::allocation_failed;
                }
                return;
            }
        }
        TCom<IDxcResult> result;
        MV_LOG(info, "Rendering: DXC compile entering, member {}", member);
        const HRESULT called = compiler->Compile((&input), pointers.data(), (static_cast<UINT32>(pointers.size())),
            (&includes), __uuidof(IDxcResult), (reinterpret_cast<void**>(&result.value)));
        MV_LOG(
            info, "Rendering: DXC compile returned, member {}, HRESULT {}", member, (static_cast<std::int32_t>(called)));
        if (FAILED(called) || (result.value == nullptr))
        {
            failure(report, EStatus::compiler_failed, "DXC invocation failed at its API boundary", limits);
            return;
        }
        HRESULT compiled = E_FAIL;
        if (FAILED(result->GetStatus(&compiled)))
        {
            failure(report, EStatus::compiler_failed, "DXC result status unavailable", limits);
            return;
        }
        report.status = SUCCEEDED(compiled)
                            ? EStatus::success
                            : ((compiled == E_OUTOFMEMORY) ? EStatus::allocation_failed : EStatus::compilation_failed);
        TCom<IDxcBlobUtf8> errors;
        if (FAILED(result->GetOutput(
                DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), (reinterpret_cast<void**>(&errors.value)), nullptr)))
        {
            failure(report, EStatus::compiler_failed, "DXC diagnostics unavailable", limits);
            return;
        }
        if (errors.value != nullptr)
        {
            auto retained = std::min<std::size_t>(errors->GetStringLength(), limits.diagnostic_bytes);
            if (retained < errors->GetStringLength())
            {
                while ((retained != 0u) &&
                       ((static_cast<unsigned char>(errors->GetStringPointer()[retained]) & 0xc0u) == 0x80u))
                {
                    --retained;
                }
            }
            preliminary.append(errors->GetStringPointer(), retained);
            report.diagnostics_truncated = (report.diagnostics_truncated || (retained < errors->GetStringLength()));
        }
        diagnostic(report, preliminary.data(), preliminary.size(), limits);
        provenance += includes.identities;
        if (!report.provenance.set(provenance.data(), provenance.size()))
        {
            report.status = EStatus::allocation_failed;
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
        if (FAILED(result->GetOutput(
                DXC_OUT_OBJECT, __uuidof(IDxcBlob), (reinterpret_cast<void**>(&object.value)), nullptr)) ||
            (object.value == nullptr))
        {
            failure(report, EStatus::compiler_failed, "DXC returned no compiled artifact", limits);
            return;
        }
        if (object->GetBufferSize() > limits.artifact_bytes)
        {
            failure(report, EStatus::limit_exceeded, "Compiled shader exceeds artifact limit", limits);
            return;
        }
        std::string requirements;
        if (!inspect_spirv((*object.value), requirements))
        {
            failure(report, EStatus::unsupported_feature, "Compiled shader uses a feature outside the initial policy",
                limits);
            return;
        }
        if (!report.shader.requirements.set(requirements.data(), requirements.size()) ||
            !report.shader.bytes.resize(object->GetBufferSize(), 16u))
        {
            failure(report, EStatus::allocation_failed, "Could not retain compiled shader", limits);
            return;
        }
        std::memcpy(report.shader.bytes.data(), object->GetBufferPointer(), object->GetBufferSize());
        report.shader.stage = entry.stage;
        report.shader.target = entry.options.target;
    }
    catch (const std::bad_alloc&)
    {
        failure(report, EStatus::allocation_failed, "DXC adapter allocation failed", limits);
    }
    catch (...)
    {
        failure(report, EStatus::compiler_failed, "Exception contained at DXC boundary", limits);
    }
}

}   //  namespace rendering::shader_compiler
