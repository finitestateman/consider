#ifndef __HIREDIS_LIBUV_H__
#define __HIREDIS_LIBUV_H__
#include <stdlib.h>
#include <uv.h>
#include "../hisider.h"
#include "../async.h"
#include <string.h>

typedef struct siderLibuvEvents {
    siderAsyncContext* context;
    uv_poll_t          handle;
    uv_timer_t         timer;
    int                events;
} siderLibuvEvents;


static void siderLibuvPoll(uv_poll_t* handle, int status, int events) {
    siderLibuvEvents* p = (siderLibuvEvents*)handle->data;
    int ev = (status ? p->events : events);

    if (p->context != NULL && (ev & UV_READABLE)) {
        siderAsyncHandleRead(p->context);
    }
    if (p->context != NULL && (ev & UV_WRITABLE)) {
        siderAsyncHandleWrite(p->context);
    }
}


static void siderLibuvAddRead(void *privdata) {
    siderLibuvEvents* p = (siderLibuvEvents*)privdata;

    if (p->events & UV_READABLE) {
        return;
    }

    p->events |= UV_READABLE;

    uv_poll_start(&p->handle, p->events, siderLibuvPoll);
}


static void siderLibuvDelRead(void *privdata) {
    siderLibuvEvents* p = (siderLibuvEvents*)privdata;

    p->events &= ~UV_READABLE;

    if (p->events) {
        uv_poll_start(&p->handle, p->events, siderLibuvPoll);
    } else {
        uv_poll_stop(&p->handle);
    }
}


static void siderLibuvAddWrite(void *privdata) {
    siderLibuvEvents* p = (siderLibuvEvents*)privdata;

    if (p->events & UV_WRITABLE) {
        return;
    }

    p->events |= UV_WRITABLE;

    uv_poll_start(&p->handle, p->events, siderLibuvPoll);
}


static void siderLibuvDelWrite(void *privdata) {
    siderLibuvEvents* p = (siderLibuvEvents*)privdata;

    p->events &= ~UV_WRITABLE;

    if (p->events) {
        uv_poll_start(&p->handle, p->events, siderLibuvPoll);
    } else {
        uv_poll_stop(&p->handle);
    }
}

static void on_timer_close(uv_handle_t *handle) {
    siderLibuvEvents* p = (siderLibuvEvents*)handle->data;
    p->timer.data = NULL;
    if (!p->handle.data) {
        // both timer and handle are closed
        hi_free(p);
    }
    // else, wait for `on_handle_close`
}

static void on_handle_close(uv_handle_t *handle) {
    siderLibuvEvents* p = (siderLibuvEvents*)handle->data;
    p->handle.data = NULL;
    if (!p->timer.data) {
        // timer never started, or timer already destroyed
        hi_free(p);
    }
    // else, wait for `on_timer_close`
}

// libuv removed `status` parameter since v0.11.23
// see: https://github.com/libuv/libuv/blob/v0.11.23/include/uv.h
#if (UV_VERSION_MAJOR == 0 && UV_VERSION_MINOR < 11) || \
    (UV_VERSION_MAJOR == 0 && UV_VERSION_MINOR == 11 && UV_VERSION_PATCH < 23)
static void siderLibuvTimeout(uv_timer_t *timer, int status) {
    (void)status; // unused
#else
static void siderLibuvTimeout(uv_timer_t *timer) {
#endif
    siderLibuvEvents *e = (siderLibuvEvents*)timer->data;
    siderAsyncHandleTimeout(e->context);
}

static void siderLibuvSetTimeout(void *privdata, struct timeval tv) {
    siderLibuvEvents* p = (siderLibuvEvents*)privdata;

    uint64_t millsec = tv.tv_sec * 1000 + tv.tv_usec / 1000.0;
    if (!p->timer.data) {
        // timer is uninitialized
        if (uv_timer_init(p->handle.loop, &p->timer) != 0) {
            return;
        }
        p->timer.data = p;
    }
    // updates the timeout if the timer has already started
    // or start the timer
    uv_timer_start(&p->timer, siderLibuvTimeout, millsec, 0);
}

static void siderLibuvCleanup(void *privdata) {
    siderLibuvEvents* p = (siderLibuvEvents*)privdata;

    p->context = NULL; // indicate that context might no longer exist
    if (p->timer.data) {
        uv_close((uv_handle_t*)&p->timer, on_timer_close);
    }
    uv_close((uv_handle_t*)&p->handle, on_handle_close);
}


static int siderLibuvAttach(siderAsyncContext* ac, uv_loop_t* loop) {
    siderContext *c = &(ac->c);

    if (ac->ev.data != NULL) {
        return REDIS_ERR;
    }

    ac->ev.addRead        = siderLibuvAddRead;
    ac->ev.delRead        = siderLibuvDelRead;
    ac->ev.addWrite       = siderLibuvAddWrite;
    ac->ev.delWrite       = siderLibuvDelWrite;
    ac->ev.cleanup        = siderLibuvCleanup;
    ac->ev.scheduleTimer  = siderLibuvSetTimeout;

    siderLibuvEvents* p = (siderLibuvEvents*)hi_malloc(sizeof(*p));
    if (p == NULL)
        return REDIS_ERR;

    memset(p, 0, sizeof(*p));

    if (uv_poll_init_socket(loop, &p->handle, c->fd) != 0) {
        return REDIS_ERR;
    }

    ac->ev.data    = p;
    p->handle.data = p;
    p->context     = ac;

    return REDIS_OK;
}
#endif
