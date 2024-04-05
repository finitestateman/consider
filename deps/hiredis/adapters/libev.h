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

#ifndef __HIREDIS_LIBEV_H__
#define __HIREDIS_LIBEV_H__
#include <stdlib.h>
#include <sys/types.h>
#include <ev.h>
#include "../hisider.h"
#include "../async.h"

typedef struct siderLibevEvents {
    siderAsyncContext *context;
    struct ev_loop *loop;
    int reading, writing;
    ev_io rev, wev;
    ev_timer timer;
} siderLibevEvents;

static void siderLibevReadEvent(EV_P_ ev_io *watcher, int revents) {
#if EV_MULTIPLICITY
    ((void)EV_A);
#endif
    ((void)revents);

    siderLibevEvents *e = (siderLibevEvents*)watcher->data;
    siderAsyncHandleRead(e->context);
}

static void siderLibevWriteEvent(EV_P_ ev_io *watcher, int revents) {
#if EV_MULTIPLICITY
    ((void)EV_A);
#endif
    ((void)revents);

    siderLibevEvents *e = (siderLibevEvents*)watcher->data;
    siderAsyncHandleWrite(e->context);
}

static void siderLibevAddRead(void *privdata) {
    siderLibevEvents *e = (siderLibevEvents*)privdata;
#if EV_MULTIPLICITY
    struct ev_loop *loop = e->loop;
#endif
    if (!e->reading) {
        e->reading = 1;
        ev_io_start(EV_A_ &e->rev);
    }
}

static void siderLibevDelRead(void *privdata) {
    siderLibevEvents *e = (siderLibevEvents*)privdata;
#if EV_MULTIPLICITY
    struct ev_loop *loop = e->loop;
#endif
    if (e->reading) {
        e->reading = 0;
        ev_io_stop(EV_A_ &e->rev);
    }
}

static void siderLibevAddWrite(void *privdata) {
    siderLibevEvents *e = (siderLibevEvents*)privdata;
#if EV_MULTIPLICITY
    struct ev_loop *loop = e->loop;
#endif
    if (!e->writing) {
        e->writing = 1;
        ev_io_start(EV_A_ &e->wev);
    }
}

static void siderLibevDelWrite(void *privdata) {
    siderLibevEvents *e = (siderLibevEvents*)privdata;
#if EV_MULTIPLICITY
    struct ev_loop *loop = e->loop;
#endif
    if (e->writing) {
        e->writing = 0;
        ev_io_stop(EV_A_ &e->wev);
    }
}

static void siderLibevStopTimer(void *privdata) {
    siderLibevEvents *e = (siderLibevEvents*)privdata;
#if EV_MULTIPLICITY
    struct ev_loop *loop = e->loop;
#endif
    ev_timer_stop(EV_A_ &e->timer);
}

static void siderLibevCleanup(void *privdata) {
    siderLibevEvents *e = (siderLibevEvents*)privdata;
    siderLibevDelRead(privdata);
    siderLibevDelWrite(privdata);
    siderLibevStopTimer(privdata);
    hi_free(e);
}

static void siderLibevTimeout(EV_P_ ev_timer *timer, int revents) {
#if EV_MULTIPLICITY
    ((void)EV_A);
#endif
    ((void)revents);
    siderLibevEvents *e = (siderLibevEvents*)timer->data;
    siderAsyncHandleTimeout(e->context);
}

static void siderLibevSetTimeout(void *privdata, struct timeval tv) {
    siderLibevEvents *e = (siderLibevEvents*)privdata;
#if EV_MULTIPLICITY
    struct ev_loop *loop = e->loop;
#endif

    if (!ev_is_active(&e->timer)) {
        ev_init(&e->timer, siderLibevTimeout);
        e->timer.data = e;
    }

    e->timer.repeat = tv.tv_sec + tv.tv_usec / 1000000.00;
    ev_timer_again(EV_A_ &e->timer);
}

static int siderLibevAttach(EV_P_ siderAsyncContext *ac) {
    siderContext *c = &(ac->c);
    siderLibevEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (siderLibevEvents*)hi_calloc(1, sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    e->context = ac;
#if EV_MULTIPLICITY
    e->loop = EV_A;
#else
    e->loop = NULL;
#endif
    e->rev.data = e;
    e->wev.data = e;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = siderLibevAddRead;
    ac->ev.delRead = siderLibevDelRead;
    ac->ev.addWrite = siderLibevAddWrite;
    ac->ev.delWrite = siderLibevDelWrite;
    ac->ev.cleanup = siderLibevCleanup;
    ac->ev.scheduleTimer = siderLibevSetTimeout;
    ac->ev.data = e;

    /* Initialize read/write events */
    ev_io_init(&e->rev,siderLibevReadEvent,c->fd,EV_READ);
    ev_io_init(&e->wev,siderLibevWriteEvent,c->fd,EV_WRITE);
    return REDIS_OK;
}

#endif
