# Apotris on PS Vita — porting status

## About the CMakeLists.txt you originally pasted
That file wasn't from Apotris — it's the build script for a different
project: an Android `.so` loader (running a proprietary "Gun Bros" native
library inside an emulated JNI environment via FalsoJNI). That approach
doesn't apply here. Apotris already has real native ports (GBA, PC/Linux,
Switch, 3DS, Android, Linux-handheld/"portmaster") built with **meson**,
so a Vita port follows that same shape: a new platform backend
(`liba_vita.cpp`/`.h`) plus a new meson cross-file, not a CMake/.so-loader
setup.

## What's done in this pass
- `include/liba_vita.h`, `source/liba_vita.cpp` — new SDL2-based backend,
  adapted directly from `liba_switch.cpp` (the closest existing template —
  Switch is also SDL2-based, unlike 3DS's custom software renderer).
- Added a `VITA` preprocessor branch alongside `SWITCH`/`PORTMASTER` in
  every shared file that needed it: `platform.hpp`, `liba_pc.h` (screen
  size), `scene.hpp`, `sceneWindow.hpp`, `sceneControls.hpp`,
  `liba_sdl_audio.cpp`, `main.cpp`.
- Excluded Vita from WebRTC multiplayer (`LinkWebRTC.hpp`) and local
  wireless "Multi Battle" (`screenTitle.cpp`), same as Switch — no local
  multiplayer transport is implemented for either console.
- `meson/vita.ini` — draft cross-file for the VitaSDK `arm-vita-eabi`
  toolchain.
- `meson.build` — added `vita` alongside `horizon` in the dependency
  branch, the `-DVITA` define, link flags (`-Wl,-q`, needed by
  `vita-elf-create`), and a `vita` branch in the final packaging step.
- `tools/vitaMakeVPK.py` — draft packaging script chaining
  `vita-elf-create` → `vita-make-fself` → `vita-mksfoex` →
  `vita-pack-vpk` into a `.vpk`, mirroring `tools/switchMakeNRO.py`.

Rumble is intentionally a no-op: the original Vita and PSTV have no
vibration motor, unlike the Switch's HD rumble.

## What's genuinely unverified (I have no VitaSDK toolchain here)
I don't have VITASDK installed in this environment, and installing the
full `arm-vita-eabi` cross-toolchain isn't practical here (it's a large
prebuilt toolchain from vitasdk's own buildbot, not something this sandbox
can reach or reasonably build from scratch). Everything above is written
by careful analogy with the existing Switch/Portmaster backends, but **none
of it has been compiled**. Treat it as a first draft, not a working build.

The real risk isn't the game code — it's the dependency stack. Every
non-GBA/3DS platform (including this one) pulls in, as meson subprojects
built from source: **Tilengine**, **SoLoud**, **openmpt**, **SDL2**,
**SDL2_mixer**, **opus/ogg**, **nlohmann_json**. Two of these are the real
open questions for Vita:

1. **SDL2** — VitaSDK's Vita backend for SDL2 is a *patched fork*
   (distributed via VitaSDK's package manager, vdpm), not something that
   exists in vanilla upstream SDL2. The `subprojects/sdl2.wrap` in this
   repo fetches vanilla SDL2 sources, which has no Vita target at all —
   cross-compiling it with `meson/vita.ini` as-is will fail. You'll likely
   need to either point that subproject at VitaSDK's SDL2 fork, or install
   it via vdpm and expose it as a system `dependency('sdl2')` instead of
   building the wrap from source.
2. **Tilengine / SoLoud / openmpt** — these have never (as far as I know)
   been cross-compiled for Vita. They may well just work once SDL2 is
   sorted (their build systems look fairly portable C/C++), but that's a
   real unknown, not something I can confirm without the toolchain.

I already excluded `cpr` (curl) and `libdatachannel` (WebRTC multiplayer)
for `vita`, the same way they're already skipped for `horizon` (Switch) —
so you don't need to cross-compile OpenSSL/curl/libdatachannel for this.

## Live build log (2026-09-17)
Real feedback from an actual attempt, updated as we go:

- `cmake ..` got past the compiler checks and **found SDL2, SDL2_mixer,
  modplug, libxmp/libxmp-lite, Vorbis, and OpusFile** via vdpm without any
  extra work — good sign that the "SDL2 needs the Vita fork" concern is
  handled correctly by going through CMake/vdpm rather than meson's
  from-source wrap.
- It then failed on `find_package(Tilengine)` — no installed CMake config
  for it yet, because it's never been built. Fix: build+install Tilengine
  itself first (see below), into `$VITASDK/arm-vita-eabi` so our
  `find_package` picks it up automatically.
- `find_package(SoLoud)` was optimistic on my part — upstream SoLoud has
  no official CMake build at all. I've since replaced that in
  `CMakeLists.txt` with directly compiling SoLoud's sources ourselves
  (`file(GLOB ...)` over `src/core`, `src/audiosource`, `src/filter`, plus
  the SDL2 backend file) — the standard way to consume it without relying
  on its own build system. This still needs the submodule fetched first.

### Getting the submodules
The zip/GitHub-zip-export doesn't include git submodules. In your real
checkout:
```
git submodule update --init subprojects/Tilengine subprojects/SoLoud
```
Both are hosted on gitea.com (`gitea.com/Apotris/Tilengine.git` branch
`static`, `gitea.com/Apotris/SoLoud.git` branch `main`) — I don't have
network access to that host from this sandbox, so I can't inspect them
myself. If either directory's layout doesn't match what `CMakeLists.txt`
expects (Tilengine: a real `CMakeLists.txt` at its root; SoLoud:
`src/core/*.cpp`, `src/audiosource/`, `src/filter/`,
`src/backend/sdl2/soloud_sdl2.cpp`), run `ls -R` on it and send me the
output so I can fix the paths.

### Building Tilengine for Vita
```
cd subprojects/Tilengine
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
         -DCMAKE_INSTALL_PREFIX=$VITASDK/arm-vita-eabi \
         -DBUILD_SHARED_LIBS=OFF
make -j$(nproc)
make install
```
If Apotris's `static` branch dropped CMake support in favor of meson-only,
this `cmake ..` step will fail immediately — paste me that error and I'll
adapt (most likely: glob-compile Tilengine's sources directly too, the
same way CMakeLists.txt now handles SoLoud).

## CMakeLists.txt alternative
There's now also a `CMakeLists.txt` at the repo root, as an *alternative*
path to meson — not a replacement for meson everywhere else, just an
optional second way to try building the Vita target specifically. It
builds Apotris's actual source list directly via VitaSDK's `vita.cmake`
macros (no `.so` loader, no FalsoJNI — that was stripped out entirely from
the CMake template it started from).

The reason this might be worth trying instead of/alongside the meson
route: it links against `SDL2`/`SDL2_mixer` via `find_package()`, which
(once installed with `vdpm sdl2 sdl2_mixer`) is VitaSDK's actual
Vita-patched fork — sidestepping the vanilla-SDL2-wrap problem described
above for meson. It does **not** solve the Tilengine/SoLoud/openmpt
question, though — those aren't vdpm packages, and the CMakeLists just
`find_package()`s them too, so it'll fail loudly and immediately until
you either find/write Vita CMake builds for them, or wire in their
submodule sources via `add_subdirectory()` yourself. I couldn't check
whether their upstream repos (hosted on gitea.com, which I don't have
network access to from here) already support that.

This file is exactly as unverified as everything else — first draft by
careful analogy, not compiled or tested.

## Suggested next steps (need real hardware/toolchain, which I don't have)
1. Install VitaSDK + vdpm locally: <https://vitasdk.org>.
2. Try `meson setup --cross-file=meson/vita.ini build-vita` and let the
   dependency-resolution errors tell you exactly what's missing — that's
   the fastest way to find out whether SDL2/Tilengine/SoLoud actually
   cross-compile, and I can't shortcut that loop from here.
3. Once it links, the input button mapping in `liba_vita.h` almost
   certainly needs correcting against whatever indices VitaSDK's SDL2
   actually reports — I made an educated guess, not a verified one.
4. Proper `sce_sys/` LiveArea assets (icon0.png, bg0.png, startup.png,
   template.xml) — `sprites/favicon32.bmp` isn't sized/formatted for this
   and isn't wired into `vitaMakeVPK.py` yet.

Happy to keep going on any of these once you've got a toolchain to test
against — in particular I can dig further into the SDL2-fork situation if
you paste back the first build errors you hit.
