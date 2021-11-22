/*
 * Copyright © 2021 Loongson Corporation
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

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <malloc.h>
#include <inputstr.h>
// XA_ATOM
#include <X11/Xatom.h>
#include <mi.h>
#include <micmap.h>
#include <xf86cmap.h>
#include <xf86DDC.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

#include "driver.h"
#include "sprite.h"


static Bool sprite_realize_realize_cursor(DeviceIntPtr pDev,
                                          ScreenPtr pScreen,
                                          CursorPtr pCursor)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ls = loongsonPTR(pScrn);
    miPointerSpriteFuncPtr pSpriteFuncs = ls->SpriteFuncs;

    return pSpriteFuncs->RealizeCursor(pDev, pScreen, pCursor);
}

static Bool sprite_realize_unrealize_cursor(DeviceIntPtr pDev,
                                            ScreenPtr pScreen,
                                            CursorPtr pCursor)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ls = loongsonPTR(pScrn);
    miPointerSpriteFuncPtr pSpriteFuncs = ls->SpriteFuncs;

    return pSpriteFuncs->UnrealizeCursor(pDev, pScreen, pCursor);
}


/*
 * We hook the screen's cursor-sprite (swcursor) functions
 * to see if a swcursor is active.
 * When a swcursor is active we disabe page-flipping.
 */
static void sprite_do_set_cursor(msSpritePrivPtr sprite_priv,
                                 ScrnInfoPtr pScrn,
                                 int x, int y)
{
    loongsonPtr ls = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ls->drmmode;
    CursorPtr cursor = sprite_priv->cursor;
    Bool sprite_visible = sprite_priv->sprite_visible;

    if (cursor)
    {
        x -= cursor->bits->xhot;
        y -= cursor->bits->yhot;

        sprite_priv->sprite_visible =
            (x < pScrn->virtualX) &&
            (y < pScrn->virtualY) &&
            (x + cursor->bits->width > 0) &&
            (y + cursor->bits->height > 0);
    }
    else
    {
        sprite_priv->sprite_visible = FALSE;
    }

    pDrmMode->sprites_visible += sprite_priv->sprite_visible - sprite_visible;
/*
    suijingfeng: pDrmMode->sprites_visible = 1 most of time.
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "Number of SW cursors visible on this screen: %d.\n",
                    pDrmMode->sprites_visible);
*/
}

static void sprite_set_cursor(DeviceIntPtr pDev,
                              ScreenPtr pScreen,
                              CursorPtr pCursor,
                              int x, int y)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);
    miPointerSpriteFuncPtr pSpriteFuncs = ms->SpriteFuncs;
    msSpritePrivPtr sprite_priv = msGetSpritePriv(pDev, ms, pScreen);

    sprite_priv->cursor = pCursor;
    sprite_do_set_cursor(sprite_priv, pScrn, x, y);

    pSpriteFuncs->SetCursor(pDev, pScreen, pCursor, x, y);
}

static void sprite_move_cursor(DeviceIntPtr pDev,
                               ScreenPtr pScreen,
                               int x, int y)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ls = loongsonPTR(pScrn);
    miPointerSpriteFuncPtr pSpriteFuncs = ls->SpriteFuncs;
    msSpritePrivPtr sprite_priv = msGetSpritePriv(pDev, ls, pScreen);

    sprite_do_set_cursor(sprite_priv, pScrn, x, y);

    pSpriteFuncs->MoveCursor(pDev, pScreen, x, y);
}


static Bool sprite_device_cursor_initialize(DeviceIntPtr pDev,
                                            ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ls = loongsonPTR(pScrn);
    miPointerSpriteFuncPtr pSpriteFuncs = ls->SpriteFuncs;

    return pSpriteFuncs->DeviceCursorInitialize(pDev, pScreen);
}

static void sprite_device_cursor_cleanup(DeviceIntPtr pDev,
                                         ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ls = loongsonPTR(pScrn);
    miPointerSpriteFuncPtr pSpriteFuncs = ls->SpriteFuncs;

    pSpriteFuncs->DeviceCursorCleanup(pDev, pScreen);
}

static miPointerSpriteFuncRec loongson_sprite_funcs = {
    .RealizeCursor = sprite_realize_realize_cursor,
    .UnrealizeCursor = sprite_realize_unrealize_cursor,
    .SetCursor = sprite_set_cursor,
    .MoveCursor = sprite_move_cursor,
    .DeviceCursorInitialize = sprite_device_cursor_initialize,
    .DeviceCursorCleanup = sprite_device_cursor_cleanup,
};


Bool loongson_hookup_sprite(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct LoongsonRec * const ls = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ls->drmmode;

    miPointerScreenPtr PointPriv = dixLookupPrivate(&pScreen->devPrivates,
                                                    miPointerScreenKey);

    if (!dixRegisterScreenPrivateKey(&pDrmMode->spritePrivateKeyRec,
                                     pScreen,
                                     PRIVATE_DEVICE,
                                     sizeof(msSpritePrivRec)))
    {
        return FALSE;
    }

    ls->SpriteFuncs = PointPriv->spriteFuncs;

    PointPriv->spriteFuncs = &loongson_sprite_funcs;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "%s: loongson_sprite_funcs hooked up\n", __func__);

    return TRUE;
}


void loongson_unhookup_sprite(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct LoongsonRec * const ls = loongsonPTR(pScrn);

    miPointerScreenPtr PointPriv = dixLookupPrivate(&pScreen->devPrivates,
                                                    miPointerScreenKey);

    if (PointPriv->spriteFuncs == &loongson_sprite_funcs)
    {
            PointPriv->spriteFuncs = ls->SpriteFuncs;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "%s: PointPriv->spriteFuncs restored\n", __func__);
}
