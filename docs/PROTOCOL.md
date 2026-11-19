# Remote Mouse protocol, version 1

The phone (client) and the PC server speak a tiny binary protocol. It is
header-only in [`shared/protocol.h`](../shared/protocol.h), which is the single
source of truth for both sides and for the tests.

## Transports

| Purpose | Transport | Port |
| --- | --- | --- |
| Pairing, keepalive and input | TCP | 47824 (`kDefaultPort`) |
| Server discovery | UDP broadcast | 47824 (`kDefaultDiscoveryPort`) |

TCP sockets are used with `TCP_NODELAY`: latency matters more than packing
moves. All integers are **little-endian** regardless of host byte order.

## Frame format

Every TCP message starts with an 8-byte header:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `4D 43 54 4C` (`"MCTL"`) |
| 4 | 1 | protocol version, currently `1` |
| 5 | 1 | message type |
| 6 | 2 | payload length in bytes (u16, little-endian) |
| 8 | n | payload |

Limits:

* Payload ≤ 1024 bytes (`kMaxPayload`).
* Strings are `u16 length` followed by UTF-8 bytes, at most 255 bytes.

A receiver reads the header, validates the magic and version, waits for the
whole payload, then dispatches. Unknown message types are ignored so newer
clients cannot break older servers.

## Message types

| Type | Name | Direction | Payload |
| --- | --- | --- | --- |
| `0x02` | `SERVER_HELLO` | server → client | `u8 version`, `u8 attemptsLeft`, `str name` |
| `0x03` | `AUTH` | client → server | `u32 pin`, `str deviceName` |
| `0x04` | `AUTH_RESULT` | server → client | `u8 ok`, `u8 attemptsLeft`, `str message` |
| `0x10` | `MOVE` | client → server | `i16 dx`, `i16 dy` |
| `0x11` | `BUTTON` | client → server | `u8 button`, `u8 pressed` |
| `0x12` | `WHEEL` | client → server | `i16 dx`, `i16 dy` |
| `0x13` | `PING` | client → server | `u32 token` |
| `0x14` | `PONG` | server → client | `u32 token` |
| `0x15` | `TEXT` | client → server | `str text` (UTF-8) |
| `0x16` | `KEY` | client → server | `u8 key` |
| `0x20` | `ERROR` | server → client | `u8 code`, `str message` |

`button`: `1` = left, `2` = right, `3` = middle. `pressed`: `1`/`0`, an
absolute state, not a toggle.

`WHEEL` deltas are notches: one notch corresponds to `WHEEL_DELTA` (120) on
Windows. `dy` is positive for scrolling up (away from the user), `dx` positive
for scrolling right. The server clamps each axis to ±30 notches per message.

`MOVE` deltas are relative cursor pixels. The client pre-multiplies finger
movement by its own sensitivity and accumulates sub-pixel remainders; the
server optionally applies its `--sensitivity` factor on top.

`TEXT` contains a non-empty run of printable UTF-8 text (at most 255 bytes).
The Windows server converts it to UTF-16 and uses `KEYEVENTF_UNICODE`, so typing
does not depend on the active Windows keyboard layout. Typed text is never
written to the server log.

`KEY` represents one press-and-release action:

| Value | Key |
| --- | --- |
| 1 | Backspace |
| 2 | Enter |
| 3 | Tab |
| 4 | Escape |
| 5 | Up arrow |
| 6 | Down arrow |
| 7 | Left arrow |
| 8 | Right arrow |

Editing and navigation keys are separate from `TEXT` so Android IME key events
are not sent twice. Both messages were added within protocol version 1: an old
version-1 server ignores them, so keyboard use requires the updated server
bundled with the keyboard-enabled app.

## Session flow

```
client                                   server
  | ------------- TCP connect -------------> |
  | <--------- SERVER_HELLO (3 tries) ------ |
  | ---------------- AUTH ------------------> |
  | <-- AUTH_RESULT ok / wrong PIN --------- |
  | ============ ready for input =========== |
  | ----- MOVE / BUTTON / WHEEL / TEXT / KEY -> |
  | ---------------- PING ------------------> |
  | <--------------- PONG ------------------- |
```

Details:

* The server accepts **one** client. A second connection receives
  `ERROR(Busy)` and is closed immediately.
* Three wrong PINs close the connection after the third `AUTH_RESULT`.
  Each failure is delayed 750 ms. The pairing window is 90 s server-side;
  the client gives up at 120 s, so the server's message normally arrives first.
* After a successful `AUTH_RESULT` the client pings every 2 s and considers the
  link dead after 12 s without a `PONG`. The server drops a session after 20 s
  with no traffic at all.
* When a session ends, the server releases any button that is still held, so a
  lost connection cannot leave the PC's left button latched.

## Example bytes

`SERVER_HELLO`, name `"PC"`, attempts left 3:

```
4D 43 54 4C  01 02  06 00   01 03 02 00 50 43
magic        v  t   len     version 3  "PC"
```

`AUTH`, PIN `123456`, name `"PHONE"`:

```
4D 43 54 4C  01 03  0B 00   40 E2 01 00  05 00  50 48 4F 4E 45
magic        v  t   len     123456       len 5  PHONE
```

`MOVE`, `dx = -3`, `dy = 2`:

```
4D 43 54 4C  01 10  04 00   FD FF  02 00
```

`TEXT`, text `"A"`:

```
4D 43 54 4C  01 15  03 00   01 00 41
```

`KEY`, Backspace:

```
4D 43 54 4C  01 16  01 00   01
```

## Discovery

Discovery is a separate, **unframed** UDP exchange.

Request (client → `255.255.255.255:47824`, also sent to loopback):

```
4D 43 54 44  01
"MCTD"       version
```

Response (server → the source address and port of the probe):

```
4D 43 54 44  01  tcpPort u16  nameLen u16  name (UTF-8)
```

`tcpPort` is the port the TCP listener actually uses, so `--port` does not
break discovery.

Client behaviour:

* probe every 1.5 s,
* remember servers by `ip + tcpPort`,
* forget a server that has not answered for 5 s,
* list sorted by IP so entries do not jump around while the user is tapping.

A server started with `--no-discovery` simply never answers; manual IP entry
still works.

## Error codes

| Code | Name | Meaning |
| --- | --- | --- |
| 1 | `Busy` | another phone is already connected |
| 2 | `BadProtocol` | malformed frame or unexpected message |
| 3 | `AuthFailed` | reserved; failures use `AUTH_RESULT` instead |
| 4 | `Timeout` | reserved |
| 5 | `Unsupported` | reserved |

On `ERROR` the server closes the connection.

## Security

The protocol has no encryption or message authentication. The PIN is sent in
the clear and the TCP stream is trivially replayable by anyone who can observe
the LAN. Use it on trusted networks only; treat the PIN as a convenience lock,
not as a security boundary.
