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

#include <etnaviv_drmif.h>

#include "driver.h"
#include "etnaviv_device.h"
#include "loongson_debug.h"

#define VIV2D_STREAM_SIZE 1024*32

static int etnaviv_report_features(ScrnInfoPtr pScrn,
                                   struct etna_gpu *gpu,
                                   struct EtnavivRec *pEnt)
{
    uint64_t val;

    if (etna_gpu_get_param(gpu, ETNA_GPU_MODEL, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_MODEL");
       goto fail;
    }
    pEnt->model = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Vivante GC%x\n", (uint32_t)val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_REVISION, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_REVISION");
       goto fail;
    }
    pEnt->revision = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "revision %x\n", (uint32_t)val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_0, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_0");
       goto fail;
    }
    pEnt->features[0] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[0]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_1, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_1");
       goto fail;
    }
    pEnt->features[1] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[1]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_2, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_2");
       goto fail;
    }
    pEnt->features[2] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[2]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_3, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_3");
       goto fail;
    }
    pEnt->features[3] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[3]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_4, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_4");
       goto fail;
    }
    pEnt->features[4] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[4]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_5, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_5");
       goto fail;
    }
    pEnt->features[5] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[5]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_6, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_6");
       goto fail;
    }
    pEnt->features[6] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[6]: %lx\n", val);


   if (etna_gpu_get_param(gpu, ETNA_GPU_INSTRUCTION_COUNT, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_INSTRUCTION_COUNT");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ETNA_GPU_INSTRUCTION_COUNT: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "vertex_output_buffer_size: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_VERTEX_CACHE_SIZE, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_VERTEX_CACHE_SIZE");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "vertex_cache_size: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_SHADER_CORE_COUNT, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_SHADER_CORE_COUNT");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "shader_core_count: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_STREAM_COUNT, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_STREAM_COUNT");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "gpu stream count: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_REGISTER_MAX, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_REGISTER_MAX");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "max_registers: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_PIXEL_PIPES, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_PIXEL_PIPES");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "pixel pipes: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_NUM_CONSTANTS, &val)) {
      DEBUG_MSG("could not get %s", "ETNA_GPU_NUM_CONSTANTS");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "num of constants: %lx\n", val);

   #define VIV_FEATURE(screen, word, feature) \
        ((screen->features[viv_ ## word] & (word ## _ ## feature)) != 0)

   /* Figure out gross GPU architecture. See rnndb/common.xml for a specific
    * description of the differences. */
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "GPU arch: we are pre-HALTI\n");

    return 0;
 fail:
    return -1;
}

Bool etnaviv_device_init(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);

        struct EtnavivRec *pGpu = &lsp->etnaviv;
        struct etna_device *dev;
        struct etna_gpu *gpu;
        struct etna_pipe *pipe;
        struct etna_cmd_stream *stream;
        uint64_t model, revision;
        int fd;

        fd = drmOpenWithType("etnaviv", NULL, DRM_NODE_PRIMARY);
        if (fd != -1)
        {
            drmVersionPtr version = drmGetVersion(fd);
            if (version)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Version: %d.%d.%d\n",
                           version->version_major, version->version_minor,
                           version->version_patchlevel);
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Name: %s\n", version->name);
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Date: %s\n", version->date);
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Description: %s\n", version->desc);
                drmFreeVersion(version);
            }
        }

        dev = etna_device_new(fd);

        /* we assume that core 0 is a 2D capable one */
        gpu = etna_gpu_new(dev, 0);
        pipe = etna_pipe_new(gpu, ETNA_PIPE_2D);

        stream = etna_cmd_stream_new(pipe, VIV2D_STREAM_SIZE, NULL, NULL);

        pGpu->fd = fd;
        pGpu->dev = dev;
        pGpu->gpu = gpu;
        pGpu->pipe = pipe;
        pGpu->stream = stream;

        etna_gpu_get_param(gpu, ETNA_GPU_MODEL, &model);
        etna_gpu_get_param(gpu, ETNA_GPU_REVISION, &revision);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "EXA: Vivante GC%x GPU revision %x found!\n",
                   (uint32_t)model, (uint32_t)revision);

        etnaviv_report_features(pScrn, gpu, pGpu);

        return TRUE;
 }
