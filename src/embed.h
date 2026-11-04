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

/* TLS certificates chosen by server name (SNI). A TLS or WSS listener
   presents its own certificate (--tls-cert-file), unless a client asks, in
   the server_name extension, for a name the host gave a certificate for.
   These calls are safe from any thread. They need a sofia-sip with server
   name support (TPORT_SNI_API); without it they return -2. */
#define DRACHTIO_EMBED_TLS 1

/* asks the host for the certificate of a server name that has none; it runs
   on drachtio-server's SIP thread and must not block. The client's handshake
   waits for drachtio_embed_tls_certificate() or
   drachtio_embed_tls_no_certificate() with that name, or for the timeout. */
typedef void (*drachtio_embed_tls_sni_cb)(void *ctx, const char *name);

/* Asks the host (cb, with the ctx of drachtio_embed_start()) for the
   certificates of server names, waiting timeout_ms (0: 2000) for each
   answer. With no answer in time, the handshake goes on with the listener's
   certificate, and the name is not asked for again for 30 s. cb NULL stops
   asking. Can be called before drachtio_embed_start(). Returns 0, -1 if the
   server can no longer be reached, or -2. */
int drachtio_embed_tls_sni(drachtio_embed_tls_sni_cb cb, unsigned timeout_ms);

/* Serves a certificate for the server names it is for: the DNS names of its
   subjectAltName, or its common name when it has none; "*.example.com"
   serves the names one level below example.com. pem has the certificate,
   then any intermediate certificates, and the private key, not encrypted
   (it is copied). name, if not NULL, is the name it answers for: when the
   certificate cannot be used, or is not for that name, the handshakes
   waiting for the name go on with the listener's certificate. The result is
   logged. Returns 0, -1 before DRACHTIO_EMBED_READY or without pem, or -2. */
int drachtio_embed_tls_certificate(const char *name, const char *pem, size_t len);

/* Answers that there is no certificate for a server name: its handshakes go
   on with the listener's certificate, and it is not asked for again for 30 s.
   Returns 0, -1 before DRACHTIO_EMBED_READY or without name, or -2. */
int drachtio_embed_tls_no_certificate(const char *name);

/* Stops serving a server name with a certificate of its own: the name as the
   certificate has it ("*.example.com" for a wildcard), or NULL for every
   name. Connections already up keep their certificate. Returns 0, -1 before
   DRACHTIO_EMBED_READY, or -2. */
int drachtio_embed_tls_forget(const char *name);

#ifdef __cplusplus
}
#endif

#endif
