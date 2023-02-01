/*
 * Copyright (C) 2023 Loongson Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Sui Jingfeng <suijingfeng@loongson.cn>
 */
#ifndef ETNAVIV_DEVICE_H_
#define ETNAVIV_DEVICE_H_

#include "etnaviv_drmif.h"

/* Enum with indices for each of the feature words */
enum viv_features_word {
   viv_chipFeatures = 0,
   viv_chipMinorFeatures0 = 1,
   viv_chipMinorFeatures1 = 2,
   viv_chipMinorFeatures2 = 3,
   viv_chipMinorFeatures3 = 4,
   viv_chipMinorFeatures4 = 5,
   viv_chipMinorFeatures5 = 6,
   VIV_FEATURES_WORD_COUNT /* Must be last */
};

struct EtnavivRec {
    int fd;
    char *render_node;
    struct etna_device *dev;
    struct etna_gpu *gpu;
    struct etna_pipe *pipe;
    struct etna_cmd_stream *stream;
    struct etna_bo *bo;

    uint32_t model;
    uint32_t revision;
    uint32_t features[VIV_FEATURES_WORD_COUNT];
};

Bool etnaviv_device_init(ScrnInfoPtr pScrn);

#endif
