# ATWT VapourSynth Plugin

This VapourSynth plugin implements the À Trous Wavelet Transform (ATWT) for extracting frequency details from grayscale images.

**NEVER USE IT FOR ANY PURPOSE. I WROTE IT ONLY FOR LEARNING VS PLUGINS**

## Build Instructions

```bash
meson setup build
ninja -C build
ninja -C build install
```

## Usage

```python
import vapoursynth as vs
core = vs.core

# Load the ATWT plugin
core.std.LoadPlugin(path="/path/to/libATWT.so")

# Load the video
clip = core.ffms2.Source("input_video.mp4")

# Extract the frequency details at level 3
detail = core.atwt.ExtractFrequency(clip=clip, level=3)
detail.set_output()
```

## Parameter Description

- **clip**: The input video node. Only 8-bit or 16-bit grayscale images are supported.
- **level**: The frequency level to extract. The `level` must be at least 1.
