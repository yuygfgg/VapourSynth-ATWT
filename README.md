# ATWT Plugin
A VapourSynth plugin that performs Anisotropic Wavelet Transform (ATWT).

## Usage
```python
clip = core.atwt.Transform(clip=vnode, int level, int mode=1, int[] kernel=[1, 4, 6, 4, 1])
```

### Parameters:
- **clip**: Input video clip. Only constant format 16-bit integer clips are supported.
- **level**: Number of scales to perform the wavelet transform. Must be a positive integer.
- **mode**: Controls the output format.
  - `1`: Default mode. Outputs the scaled difference between levels.
  - `2`: Mask mode. Outputs the square of absolute value.
- **kernel**: A 5-element integer array representing the convolution kernel. If not provided, a default kernel `[1, 4, 6, 4, 1]` is used. The sum of kernel values is automatically normalized.

### Example
```python
import vapoursynth as vs

core = vs.core
clip = core.ffms2.Source("input_video.mkv")

# Apply ATWT with level 3, absolute value mode, and custom kernel
filtered = core.atwt.Transform(clip=clip, level=3, mode=2, kernel=[1, 2, 3, 2, 1])

filtered.set_output()
```

## Compilation
```bash
meson setup build
ninja -C build
ninja -C build install
```

## License
GPLv3
