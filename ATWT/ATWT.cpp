#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "VapourSynth4.h"
#include "VSHelper4.h"

typedef struct {
    VSNode *node;
    const VSVideoInfo *vi;
    int level;
    int mode;
    int kernel[5];
} ATWTData;

// Perform the ATWT transform with user-defined kernel
static void atrous_transform(const uint16_t *src, uint16_t *dst, int width, int height, ptrdiff_t stride, int level, const int kernel[5], int mode) {
    // Allocate intermediate buffers
    uint16_t *c_prev = (uint16_t *)malloc(height * stride * sizeof(uint16_t));
    uint16_t *c_curr = (uint16_t *)malloc(height * stride * sizeof(uint16_t));
    int kernel_sum = 0;

    // Calculate kernel sum for normalization
    for (int i = 0; i < 5; i++) {
        kernel_sum += kernel[i];
    }

    // Copy source data to c_prev as the initial scale
    for (int y = 0; y < height; y++) {
        memcpy(c_prev + y * stride, src + y * stride, width * sizeof(uint16_t));
    }

    // Dilation factor, starting from 1
    int step = 1;

    // Loop for each scale level
    for (int l = 1; l <= level; l++) {
        // Horizontal convolution
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int sum = 0;
                for (int k = -2; k <= 2; k++) {
                    int idx = x + k * step;
                    if (idx < 0)
                        idx = 0;
                    else if (idx >= width)
                        idx = width - 1;
                    sum += c_prev[y * stride + idx] * kernel[k + 2];
                }
                c_curr[y * stride + x] = (uint16_t)((sum + (kernel_sum / 2)) / kernel_sum); // Round to nearest integer
            }
        }

        // Swap buffers
        uint16_t *temp = c_prev;
        c_prev = c_curr;
        c_curr = temp;

        // Vertical convolution
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int sum = 0;
                for (int k = -2; k <= 2; k++) {
                    int idy = y + k * step;
                    if (idy < 0)
                        idy = 0;
                    else if (idy >= height)
                        idy = height - 1;
                    sum += c_prev[idy * stride + x] * kernel[k + 2];
                }
                c_curr[y * stride + x] = (uint16_t)((sum + (kernel_sum / 2)) / kernel_sum); // Round to nearest integer
            }
        }

        // Swap buffers
        temp = c_prev;
        c_prev = c_curr;
        c_curr = temp;

        // Update dilation factor
        step <<= 1; // step *= 2;
    }

    // Compute w[level] = c[level-1] - c[level]
    // After the final convolution, c_prev contains c[level], c_curr contains c[level-1]

    // Calculate the detail coefficients for the specified level
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int diff = (int)c_curr[y * stride + x] - (int)c_prev[y * stride + x];

            // Initialize val to 0 to avoid uninitialized usage.
            int val = 0;
            if (mode == 1) {
                // Shift to unsigned range and enhance contrast
                val = diff * 2 + 32768; // Amplify detail differences and center around 32768
            } else if (mode == 2) {
                // Mode 2: Output absolute value, and black out negative areas
                val = pow(abs(diff), 2); // Take absolute value of the difference
            }

            // Clamp to valid 16-bit range
            if (val < 0)
                val = 0;
            else if (val > 65535)
                val = 65535;

            dst[y * stride + x] = (uint16_t)val;
        }
    }

    // Free intermediate buffers
    free(c_prev);
    free(c_curr);
}

static const VSFrame *VS_CC atwtGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    ATWTData *d = (ATWTData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSVideoFormat *fi = vsapi->getVideoFrameFormat(src);
        int height = vsapi->getFrameHeight(src, 0);
        int width = vsapi->getFrameWidth(src, 0);

        VSFrame *dst = vsapi->newVideoFrame(fi, width, height, src, core);

        int plane;
        for (plane = 0; plane < fi->numPlanes; plane++) {
            const uint16_t *srcp = (const uint16_t *)vsapi->getReadPtr(src, plane);
            ptrdiff_t src_stride = vsapi->getStride(src, plane) / sizeof(uint16_t);
            uint16_t *dstp = (uint16_t *)vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane) / sizeof(uint16_t); // dst_stride is now used for clarity
            (void)dst_stride; // Silence unused variable warning
            int h = vsapi->getFrameHeight(src, plane);
            int w = vsapi->getFrameWidth(src, plane);

            // Perform ATWT transform on each plane
            atrous_transform(srcp, dstp, w, h, src_stride, d->level, d->kernel, d->mode);
        }

        vsapi->freeFrame(src);

        return dst;
    }

    return NULL;
}

static void VS_CC atwtFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    ATWTData *d = (ATWTData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
    (void)core; // Silence unused parameter warning
}

static void VS_CC atwtCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    ATWTData d;
    ATWTData *data;
    int err;

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.vi = vsapi->getVideoInfo(d.node);

    // Only support constant format 16-bit integer input
    if (! vsh::isConstantVideoFormat(d.vi) || d.vi->format.sampleType != stInteger || d.vi->format.bitsPerSample != 16) {
        vsapi->mapSetError(out, "ATWT: Only constant format 16-bit integer input supported.");
        vsapi->freeNode(d.node);
        return;
    }

    d.level = (int)vsapi->mapGetInt(in, "level", 0, &err);
    if (err) {
        vsapi->mapSetError(out, "ATWT: 'level' parameter is required.");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.level < 1) {
        vsapi->mapSetError(out, "ATWT: 'level' must be a positive integer.");
        vsapi->freeNode(d.node);
        return;
    }

    d.mode = (int)vsapi->mapGetInt(in, "mode", 0, &err);
    if (err) {
        d.mode = 1; // Default mode is 1
    }

    // Get user-defined kernel or use default kernel
    const int64_t *kernel = vsapi->mapGetIntArray(in, "kernel", &err); // Changed to int64_t for correct type
    if (err || kernel == NULL) {
        // Default kernel
        d.kernel[0] = 1;
        d.kernel[1] = 4;
        d.kernel[2] = 6;
        d.kernel[3] = 4;
        d.kernel[4] = 1;
    } else {
        // Copy user-defined kernel
        for (int i = 0; i < 5; i++) {
            d.kernel[i] = (int)kernel[i]; // Cast kernel values to int from int64_t
        }
    }

    // Check if image dimensions are large enough
    int min_size = 5 * (1 << (d.level - 1)); // Minimum size to support the specified level of convolution
    if (d.vi->width < min_size || d.vi->height < min_size) {
        vsapi->mapSetError(out, "ATWT: Image dimensions are too small for the specified 'level'.");
        vsapi->freeNode(d.node);
        return;
    }

    data = (ATWTData *)malloc(sizeof(d));
    *data = d;

    VSFilterDependency deps[] = { {d.node, rpStrictSpatial} };
    vsapi->createVideoFilter(out, "ATWT", d.vi, atwtGetFrame, atwtFree, fmParallel, deps, 1, data, core);
    (void)userData; // Silence unused parameter warning
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.yuygfgg.atwt", "atwt", "VapourSynth ATWT Plugin", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Transform", "clip:vnode;level:int;mode:int:opt;kernel:int[]:opt;", "clip:vnode;", atwtCreate, NULL, plugin);
}
