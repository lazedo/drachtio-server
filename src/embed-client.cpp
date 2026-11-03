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
#include "embed-client.hpp"
#include "controller.hpp"

namespace drachtio {

    EmbedClient::EmbedClient(ClientController& controller, drachtio_embed_frame_cb onFrame, void* ctx) :
        BaseClient(controller), m_onFrame(onFrame), m_ctx(ctx) {
    }

    EmbedClient::~EmbedClient() {
        DR_LOG(log_debug) << "EmbedClient::~EmbedClient" ;
    }

    void EmbedClient::receive(const string& msg) {
        string msgResponse ;

        /* a socket client is dropped when this returns false; the host stays,
           and gets the refusal */
        if (!processClientMessage(msg, msgResponse)) {
            DR_LOG(log_error) << "EmbedClient::receive - message refused: " << msg.substr(0, 256) ;
        }
        if (!msgResponse.empty()) send(msgResponse) ;
    }

    void EmbedClient::send(const string& str) {
        m_onFrame(m_ctx, str.data(), str.size()) ;
    }
}
