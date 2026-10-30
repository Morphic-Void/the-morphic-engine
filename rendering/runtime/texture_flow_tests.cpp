
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_flow_tests.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    27 Oct 26
//
//  Texture flow fixtures and assertions belong to Rendering. Host retains the
//  existing typed asset plumbing during this first boundary migration stage.

#include "threading/messages/message_helpers.hpp"
#include "runtime/texture_flow_tests.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include "debug/macros.hpp"
#include "image/texture_messages.hpp"
#include "types/fp16data_t.hpp"

namespace rendering
{
namespace texture_acceptance
{

enum class EScenario : std::uint8_t
{
    encode_texture_ldr,
    decode_texture_ldr_bc7,
    decode_texture_ldr_rgba,
    encode_texture_hdr,
    decode_texture_hdr_bc6h,
    decode_texture_hdr_half,
    encode_texture_gray_mips,
    decode_texture_gray_bc7,
    decode_texture_gray_mip,
    encode_texture_float_mips,
    decode_texture_float_mip,
    reject_texture_budget,
    reject_texture_row,
    reject_texture_negative,
    reject_texture_nonfinite,
    reject_texture_transfer,
    reject_texture_mode,
    reject_texture_profile,
    reject_texture_level,
    reject_texture_target,
    reject_texture_bc7_transfer,
    reject_texture_container,
    reject_texture_asset,
};

struct SScenario
{
    EScenario identity;
    const char* name;
    EAssetStatus status;
    EAssetKind retained_kind;
    EDocumentPolicyStatus policy;
    std::uint32_t findings;
};

//  Preserve the original cases, ordering, expected statuses and assertions.
static constexpr SScenario scenarios[]{
    { EScenario::encode_texture_ldr,           "encode_texture_ldr",           EAssetStatus::success,             EAssetKind::encoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::decode_texture_ldr_bc7,       "decode_texture_ldr_bc7",       EAssetStatus::success,             EAssetKind::decoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::decode_texture_ldr_rgba,      "decode_texture_ldr_rgba",      EAssetStatus::success,             EAssetKind::decoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::encode_texture_hdr,           "encode_texture_hdr",           EAssetStatus::success,             EAssetKind::encoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::decode_texture_hdr_bc6h,      "decode_texture_hdr_bc6h",      EAssetStatus::success,             EAssetKind::decoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::decode_texture_hdr_half,      "decode_texture_hdr_half",      EAssetStatus::success,             EAssetKind::decoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::encode_texture_gray_mips,     "encode_texture_gray_mips",     EAssetStatus::success,             EAssetKind::encoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::decode_texture_gray_bc7,      "decode_texture_gray_bc7",      EAssetStatus::success,             EAssetKind::decoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::decode_texture_gray_mip,      "decode_texture_gray_mip",      EAssetStatus::success,             EAssetKind::decoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::encode_texture_float_mips,    "encode_texture_float_mips",    EAssetStatus::success,             EAssetKind::encoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::decode_texture_float_mip,     "decode_texture_float_mip",     EAssetStatus::success,             EAssetKind::decoded_texture, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_budget,        "reject_texture_budget",        EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_row,           "reject_texture_row",           EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_negative,      "reject_texture_negative",      EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_nonfinite,     "reject_texture_nonfinite",     EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_transfer,      "reject_texture_transfer",      EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_mode,          "reject_texture_mode",          EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_profile,       "reject_texture_profile",       EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_level,         "reject_texture_level",         EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_target,        "reject_texture_target",        EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_bc7_transfer,  "reject_texture_bc7_transfer",  EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_container,     "reject_texture_container",     EAssetStatus::invalid_request,     EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_texture_asset,         "reject_texture_asset",         EAssetStatus::invalid_asset,       EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
};
static constexpr std::uint32_t scenario_count = sizeof(scenarios) / sizeof(scenarios[0]);
static_assert(scenario_count == 23u, "Texture acceptance coverage");
static constexpr char fixture[] = R"({"null":null,"bool":true,"signed":-123,"unsigned":18446744073709551615,"float":1.25,"text":"line\ntext","object":{},"mixed":[null,true,3,"text"],"bools":[true,false],"ints":[-1,-2],"uints":[18446744073709551615,18446744073709551614],"floats":[1.25,2.5],"strings":["a","b"],"empty":[]})";

static bool expect(const SScenario& scenario, const bool satisfied, const char* const property) noexcept
{
    if (!satisfied)
    {
        MV_LOG_RICH(error, "Rendering: %s: failed expectation: %s", scenario.name, property);
    }
    return satisfied;
}

}   //  namespace texture_acceptance

static bool prepare_texture_decode_request(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const CAssetId source, const image::texture::EStorageFormat target, const std::uint32_t level = 0u) noexcept
{
    auto candidate = threading::make_owner_message<TextureDecodeRequest>(slot);
    auto* const request = candidate.owner().payload<TextureDecodeRequest>();
    if (request == nullptr) return false;
    request->source = source;
    request->target = target;
    request->level = level;
    pending = std::move(candidate);
    return true;
}

bool CTextureFlowTests::prepare_texture_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const texture_acceptance::EScenario scenario) noexcept
{
    using namespace texture_acceptance;
    using namespace image::texture;
    const bool gray = (scenario == EScenario::encode_texture_gray_mips);
    const bool full_float = (scenario == EScenario::encode_texture_float_mips) ||
        (scenario == EScenario::reject_texture_negative) || (scenario == EScenario::reject_texture_nonfinite);
    const bool hdr = full_float || (scenario == EScenario::encode_texture_hdr) ||
        (scenario == EScenario::reject_texture_transfer);
    const bool mips = gray || (scenario == EScenario::encode_texture_float_mips);
    const std::uint32_t pixel_bytes = gray ? 1u : (hdr ? (full_float ? 16u : 8u) : 4u);
    const std::uint32_t row_bytes = (scenario == EScenario::reject_texture_row) ? 15u : (4u * pixel_bytes);
    auto candidate = threading::make_owner_message<TextureEncodeRequest>(slot);
    TextureEncodeRequest* const request = candidate.owner().payload<TextureEncodeRequest>();
    if ((request == nullptr) || !request->input.allocate(row_bytes, 4u, 16u))
    {
        return false;
    }
    request->format = gray ? EInputFormat::gray8 :
        (hdr ? (full_float ? EInputFormat::rgba32f : EInputFormat::rgba16f) : EInputFormat::rgba8);
    request->transfer = (hdr || gray) ? ETransfer::linear : ETransfer::srgb;
    request->options.encoding = hdr ? EEncoding::uastc_hdr_4x4 : EEncoding::uastc_ldr_4x4;
    request->options.max_pixels = (scenario == EScenario::reject_texture_budget) ? 15u : 16u;
    request->options.generate_mips = mips;
    request->options.profile = mips ? EEncodeProfile::build_quality : EEncodeProfile::runtime_fast;
    if (scenario == EScenario::reject_texture_transfer)
    {
        request->transfer = ETransfer::srgb;
    }
    if (scenario == EScenario::reject_texture_mode)
    {
        request->options.encoding = EEncoding::uastc_hdr_4x4;
    }
    if (scenario == EScenario::reject_texture_profile)
    {
        request->options.profile = static_cast<EEncodeProfile>(255u);
    }
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        std::uint8_t* const row = request->input.row_data(y);
        std::memset(row, 0, row_bytes);
        for (std::uint32_t x = 0u; x < (row_bytes / pixel_bytes); ++x)
        {
            if (gray)
            {
                row[x] = 128u;
            }
            else if (full_float)
            {
                const float red = (scenario == EScenario::reject_texture_negative) ? -1.0f :
                    ((scenario == EScenario::reject_texture_nonfinite) ? std::numeric_limits<float>::infinity() : 4.0f);
                const float pixel[4]{ red, 0.5f, 0.25f, 1.0f };
                std::memcpy((row + (x * sizeof(pixel))), pixel, sizeof(pixel));
            }
            else if (hdr)
            {
                const std::uint16_t pixel[4]{ 0x4400u, 0x3800u, 0x3400u, 0x3c00u };
                std::memcpy((row + (x * sizeof(pixel))), pixel, sizeof(pixel));
            }
            else
            {
                const std::uint8_t pixel[4]{ static_cast<std::uint8_t>(x * 50u),
                    static_cast<std::uint8_t>(y * 50u), 80u, 255u };
                std::memcpy((row + (x * sizeof(pixel))), pixel, sizeof(pixel));
            }
        }
    }
    pending = std::move(candidate);
    return true;
}

bool CTextureFlowTests::prepare_scenario(threading::CErasedOwnerMsg& pending,
    const texture_acceptance::SScenario& scenario, const std::int32_t slot) noexcept
{
    using namespace texture_acceptance;
    switch (scenario.identity)
    {
        case EScenario::encode_texture_gray_mips:
        case EScenario::encode_texture_float_mips:
        case EScenario::reject_texture_budget:
        case EScenario::reject_texture_row:
        case EScenario::reject_texture_negative:
        case EScenario::reject_texture_nonfinite:
        case EScenario::reject_texture_transfer:
        case EScenario::reject_texture_mode:
        case EScenario::reject_texture_profile:
        case EScenario::encode_texture_ldr:
        {
            return prepare_texture_fixture(pending, slot, scenario.identity);
        }
        case EScenario::decode_texture_ldr_bc7:
        {
            return prepare_texture_decode_request(pending, slot, m_ldr_texture.asset, image::texture::EStorageFormat::bc7_srgb);
        }
        case EScenario::decode_texture_ldr_rgba:
        {
            return prepare_texture_decode_request(pending, slot, m_ldr_texture.asset, image::texture::EStorageFormat::rgba8);
        }
        case EScenario::encode_texture_hdr:
        {
            return prepare_texture_fixture(pending, slot, scenario.identity);
        }
        case EScenario::decode_texture_hdr_bc6h:
        {
            return prepare_texture_decode_request(pending, slot, m_hdr_texture.asset, image::texture::EStorageFormat::bc6h_ufloat);
        }
        case EScenario::decode_texture_hdr_half:
        {
            return prepare_texture_decode_request(pending, slot, m_hdr_texture.asset, image::texture::EStorageFormat::rgba16f);
        }
        case EScenario::decode_texture_gray_bc7:
        {
            return prepare_texture_decode_request(pending, slot, m_ldr_texture.asset, image::texture::EStorageFormat::bc7_unorm, 0u);
        }
        case EScenario::decode_texture_gray_mip:
        {
            return prepare_texture_decode_request(pending, slot, m_ldr_texture.asset, image::texture::EStorageFormat::rgba8, 2u);
        }
        case EScenario::decode_texture_float_mip:
        {
            return prepare_texture_decode_request(pending, slot, m_hdr_texture.asset, image::texture::EStorageFormat::rgba16f, 1u);
        }
        case EScenario::reject_texture_level:
        {
            return prepare_texture_decode_request(pending, slot, m_ldr_texture.asset, image::texture::EStorageFormat::rgba8, 3u);
        }
        case EScenario::reject_texture_target:
        {
            return prepare_texture_decode_request(pending, slot, m_hdr_texture.asset, image::texture::EStorageFormat::rgba8, 0u);
        }
        case EScenario::reject_texture_bc7_transfer:
        {
            return prepare_texture_decode_request(pending, slot, m_ldr_texture.asset, image::texture::EStorageFormat::bc7_srgb, 0u);
        }
        case EScenario::reject_texture_container:
        {
            return prepare_texture_decode_request(pending, slot, m_raw.asset, image::texture::EStorageFormat::rgba8, 0u);
        }
        case EScenario::reject_texture_asset:
        {
            return prepare_texture_decode_request(pending, slot, CAssetId{}, image::texture::EStorageFormat::rgba8, 0u);
        }
    }
    return false;
}

bool CTextureFlowTests::check(const texture_acceptance::SScenario& scenario, const AssetResult& result) noexcept
{
    using namespace texture_acceptance;
    if ((result.status != scenario.status) || (result.kind != scenario.retained_kind) ||
        (result.document_policy != scenario.policy) || (result.document_findings != scenario.findings))
    {
        MV_LOG_RICH(error, "Rendering: %s: status %u/%u, kind %u/%u, policy %u/%u, findings 0x%08x/0x%08x (actual/expected)",
            scenario.name, static_cast<unsigned int>(result.status), static_cast<unsigned int>(scenario.status),
            static_cast<unsigned int>(result.kind), static_cast<unsigned int>(scenario.retained_kind),
            static_cast<unsigned int>(result.document_policy), static_cast<unsigned int>(scenario.policy),
            result.document_findings, scenario.findings);
        return false;
    }
    if (!expect(scenario, (static_cast<bool>(result.asset) == (scenario.retained_kind != EAssetKind::none)), "retained asset identity"))
    {
        return false;
    }
    switch (scenario.identity)
    {
        case EScenario::encode_texture_gray_mips:
        case EScenario::encode_texture_float_mips:
        case EScenario::encode_texture_ldr:
        case EScenario::encode_texture_hdr:
        {
            const image::texture::CEncodedView view = result.encoded_texture_view();
            const bool hdr = (scenario.identity == EScenario::encode_texture_hdr) ||
                (scenario.identity == EScenario::encode_texture_float_mips);
            const bool mips = (scenario.identity == EScenario::encode_texture_gray_mips) ||
                (scenario.identity == EScenario::encode_texture_float_mips);
            if (hdr)
            {
                m_hdr_texture = result;
            }
            else
            {
                m_ldr_texture = result;
            }
            return
                expect(scenario, (view.is_ready() && (view.bytes.align() >= 16u)),
                    "aligned KTX2 view") &&
                expect(scenario,
                    ((view.description.width == 4u) && (view.description.height == 4u) && (view.description.levels == (mips ? 3u : 1u))),
                    "encoded dimensions and levels") &&
                expect(scenario,
                    (view.description.container == image::texture::EContainer::ktx2),
                    "KTX2 container") &&
                expect(scenario,
                    (view.description.encoding == (hdr ? image::texture::EEncoding::uastc_hdr_4x4 : image::texture::EEncoding::uastc_ldr_4x4)),
                    "encoded mode") &&
                expect(scenario,
                    (view.description.transfer == ((hdr || mips) ? image::texture::ETransfer::linear : image::texture::ETransfer::srgb)),
                    "encoded transfer");
        }
        case EScenario::decode_texture_gray_bc7:
        case EScenario::decode_texture_gray_mip:
        case EScenario::decode_texture_float_mip:
        {
            const image::texture::CDecodedView view = result.decoded_texture_view();
            const bool hdr = (scenario.identity == EScenario::decode_texture_float_mip);
            const bool blocks = (scenario.identity == EScenario::decode_texture_gray_bc7);
            const std::uint32_t width = blocks ? 4u : (hdr ? 2u : 1u);
            const std::uint32_t level = blocks ? 0u : (hdr ? 1u : 2u);
            const std::uint32_t pitch = blocks ? 16u : (width * (hdr ? 8u : 4u));
            if (!expect(scenario, (view.is_ready() && (view.bytes.align() >= 16u) &&
                (view.description.width == width) && (view.description.height == width) &&
                (view.description.level == level) && (view.description.row_pitch == pitch) &&
                (view.description.format == (blocks ? image::texture::EStorageFormat::bc7_unorm :
                    (hdr ? image::texture::EStorageFormat::rgba16f : image::texture::EStorageFormat::rgba8))) &&
                (view.description.transfer == image::texture::ETransfer::linear) &&
                (view.bytes.size() == (blocks ? 16u : (pitch * width)))), "mip layout"))
            {
                return false;
            }
            if (hdr)
            {
                std::uint16_t red_bits = 0u;
                std::memcpy((&red_bits), view.bytes.data(), sizeof(red_bits));
                const float red = static_cast<float>(fp16data_t::fromBits(red_bits)) * view.description.hdr_scale;
                return expect(scenario, ((red > 3.0f) && (red < 5.0f)), "float HDR mip range");
            }
            if (!blocks)
            {
                const std::uint8_t* const pixel = view.bytes.data();
                return expect(scenario, ((pixel[0] >= 120u) && (pixel[0] <= 136u) &&
                    (pixel[1] >= 120u) && (pixel[1] <= 136u) &&
                    (pixel[2] >= 120u) && (pixel[2] <= 136u) && (pixel[3] > 250u)), "grayscale expansion");
            }
            return true;
        }
        case EScenario::decode_texture_ldr_bc7:
        case EScenario::decode_texture_ldr_rgba:
        case EScenario::decode_texture_hdr_bc6h:
        case EScenario::decode_texture_hdr_half:
        {
            const image::texture::CDecodedView view = result.decoded_texture_view();
            const bool blocks =
                (scenario.identity == EScenario::decode_texture_ldr_bc7) ||
                (scenario.identity == EScenario::decode_texture_hdr_bc6h);
            const std::size_t expected_size = blocks ? 16u :
                ((scenario.identity == EScenario::decode_texture_ldr_rgba) ? 64u : 128u);
            if (!expect(scenario, (view.is_ready() && (view.bytes.align() >= 16u) && (view.bytes.size() == expected_size) &&
                (view.description.width == 4u) && (view.description.height == 4u) &&
                (view.description.row_pitch == (blocks ? 16u : ((scenario.identity == EScenario::decode_texture_ldr_rgba) ? 16u : 32u)))),
                "decoded layout"))
            {
                return false;
            }
            if (scenario.identity == EScenario::decode_texture_ldr_rgba)
            {
                return expect(scenario, (view.bytes.data()[3] > 200u), "LDR alpha");
            }
            if (scenario.identity == EScenario::decode_texture_hdr_half)
            {
                std::uint16_t red_bits = 0u;
                std::memcpy((&red_bits), view.bytes.data(), sizeof(red_bits));
                const float red = static_cast<float>(fp16data_t::fromBits(red_bits)) * view.description.hdr_scale;
                return expect(scenario, (red > 1.5f), "HDR value above bloom threshold");
            }
            return true;
        }
        default: break;
    }
    return true;
}

bool CTextureFlowTests::start(const threading::CErasedPodMsg& message) noexcept
{
    RunTestsRequest request;
    RunTestsResult rejected;
    rejected.module = m_module;
    if (!message.copy_payload_to(request) || (request.module != m_module) || (message.query_async_slot() < 0))
        rejected.status = ETestRunStatus::invalid_request;
    else if (m_active) rejected.status = ETestRunStatus::busy;
    else if (m_context.exit_requested()) rejected.status = ETestRunStatus::cancelled;
    else
    {
        m_active = true;
        m_request_slot = message.query_async_slot();
        m_pending_slot = 0;
        m_scenario_index = 0u;
        m_asset_count = 0u;
        m_raw = {};
        m_ldr_texture = {};
        m_hdr_texture = {};
        m_result = {};
        m_result.module = m_module;
        m_result.status = ETestRunStatus::success;
        m_phase = EPhase::fixture;
        auto fixture = threading::make_owner_message<RawAssetTransfer>(m_pending_slot);
        auto* const transfer = fixture.owner().payload<RawAssetTransfer>();
        if ((transfer != nullptr) && transfer->storage.value.resize(sizeof(texture_acceptance::fixture) - 1u))
        {
            std::memcpy(transfer->storage.value.data(), texture_acceptance::fixture, sizeof(texture_acceptance::fixture) - 1u);
            if (m_context.post(std::move(fixture))) return true;
        }
        m_result.status = ETestRunStatus::failed;
        return finish();
    }
    return m_context.post(threading::make_pod_message(message.query_async_slot(), rejected));
}

bool CTextureFlowTests::complete(const threading::CErasedPodMsg& message) noexcept
{
    using namespace texture_acceptance;
    if (!m_active || (message.query_async_slot() != m_pending_slot)) return false;
    if (m_phase == EPhase::disposal)
    {
        AssetDisposeResult result;
        if (!message.copy_payload_to(result) || (result.asset != m_assets[m_asset_count - 1u]) ||
            (result.status != EAssetStatus::success)) return false;
        m_assets[--m_asset_count] = {};
        return dispose_next();
    }
    AssetResult result;
    if (!message.copy_payload_to(result)) return false;
    if (result.asset)
    {
        if (m_asset_count == (sizeof(m_assets) / sizeof(m_assets[0]))) return false;
        m_assets[m_asset_count++] = result.asset;
    }
    if (m_phase == EPhase::fixture)
    {
        if ((result.status != EAssetStatus::success) || (result.kind != EAssetKind::raw) || !result.asset)
        {
            m_result.status = ETestRunStatus::failed;
            return dispose_next();
        }
        m_raw = result;
        m_phase = EPhase::scenarios;
    }
    else
    {
        const auto& scenario = scenarios[m_scenario_index++];
        if (check(scenario, result))
        {
            ++m_result.passed;
            MV_LOG_RICH(info, "Rendering: %s passed", scenario.name);
        }
        else
        {
            ++m_result.failed;
            m_result.status = ETestRunStatus::failed;
            return dispose_next();
        }
    }
    return next();
}

bool CTextureFlowTests::next() noexcept
{
    if (m_context.exit_requested())
    {
        m_result.status = ETestRunStatus::cancelled;
        return dispose_next();
    }
    if (m_scenario_index == texture_acceptance::scenario_count) return dispose_next();
    threading::CErasedOwnerMsg pending;
    if (prepare_scenario(pending, texture_acceptance::scenarios[m_scenario_index], ++m_pending_slot) &&
        m_context.post(std::move(pending))) return true;
    ++m_result.failed;
    m_result.status = ETestRunStatus::failed;
    return dispose_next();
}

bool CTextureFlowTests::dispose_next() noexcept
{
    m_phase = EPhase::disposal;
    //  No borrowed views are inspected after cleanup starts. Dispose every
    //  fixture and result before publishing the test completion.
    m_raw = {};
    m_ldr_texture = {};
    m_hdr_texture = {};
    if (m_asset_count == 0u) return finish();
    return m_context.post(threading::make_pod_message(++m_pending_slot, AssetDisposeRequest{ m_assets[m_asset_count - 1u] }));
}

bool CTextureFlowTests::finish() noexcept
{
    m_result.skipped = texture_acceptance::scenario_count - m_result.passed - m_result.failed;
    m_active = false;
    if (m_result.status == ETestRunStatus::success)
        MV_LOG(info, "Rendering acceptance: {} texture flow tests passed", m_result.passed);
    return m_context.post(threading::make_pod_message(m_request_slot, m_result));
}

}   //  namespace rendering
