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
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

#include <boost/asio.hpp>

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

    /* runs on the sofia thread, just before its event loop starts */
    void onServerReady() {
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
            ready = false ;
            eventCb(cbCtx, DRACHTIO_EMBED_STOPPED, "") ;
        }
        catch (std::exception& e) {
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
