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
