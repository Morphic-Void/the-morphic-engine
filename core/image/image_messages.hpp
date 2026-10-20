
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

#include "threading/messages/message_helpers.hpp"
#include <cstddef>
#include <cstdint>

#include "containers/ByteBuffers.hpp"
#include "assets/asset_storage.hpp"
#include "image/codec/tga.hpp"
#include "image/image_view.hpp"

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

struct DecodedTga
{
    CByteRectBuffer buffer;
    image::codec::tga::decoded_image_desc desc;
    bool storage_bottom_up{ false };

    //  Initialise after the buffer arrives at the Host; never change after publication.
    image::CImageView view;
};

namespace threading
{

template<> struct TMessagePayload<TgaEncodeResult> { using type = ByteStorage; };
template<> struct TMessagePayload<TgaDecodeResult> { using type = DecodedTga; };

}   //  namespace threading

#endif  //  IMAGE_MESSAGES_HPP_INCLUDED
