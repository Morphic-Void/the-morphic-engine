
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    image_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Host TGA processing: borrowed inputs and owning worker completions.

#pragma once

#ifndef IMAGE_MESSAGES_HPP_INCLUDED
#define IMAGE_MESSAGES_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "containers/ByteBuffers.hpp"
#include "image/codec/tga.hpp"
#include "image/image_view.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/system_type_registration.hpp"

//==============================================================================
//  Image conditioning: borrowed Host inputs and owning worker completions
//  Result identities describe the operation; payloads own the resulting bytes.
//==============================================================================

struct TgaEncodeRequest
{
    CByteRectConstView view;
    image::codec::tga::EncodeOptions options;
};

struct TgaDecodeRequest { CByteConstView view; bool decode_top_down; };

struct TgaEncodeResult {};
struct TgaDecodeResult {};

struct EncodedTga { CByteBuffer buffer; };

struct DecodedTga
{
    CByteRectBuffer buffer;
    image::codec::tga::decoded_image_desc desc;
    bool storage_bottom_up{ false };

    //  Initialise after the buffer arrives at the Host; never change after publication.
    image::CImageView view;
};

//  Type bindings precede owning-payload eligibility.
MV_REGISTER_SYSTEM_TYPE(TgaEncodeRequest, system_type_ids::tga_encode_request);
MV_REGISTER_SYSTEM_TYPE(TgaDecodeRequest, system_type_ids::tga_decode_request);
MV_REGISTER_SYSTEM_TYPE(TgaEncodeResult, system_type_ids::tga_encode_result);
MV_REGISTER_SYSTEM_TYPE(TgaDecodeResult, system_type_ids::tga_decode_result);
MV_REGISTER_SYSTEM_TYPE(EncodedTga, system_type_ids::encoded_tga);
MV_REGISTER_SYSTEM_TYPE(DecodedTga, system_type_ids::decoded_tga);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "image/image_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

#endif  //  IMAGE_MESSAGES_HPP_INCLUDED
