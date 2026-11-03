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
/*
 * Embedding API: runs drachtio-server inside a host process (for example an
 * Erlang VM, through a NIF). The host is the only client: there is no admin
 * port, and the messages of the client protocol (without their length
 * prefix) travel through drachtio_embed_send() and the frame callback.
 *
 * Only one server runs per process. Callbacks run on drachtio-server's
 * threads and must not block.
 */
#ifndef __DRACHTIO_EMBED_H__
#define __DRACHTIO_EMBED_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* events passed to the event callback */
#define DRACHTIO_EMBED_READY   1  /* the host is a client: messages can be sent */
#define DRACHTIO_EMBED_FAILED  2  /* the server did not start; message says why */
#define DRACHTIO_EMBED_STOPPED 3  /* the server's event loop ended */

/* one message of the client protocol, from the server to the host */
typedef void (*drachtio_embed_frame_cb)(void *ctx, const char *data, size_t len);

/* a server event; message is empty unless the event is DRACHTIO_EMBED_FAILED */
typedef void (*drachtio_embed_event_cb)(void *ctx, int event, const char *message);

/* Starts drachtio-server on a new thread, with command-line arguments
   (argv[0] included; argv is copied). Returns 0, -1 if it already started,
   or -2 if the thread could not start. On Linux it also sets the default
   stack of new threads to 8 MiB, which drachtio-server's threads have as a
   program on glibc (musl's default is 128 KiB). */
int drachtio_embed_start(int argc, char **argv,
                         drachtio_embed_frame_cb on_frame,
                         drachtio_embed_event_cb on_event,
                         void *ctx);

/* Hands one message of the client protocol to the server (data is copied).
   Returns 0, or -1 before DRACHTIO_EMBED_READY. Safe from any thread. */
int drachtio_embed_send(const char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
