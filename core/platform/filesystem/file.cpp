
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   file.cpp
//  Author: Ritchie Brannan
//  Date:   1 Mar 26
//
//  Basic load and save utility functions (file <=> memory blob)
//
//  Multi-threaded usage assumes that multiple threads will not be saving
//  files with the same name.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "platform/filesystem/file.hpp"
#include "platform/filesystem/internal/file_utils.hpp"
#include "platform/path/native_path.hpp"
#include "memory/memory_policies.hpp"

namespace platform::filesystem
{

CByteBuffer loadFile(const char* const utf8_path, const std::size_t pad, const std::size_t alignment) noexcept
{
    CByteBuffer buffer;
    //  Token/view alignment is represented by an exponent in the range 0..31.
    if (alignment > memory::k_byte_size_ceiling)
    {
        return buffer;
    }
    const std::size_t effective_alignment = std::max(std::size_t{ 16u }, memory::condition_alignment(alignment));
    path::NativePath std_path = path::makeNativePath(utf8_path);
    if (!std_path.is_empty())
    {
        std::FILE* handle = openFile(std_path, EOpenMode::BinaryRead);
        if (handle != nullptr)
        {
            bool success = false;
            std::size_t size = getFileSize(handle, pad);
            if (size != 0u)
            {
                const std::size_t aligned_size = memory::condition_bytes(effective_alignment, size);
                if ((aligned_size != 0u) && buffer.allocate(aligned_size, effective_alignment))
                {
                    std::uint8_t* data = buffer.data();
                    std::size_t file_size = size - pad;
                    if (std::fread(data, 1, file_size, handle) == file_size)
                    {
                        const std::size_t clear_size = aligned_size - file_size;
                        if (clear_size != 0u)
                        {
                            std::memset((data + file_size), 0, clear_size);
                        }
                        (void)buffer.set_size(size);
                        success = true;
                    }
                }
            }
            if (std::fclose(handle) != 0)
            {   //  close failed
                success = false;
            }
            if (!success)
            {
                buffer.deallocate();
            }
        }
    }
    return buffer;
}

bool saveFile(const char* const utf8_path, const CByteConstView& view) noexcept
{
    bool success = false;
    if (!view.is_empty())
    {
        path::NativePath std_path = path::makeNativePath(utf8_path);
        if (!std_path.is_empty())
        {
            path::NativePath tmp_path = path::makeTempNativePath(std_path);
            if (!tmp_path.is_empty())
            {
                std::FILE* handle = openFile(tmp_path, EOpenMode::BinaryWrite);
                if (handle != nullptr)
                {
                    const std::size_t size = view.size();
                    success = std::fwrite(view.data(), 1, size, handle) == size;
                    if (std::fflush(handle) != 0)
                    {   //  flush failed
                        success = false;
                    }
                    if (std::fclose(handle) != 0)
                    {   //  close failed
                        success = false;
                    }
                    if (!success || !renameFile(tmp_path, std_path))
                    {   //  rename failed or prior failure
                        success = false;
                        removeFile(tmp_path);
                    }
                }
            }
        }
    }
    return success;
}

}	//	namespace platform::filesystem
