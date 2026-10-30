
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_requests.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Compile-only publication; renderer jobs use the underlying owned types.

#pragma once

#ifndef RENDERING_SHADER_REQUESTS_HPP_INCLUDED
#define RENDERING_SHADER_REQUESTS_HPP_INCLUDED

#include "assets/asset_repository.hpp"
#include "assets/bundle.hpp"
#include "shader/shader_data.hpp"

struct ShaderCompileRequest
{
    CAssetId bundle;
    shader::CCompileRequest input;
};

struct ShaderCompileWork
{
    const shader::CCompileRequest* request{ nullptr };
    bundle::CBundleView sources;
};

struct ShaderCompileResult
{
    shader::CCompileGroup group;
    shader::EStatus status{ shader::EStatus::allocation_failed };
};

#endif   //  RENDERING_SHADER_REQUESTS_HPP_INCLUDED
