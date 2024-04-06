/*
 * Copyright (c) 2010-2011, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Sidertribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Sidertributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Sidertributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Sider nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __HIREDIS_AE_H__
#define __HIREDIS_AE_H__
#include <sys/types.h>
#include <ae.h>
#include "../hisider.h"
#include "../async.h"

typedef struct siderAeEvents {
    siderAsyncContext *context;
    aeEventLoop *loop;
    int fd;
    int reading, writing;
} siderAeEvents;

static void siderAeReadEvent(aeEventLoop *el, int fd, void *privdata, int mask) {
    ((void)el); ((void)fd); ((void)mask);

    siderAeEvents *e = (siderAeEvents*)privdata;
    siderAsyncHandleRead(e->context);
}

static void siderAeWriteEvent(aeEventLoop *el, int fd, void *privdata, int mask) {
    ((void)el); ((void)fd); ((void)mask);

    siderAeEvents *e = (siderAeEvents*)privdata;
    siderAsyncHandleWrite(e->context);
}

static void siderAeAddRead(void *privdata) {
    siderAeEvents *e = (siderAeEvents*)privdata;
    aeEventLoop *loop = e->loop;
    if (!e->reading) {
        e->reading = 1;
        aeCreateFileEvent(loop,e->fd,AE_READABLE,siderAeReadEvent,e);
    }
}

static void siderAeDelRead(void *privdata) {
    siderAeEvents *e = (siderAeEvents*)privdata;
    aeEventLoop *loop = e->loop;
    if (e->reading) {
        e->reading = 0;
        aeDeleteFileEvent(loop,e->fd,AE_READABLE);
    }
}

static void siderAeAddWrite(void *privdata) {
    siderAeEvents *e = (siderAeEvents*)privdata;
    aeEventLoop *loop = e->loop;
    if (!e->writing) {
        e->writing = 1;
        aeCreateFileEvent(loop,e->fd,AE_WRITABLE,siderAeWriteEvent,e);
    }
}

static void siderAeDelWrite(void *privdata) {
    siderAeEvents *e = (siderAeEvents*)privdata;
    aeEventLoop *loop = e->loop;
    if (e->writing) {
        e->writing = 0;
        aeDeleteFileEvent(loop,e->fd,AE_WRITABLE);
    }
}

static void siderAeCleanup(void *privdata) {
    siderAeEvents *e = (siderAeEvents*)privdata;
    siderAeDelRead(privdata);
    siderAeDelWrite(privdata);
    hi_free(e);
}

static int siderAeAttach(aeEventLoop *loop, siderAsyncContext *ac) {
    siderContext *c = &(ac->c);
    siderAeEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (siderAeEvents*)hi_malloc(sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    e->context = ac;
    e->loop = loop;
    e->fd = c->fd;
    e->reading = e->writing = 0;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = siderAeAddRead;
    ac->ev.delRead = siderAeDelRead;
    ac->ev.addWrite = siderAeAddWrite;
    ac->ev.delWrite = siderAeDelWrite;
    ac->ev.cleanup = siderAeCleanup;
    ac->ev.data = e;

    return REDIS_OK;
}
#endif
