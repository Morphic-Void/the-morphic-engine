
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    texture_jobs.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    10 Oct 26
//
//  Rendering-owned codec jobs with stable addresses for batch submission.

#pragma once

#ifndef RENDERING_TEXTURE_JOBS_HPP_INCLUDED
#define RENDERING_TEXTURE_JOBS_HPP_INCLUDED

#include <utility>

#include "platform/platform_defines.hpp"
#include "rendering/runtime/basis_codec.hpp"

namespace rendering
{

//  Input storage is borrowed and must remain immutable until completion.
//  Initialise Basis before submission and drain jobs before shutting it down.
//  Only the executing thread accesses a queued job. The owner may inspect or
//  take its result after inline execution or consumption of its batch response.
class CTextureEncodeJob final
{
public:
    CTextureEncodeJob(const image::texture::CInputView& input, const image::texture::CEncodeOptions& options) noexcept
        : m_input{ input }, m_options{ options } {}
    CTextureEncodeJob(const CTextureEncodeJob&) = delete;
    CTextureEncodeJob& operator=(const CTextureEncodeJob&) = delete;
    CTextureEncodeJob(CTextureEncodeJob&&) = delete;
    CTextureEncodeJob& operator=(CTextureEncodeJob&&) = delete;

    void run() noexcept;
    static void MV_STD_ABI_CALL execute(void* const object) noexcept;

    [[nodiscard]] basis_codec::EStatus status() const noexcept { return m_status; }
    [[nodiscard]] const image::texture::CEncodedTexture& output() const noexcept { return m_output; }
    [[nodiscard]] image::texture::CEncodedTexture take_output() noexcept { return std::move(m_output); }

private:
    const image::texture::CInputView m_input;
    const image::texture::CEncodeOptions m_options;
    image::texture::CEncodedTexture m_output;
    basis_codec::EStatus m_status{ basis_codec::EStatus::codec_failed };
};

class CTextureDecodeJob final
{
public:
    CTextureDecodeJob(const CByteConstView& input, const image::texture::EStorageFormat target, const std::uint32_t level) noexcept
        : m_input{ input }, m_target{ target }, m_level{ level } {}
    CTextureDecodeJob(const CTextureDecodeJob&) = delete;
    CTextureDecodeJob& operator=(const CTextureDecodeJob&) = delete;
    CTextureDecodeJob(CTextureDecodeJob&&) = delete;
    CTextureDecodeJob& operator=(CTextureDecodeJob&&) = delete;

    void run() noexcept;
    static void MV_STD_ABI_CALL execute(void* const object) noexcept;

    [[nodiscard]] basis_codec::EStatus status() const noexcept { return m_status; }
    [[nodiscard]] const image::texture::CDecodedTexture& output() const noexcept { return m_output; }
    [[nodiscard]] image::texture::CDecodedTexture take_output() noexcept { return std::move(m_output); }

private:
    const CByteConstView m_input;
    const image::texture::EStorageFormat m_target;
    const std::uint32_t m_level;
    image::texture::CDecodedTexture m_output;
    basis_codec::EStatus m_status{ basis_codec::EStatus::codec_failed };
};

}   //  namespace rendering

#endif  //  RENDERING_TEXTURE_JOBS_HPP_INCLUDED
