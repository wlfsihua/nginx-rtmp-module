/*
 * Copyright (C) AlexWoo(Wu Jie) wj19840501@gmail.com
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp.h"
#include "ngx_rbuf.h"


static void *ngx_rtmp_shared_create_conf(ngx_cycle_t *cycle);
static char *ngx_rtmp_shared_init_conf(ngx_cycle_t *cycle, void *conf);


typedef struct {
    ngx_rtmp_frame_t           *free_frame;
    ngx_pool_t                 *pool;
} ngx_rtmp_shared_conf_t;


static ngx_command_t  ngx_rtmp_shared_commands[] = {

      ngx_null_command
};


static ngx_core_module_t  ngx_rtmp_shared_module_ctx = {
    ngx_string("rtmp_shared"),
    ngx_rtmp_shared_create_conf,            /* create conf */
    ngx_rtmp_shared_init_conf               /* init conf */
};


ngx_module_t  ngx_rtmp_shared_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_shared_module_ctx,            /* module context */
    ngx_rtmp_shared_commands,               /* module directives */
    NGX_CORE_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_rtmp_shared_create_conf(ngx_cycle_t *cycle)
{
    ngx_rtmp_shared_conf_t     *rscf;

    rscf = ngx_pcalloc(cycle->pool, sizeof(ngx_rtmp_shared_conf_t));
    if (rscf == NULL) {
        return NULL;
    }

    return rscf;
}


static char *
ngx_rtmp_shared_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_rtmp_shared_conf_t     *rscf = conf;

    rscf->pool = ngx_create_pool(4096, cycle->log);
    if (rscf->pool == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


void
ngx_rtmp_shared_append_chain(ngx_rtmp_frame_t *frame, size_t size,
        ngx_chain_t *cl, ngx_flag_t mandatory)
{
    ngx_chain_t               **ll;
    u_char                     *p;
    size_t                      len;

    for (ll = &frame->chain; *ll; ll = &(*ll)->next);

    if (cl == NULL) {
        if (mandatory) {
            *ll = ngx_get_chainbuf(size, 1);
        }
        return;
    }

    p = cl->buf->pos;

    for (;;) {
        if (*ll == NULL) {
            *ll = ngx_get_chainbuf(size, 1);
        }

        while ((*ll)->buf->end - (*ll)->buf->last >= cl->buf->last - p) {
            len = cl->buf->last - p;
            (*ll)->buf->last = ngx_cpymem((*ll)->buf->last, p, len);
            cl = cl->next;
            if (cl == NULL) {
                return;
            }
            p = cl->buf->pos;
        }

        len = (*ll)->buf->end - (*ll)->buf->last;
        (*ll)->buf->last = ngx_cpymem((*ll)->buf->last, p, len);
        p += len;

        if ((*ll)->buf->last == (*ll)->buf->end) {
            ll = &(*ll)->next;
        }
    }
}

ngx_rtmp_frame_t *
ngx_rtmp_shared_alloc_frame(size_t size, ngx_chain_t *cl, ngx_flag_t mandatory)
{
    ngx_rtmp_shared_conf_t     *rscf;
    ngx_rtmp_frame_t           *frame;

    rscf = (ngx_rtmp_shared_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                                   ngx_rtmp_shared_module);

    frame = rscf->free_frame;
    if (frame) {
        rscf->free_frame = frame->next;
    } else {
        frame = ngx_pcalloc(rscf->pool, sizeof(ngx_rtmp_frame_t));
        if (frame == NULL) {
            return NULL;
        }
    }

    frame->ref = 1;
    frame->next = NULL;

    ngx_rtmp_shared_append_chain(frame, size, cl, mandatory);

    return frame;
}

void
ngx_rtmp_shared_free_frame(ngx_rtmp_frame_t *frame)
{
    ngx_rtmp_shared_conf_t     *rscf;
    ngx_chain_t                *cl;

    rscf = (ngx_rtmp_shared_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                                   ngx_rtmp_shared_module);

    if (frame == NULL || --frame->ref) {
        return;
    }

    /* recycle chainbuf */
    cl = frame->chain;
    while (cl) {
        frame->chain = cl->next;
        ngx_put_chainbuf(cl);
        cl = frame->chain;
    }

    /* recycle frame */
    frame->next = rscf->free_frame;
    rscf->free_frame = frame;
}
