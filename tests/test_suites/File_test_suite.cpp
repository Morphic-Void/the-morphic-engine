//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    File_test_suite.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    12 Oct 26

#include "tests/test_suites/File_test_suite.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include "platform/filesystem/file.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "tests/environment/test_paths.hpp"
#include "tests/support/test_context.hpp"

namespace file_tests
{

using tests::TTestContext;
namespace fs = platform::filesystem;

static void test_sizes_and_empty_files(TTestContext& ctx)
{
    const std::string path = test_environment::test_output_path("file_size.bin");
    const auto native = platform::path::makeNativePath(path.c_str());
    std::FILE* handle = fs::openFile(native, fs::EOpenMode::BinaryWrite);
    TEST_EXPECT(ctx, handle != nullptr);
    if (handle == nullptr) return;
    TEST_EXPECT(ctx, std::fclose(handle) == 0);

    std::size_t size = 99u;
    TEST_EXPECT(ctx, fs::queryFileSize(path.c_str(), size) && size == 0u);
    handle = fs::openFile(native);
    TEST_EXPECT(ctx, handle != nullptr);
    if (handle != nullptr)
    {
        size = 99u;
        TEST_EXPECT(ctx, fs::queryFileSize(handle, size) && size == 0u);
        TEST_EXPECT(ctx, fs::getFileSize(handle) == 0u);
        TEST_EXPECT(ctx, fs::getFileSize(handle, 17u) == 0u);
        TEST_EXPECT(ctx, std::fclose(handle) == 0);
    }
    TEST_EXPECT(ctx, !fs::loadFile(path.c_str(), 17u).is_ready());
    std::uint8_t byte = 0xa5u;
    TEST_EXPECT(ctx, !fs::loadFileInto(path.c_str(), CByteView{ &byte, 1u }));
    TEST_EXPECT(ctx, byte == 0xa5u);
    TEST_EXPECT(ctx, !fs::loadFileInto(path.c_str(), CByteView{}));
    TEST_EXPECT(ctx, !fs::saveFile(path.c_str(), CByteConstView{}));

    const std::uint8_t payload[]{ 0u, 0xffu, 42u };
    const bool saved = fs::saveFile(path.c_str(), CByteConstView{ payload, sizeof(payload) });
    TEST_EXPECT(ctx, saved);
    if (saved)
    {
        TEST_EXPECT(ctx, fs::queryFileSize(path.c_str(), size) && size == sizeof(payload));
        handle = fs::openFile(native);
        TEST_EXPECT(ctx, handle != nullptr);
        if (handle != nullptr)
        {
            TEST_EXPECT(ctx, std::fgetc(handle) == 0);
            TEST_EXPECT(ctx, fs::queryFileSize(handle, size) && size == sizeof(payload));
            TEST_EXPECT(ctx, std::ftell(handle) == 0);
            TEST_EXPECT(ctx, fs::getFileSize(handle, 5u) == sizeof(payload) + 5u);
            const std::size_t maximum = std::numeric_limits<std::size_t>::max();
            TEST_EXPECT(ctx, fs::getFileSize(handle, maximum - sizeof(payload)) == maximum);
            TEST_EXPECT(ctx, fs::getFileSize(handle, maximum - sizeof(payload) + 1u) == 0u);
            TEST_EXPECT(ctx, std::fclose(handle) == 0);
        }
    }
    fs::removeFile(native);
    for (const char* invalid : { path.c_str(), "", static_cast<const char*>(nullptr), "bad*path" })
    {
        size = 99u;
        TEST_EXPECT(ctx, !fs::queryFileSize(invalid, size) && size == 0u);
        TEST_EXPECT(ctx, !fs::loadFileInto(invalid, CByteView{ &byte, 1u }));
        TEST_EXPECT(ctx, byte == 0xa5u);
    }
    size = 99u;
    TEST_EXPECT(ctx, !fs::queryFileSize(static_cast<std::FILE*>(nullptr), size) && size == 0u);
    TEST_EXPECT(ctx, fs::getFileSize(nullptr, 17u) == 0u);
}

static void test_loading_into_storage(TTestContext& ctx)
{
    const std::string path = test_environment::test_output_path("file_into_\xc3\xa9.bin");
    const auto native = platform::path::makeNativePath(path.c_str());
    std::array<std::uint8_t, 256u> payload{};
    for (std::size_t index = 0u; index < payload.size(); ++index)
    {
        payload[index] = static_cast<std::uint8_t>(index);
    }
    //  Deliberately unaligned content with sentinels on both sides.
    alignas(128) std::array<std::uint8_t, 258u> storage{};
    for (const std::size_t count : { 1u, 127u, 128u, 129u, 255u, 256u })
    {
        const bool saved = fs::saveFile(path.c_str(), CByteConstView{ payload.data(), count });
        TEST_EXPECT(ctx, saved);
        if (!saved) continue;
        storage.fill(0xa5u);
        CByteView destination{ storage.data() + 1u, count };
        TEST_EXPECT(ctx, destination.is_ready());
        TEST_EXPECT(ctx, fs::loadFileInto(path.c_str(), destination));
        TEST_EXPECT(ctx, destination.data() == storage.data() + 1u && destination.size() == count);
        TEST_EXPECT(ctx, std::memcmp(destination.data(), payload.data(), count) == 0);
        TEST_EXPECT(ctx, storage[0] == 0xa5u);
        TEST_EXPECT(ctx, std::all_of(storage.begin() + count + 1u, storage.end(),
            [](const std::uint8_t value) { return value == 0xa5u; }));

        storage.fill(0xa5u);
        TEST_EXPECT(ctx, !fs::loadFileInto(path.c_str(), CByteView{ storage.data() + 1u, count + 1u }));
        TEST_EXPECT(ctx, !fs::loadFileInto(path.c_str(), CByteView{ storage.data() + 1u, count - 1u }));
        TEST_EXPECT(ctx, !fs::loadFileInto(path.c_str(), CByteView{}));
        TEST_EXPECT(ctx, std::all_of(storage.begin(), storage.end(),
            [](const std::uint8_t value) { return value == 0xa5u; }));

        CByteBuffer loaded = fs::loadFile(path.c_str(), 3u, 128u);
        TEST_EXPECT(ctx, loaded.is_ready());
        if (loaded.is_ready())
        {
            TEST_EXPECT(ctx, loaded.size() == count + 3u && loaded.align() == 128u);
            TEST_EXPECT(ctx, loaded.capacity() == memory::condition_bytes(128u, count + 3u));
            TEST_EXPECT(ctx, std::memcmp(loaded.data(), payload.data(), count) == 0);
            TEST_EXPECT(ctx, std::all_of(loaded.data() + count, loaded.data() + loaded.capacity(),
                [](const std::uint8_t value) { return value == 0u; }));
        }
    }
    fs::removeFile(native);
}

static void test_short_reads(TTestContext& ctx)
{
    const std::string path = test_environment::test_output_path("file_short_read.bin");
    const auto native = platform::path::makeNativePath(path.c_str());
    const std::uint8_t payload[]{ 3u, 0u, 7u };
    const bool saved = fs::saveFile(path.c_str(), CByteConstView{ payload, sizeof(payload) });
    TEST_EXPECT(ctx, saved);
    if (!saved) return;
    std::FILE* handle = fs::openFile(native);
    TEST_EXPECT(ctx, handle != nullptr);
    if (handle != nullptr)
    {
        std::uint8_t storage[]{ 0xa5u, 0xa5u, 0xa5u, 0xa5u, 0xa5u, 0xa5u };
        //  Exercise the shared reader with more bytes than remain, without races.
        TEST_EXPECT(ctx, !fs::readFileContent(handle, storage + 1u, 4u));
        TEST_EXPECT(ctx, std::memcmp(storage + 1u, payload, sizeof(payload)) == 0);
        TEST_EXPECT(ctx, storage[0] == 0xa5u && storage[4] == 0xa5u && storage[5] == 0xa5u);
        TEST_EXPECT(ctx, std::feof(handle) != 0);
        std::size_t size = 0u;
        TEST_EXPECT(ctx, fs::queryFileSize(handle, size) && size == sizeof(payload));
        TEST_EXPECT(ctx, fs::readFileContent(handle, storage + 1u, size));
        TEST_EXPECT(ctx, !fs::readFileContent(handle, nullptr, size));
        TEST_EXPECT(ctx, !fs::readFileContent(handle, storage, 0u));
        TEST_EXPECT(ctx, std::fclose(handle) == 0);
    }
    TEST_EXPECT(ctx, !fs::readFileContent(nullptr, nullptr, 1u));
    fs::removeFile(native);
}

}   //  namespace file_tests

int run_file_tests()
{
    tests::TTestContext ctx;
    file_tests::test_sizes_and_empty_files(ctx);
    file_tests::test_loading_into_storage(ctx);
    file_tests::test_short_reads(ctx);
    return ctx.report("File");
}
