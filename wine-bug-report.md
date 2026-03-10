# Wine Bug Report Draft

**Product:** Wine
**Component:** mf
**Summary:** Media Foundation topology resolution fails: no MFT_CATEGORY_VIDEO_PROCESSOR registered (NV12→RGB32 color conversion)

---

## Description

Media Foundation's topology loader fails to connect the video pipeline because no `MFT_CATEGORY_VIDEO_PROCESSOR` transform is registered for color space conversion (NV12 → RGB32).

On Windows, the `Color Converter DSP` (`CLSID_CColorConvertDMO`, documented at https://learn.microsoft.com/en-us/windows/win32/medfound/colorconverter) handles this conversion. Wine does not register any equivalent MFT in this category.

### Impact

Any application using `IMFMediaEngine` with H264 video will fail because:
1. The H264 decoder (via GStreamer `avdec_h264`) correctly outputs **NV12**
2. The video frame sink (`IMFMediaEngineNotify`) expects **RGB32** (`MFVideoFormat_RGB32`, GUID `{00000020-0000-0010-8000-00aa00389b71}`)
3. `MFTEnumEx(MFT_CATEGORY_VIDEO_PROCESSOR, NV12→RGB32)` returns **0 results**
4. `topology_branch_connect` returns `0xc00d5212` (`MF_E_TRANSFORM_NOT_POSSIBLE_FOR_CURRENT_MEDIATYPE_COMBINATION`)
5. `session_set_topology` fails with `0xc00d36b9` (`MF_E_TOPO_UNSUPPORTED`)

### Affected application

Wallpaper Engine (Steam AppID 431960) — Scene wallpapers with embedded video textures (`supportsvideo` flag in `project.json`). The application uses `IMFMediaEngine::SetSourceFromByteStream()` to decode `.mp4` video files as texture layers.

### Additional stubs

`IMFMediaEngineEx::SetAutoPlay()` and `IMFMediaEngineEx::SetLoop()` are also stubs — they store the flag internally but the playback engine never consumes it, so video looping does not work even if decoding succeeds.

## Steps to reproduce

1. Run any application that uses `IMFMediaEngine` with an H264 video source
2. The video will fail to play

## Debug trace

With `WINEDEBUG=+mf,+mfplat,+mfmediaengine`:

```
01b0:trace:mfplat:media_engine_SetSourceFromByteStream ... L"video.mp4"
01b0:trace:mfplat:source_resolver_BeginCreateObjectFromByteStream ... L"video.mp4"

# Topology loader runs (not a stub despite FIXME string)
01d0:fixme:mfplat:topology_loader_Load ... stub!

# H264 decoder found and activated successfully
01d0:trace:mfplat:MFTEnumEx MFT_CATEGORY_VIDEO_DECODER, 0x3f, {MFMediaType_Video,MFVideoFormat_H264}, ...
01d0:trace:mfplat:h264_decoder_create ...

# GStreamer pipeline created successfully
wg_transform_create: transform ... input caps video/x-h264, stream-format=(string)byte-stream
wg_transform_create: transform ... output caps video/x-raw, format=(string)I420, width=(int)1920, height=(int)1080
gst_element_factory_create: creating element "avdec_h264"
gst_pad_link_full: linked wgstepper0:src and avdec_h264-0:sink, successful
gst_pad_link_full: linked avdec_h264-0:src and videoconvert0:sink, successful

# Color converter search fails - no VIDEO_PROCESSOR registered
01d0:trace:mfplat:MFTEnumEx MFT_CATEGORY_VIDEO_PROCESSOR, 0x3f, {MFMediaType_Video,MFVideoFormat_NV12}, {MFMediaType_Video,{00000020-0000-0010-8000-00aa00389b71}}, ...
# Returns 0 results

# Pipeline connection fails
01d0:trace:mfplat:topology_branch_connect returning 0xc00d5212
01d0:trace:mfplat:topology_branch_connect returning 0xc00d36b9
01d0:warn:mfplat:session_set_topology failed to load topology ..., hr 0xc00d36b9.
```

## System information

- Wine: 11.4 (also reproduced with Proton/Wine-staging 10.0 via GE-Proton10-32)
- OS: Arch Linux x86_64, kernel 6.19.6
- GStreamer: 1.22.5 (bundled with Proton), h264 decoders available (avdec_h264, openh264dec, nvh264dec)
- GPU: NVIDIA (driver 570.x)

## Suggested fix

Register a GStreamer-backed `MFT_CATEGORY_VIDEO_PROCESSOR` that wraps `videoconvert` for NV12/I420/YV12 → RGB32/ARGB32 conversion. The GStreamer `videoconvert` element is already used internally by the H264 decoder pipeline and supports all necessary format conversions.
