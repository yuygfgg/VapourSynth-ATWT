// ATWT.CPP
// VapourSynth Plugin: Use À Trous Wavelet Transform to extract specified frequency details (supports 8-bit and 16-bit grayscale images)

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "VSHelper4.h"
#include "VapourSynth4.h"

typedef struct {
    VSNode *node;
    int level;
    VSVideoInfo vi;
} ATWTData;

// Generate kernel for the specified level
static void generate_kernel(const double *kernel_base, int base_len, int level, double **kernel_out, int *kernel_len_out) {
    int step = 1 << level;
    int kernel_len = (base_len - 1) * step + 1;
    double *kernel = (double *)malloc(kernel_len * sizeof(double));
    memset(kernel, 0, kernel_len * sizeof(double));
    for (int i = 0; i < base_len; i++) {
        kernel[i * step] = kernel_base[i];
    }
    *kernel_out = kernel;
    *kernel_len_out = kernel_len;
}

// Convolution function with mirror boundary handling
static void convolve_mirror(const double *src, double *dst, int width, int height, const double *kernel, int kernel_len) {
    int half = kernel_len / 2;
    double *temp = (double *)malloc(width * height * sizeof(double));

    // Horizontal convolution
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double sum = 0.0;
            for (int k = -half; k <= half; k++) {
                int idx = x + k;
                if (idx < 0) idx = -idx;
                if (idx >= width) idx = 2 * width - idx - 2;
                sum += src[y * width + idx] * kernel[k + half];
            }
            temp[y * width + x] = sum;
        }
    }

    // Vertical convolution
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double sum = 0.0;
            for (int k = -half; k <= half; k++) {
                int idy = y + k;
                if (idy < 0) idy = -idy;
                if (idy >= height) idy = 2 * height - idy - 2;
                sum += temp[idy * width + x] * kernel[k + half];
            }
            dst[y * width + x] = sum;
        }
    }
    free(temp);
}

// À Trous wavelet transform decomposition
static void atwt_decompose(const double *arr, int width, int height, int levels, double **coeffs) {
    double *arr_current = (double *)malloc(width * height * sizeof(double));
    memcpy(arr_current, arr, width * height * sizeof(double));

    const double kernel_base[] = {1.0 / 16, 1.0 / 4, 3.0 / 8, 1.0 / 4, 1.0 / 16};
    int base_len = sizeof(kernel_base) / sizeof(kernel_base[0]);

    for (int level = 0; level < levels; level++) {
        double *kernel;
        int kernel_len;
        generate_kernel(kernel_base, base_len, level, &kernel, &kernel_len);

        double *arr_smooth = (double *)malloc(width * height * sizeof(double));
        convolve_mirror(arr_current, arr_smooth, width, height, kernel, kernel_len);

        double *detail = coeffs[level];
        for (int i = 0; i < width * height; i++) {
            detail[i] = arr_current[i] - arr_smooth[i];
        }

        free(arr_current);
        arr_current = arr_smooth;
        free(kernel);
    }
    coeffs[levels] = arr_current;
}

// Extract detail coefficients for the specified level
static void atwt_extract(const double *arr, int width, int height, int level, double *detail_coeff) {
    int levels = level;
    double **coeffs = (double **)malloc((levels + 1) * sizeof(double *));
    for (int i = 0; i <= levels; i++) {
        coeffs[i] = (double *)malloc(width * height * sizeof(double));
    }
    atwt_decompose(arr, width, height, levels, coeffs);
    memcpy(detail_coeff, coeffs[level - 1], width * height * sizeof(double));
    for (int i = 0; i <= levels; i++) {
        free(coeffs[i]);
    }
    free(coeffs);
}

// Function to process each frame
static const VSFrame *VS_CC atwtGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ATWTData *d = (ATWTData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        int width = vsapi->getFrameWidth(src, 0);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        int height = vsapi->getFrameHeight(src, 0);

        VSFrame *dst = vsapi->newVideoFrame(fi, width, height, src, core);

        const uint8_t *srcp = vsapi->getReadPtr(src, 0);
        ptrdiff_t src_stride = vsapi->getStride(src, 0);

        uint8_t *dstp = vsapi->getWritePtr(dst, 0);
        ptrdiff_t dst_stride = vsapi->getStride(dst, 0);

        int bits = fi->bitsPerSample;
        double max_value = (double)((1 << bits) - 1);

        double *arr = (double *)malloc(width * height * sizeof(double));
        double *detail_coeff = (double *)malloc(width * height * sizeof(double));

        if (fi->sampleType == stInteger) {
            if (fi->bytesPerSample == 1) {
                // 8-bit integer
                for (int y = 0; y < height; y++) {
                    const uint8_t *src_line = srcp + y * src_stride;
                    for (int x = 0; x < width; x++) {
                        arr[y * width + x] = (double)src_line[x];
                    }
                }
            } else if (fi->bytesPerSample == 2) {
                // 16-bit integer
                for (int y = 0; y < height; y++) {
                    const uint16_t *src_line = (const uint16_t *)(srcp + y * src_stride);
                    for (int x = 0; x < width; x++) {
                        arr[y * width + x] = (double)src_line[x];
                    }
                }
            }
        } else {
            vsapi->mapSetError(vsapi->getFramePropertiesRW(dst), "ATWT: Unsupported pixel format.");
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            free(arr);
            free(detail_coeff);
            return NULL;
        }

        atwt_extract(arr, width, height, d->level, detail_coeff);

        if (fi->sampleType == stInteger) {
            if (fi->bytesPerSample == 1) {
                // 8-bit integer
                for (int y = 0; y < height; y++) {
                    uint8_t *dst_line = dstp + y * dst_stride;
                    for (int x = 0; x < width; x++) {
                        double val = detail_coeff[y * width + x];
                        val = fmin(fmax(val, 0.0), max_value);
                        dst_line[x] = (uint8_t)(val + 0.5);
                    }
                }
            } else if (fi->bytesPerSample == 2) {
                // 16-bit integer
                for (int y = 0; y < height; y++) {
                    uint16_t *dst_line = (uint16_t *)(dstp + y * dst_stride);
                    for (int x = 0; x < width; x++) {
                        double val = detail_coeff[y * width + x];
                        val = fmin(fmax(val, 0.0), max_value);
                        dst_line[x] = (uint16_t)(val + 0.5);
                    }
                }
            }
        }

        free(arr);
        free(detail_coeff);

        vsapi->freeFrame(src);
        return dst;
    }

    return NULL;
    (void) frameData;
}

// Free plugin instance data
static void VS_CC atwtFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ATWTData *d = (ATWTData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);

    (void) core;
}

// Create filter function
static void VS_CC atwtCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ATWTData d;
    ATWTData *data;
    int err;

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d.node);

    if (!vsh::isConstantVideoFormat(vi) || vi->format.colorFamily != cfGray ||
        vi->format.sampleType != stInteger || (vi->format.bitsPerSample != 8 && vi->format.bitsPerSample != 16)) {
        vsapi->mapSetError(out, "ATWT: Only supports 8-bit or 16-bit integer grayscale images.");
        vsapi->freeNode(d.node);
        return;
    }

    d.level = (int)vsapi->mapGetInt(in, "level", 0, &err);
    if (err) {
        vsapi->mapSetError(out, "ATWT: The level parameter must be specified.");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.level < 1) {
        vsapi->mapSetError(out, "ATWT: Level must be at least 1.");
        vsapi->freeNode(d.node);
        return;
    }

    data = (ATWTData *)malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = {{d.node, rpStrictSpatial}};
    vsapi->createVideoFilter(out, "ATWT", vi, atwtGetFrame, atwtFree, fmParallel, deps, 1, data, core);
    (void)userData;
}

// Plugin entry function
VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.yuygfgg.atwt", "atwt", "À Trous Wavelet Transform Plugin", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("ExtractFrequency", "clip:vnode;level:int;", "clip:vnode;", atwtCreate, NULL, plugin);
}
