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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "driver.h"

#include "loongson_entity.h"

// Entity
//
// The smallest independently addressable unit on a system
// bus is referred to as an entity.
//
// So far we know ISA and PCI entities.
// PCI entities can be located on the PCI bus by an unique ID
// consisting of the bus, card and function number.
//

// suijingfeng: I Really want to isolate the static and entity
// related stuff, see what it use

struct loongsonEntRec
{
    int fd;
    int fd_ref;
    /* server generation for which fd has been registered for wakeup handling */
    unsigned long fd_wakeup_registered;
    int fd_wakeup_ref;
    unsigned int assigned_crtcs;
};



static int ls_entity_index = -1;

void LS_SetupEntity(ScrnInfoPtr scrn, int entity_num)
{
    DevUnion *pPriv = NULL;

    xf86SetEntitySharable(entity_num);

    if (ls_entity_index == -1)
    {
        ls_entity_index = xf86AllocateEntityPrivateIndex();
    }

    pPriv = xf86GetEntityPrivate(entity_num, ls_entity_index);
    // suijingfeng: pPriv is actually following type.
    // pPriv = &(xf86Entities[entity_num]->entityPrivates[ls_entity_index]);

    xf86SetEntityInstanceForScreen(scrn, entity_num,
        xf86GetNumEntityInstances(entity_num) - 1);

    if (NULL == pPriv->ptr)
    {
        pPriv->ptr = xnfcalloc(sizeof(struct loongsonEntRec), 1);
    }

    xf86DrvMsg(scrn->scrnIndex, X_INFO,
            "Setup entity: entity_num=%d, entity_index=%d\n",
            entity_num, ls_entity_index);
}

// TODO: replace modesettingPTR with loongsonPTR
//
static struct loongsonEntRec * LS_GetPrivEntity(ScrnInfoPtr pScrn)
{
    DevUnion *pPriv;
    loongsonPtr lsp = loongsonPTR(pScrn);

    pPriv = xf86GetEntityPrivate(lsp->pEnt->index, ls_entity_index);
    return pPriv->ptr;
}


int LS_EntityIncreaseFdReference(ScrnInfoPtr pScrn)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(pScrn);

    ++pLsEnt->fd_ref;

    return pLsEnt->fd_ref;
}


int LS_EntityDecreaseFdReference(ScrnInfoPtr pScrn)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(pScrn);

    --pLsEnt->fd_ref;

    if (0 == pLsEnt->fd_ref)
    {
        // if nobody refernecing me, clear cached fd.
        pLsEnt->fd = 0;
    }

    return pLsEnt->fd_ref;
}


int LS_EntityGetCachedFd(ScrnInfoPtr scrn)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(scrn);

    return pLsEnt->fd;
}


void LS_EntityInitFd(ScrnInfoPtr pScrn, int fd)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(pScrn);

    pLsEnt->fd = fd;
    pLsEnt->fd_ref = 1;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "Init Entity: Caching fd(=%d) and set its rederence to 1.\n", fd);
}


// CRTC Related

void LS_MarkCrtcInUse(ScrnInfoPtr pScrn, int num)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(pScrn);

    /* Mark num'th crtc as in use on this device. */
    pLsEnt->assigned_crtcs |= (1 << num);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "CRTC%d is in use on this screen\n", num);
}


unsigned int LS_GetAssignedCrtc(ScrnInfoPtr pScrn)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(pScrn);
    return pLsEnt->assigned_crtcs;
}


void LS_EntityClearAssignedCrtc(ScrnInfoPtr pScrn)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(pScrn);

    /* Clear mask of assigned crtc's in this generation */
    pLsEnt->assigned_crtcs = 0;
}


//// ugly
unsigned long LS_EntityGetFd_wakeup(ScrnInfoPtr scrn)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(scrn);

    return pLsEnt->fd_wakeup_registered;
}


void LS_EntityInitFd_wakeup(ScrnInfoPtr pScrn, unsigned long serverGen)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(pScrn);

    pLsEnt->fd_wakeup_registered = serverGen;
    pLsEnt->fd_wakeup_ref = 1;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: serverGen: %lu\n",
                                         __func__, serverGen);
}



int LS_EntityIncRef_weakeup(ScrnInfoPtr pScrn)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(pScrn);
    ++pLsEnt->fd_wakeup_ref;


    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: %d, fd_wakeup_ref=%d\n",
                                 __func__, __LINE__, pLsEnt->fd_wakeup_ref);

    return pLsEnt->fd_wakeup_ref;
}

int LS_EntityDecRef_weakeup(ScrnInfoPtr scrn)
{
    struct loongsonEntRec * const pLsEnt = LS_GetPrivEntity(scrn);
    --pLsEnt->fd_wakeup_ref;
    return pLsEnt->fd_wakeup_ref;
}
