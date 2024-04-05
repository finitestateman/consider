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

#ifndef __HIREDIS_LIBEVENT_H__
#define __HIREDIS_LIBEVENT_H__
#include <event2/event.h>
#include "../hisider.h"
#include "../async.h"

#define REDIS_LIBEVENT_DELETED 0x01
#define REDIS_LIBEVENT_ENTERED 0x02

typedef struct siderLibeventEvents {
    siderAsyncContext *context;
    struct event *ev;
    struct event_base *base;
    struct timeval tv;
    short flags;
    short state;
} siderLibeventEvents;

static void siderLibeventDestroy(siderLibeventEvents *e) {
    hi_free(e);
}

static void siderLibeventHandler(evutil_socket_t fd, short event, void *arg) {
    ((void)fd);
    siderLibeventEvents *e = (siderLibeventEvents*)arg;
    e->state |= REDIS_LIBEVENT_ENTERED;

    #define CHECK_DELETED() if (e->state & REDIS_LIBEVENT_DELETED) {\
        siderLibeventDestroy(e);\
        return; \
    }

    if ((event & EV_TIMEOUT) && (e->state & REDIS_LIBEVENT_DELETED) == 0) {
        siderAsyncHandleTimeout(e->context);
        CHECK_DELETED();
    }

    if ((event & EV_READ) && e->context && (e->state & REDIS_LIBEVENT_DELETED) == 0) {
        siderAsyncHandleRead(e->context);
        CHECK_DELETED();
    }

    if ((event & EV_WRITE) && e->context && (e->state & REDIS_LIBEVENT_DELETED) == 0) {
        siderAsyncHandleWrite(e->context);
        CHECK_DELETED();
    }

    e->state &= ~REDIS_LIBEVENT_ENTERED;
    #undef CHECK_DELETED
}

static void siderLibeventUpdate(void *privdata, short flag, int isRemove) {
    siderLibeventEvents *e = (siderLibeventEvents *)privdata;
    const struct timeval *tv = e->tv.tv_sec || e->tv.tv_usec ? &e->tv : NULL;

    if (isRemove) {
        if ((e->flags & flag) == 0) {
            return;
        } else {
            e->flags &= ~flag;
        }
    } else {
        if (e->flags & flag) {
            return;
        } else {
            e->flags |= flag;
        }
    }

    event_del(e->ev);
    event_assign(e->ev, e->base, e->context->c.fd, e->flags | EV_PERSIST,
                 siderLibeventHandler, privdata);
    event_add(e->ev, tv);
}

static void siderLibeventAddRead(void *privdata) {
    siderLibeventUpdate(privdata, EV_READ, 0);
}

static void siderLibeventDelRead(void *privdata) {
    siderLibeventUpdate(privdata, EV_READ, 1);
}

static void siderLibeventAddWrite(void *privdata) {
    siderLibeventUpdate(privdata, EV_WRITE, 0);
}

static void siderLibeventDelWrite(void *privdata) {
    siderLibeventUpdate(privdata, EV_WRITE, 1);
}

static void siderLibeventCleanup(void *privdata) {
    siderLibeventEvents *e = (siderLibeventEvents*)privdata;
    if (!e) {
        return;
    }
    event_del(e->ev);
    event_free(e->ev);
    e->ev = NULL;

    if (e->state & REDIS_LIBEVENT_ENTERED) {
        e->state |= REDIS_LIBEVENT_DELETED;
    } else {
        siderLibeventDestroy(e);
    }
}

static void siderLibeventSetTimeout(void *privdata, struct timeval tv) {
    siderLibeventEvents *e = (siderLibeventEvents *)privdata;
    short flags = e->flags;
    e->flags = 0;
    e->tv = tv;
    siderLibeventUpdate(e, flags, 0);
}

static int siderLibeventAttach(siderAsyncContext *ac, struct event_base *base) {
    siderContext *c = &(ac->c);
    siderLibeventEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (siderLibeventEvents*)hi_calloc(1, sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    e->context = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = siderLibeventAddRead;
    ac->ev.delRead = siderLibeventDelRead;
    ac->ev.addWrite = siderLibeventAddWrite;
    ac->ev.delWrite = siderLibeventDelWrite;
    ac->ev.cleanup = siderLibeventCleanup;
    ac->ev.scheduleTimer = siderLibeventSetTimeout;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    e->ev = event_new(base, c->fd, EV_READ | EV_WRITE, siderLibeventHandler, e);
    e->base = base;
    return REDIS_OK;
}
#endif
