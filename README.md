# openh264-esp32

**H.264 decoding — Baseline, Main *and* High profile — on the ESP32, built from source, no assembly.**

[![ESP-IDF build](https://github.com/jonasborn/openh264-esp32/actions/workflows/esp32-idf-build.yml/badge.svg?branch=master)](https://github.com/jonasborn/openh264-esp32/actions/workflows/esp32-idf-build.yml)

This is a community fork of [cisco/openh264](https://github.com/cisco/openh264) that adds a
small **ESP32 / ESP-IDF port of the decoder**. It is packaged as a drop-in ESP-IDF
component. The codec itself is unmodified upstream code — see
[*What changed*](#what-changed) for the complete (tiny) patch set, and
[`README.upstream.md`](README.upstream.md) for the original OpenH264 readme.

> `master` tracks upstream [cisco/openh264](https://github.com/cisco/openh264) and carries
> the ESP-IDF port on top. The port was developed and hardware-verified against the
> `v2.6.0` line.

---

## Why this exists

The H.264 decoders normally available on the ESP32 —
[`espressif/esp_h264`](https://components.espressif.com/components/espressif/esp_h264)
and the tinyH264 it is built on — implement **Constrained Baseline profile only**. The
moment they meet a **Main** or **High** profile stream (which is what essentially every
IP camera, phone and hardware encoder produces) they bail out at the SPS:

```
H264_DEC: profile_idc is error
H264_DEC: Decode sequence parameter set error.
```

The ESP32-S3 has **no hardware H.264 decoder** (that arrived with the ESP32-P4). So to
decode a real-world High-profile stream on an S3 you need a full software decoder.
OpenH264 is one — it just was never packaged for ESP-IDF or built without its x86/ARM
assembly. This fork does both.

## Verified on hardware

| | |
|---|---|
| Board | ESP32-S3 (ESP32-S3-PICO-1, 8 MB octal PSRAM) |
| Toolchain | ESP-IDF 6.0, `xtensa-esp-elf` GCC 15 |
| Stream | Tapo C100 RTSP substream — **H.264 High profile**, `profile-level-id=640016`, 640×360 |
| Decode | one IDR keyframe → I420 in **~400 ms** |
| Then | JPEG-encoded on-device (`esp_new_jpeg`) → full frame in **~0.6 s** |

Baseline, Main and High profile bitstreams all decode. CABAC and CAVLC, 4×4 and 8×8
transforms, scaling matrices, deblocking — all upstream, all unmodified.

---

## Quick start (ESP-IDF component)

Add the repo to your project as a component:

```bash
cd your-project
git submodule add https://github.com/jonasborn/openh264-esp32 components/openh264
```

Require it from your component:

```cmake
# main/CMakeLists.txt
idf_component_register(SRCS "app_main.c"
                       REQUIRES openh264)
```

Include the API (the component puts `codec/api/wels` on your include path):

```c
#include "codec_api.h"
```

That is all the wiring. `pio run` / `idf.py build` compiles ~37 decoder + common
`.cpp` files into `libopenh264.a` (adds roughly **170 KB** to the final image).

## Decoding a keyframe → I420

OpenH264's no-delay path wants **one NAL per call**, and it holds the finished picture
in its reorder buffer until you pull it with `FlushFrame()`. Both are easy to get wrong,
so here is the whole dance:

```cpp
#include "codec_api.h"
#include "codec_app_def.h"

// annexb = SPS + PPS + IDR, each prefixed with 00 00 00 01 (or 00 00 01)
bool decode_keyframe(const uint8_t *annexb, int len,
                     const uint8_t **Y, const uint8_t **U, const uint8_t **V,
                     int *w, int *h, int *y_stride, int *c_stride)
{
    ISVCDecoder *dec = nullptr;
    if (WelsCreateDecoder(&dec) != 0 || !dec) return false;

    SDecodingParam p{};
    p.uiTargetDqLayer             = (unsigned char)-1;
    p.eEcActiveIdc                = ERROR_CON_SLICE_MV_COPY_CROSS_IDR_FREEZE_RES_CHANGE;
    p.sVideoProperty.size         = sizeof(p.sVideoProperty);
    p.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    if (dec->Initialize(&p) != 0) { WelsDestroyDecoder(dec); return false; }

    uint8_t   *planes[3] = {};
    SBufferInfo info{};

    // 1. feed NAL by NAL
    for (int i = 0; i + 4 <= len && info.iBufferStatus != 1; ) {
        int sc = (annexb[i+2] == 1) ? 3 : 4;               // start-code length
        int j  = i + sc;
        while (j + 3 <= len && !(annexb[j] == 0 && annexb[j+1] == 0 &&
              (annexb[j+2] == 1 || (annexb[j+2] == 0 && annexb[j+3] == 1)))) j++;
        if (j + 3 > len) j = len;                          // last NAL to the end
        planes[0] = planes[1] = planes[2] = nullptr;
        info = {};
        dec->DecodeFrameNoDelay(annexb + i, j - i, planes, &info);
        i = j;
    }

    // 2. the frame is decoded but buffered - flush it out
    if (info.iBufferStatus != 1) {
        int32_t eos = 1;
        dec->SetOption(DECODER_OPTION_END_OF_STREAM, &eos);
        for (int k = 0; k < 4 && info.iBufferStatus != 1; k++) {
            planes[0] = planes[1] = planes[2] = nullptr;
            info = {};
            dec->FlushFrame(planes, &info);
        }
    }

    bool ok = info.iBufferStatus == 1 && planes[0];
    if (ok) {
        *Y = planes[0]; *U = planes[1]; *V = planes[2];
        *w = info.UsrData.sSystemBuffer.iWidth;
        *h = info.UsrData.sSystemBuffer.iHeight;
        *y_stride = info.UsrData.sSystemBuffer.iStride[0];  // NOTE: >= width
        *c_stride = info.UsrData.sSystemBuffer.iStride[1];
    }
    dec->Uninitialize();
    WelsDestroyDecoder(dec);
    return ok;
}
```

The planes are **stride-padded** (e.g. a 640-wide frame comes back with `iStride[0] == 704`);
copy row by row if your consumer wants tight `width*height` data.

## PSRAM

The decoder's working set for a 640×360 frame is **~2–3 MB** (reference frames + scratch).
This fork routes OpenH264's `WelsMalloc` through
`heap_caps_malloc(MALLOC_CAP_SPIRAM)` so it lands in PSRAM and never competes with WiFi
for the small internal DMA heap. If the chip has no PSRAM it falls back to `malloc`
(you then need enough internal RAM, i.e. only small frames).

Enable PSRAM in your `sdkconfig` as usual (`CONFIG_SPIRAM=y` + the correct
`CONFIG_SPIRAM_MODE_*` for your module — **octal** for N8R8 / S3-PICO-1, quad for R2).

## Performance & footprint

| | 640×360 High profile IDR, ESP32-S3 @ 240 MHz |
|---|---|
| Decode to I420 | ~400 ms |
| Peak heap (PSRAM) | ~3 MB during decode, freed after |
| Flash added | ~170 KB (`libopenh264.a`, decoder + common, `-O2`) |
| Internal RAM added | negligible (buffers go to PSRAM) |

Single keyframe / periodic-snapshot use is comfortable. Real-time video is not the goal
of a pure-software decoder on a 240 MHz core.

## Supported targets

CI builds the decoder for every push:

| Target | Arch | IDF 5.3 | IDF 5.4 |
|---|---|:-:|:-:|
| `esp32s3` | Xtensa LX7 | ✅ | ✅ |
| `esp32`   | Xtensa LX6 | ✅ | ✅ |
| `esp32p4` | RISC-V     | –  | ✅ |

The port is plain portable C/C++; other targets should work but aren't in the matrix.

## What's included / not included

**Included:** the OpenH264 **decoder** (`codec/decoder/**`) + `codec/common/**`, portable
C only.

**Not included / not wired:**
- **Encoder.** OpenH264's encoder is here in the tree but not in the component; it is
  Constrained-Baseline-only by design and outside this fork's scope.
- **Assembly / SIMD.** X86 and NEON paths are compiled out; scalar C only.
- **Multi-threaded decode.** `WelsQueryLogicalProcessInfo` reports 1 core; run the
  decoder single-threaded.

<a name="what-changed"></a>
## What changed vs. upstream

New files (build glue, not codec changes): `CMakeLists.txt`, `idf_component.yml`,
`NOTICE`, `ci/`, `.github/workflows/esp32-idf-build.yml`, this `README.md`
(original kept as `README.upstream.md`).

Source changes — **3 files, 7 hunks**, every platform path guarded by
`#if defined(ESP_PLATFORM)`:

| File | Change |
|---|---|
| `codec/common/src/WelsThreadLib.cpp` | ① don't `#include <sys/sysctl.h>` (not in newlib) ② skip `pthread_attr_setscope` / `setschedpolicy` (absent from ESP-IDF pthread; decoder is single-threaded here anyway) ③ `WelsQueryLogicalProcessInfo` → `ProcessorCount = 1` |
| `codec/common/src/memory_align.cpp` | `WelsMalloc` allocates via `heap_caps_malloc(MALLOC_CAP_SPIRAM)` (falls back to `malloc`) |
| `codec/decoder/core/src/decoder_core.cpp` | `ExpandBsBuffer` / `ExpandBsLenBuffer` **definitions** used bare `int` where `decoder_core.h` declares `int32_t`. On toolchains where `int32_t` is `long` (`xtensa-esp-elf`) the C++ mangled names don't match the call sites → link failure. **This is an upstream bug**, still present on `master`. |

Nothing in the decode logic (CABAC, transforms, intra prediction, deblocking) is touched.

## Building the smoke test locally

```bash
cd ci/smoke
idf.py set-target esp32s3
idf.py build
```

It creates a decoder, `Initialize()`s and tears it down — a link + init check for the
component on your target. A full bitstream example is the snippet above.

## Licensing

OpenH264 source is **BSD-2-Clause** (`LICENSE`, © 2013 Cisco Systems). This fork's
changes are under the same licence.

**Community build — please read [`NOTICE`](NOTICE).** In short: a build made from this
source is *not* the binary module Cisco distributes from openh264.org, and Cisco's
patent-pool royalty arrangement for that module does not extend to it. H.264 is
patent-encumbered; the BSD licence grants no patent rights. If you ship an H.264 decoder
built from this source, determining and obtaining any required patent licence for your
product and territory is on you. No patent grant, no legal advice is offered here.

## Credits

- **OpenH264** — Cisco Systems and the OpenH264 contributors. All decoding is their work.
- ESP-IDF port — this fork.
