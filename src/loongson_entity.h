/*
 * Copyright © 2020 Loongson Corporation
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


#ifndef LOONGSON_ENTITY_H_
#define LOONGSON_ENTITY_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86str.h>

//// Entity
void LS_SetupEntity(ScrnInfoPtr scrn, int entity_num);
// bith return the renerence count
int LS_EntityIncreaseFdReference(ScrnInfoPtr pScrn);
int LS_EntityDecreaseFdReference(ScrnInfoPtr pScrn);
// cached fd
int LS_EntityGetCachedFd(ScrnInfoPtr scrn);
void LS_EntityInitFd(ScrnInfoPtr pScrn, int fd);

////
void LS_MarkCrtcInUse(ScrnInfoPtr pScrn, int num);
unsigned int LS_GetAssignedCrtc(ScrnInfoPtr pScrn);
void LS_EntityClearAssignedCrtc(ScrnInfoPtr pScrn);


//// wakeup and server generation related stuff
unsigned long LS_EntityGetFd_wakeup(ScrnInfoPtr scrn);
void LS_EntityInitFd_wakeup(ScrnInfoPtr scrn, unsigned long serverGen);

int LS_EntityIncRef_weakeup(ScrnInfoPtr scrn);
int LS_EntityDecRef_weakeup(ScrnInfoPtr scrn);

#endif
