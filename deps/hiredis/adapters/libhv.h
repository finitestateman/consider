#ifndef __HIREDIS_LIBHV_H__
#define __HIREDIS_LIBHV_H__

#include <hv/hloop.h>
#include "../hisider.h"
#include "../async.h"

typedef struct siderLibhvEvents {
    hio_t *io;
    htimer_t *timer;
} siderLibhvEvents;

static void siderLibhvHandleEvents(hio_t* io) {
    siderAsyncContext* context = (siderAsyncContext*)hevent_userdata(io);
    int events = hio_events(io);
    int revents = hio_revents(io);
    if (context && (events & HV_READ) && (revents & HV_READ)) {
        siderAsyncHandleRead(context);
    }
    if (context && (events & HV_WRITE) && (revents & HV_WRITE)) {
        siderAsyncHandleWrite(context);
    }
}

static void siderLibhvAddRead(void *privdata) {
    siderLibhvEvents* events = (siderLibhvEvents*)privdata;
    hio_add(events->io, siderLibhvHandleEvents, HV_READ);
}

static void siderLibhvDelRead(void *privdata) {
    siderLibhvEvents* events = (siderLibhvEvents*)privdata;
    hio_del(events->io, HV_READ);
}

static void siderLibhvAddWrite(void *privdata) {
    siderLibhvEvents* events = (siderLibhvEvents*)privdata;
    hio_add(events->io, siderLibhvHandleEvents, HV_WRITE);
}

static void siderLibhvDelWrite(void *privdata) {
    siderLibhvEvents* events = (siderLibhvEvents*)privdata;
    hio_del(events->io, HV_WRITE);
}

static void siderLibhvCleanup(void *privdata) {
    siderLibhvEvents* events = (siderLibhvEvents*)privdata;

    if (events->timer)
        htimer_del(events->timer);

    hio_close(events->io);
    hevent_set_userdata(events->io, NULL);

    hi_free(events);
}

static void siderLibhvTimeout(htimer_t* timer) {
    hio_t* io = (hio_t*)hevent_userdata(timer);
    siderAsyncHandleTimeout((siderAsyncContext*)hevent_userdata(io));
}

static void siderLibhvSetTimeout(void *privdata, struct timeval tv) {
    siderLibhvEvents* events;
    uint32_t millis;
    hloop_t* loop;

    events = (siderLibhvEvents*)privdata;
    millis = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    if (millis == 0) {
        /* Libhv disallows zero'd timers so treat this as a delete or NO OP */
        if (events->timer) {
            htimer_del(events->timer);
            events->timer = NULL;
        }
    } else if (events->timer == NULL) {
        /* Add new timer */
        loop = hevent_loop(events->io);
        events->timer = htimer_add(loop, siderLibhvTimeout, millis, 1);
        hevent_set_userdata(events->timer, events->io);
    } else {
        /* Update existing timer */
        htimer_reset(events->timer, millis);
    }
}

static int siderLibhvAttach(siderAsyncContext* ac, hloop_t* loop) {
    siderContext *c = &(ac->c);
    siderLibhvEvents *events;
    hio_t* io = NULL;

    if (ac->ev.data != NULL) {
        return REDIS_ERR;
    }

    /* Create container struct to keep track of our io and any timer */
    events = (siderLibhvEvents*)hi_malloc(sizeof(*events));
    if (events == NULL) {
        return REDIS_ERR;
    }

    io = hio_get(loop, c->fd);
    if (io == NULL) {
        hi_free(events);
        return REDIS_ERR;
    }

    hevent_set_userdata(io, ac);

    events->io = io;
    events->timer = NULL;

    ac->ev.addRead  = siderLibhvAddRead;
    ac->ev.delRead  = siderLibhvDelRead;
    ac->ev.addWrite = siderLibhvAddWrite;
    ac->ev.delWrite = siderLibhvDelWrite;
    ac->ev.cleanup  = siderLibhvCleanup;
    ac->ev.scheduleTimer = siderLibhvSetTimeout;
    ac->ev.data = events;

    return REDIS_OK;
}
#endif
