//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    miniaudio_context.inl
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    31 Oct 26
//
//  Private context initializer derived from miniaudio 0.11.25, commit
//  9634bedb5b5a2ca38c1ee7108a9358a4e233f14d. Original code by David Reid,
//  used under MIT No Attribution (external/miniaudio/LICENSE).
//  Upstream initializer SHA-256 (LF, without trailing whitespace):
//  07161a4908d6e2bc7333a591810c1583422c28f2c54f0b1970a547b130128bd5
//
//  Deliberate changes: require Audio's external log, retain partial native API
//  state, stop at either mutex failure, and release only acquired resources.
//  Backend dispatch/order and platform initialization remain upstream's.
//  tests/audio/check_vendor.py checks provenance and dispatch drift.
//  Include only after the pinned implementation, inside namespace audio.

static_assert(MA_VERSION_MAJOR == 0 && MA_VERSION_MINOR == 11 && MA_VERSION_REVISION == 25,
    "Review the private Audio context initializer when updating miniaudio");

struct SContextLifetime
{
    bool apis_attempted{ false };
    bool backend_ready{ false };
    bool enumeration_mutex_ready{ false };
    bool info_mutex_ready{ false };
    SContextProbe* probe{ nullptr };
};

static ma_result initialise_context_mutex(ma_mutex* mutex, SContextLifetime& lifetime) noexcept
{
    if (lifetime.probe)
    {
        const unsigned attempt = ++lifetime.probe->mutex_attempts;
        if (attempt == lifetime.probe->fail_mutex) return MA_OUT_OF_MEMORY;
    }
    const ma_result result = ma_mutex_init(mutex);
    if (result == MA_SUCCESS && lifetime.probe) ++lifetime.probe->mutex_acquisitions;
    return result;
}

static ma_result release_context(ma_context& context, SContextLifetime& lifetime) noexcept
{
    ma_result result = MA_SUCCESS;
    if (lifetime.backend_ready)
    {
        if (context.callbacks.onContextUninit) result = context.callbacks.onContextUninit(&context);
        lifetime.backend_ready = false;
        if (lifetime.probe) ++lifetime.probe->backend_releases;
    }
    if (lifetime.enumeration_mutex_ready)
    {
        ma_mutex_uninit(&context.deviceEnumLock);
        lifetime.enumeration_mutex_ready = false;
        if (lifetime.probe) ++lifetime.probe->mutex_releases;
    }
    if (lifetime.info_mutex_ready)
    {
        ma_mutex_uninit(&context.deviceInfoLock);
        lifetime.info_mutex_ready = false;
        if (lifetime.probe) ++lifetime.probe->mutex_releases;
    }
    ma_free(context.pDeviceInfos, &context.allocationCallbacks);
    context.pDeviceInfos = nullptr;
    if (lifetime.apis_attempted)
    {
        const ma_result api_result = ma_context_uninit_backend_apis(&context);
        if (result == MA_SUCCESS) result = api_result;
        lifetime.apis_attempted = false;
        if (lifetime.probe) ++lifetime.probe->api_releases;
    }
    return result;
}

static ma_result initialise_context(const ma_backend backends[], ma_uint32 backendCount, const ma_context_config* pConfig, ma_context* pContext, SContextLifetime& lifetime) noexcept
{
    ma_result result;
    ma_backend defaultBackends[ma_backend_null+1];
    ma_uint32 iBackend;
    ma_backend* pBackendsToIterate;
    ma_uint32 backendsToIterateCount;

    if (pContext == NULL) {
        return MA_INVALID_ARGS;
    }

    MA_ZERO_OBJECT(pContext);

    // Audio always supplies a stable, explicitly allocated external log.
    if (pConfig == nullptr || pConfig->pLog == nullptr) return MA_INVALID_ARGS;

    /* Allocation callbacks need to come first because they'll be passed around to other areas. */
    result = ma_allocation_callbacks_init_copy(&pContext->allocationCallbacks, &pConfig->allocationCallbacks);
    if (result != MA_SUCCESS) {
        return result;
    }

    pContext->pLog = pConfig->pLog;

    pContext->threadPriority  = pConfig->threadPriority;
    pContext->threadStackSize = pConfig->threadStackSize;
    pContext->pUserData       = pConfig->pUserData;

    /* Backend APIs need to be initialized first. This is where external libraries will be loaded and linked. */
    // The native loader may fail before CoInitializeEx. Preserve a failure
    // sentinel so its cleanup never calls a missing CoUninitialize function.
#ifdef MA_WIN32
    pContext->win32.CoInitializeResult = -1;
#endif
    lifetime.apis_attempted = true;
    if (lifetime.probe) ++lifetime.probe->api_attempts;
    result = ma_context_init_backend_apis(pContext);
    if (result != MA_SUCCESS) {
        return result;
    }

    for (iBackend = 0; iBackend <= ma_backend_null; ++iBackend) {
        defaultBackends[iBackend] = (ma_backend)iBackend;
    }

    pBackendsToIterate = (ma_backend*)backends;
    backendsToIterateCount = backendCount;
    if (pBackendsToIterate == NULL) {
        pBackendsToIterate = (ma_backend*)defaultBackends;
        backendsToIterateCount = ma_countof(defaultBackends);
    }

    MA_ASSERT(pBackendsToIterate != NULL);

    for (iBackend = 0; iBackend < backendsToIterateCount; iBackend += 1) {
        ma_backend backend = pBackendsToIterate[iBackend];

        /* Make sure all callbacks are reset so we don't accidentally drag in any from previously failed initialization attempts. */
        MA_ZERO_OBJECT(&pContext->callbacks);

        /* These backends are using the new callback system. */
        switch (backend) {
        #ifdef MA_HAS_WASAPI
            case ma_backend_wasapi:
            {
                pContext->callbacks.onContextInit = ma_context_init__wasapi;
            } break;
        #endif
        #ifdef MA_HAS_DSOUND
            case ma_backend_dsound:
            {
                pContext->callbacks.onContextInit = ma_context_init__dsound;
            } break;
        #endif
        #ifdef MA_HAS_WINMM
            case ma_backend_winmm:
            {
                pContext->callbacks.onContextInit = ma_context_init__winmm;
            } break;
        #endif
        #ifdef MA_HAS_COREAUDIO
            case ma_backend_coreaudio:
            {
                pContext->callbacks.onContextInit = ma_context_init__coreaudio;
            } break;
        #endif
        #ifdef MA_HAS_SNDIO
            case ma_backend_sndio:
            {
                pContext->callbacks.onContextInit = ma_context_init__sndio;
            } break;
        #endif
        #ifdef MA_HAS_AUDIO4
            case ma_backend_audio4:
            {
                pContext->callbacks.onContextInit = ma_context_init__audio4;
            } break;
        #endif
        #ifdef MA_HAS_OSS
            case ma_backend_oss:
            {
                pContext->callbacks.onContextInit = ma_context_init__oss;
            } break;
        #endif
        #ifdef MA_HAS_PULSEAUDIO
            case ma_backend_pulseaudio:
            {
                pContext->callbacks.onContextInit = ma_context_init__pulse;
            } break;
        #endif
        #ifdef MA_HAS_ALSA
            case ma_backend_alsa:
            {
                pContext->callbacks.onContextInit = ma_context_init__alsa;
            } break;
        #endif
        #ifdef MA_HAS_JACK
            case ma_backend_jack:
            {
                pContext->callbacks.onContextInit = ma_context_init__jack;
            } break;
        #endif
        #ifdef MA_HAS_AAUDIO
            case ma_backend_aaudio:
            {
                if (ma_is_backend_enabled(backend)) {
                    pContext->callbacks.onContextInit = ma_context_init__aaudio;
                }
            } break;
        #endif
        #ifdef MA_HAS_OPENSL
            case ma_backend_opensl:
            {
                if (ma_is_backend_enabled(backend)) {
                    pContext->callbacks.onContextInit = ma_context_init__opensl;
                }
            } break;
        #endif
        #ifdef MA_HAS_WEBAUDIO
            case ma_backend_webaudio:
            {
                pContext->callbacks.onContextInit = ma_context_init__webaudio;
            } break;
        #endif
        #ifdef MA_HAS_CUSTOM
            case ma_backend_custom:
            {
                /* Slightly different logic for custom backends. Custom backends can optionally set all of their callbacks in the config. */
                pContext->callbacks = pConfig->custom;
            } break;
        #endif
        #ifdef MA_HAS_NULL
            case ma_backend_null:
            {
                pContext->callbacks.onContextInit = ma_context_init__null;
            } break;
        #endif

            default: break;
        }

        if (pContext->callbacks.onContextInit != NULL) {
            ma_log_postf(ma_context_get_log(pContext), MA_LOG_LEVEL_DEBUG, "Attempting to initialize %s backend...\n", ma_get_backend_name(backend));
            result = pContext->callbacks.onContextInit(pContext, pConfig, &pContext->callbacks);
        } else {
            /* Getting here means the onContextInit callback is not set which means the backend is not enabled. Special case for the custom backend. */
            if (backend != ma_backend_custom) {
                result = MA_BACKEND_NOT_ENABLED;
            } else {
            #if !defined(MA_HAS_CUSTOM)
                result = MA_BACKEND_NOT_ENABLED;
            #else
                result = MA_NO_BACKEND;
            #endif
            }
        }

        /* If this iteration was successful, return. */
        if (result == MA_SUCCESS) {
            lifetime.backend_ready = true;
            if (lifetime.probe) ++lifetime.probe->backend_acquisitions;
            pContext->backend = backend;
            result = initialise_context_mutex(&pContext->deviceEnumLock, lifetime);
            if (result != MA_SUCCESS) return result;
            lifetime.enumeration_mutex_ready = true;
            result = initialise_context_mutex(&pContext->deviceInfoLock, lifetime);
            if (result != MA_SUCCESS) return result;
            lifetime.info_mutex_ready = true;

            ma_log_postf(ma_context_get_log(pContext), MA_LOG_LEVEL_DEBUG, "System Architecture:\n");
            ma_log_postf(ma_context_get_log(pContext), MA_LOG_LEVEL_DEBUG, "  Endian: %s\n", ma_is_little_endian() ? "LE"  : "BE");
            ma_log_postf(ma_context_get_log(pContext), MA_LOG_LEVEL_DEBUG, "  SSE2:   %s\n", ma_has_sse2()         ? "YES" : "NO");
            ma_log_postf(ma_context_get_log(pContext), MA_LOG_LEVEL_DEBUG, "  AVX2:   %s\n", ma_has_avx2()         ? "YES" : "NO");
            ma_log_postf(ma_context_get_log(pContext), MA_LOG_LEVEL_DEBUG, "  NEON:   %s\n", ma_has_neon()         ? "YES" : "NO");

            pContext->backend = backend;
            return result;
        } else {
            if (result == MA_BACKEND_NOT_ENABLED) {
                ma_log_postf(ma_context_get_log(pContext), MA_LOG_LEVEL_DEBUG, "%s backend is disabled.\n", ma_get_backend_name(backend));
            } else {
                ma_log_postf(ma_context_get_log(pContext), MA_LOG_LEVEL_DEBUG, "Failed to initialize %s backend.\n", ma_get_backend_name(backend));
            }
        }
    }

    /* If we get here it means an error occurred. */
    // Keep acquired resource state available to release_context().
    return MA_NO_BACKEND;
}
