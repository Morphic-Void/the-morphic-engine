//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    morphic_shader.hlsli
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    22 Oct 26
//
//  Bundle this header with shader sources; the compiler never loads it from disk.
#ifndef MORPHIC_SHADER_HLSLI
#define MORPHIC_SHADER_HLSLI

#if MV_SHADER_SUPPORT_VERSION != 1
#error Incompatible Morphic shader support version
#endif

#define MV_SHADER_JOIN_INNER(a, b) a##b
#define MV_SHADER_JOIN(a, b) MV_SHADER_JOIN_INNER(a, b)
#define MV_SHADER_RESOURCE(kind, group, binding) \
    register(MV_SHADER_JOIN(kind, binding), MV_SHADER_JOIN(space, group))

#if MV_SHADER_SPIRV
#define MV_SHADER_LOCATION(index) [[vk::location(index)]]
#else
#define MV_SHADER_LOCATION(index)
#endif

#endif
