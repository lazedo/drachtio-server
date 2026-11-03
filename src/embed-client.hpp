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
#ifndef __EMBED_CLIENT_HPP__
#define __EMBED_CLIENT_HPP__

#include "drachtio.h"
#include "client.hpp"
#include "embed.h"

namespace drachtio {

    /* A client in the same process as the server (see embed.h): what a
       socket client reads and writes goes through a callback instead.
       The protocol itself is BaseClient's. */
    class EmbedClient : public BaseClient {
    public:
        EmbedClient(ClientController& controller, drachtio_embed_frame_cb onFrame, void* ctx) ;
        ~EmbedClient() ;

        void start() override {}
        void async_connect() override {}
        void connect_handler(const boost::system::error_code&, boost::asio::ip::tcp::resolver::iterator) override {}
        void read_handler(const boost::system::error_code&, std::size_t) override {}
        void write_handler(const boost::system::error_code&, std::size_t) override {}
        void handle_handshake(const boost::system::error_code&) override {}

        /* one message from the host; runs on the client controller's thread */
        void receive(const string& msg) ;

    protected:
        void send(const string& str) override ;

    private:
        drachtio_embed_frame_cb m_onFrame ;
        void* m_ctx ;
    } ;
}

#endif
