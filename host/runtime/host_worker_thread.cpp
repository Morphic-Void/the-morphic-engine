
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_worker_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Host-owned file and image conditioning worker thread.

#include <cstdint>      //  std::uint32_t
#include <utility>      //  std::move

#include "host/runtime/host_worker_thread.hpp"

#include "debug/macros.hpp"
#include "image/codec/tga.hpp"
#include "platform/filesystem/file.hpp"
#include "system/erased_owner.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"

namespace host
{

class CHostWorkerThread
{
public:
    static std::uint32_t MV_STD_ABI_CALL entry_point(void* const user_data) noexcept;

private:
    CHostWorkerThread(const CHostWorkerThread&) noexcept = delete;
    CHostWorkerThread& operator=(const CHostWorkerThread&) noexcept = delete;
    CHostWorkerThread(CHostWorkerThread&&) noexcept = delete;
    CHostWorkerThread& operator=(CHostWorkerThread&&) noexcept = delete;

    explicit CHostWorkerThread(threading::CThreadResources& resources) noexcept : m_context{ resources } {}
    ~CHostWorkerThread() noexcept = default;

    std::uint32_t main() noexcept;
    void startup() noexcept;
    void operate() noexcept;
    void shutdown() noexcept;

    threading::CThreadContext m_context;
};

std::uint32_t MV_STD_ABI_CALL CHostWorkerThread::entry_point(void* const user_data) noexcept
{
    if (user_data == nullptr)
    {
        MV_ERROR("CHostWorkerThread entry received a null user_data pointer");
        return ~0u;
    }

    threading::CThreadResources& resources = *static_cast<threading::CThreadResources*>(user_data);
    CHostWorkerThread thread(resources);
    return thread.main();
}

std::uint32_t CHostWorkerThread::main() noexcept
{
    startup();
    operate();
    shutdown();
    return 0u;
}

void CHostWorkerThread::startup() noexcept
{
    m_context.startup();
    MV_INFO("Worker starting");
    m_context.mark_running();
}

void CHostWorkerThread::operate() noexcept
{
    std::uint32_t epoch = 0u;
    while (!m_context.exit_requested())
    {
        m_context.advance_heartbeat();
        threading::CErasedPodMsg inbound_msg;
        if (m_context.read(inbound_msg))
        {
            MV_TRACE("Worker message received");

            switch (inbound_msg.query_message_type_id().raw_value())
            {
                case k_type_id_v<FileLoadRequest>.raw_value():
                {
                    MV_DETAIL("Worker file load request");

                    FileLoadRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    CErasedOwner content = CErasedOwner::create<LoadedFile>();
                    if (LoadedFile* const result = content.payload<LoadedFile>())
                    {
                        result->buffer = platform::filesystem::loadFile(request.file, 0u, request.alignment);
                    }
                    threading::CErasedOwnerMsg outbound_msg;
                    outbound_msg.set_message_type<FileLoadResult>();
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.set_owner(std::move(content));
                    (void)m_context.post(std::move(outbound_msg));
                    break;
                }
                case k_type_id_v<FileSaveRequest>.raw_value():
                {
                    MV_DETAIL("Worker file save request");

                    FileSaveRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    FileSaveResult result;
                    result.success = platform::filesystem::saveFile(request.file, request.view);
                    threading::CErasedPodMsg outbound_msg;
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.assign_payload(result);
                    (void)m_context.post(outbound_msg);
                    break;
                }
                case k_type_id_v<TgaEncodeRequest>.raw_value():
                {
                    MV_DETAIL("Worker TGA encode request");

                    TgaEncodeRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    CErasedOwner content = CErasedOwner::create<EncodedTga>();
                    if (EncodedTga* const result = content.payload<EncodedTga>())
                    {
                        result->buffer = image::codec::tga::encode(request.view, request.options);
                    }
                    threading::CErasedOwnerMsg outbound_msg;
                    outbound_msg.set_message_type<TgaEncodeResult>();
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.set_owner(std::move(content));
                    (void)m_context.post(std::move(outbound_msg));
                    break;
                }
                case k_type_id_v<TgaDecodeRequest>.raw_value():
                {
                    MV_DETAIL("Worker TGA decode request");

                    TgaDecodeRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    CErasedOwner content = CErasedOwner::create<DecodedTga>();
                    if (DecodedTga* const result = content.payload<DecodedTga>())
                    {
                        result->buffer = image::codec::tga::decode(request.view, result->desc, request.vflip);
                    }
                    threading::CErasedOwnerMsg outbound_msg;
                    outbound_msg.set_message_type<TgaDecodeResult>();
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.set_owner(std::move(content));
                    (void)m_context.post(std::move(outbound_msg));
                    break;
                }
                default:
                {
                    system_type_id unrecognised_id;
                    if (!inbound_msg.query_message_type_id().try_system_type_id(
                            unrecognised_id))
                    {
                        MV_DETAIL("Worker unrecognised LOCAL message type");
                        break;
                    }
                    MV_DETAIL("Worker unrecognised message type {}", unrecognised_id);

                    UnrecognisedMsg unrecognised;
                    unrecognised.msg_id = unrecognised_id;
                    threading::CErasedPodMsg outbound_msg;
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.assign_payload(unrecognised);
                    (void)m_context.post(outbound_msg);
                    break;
                }
            }
        }
        else
        {
            epoch = m_context.wait_for_new_epoch(epoch);
        }
    }
}

void CHostWorkerThread::shutdown() noexcept
{
    m_context.mark_exiting();
    m_context.mark_exited();
    MV_INFO("Worker exited");
}

platform::threading::FThreadEntry host_worker_thread_entry_point() noexcept
{
    return &CHostWorkerThread::entry_point;
}

}   //  namespace host
