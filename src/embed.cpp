/*
The MIT License (MIT)

Copyright (c) 2026, Luis Azedo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
/* the library's only definition of theOneAndOnlyController (main.cpp is not in it) */
#define DRACHTIO_MAIN

#include <pthread.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <stdexcept>

#include <boost/asio.hpp>
#include <openssl/crypto.h>

#include "controller.hpp"
#include "client-controller.hpp"
#include "embed-client.hpp"
#include "embed.h"

using namespace drachtio ;

namespace {
    const size_t threadStackSize = 8 * 1024 * 1024 ;

    std::atomic<bool> started(false) ;
    std::atomic<bool> ready(false) ;
    std::shared_ptr<EmbedClient> client ;   // written once, before ready

    drachtio_embed_frame_cb frameCb = nullptr ;
    drachtio_embed_event_cb eventCb = nullptr ;
    void* cbCtx = nullptr ;

    /* whether the sofia thread takes work, and the server names (SNI) setting
       the host gave before it did */
    enum SipState { sipNotYet, sipUp, sipGone } ;
    std::mutex sipLock ;
    SipState sipState = sipNotYet ;                 // guarded by sipLock
    bool sniWanted = false ;                        // guarded by sipLock
    drachtio_embed_tls_sni_cb sniCb = nullptr ;     // guarded by sipLock
    unsigned sniTimeout = 0 ;                       // guarded by sipLock

    void setSipState(SipState state) {
        std::lock_guard<std::mutex> lock(sipLock) ;
        sipState = state ;
    }

#ifdef TPORT_SNI_API
    drachtio_embed_tls_sni_cb sipSniCb = nullptr ;  // the sofia thread's copy

    tport_t* sipTports() {
        return nta_agent_tports(theOneAndOnlyController->getAgent()) ;
    }

    /* sofia asks for the certificate of a server name (sofia thread) */
    void onServerName(void*, tport_t*, char const* name) {
        if (sipSniCb) sipSniCb(cbCtx, name) ;
    }

    /* sofia thread */
    void bindServerNames(drachtio_embed_tls_sni_cb cb, unsigned timeoutMs) {
        sipSniCb = cb ;
        if (tport_sni_bind(sipTports(), cb ? onServerName : nullptr, nullptr, timeoutMs) < 0) {
            DR_LOG(log_error) << "drachtio_embed_tls_sni: certificates cannot be chosen by server name: "
                << std::strerror(errno) ;
        }
    }

    /* posts work for the sofia thread: 0, or -1 if it takes none */
    int postToSip(std::function<void()> work) {
        std::lock_guard<std::mutex> lock(sipLock) ;
        if (sipState != sipUp) return -1 ;
        return theOneAndOnlyController->runInSipThread(std::move(work)) ? 0 : -1 ;
    }
#endif

    /* runs on the sofia thread, just before its event loop starts */
    void onServerReady() {
        {
            std::lock_guard<std::mutex> lock(sipLock) ;
            sipState = sipUp ;
#ifdef TPORT_SNI_API
            if (sniWanted) bindServerNames(sniCb, sniTimeout) ;
#endif
        }

        std::shared_ptr<ClientController> cc = theOneAndOnlyController->getClientController() ;
        boost::asio::post(cc->getIOService(), [cc]() {
            client = std::make_shared<EmbedClient>(*cc, frameCb, cbCtx) ;
            cc->join(client) ;
            ready = true ;
            eventCb(cbCtx, DRACHTIO_EMBED_READY, "") ;
        }) ;
    }

    void* runServer(void* arg) {
        std::unique_ptr<std::vector<std::string>> args(static_cast<std::vector<std::string>*>(arg)) ;
        std::vector<char*> argv ;
        for (auto& a : *args) argv.push_back(const_cast<char*>(a.c_str())) ;
        argv.push_back(nullptr) ;

        try {
            DrachtioController::setEmbedded(onServerReady) ;
            theOneAndOnlyController = new DrachtioController(static_cast<int>(args->size()), argv.data()) ;
            theOneAndOnlyController->run() ;
            setSipState(sipGone) ;
            ready = false ;
            eventCb(cbCtx, DRACHTIO_EMBED_STOPPED, "") ;
        }
        catch (std::exception& e) {
            setSipState(sipGone) ;
            ready = false ;
            eventCb(cbCtx, DRACHTIO_EMBED_FAILED, e.what()) ;
        }
        return nullptr ;
    }
}

extern "C" int drachtio_embed_start(int argc, char **argv,
                                    drachtio_embed_frame_cb onFrame,
                                    drachtio_embed_event_cb onEvent,
                                    void *ctx) {
    bool expected = false ;
    if (!started.compare_exchange_strong(expected, true)) return -1 ;

    frameCb = onFrame ;
    eventCb = onEvent ;
    cbCtx = ctx ;

    std::vector<std::string>* args = new std::vector<std::string>(argv, argv + argc) ;

    pthread_attr_t attr ;
    pthread_attr_init(&attr) ;
    pthread_attr_setstacksize(&attr, threadStackSize) ;
#if defined(__linux__)
    /* for the threads drachtio-server starts later (asio, http, stats) */
    pthread_setattr_default_np(&attr) ;
#endif
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) ;

    pthread_t thread ;
    int rc = pthread_create(&thread, &attr, runServer, args) ;
    pthread_attr_destroy(&attr) ;
    if (0 != rc) {
        delete args ;
        started = false ;
        return -2 ;
    }
    return 0 ;
}

extern "C" int drachtio_embed_send(const char *data, size_t len) {
    if (!ready) return -1 ;

    std::shared_ptr<EmbedClient> c = client ;
    std::shared_ptr<ClientController> cc = theOneAndOnlyController->getClientController() ;
    std::string msg(data, len) ;
    boost::asio::post(cc->getIOService(), [c, msg]() { c->receive(msg) ; }) ;
    return 0 ;
}

extern "C" int drachtio_embed_tls_sni(drachtio_embed_tls_sni_cb cb, unsigned timeout_ms) {
#ifdef TPORT_SNI_API
    std::lock_guard<std::mutex> lock(sipLock) ;
    sniWanted = true, sniCb = cb, sniTimeout = timeout_ms ;
    if (sipState == sipNotYet) return 0 ;      // onServerReady() binds it
    if (sipState == sipGone) return -1 ;
    return theOneAndOnlyController->runInSipThread([cb, timeout_ms]() {
        bindServerNames(cb, timeout_ms) ;
    }) ? 0 : -1 ;
#else
    (void)cb, (void)timeout_ms ;
    return -2 ;
#endif
}

extern "C" int drachtio_embed_tls_certificate(const char *name, const char *pem, size_t len) {
#ifdef TPORT_SNI_API
    if (!ready || !pem) return -1 ;

    std::string asked(name ? name : "") ;
    return postToSip([asked, text = std::string(pem, len)]() mutable {
        tport_t* tp = sipTports() ;
        int n = tport_sni_add(tp, text.data(), text.size()) ;

        /* the private key stays in the certificate's context only */
        OPENSSL_cleanse(&text[0], text.size()) ;

        if (n < 0) {
            DR_LOG(log_error) << "drachtio_embed_tls_certificate: the certificate"
                << (asked.empty() ? "" : " given for " + asked) << " cannot be used" ;
        }
        else {
            DR_LOG(log_info) << "drachtio_embed_tls_certificate: serving " << n
                << " server name" << (n == 1 ? "" : "s") << " with a certificate" ;
        }
        if (!asked.empty() && 1 != tport_sni_served(tp, asked.c_str())) {
            if (n >= 0) {
                DR_LOG(log_error) << "drachtio_embed_tls_certificate: the certificate given for "
                    << asked << " is not for it" ;
            }
            /* its handshakes go on with the listener's certificate */
            tport_sni_none(tp, asked.c_str()) ;
        }
    }) ;
#else
    (void)name, (void)pem, (void)len ;
    return -2 ;
#endif
}

extern "C" int drachtio_embed_tls_no_certificate(const char *name) {
#ifdef TPORT_SNI_API
    if (!ready || !name) return -1 ;

    std::string asked(name) ;
    return postToSip([asked]() {
        if (tport_sni_none(sipTports(), asked.c_str()) < 0) {
            DR_LOG(log_error) << "drachtio_embed_tls_no_certificate: not a server name: " << asked ;
        }
    }) ;
#else
    (void)name ;
    return -2 ;
#endif
}

extern "C" int drachtio_embed_tls_forget(const char *name) {
#ifdef TPORT_SNI_API
    if (!ready) return -1 ;

    bool every = nullptr == name ;
    std::string forgotten(name ? name : "") ;
    return postToSip([every, forgotten]() {
        int n = tport_sni_remove(sipTports(), every ? nullptr : forgotten.c_str()) ;
        if (n < 0) {
            DR_LOG(log_error) << "drachtio_embed_tls_forget: not a server name: " << forgotten ;
        }
        else {
            DR_LOG(log_info) << "drachtio_embed_tls_forget: " << n << " server name"
                << (n == 1 ? "" : "s") << " no longer served" ;
        }
    }) ;
#else
    (void)name ;
    return -2 ;
#endif
}
