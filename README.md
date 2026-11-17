# Remote Mouse

Turn an Android phone into a wireless touchpad for a Windows PC. A tiny C++
server runs in the tray-less console on the PC, injects real mouse input with
`SendInput`, and the phone talks to it over your local WiFi.

```
  Android phone (SDL2 native activity)              Windows PC (Win32)
  ┌───────────────────────────────┐                ┌──────────────────────────┐
  │ touch gestures → PadEvents    │  TCP 47824     │ remote_mouse_server.exe  │
  │ NetClient (framed protocol)   │ ─────────────► │ auth → SendInput/SetPos  │
  │ UDP discovery probe           │ ◄───────────── │ UDP 47824 responder      │
  └───────────────────────────────┘                └──────────────────────────┘
```

No drivers, no root, no cloud service: the phone and the PC only need to be on
the same network.

## Features

* One-finger cursor movement, tap to click, long-press to drag.
* Two-finger tap = right click, three-finger tap = middle click.
* Two-finger vertical drag = natural scrolling.
* Adjustable cursor sensitivity on the touchpad screen.
* Zero-config discovery by UDP broadcast, plus a manual IPv4 entry fallback.
* 6-digit PIN pairing, maximum 3 attempts per connection.
* Latency indicator (ping/pong every 2 s) and automatic disconnect detection.
* Server runs without a window; console keys for status, PIN, kick and quit.
* `--dry-run` mode plus a protocol simulator for testing without a phone.

## Requirements

**PC**

* Windows 10 or 11.
* CMake 3.20+.
* A C++17 compiler: Visual Studio 2022 (Desktop development with C++) or
  MinGW-w64.

**Phone**

* Android 7.0 (API 24) or newer, 64-bit (`arm64-v8a`).
* Android Studio (recommended) or a command-line Android SDK with:
  * SDK Platform 35,
  * NDK (any recent version; side-by-side is fine),
  * CMake 3.22.1,
  * JDK 17.
* The phone and the PC on the same WiFi/LAN.

SDL2 is **not** vendored. The scripts fetch release `2.30.9` into
`third_party/SDL2`, which the Android build expects.

## Quick start

```powershell
# 1. Get SDL2 once (only needed for the Android app).
powershell -ExecutionPolicy Bypass -File scripts\fetch-sdl2.ps1

# 2. Build the Windows server.
powershell -ExecutionPolicy Bypass -File scripts\build-server.ps1

# 3. Run it. The PIN is printed in the console.
build-release\desktop\Release\remote_mouse_server.exe
```

Then build and install the APK (see below), open **Remote Mouse**, tap the PC
name that appears, and type the PIN.

## Build the Windows server

With Visual Studio 2022:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The binaries land in `build\desktop\Release\remote_mouse_server.exe` and
`build\tools\Release\mouse_sim.exe`.

With MinGW-w64:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

or simply `scripts\build-server.ps1` (pass `-Config Debug` for a debug build).

Run the protocol tests on any platform:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

### Server options

```
remote_mouse_server.exe [options]

  --port N            TCP port to listen on (default 47824)
  --discovery-port N  UDP discovery port (default 47824)
  --pin NNNNNN        fixed pairing PIN (default: random each run)
  --name TEXT         name shown on the phone (default: computer name)
  --sensitivity F     cursor multiplier (0.1-10.0, default 1.0)
  --dry-run           log input without moving the real cursor
  --no-discovery      do not answer UDP probes (manual IP still works)
  --verbose, -v       log every input event
  --version           print the protocol version
  --help, -h          show help
```

Console keys while the server runs: `q` quit, `p` print the PIN again,
`s` status, `k` kick the connected device, `h` help. `Ctrl+C` also stops it.

The phone can already scale movement, so the default `--sensitivity 1.0` is
usually right; lower it if the cursor feels too fast, raise it if it crawls.

### Windows Firewall

The first run triggers a Windows Firewall prompt. Allow it on **private**
networks. If discovery finds nothing, add the rules manually (admin shell):

```powershell
netsh advfirewall firewall add rule name="Remote Mouse TCP" dir=in action=allow protocol=TCP localport=47824 profile=private
netsh advfirewall firewall add rule name="Remote Mouse UDP" dir=in action=allow protocol=UDP localport=47824 profile=private
```

## Build the Android app

### Android Studio (recommended)

1. Run `scripts\fetch-sdl2.ps1` (or `scripts/fetch-sdl2.sh`).
2. Open the **`android`** folder (not the repository root) in Android Studio.
3. Accept/install SDK 35, the NDK and CMake 3.22.1 when prompted.
4. Press **Run** with the phone connected over USB.

### Command line

```powershell
cd android
.\gradlew.bat assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

On macOS/Linux use `./gradlew assembleDebug` (run `chmod +x gradlew` once).
Gradle 8.9 is downloaded by the wrapper on first use; JDK 17 must be on
`JAVA_HOME` or `PATH`.

For command-line builds Gradle needs to find the SDK: either set `ANDROID_HOME`
(plus `ANDROID_SDK_ROOT`) or create `android/local.properties` with
`sdk.dir=C\:\\Users\\you\\AppData\\Local\\Android\\Sdk`. The SDK must contain
platform 35, the NDK, and CMake 3.22.1 — Android Studio installs all three when
you open the project and accept the prompts, and `sdkmanager` can do it too:

```powershell
sdkmanager "platforms;android-35" "ndk;27.0.12077973" "cmake;3.22.1"
```

The build only produces `arm64-v8a` and `x86_64` APKs. The x86_64 slice is for
the emulator; on a physical phone the ARM64 one is used.

### Build in the cloud (GitHub Actions)

No local SDK is required. Push the repository to GitHub and the
`.github/workflows/android.yml` workflow builds `app-debug.apk` on a hosted
runner (platform 35, NDK 27.0.12077973, CMake 3.22.1, JDK 17):

```powershell
git init
git add .
git commit -m "Remote Mouse: Android touchpad + Windows server"
git remote add origin https://github.com/<you>/remote-mouse.git
git push -u origin main
```

Open the **Actions** tab, wait for the *Android APK* run (first build
5–15 minutes, cached afterwards), download the
`remote-mouse-debug-apk` artifact, unzip it, then:

```powershell
adb install -r app-debug.apk
```

The debug APK is signed with the standard debug key, so it installs on any
phone without extra setup. `adb` alone is enough locally — `git`, `adb` and a
browser are all this route needs.

## Pairing and controls

1. Start the server on the PC. Note the 6-digit **PAIRING PIN**.
2. Open the app. It broadcasts a discovery probe every 1.5 s.
3. Tap the PC in **AVAILABLE PCS**.
4. Type the PIN on the keypad. Six digits submit automatically.
5. The touchpad appears once the server accepts the PIN.

| Gesture | Action |
| --- | --- |
| One-finger drag | Move the cursor |
| One-finger tap | Left click |
| Long press, then drag | Hold the left button and drag |
| Two-finger tap | Right click |
| Two-finger vertical drag | Scroll (natural direction) |
| Three-finger tap | Middle click |
| `SENS -` / `SENS +` | Lower/raise cursor speed (0.6×–3.0×) |
| `X` in the top-right | Disconnect |

Android's back button goes back one screen (disconnecting when leaving the
touchpad) and exits from the server list.

Each server restart generates a new PIN. Use `--pin` for a fixed one while
testing.

### Manual IP

If broadcasts are blocked (guest WiFi, VLAN, some routers), tap
**ENTER IP MANUALLY**, type the PC's IPv4 address and press **CONNECT**. The
server prints its addresses in the `Local IPv4` line at startup (or run
`ipconfig` on the PC). Manual mode assumes the default TCP port 47824; if you
changed `--port`, use discovery instead.

## Testing without a phone

`mouse_sim` is a small client that speaks the same protocol:

```powershell
# Server in one terminal (dry run: the real cursor is left alone)
build\desktop\Release\remote_mouse_server.exe --dry-run --pin 123456

# Simulator in another
build\tools\Release\mouse_sim.exe --pin 123456
```

Without `--dry-run` the simulator really moves the cursor, clicks and scrolls.

## Troubleshooting

| Symptom | Likely fix |
| --- | --- |
| "SEARCH UNAVAILABLE" / no PC listed | Allow the server in Windows Firewall (TCP **and** UDP 47824), disable client isolation on the router, avoid guest WiFi/VPNs, or use manual IP. |
| "CONNECT FAILED" | Wrong IP, server not running, different subnet, or the port was changed. |
| "WRONG PIN" | Use `p` in the server console to reprint it. Three wrong tries close the connection. |
| "Another device is already connected" | The server accepts one phone at a time; press `k` on the PC console to kick it. |
| Clicks do nothing in elevated apps | Windows blocks input from non-elevated processes. Run the server as administrator if you need to control admin windows. |
| Cursor drifts or is too slow | Adjust `SENS` in the app or the server's `--sensitivity`. |
| Emulator can't discover the host | Use manual IP `10.0.2.2`. |

If the server starts but the phone never sees it, `--verbose` prints discovery
probes and every input event.

## Security and limitations

* **Plaintext on the LAN.** The PIN and all traffic are unencrypted. Anyone who
  can sniff the network can read the PIN or inject input. Use this only on a
  network you trust, and stop the server when you are done.
* The PIN limits each *connection* to 3 tries with a 750 ms penalty, but a new
  TCP connection resets that counter. This is a convenience lock, not a
  hardened authentication system.
* One phone at a time; there is no pairing memory, so the PIN is needed after
  every reconnect (including app restarts).
* The app keeps the phone awake while the touchpad is open, so remember to
  disconnect or leave the app.
* Windows only for the PC side; the phone side is Android only.

## Project layout

```
shared/protocol.h            wire format, framing, discovery codec (header-only)
desktop/src/                 Windows server: sockets, pairing, input injection
android/app/src/main/cpp/    SDL2 app: UI, gestures, TCP/UDP client
android/app/src/main/java/   MainActivity (extends SDLActivity)
tools/mouse_sim.cpp          protocol client used for end-to-end tests
tests/protocol_test.cpp      framing/codec unit tests
scripts/                     SDL2 fetch + build helpers
docs/PROTOCOL.md             byte-level protocol reference
```

SDL2 is built from source by the Android CMake project; no prebuilt `.so` is
checked in. See `docs/PROTOCOL.md` if you want to write another client.
