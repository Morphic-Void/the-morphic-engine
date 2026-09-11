
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    transported_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Jul 26
//
//  Built-in system payload declarations and C++ type registrations.

#pragma once

#ifndef TRANSPORTED_TYPES_HPP_INCLUDED
#define TRANSPORTED_TYPES_HPP_INCLUDED

#include <cstddef>      //  std::size_t

#include "assets/asset_repository.hpp"
#include "containers/ByteBuffers.hpp"
#include "containers/StringBuffers.hpp"
#include "data_model/baked_document.hpp"
#include "image/codec/tga.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/system_type_registration.hpp"

struct UnrecognisedMsg { system_type_id msg_id; };

struct FileLoadRequest { const char* file; };
struct FileSaveRequest { const char* file; CByteConstView view; };

struct TgaLoadRequest
{
    CSimpleString file;
    bool vflip{ false };
};

struct TgaSaveRequest
{
    CSimpleString file;
    CAssetId source{};
    image::codec::tga::EncodeOptions options{};
};

struct TgaEncodeRequest
{
    CByteRectConstView view;
    image::codec::tga::EncodeOptions options;
};

struct TgaDecodeRequest { CByteConstView view; bool vflip; };

struct FileLoadResult {};
struct FileSaveResult { bool success; };
struct TgaLoadResult { CAssetId asset; image::codec::tga::decoded_image_desc desc; bool success; };
struct TgaSaveResult { bool success; };
struct TgaEncodeResult {};
struct TgaDecodeResult {};

struct LoadedFile { CByteBuffer buffer; };
struct EncodedTga { CByteBuffer buffer; };
struct DecodedTga { CByteRectBuffer buffer; image::codec::tga::decoded_image_desc desc; };
struct BakedDocumentAsset { CBakedDocumentBlock block; };

MV_REGISTER_SYSTEM_TYPE(CByteBuffer, system_type_ids::byte_buffer);
MV_REGISTER_SYSTEM_TYPE(CByteRectBuffer, system_type_ids::byte_rect_buffer);
MV_REGISTER_SYSTEM_TYPE(CSimpleString, system_type_ids::simple_string);
MV_REGISTER_SYSTEM_TYPE(CStringBuffer, system_type_ids::string_buffer);
MV_REGISTER_SYSTEM_TYPE(CStableStrings, system_type_ids::stable_strings);

MV_REGISTER_SYSTEM_TYPE(UnrecognisedMsg, system_type_ids::unrecognised_msg);

MV_REGISTER_SYSTEM_TYPE(FileLoadRequest, system_type_ids::file_load_request);
MV_REGISTER_SYSTEM_TYPE(FileSaveRequest, system_type_ids::file_save_request);
MV_REGISTER_SYSTEM_TYPE(TgaLoadRequest, system_type_ids::tga_load_request);
MV_REGISTER_SYSTEM_TYPE(TgaSaveRequest, system_type_ids::tga_save_request);
MV_REGISTER_SYSTEM_TYPE(TgaEncodeRequest, system_type_ids::tga_encode_request);
MV_REGISTER_SYSTEM_TYPE(TgaDecodeRequest, system_type_ids::tga_decode_request);

MV_REGISTER_SYSTEM_TYPE(FileLoadResult, system_type_ids::file_load_result);
MV_REGISTER_SYSTEM_TYPE(FileSaveResult, system_type_ids::file_save_result);
MV_REGISTER_SYSTEM_TYPE(TgaLoadResult, system_type_ids::tga_load_result);
MV_REGISTER_SYSTEM_TYPE(TgaSaveResult, system_type_ids::tga_save_result);
MV_REGISTER_SYSTEM_TYPE(TgaEncodeResult, system_type_ids::tga_encode_result);
MV_REGISTER_SYSTEM_TYPE(TgaDecodeResult, system_type_ids::tga_decode_result);

MV_REGISTER_SYSTEM_TYPE(LoadedFile, system_type_ids::loaded_file);
MV_REGISTER_SYSTEM_TYPE(EncodedTga, system_type_ids::encoded_tga);
MV_REGISTER_SYSTEM_TYPE(DecodedTga, system_type_ids::decoded_tga);
MV_REGISTER_SYSTEM_TYPE(BakedDocumentAsset, system_type_ids::baked_document_asset);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) \
    MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "system/system_erased_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

#endif  //  #ifndef TRANSPORTED_TYPES_HPP_INCLUDED
