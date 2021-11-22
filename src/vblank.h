#ifndef VBLANK_H_
#define VBLANK_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86str.h>
#include <randrstr.h>


typedef void (*ms_drm_handler_proc)(uint64_t frame,
                                    uint64_t usec,
                                    void *data);

typedef void (*ms_drm_abort_proc)(void *data);


#ifdef GLAMOR_HAS_GBM

typedef void (*pageflip_handler_cb)(struct LoongsonRec *lsp,
                                    uint64_t frame,
                                    uint64_t usec,
                                    void *data);

typedef void (*pageflip_abort_cb)(struct LoongsonRec *lsp,
                                  void *data);

Bool ms_do_pageflip(ScreenPtr screen,
                    PixmapPtr new_front,
                    void *event,
                    int ref_crtc_vblank_pipe,
                    Bool async,
                    pageflip_handler_cb pHandlerCB,
                    pageflip_abort_cb pAbortCB,
                    const char *log_prefix);

#endif

/**
 * A tracked handler for an event that will hopefully be generated
 * by the kernel, and what to do when it is encountered.
 */
struct ls_drm_queue {
    struct xorg_list list;
    xf86CrtcPtr crtc;
    uint32_t seq;
    void *data;
    ScrnInfoPtr scrn;
    ms_drm_handler_proc handler;
    ms_drm_abort_proc abort;
};


uint32_t ms_drm_queue_alloc(xf86CrtcPtr crtc,
                            void *data,
                            ms_drm_handler_proc handler,
                            ms_drm_abort_proc abort);

typedef enum ms_queue_flag
{
    MS_QUEUE_ABSOLUTE = 0,
    MS_QUEUE_RELATIVE = 1,
    MS_QUEUE_NEXT_ON_MISS = 2
} ms_queue_flag;

Bool ms_queue_vblank(xf86CrtcPtr crtc,
                     ms_queue_flag flags,
                     uint64_t msc,
                     uint64_t *msc_queued,
                     uint32_t seq);

void ms_drm_abort(ScrnInfoPtr scrn,
                  Bool (*match)(void *data, void *match_data),
                  void *match_data);

void ms_drm_abort_seq(ScrnInfoPtr scrn, uint32_t seq);


Bool ls_is_crtc_on(xf86CrtcPtr crtc);
#endif
