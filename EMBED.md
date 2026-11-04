# Embedding drachtio-server

`./configure --enable-embed` also builds `libdrachtio`, drachtio-server as a
shared library that another process runs (for example an Erlang VM, through a
NIF). The host is the only client: there is no admin port, and the messages of
the client protocol travel through two C calls instead of a socket
(`src/embed.h`):

- `drachtio_embed_start(argc, argv, on_frame, on_event, ctx)` starts the server
  on its own thread, with the usual command-line arguments (`-f <config>`).
  `on_event` reports `DRACHTIO_EMBED_READY`, `DRACHTIO_EMBED_FAILED` or
  `DRACHTIO_EMBED_STOPPED`.
- `drachtio_embed_send(data, len)` hands the server one message, exactly what
  a client would send without the `<length>#` prefix; `on_frame` receives the
  server's messages the same way.

The protocol is `BaseClient`'s, unchanged; `EmbedClient` only replaces the
socket. In embedded mode the server throws instead of calling `exit()` on bad
arguments or configuration, opens no admin port (the `<admin>` element is still
required in the configuration file), and does not register `su_deinit` with
`atexit()`.

## TLS certificates by server name

A TLS or WSS listener presents its own certificate (`--tls-cert-file`), unless
a client asks, in the server_name extension (SNI), for a name the host gave a
certificate for. The host can give certificates beforehand, or be asked for
one when a client asks for a name with none:

- `drachtio_embed_tls_sni(cb, timeout_ms)` makes the server call `cb` with
  such a name, on its SIP thread; `cb` must not block. The client's handshake
  waits for the answer without blocking the server. With no answer in
  `timeout_ms` (2000 by default), it goes on with the listener's certificate,
  and the name is not asked for again for 30 s. It can be called before
  `drachtio_embed_start()`.
- `drachtio_embed_tls_certificate(name, pem, len)` answers with a
  certificate, or gives one beforehand (`name` NULL). `pem` has the
  certificate, any intermediate certificates and the private key. It serves
  the DNS names of its subjectAltName, or its common name;
  `*.example.com` serves the names one level below.
- `drachtio_embed_tls_no_certificate(name)` answers that there is none: the
  listener's certificate, without asking again for 30 s.
- `drachtio_embed_tls_forget(name)` stops serving a name (NULL: every name).

These need a sofia-sip with server name support (`TPORT_SNI_API` in
`<sofia-sip/tport.h>`, as in 2600hz/sofia-sip from branch 2600hz-24); with
another one they return -2.

## Building

sofia-sip (LGPL) is linked as a shared library from `$(libdir)`. Its build in
`deps/sofia-sip` only makes a convenience archive, so build it
position-independent and make the shared library from the archive; the other
dependencies are static and position-independent too:

```sh
cd deps/sofia-sip && ./bootstrap.sh && ./configure --prefix=$PREFIX --with-glib=no --with-pic && make
gcc -shared -o $PREFIX/lib/libsofia-sip-ua.so.0 -Wl,-soname,libsofia-sip-ua.so.0 \
    -Wl,--whole-archive libsofia-sip-ua/.libs/libsofia-sip-ua.a -Wl,--no-whole-archive \
    -lssl -lcrypto -lz -lpthread
ln -sf libsofia-sip-ua.so.0 $PREFIX/lib/libsofia-sip-ua.so
cd ../.. && ./autogen.sh && ./configure --prefix=$PREFIX --enable-embed && make && make install
```

This installs `$PREFIX/lib/libdrachtio.so` and `$PREFIX/include/drachtio/embed.h`.
With `--enable-embed` the `drachtio` program is not built.

## Fixes

These change drachtio-server for every client, embedded or not:

- A NOTIFY that terminates a subscription clears its dialog when it arrives,
  as a BYE does. Like the BYE, it is no longer kept among the dialog's
  pending requests, which drachtio-server answers with 500 when the dialog
  goes; the client answers it, and a timer cleans it up after 32 s if the
  client never does.
- A request inside a dialog whose INVITE or SUBSCRIBE is still in progress
  (a NOTIFY before the 2xx to a SUBSCRIBE, an INFO before the 2xx to an
  INVITE) goes to the client that holds that initial transaction, as a
  request inside the dialog. It used to be routed as a new request while
  its server transaction was destroyed, so the caller got 500 and the
  client's answer could not be sent.
- A 2xx to an INVITE we sent whose To tag differs from the 2xx that made the
  dialog comes from another branch of a forking proxy. It used to be taken
  for a retransmission (the first branch's ACK was sent again), so that
  branch never got an ACK or a BYE. It now gets both (RFC 3261 13.2.2.4), the
  BYE with `Reason: SIP ;cause=200 ;text="forked 2xx"`; a retransmission of
  it is acknowledged again. The client keeps the dialog of the first 2xx.
- The invite-in-progress record of a SUBSCRIBE we sent is cleared on its 2xx,
  as there is no ACK to clear it; it used to stay until a final NOTIFY.
- A REFER we send outside a dialog makes a dialog, as a SUBSCRIBE does: it
  carries a Contact, its transaction is kept in progress, and its 2xx (200 or
  202) makes the dialog of its implicit subscription (RFC 3515), whose
  NOTIFYs then reach the client. drachtio-server already did so for a REFER
  it receives; one it sent had no Contact and no dialog, so the NOTIFYs went
  nowhere.
- A reliable provisional response that ends without a PRACK no longer
  crashes the server. sofia-sip calls the PRACK callback with no request
  when it flushes such a response: when a final response is sent before the
  PRACK (a 3xx-6xx, or a 2xx after a reliable response without SDP), or when
  the PRACK never comes (64*T1, then sofia answers 503 itself). The callback
  read that missing request (SIGSEGV); now it only forgets the response.
  Reported upstream as drachtio/drachtio-server#508, fixed by #509.
