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

#ifndef LOONGSON_DEBUG_H_
#define LOONGSON_DEBUG_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>
#include <xf86drm.h>
#include <errno.h>

/**
 * This controls whether debug statements (and function "trace" enter/exit)
 * messages are sent to the log file (TRUE) or are ignored (FALSE).
 */
extern _X_EXPORT Bool lsEnableDebug;


#define TRACE_ENTER()                           \
    do {                                        \
        if (lsEnableDebug) {                    \
            xf86DrvMsg(-1,                      \
                X_INFO, "%s:%d: Entering\n",    \
                __func__, __LINE__);            \
        }                                       \
    } while (0)


#define TRACE_EXIT()                            \
    do {                                        \
        if (lsEnableDebug) {                    \
            xf86DrvMsg(-1,                      \
                X_INFO, "%s at %d: Exiting\n",  \
                __func__, __LINE__);            \
        }                                       \
    } while (0)


#define DEBUG_MSG(fmt, ...)                             \
    do {                                                \
        if (lsEnableDebug) {                            \
            xf86Msg(X_INFO, "%s at %d: " fmt "\n",      \
                __func__, __LINE__, ##__VA_ARGS__);     \
        }                                               \
    } while (0)


#define ERROR_MSG(fmt, ...)                             \
    do {                                                \
        xf86DrvMsg(-1, X_ERROR, "%s at %d: " fmt "\n",  \
                 __func__, __LINE__, ##__VA_ARGS__);    \
    } while (0)



#define INFO_MSG(fmt, ...) \
		do { xf86DrvMsg(-1, X_INFO, fmt "\n", \
			##__VA_ARGS__); } while (0)


#define EARLY_INFO_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
			##__VA_ARGS__); } while (0)


#define CONFIG_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, fmt "\n",\
			##__VA_ARGS__); } while (0)


#define WARNING_MSG(fmt, ...) \
		do { xf86DrvMsg(pScrn->scrnIndex, \
			X_WARNING, "WARNING: " fmt "\n",\
			##__VA_ARGS__); \
		} while (0)


#define EARLY_WARNING_MSG(fmt, ...) \
		do { xf86Msg(X_WARNING, "WARNING: " fmt "\n",\
				##__VA_ARGS__); \
		} while (0)


#define EARLY_ERROR_MSG(fmt, ...) \
		do { xf86Msg(X_ERROR, "ERROR: " fmt "\n",\
				##__VA_ARGS__); \
		} while (0)


void LS_PrepareDebug(ScrnInfoPtr pScrn);

#endif
