//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  Compiled-only manual TGA round trip. This is intentionally neither
//  registered with nor invoked by the ordinary MorphicTests runner.

#include <string>

#include "containers/ByteBuffers.hpp"
#include "image/codec/tga.hpp"
#include "platform/filesystem/file.hpp"
#include "tests/environment/test_paths.hpp"

namespace tests::manual
{

[[maybe_unused]] bool tga_round_trip()
{
    const std::string input_path = test_environment::repository_path(
        "development/logical-roots/dev-source/test_input.tga");
    const std::string output_path = test_environment::test_output_path(
        "manual_tga_round_trip.tga");

    CByteBuffer loaded_tga = platform::filesystem::loadFile(input_path.c_str());
    if (loaded_tga.is_empty())
    {
        return false;
    }

    image::codec::tga::decoded_image_desc desc;
    CByteRectBuffer decoded_tga = image::codec::tga::decode(
        loaded_tga.const_view(), desc);
    if (decoded_tga.is_empty())
    {
        return false;
    }

    image::codec::tga::EncodeOptions options;
    CByteBuffer encoded_tga = image::codec::tga::encode(
        decoded_tga.const_view(), options);
    return !encoded_tga.is_empty() && platform::filesystem::saveFile(
        output_path.c_str(), encoded_tga.const_view());
}

}   //  namespace tests::manual
