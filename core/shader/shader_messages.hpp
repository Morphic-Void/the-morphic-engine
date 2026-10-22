
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    shader_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Compile-only publication; renderer jobs use the underlying owned types.

#pragma once

#ifndef CORE_SHADER_MESSAGES_HPP_INCLUDED
#define CORE_SHADER_MESSAGES_HPP_INCLUDED

#include "assets/asset_repository.hpp"
#include "assets/bundle.hpp"
#include "shader/shader_data.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/system_type_registration.hpp"

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

MV_REGISTER_SYSTEM_TYPE(ShaderCompileRequest, system_type_ids::shader_compile_request);
MV_REGISTER_SYSTEM_TYPE(ShaderCompileWork, system_type_ids::shader_compile_work);
MV_REGISTER_SYSTEM_TYPE(ShaderCompileResult, system_type_ids::shader_compile_result);
MV_REGISTER_ERASED_OWNER_PAYLOAD(ShaderCompileRequest);
MV_REGISTER_ERASED_OWNER_PAYLOAD(ShaderCompileResult);

#endif   //  CORE_SHADER_MESSAGES_HPP_INCLUDED
