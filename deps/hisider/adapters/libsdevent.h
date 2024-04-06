#ifndef HIREDIS_LIBSDEVENT_H
#define HIREDIS_LIBSDEVENT_H
#include <systemd/sd-event.h>
#include "../hisider.h"
#include "../async.h"

#define REDIS_LIBSDEVENT_DELETED 0x01
#define REDIS_LIBSDEVENT_ENTERED 0x02

typedef struct siderLibsdeventEvents {
    siderAsyncContext *context;
    struct sd_event *event;
    struct sd_event_source *fdSource;
    struct sd_event_source *timerSource;
    int fd;
    short flags;
    short state;
} siderLibsdeventEvents;

static void siderLibsdeventDestroy(siderLibsdeventEvents *e) {
    if (e->fdSource) {
        e->fdSource = sd_event_source_disable_unref(e->fdSource);
    }
    if (e->timerSource) {
        e->timerSource = sd_event_source_disable_unref(e->timerSource);
    }
    sd_event_unref(e->event);
    hi_free(e);
}

static int siderLibsdeventTimeoutHandler(sd_event_source *s, uint64_t usec, void *userdata) {
    ((void)s);
    ((void)usec);
    siderLibsdeventEvents *e = (siderLibsdeventEvents*)userdata;
    siderAsyncHandleTimeout(e->context);
    return 0;
}

static int siderLibsdeventHandler(sd_event_source *s, int fd, uint32_t event, void *userdata) {
    ((void)s);
    ((void)fd);
    siderLibsdeventEvents *e = (siderLibsdeventEvents*)userdata;
    e->state |= REDIS_LIBSDEVENT_ENTERED;

#define CHECK_DELETED() if (e->state & REDIS_LIBSDEVENT_DELETED) {\
        siderLibsdeventDestroy(e);\
        return 0; \
    }

    if ((event & EPOLLIN) && e->context && (e->state & REDIS_LIBSDEVENT_DELETED) == 0) {
        siderAsyncHandleRead(e->context);
        CHECK_DELETED();
    }

    if ((event & EPOLLOUT) && e->context && (e->state & REDIS_LIBSDEVENT_DELETED) == 0) {
        siderAsyncHandleWrite(e->context);
        CHECK_DELETED();
    }

    e->state &= ~REDIS_LIBSDEVENT_ENTERED;
#undef CHECK_DELETED

    return 0;
}

static void siderLibsdeventAddRead(void *userdata) {
    siderLibsdeventEvents *e = (siderLibsdeventEvents*)userdata;

    if (e->flags & EPOLLIN) {
        return;
    }

    e->flags |= EPOLLIN;

    if (e->flags & EPOLLOUT) {
        sd_event_source_set_io_events(e->fdSource, e->flags);
    } else {
        sd_event_add_io(e->event, &e->fdSource, e->fd, e->flags, siderLibsdeventHandler, e);
    }
}

static void siderLibsdeventDelRead(void *userdata) {
    siderLibsdeventEvents *e = (siderLibsdeventEvents*)userdata;

    e->flags &= ~EPOLLIN;

    if (e->flags) {
        sd_event_source_set_io_events(e->fdSource, e->flags);
    } else {
        e->fdSource = sd_event_source_disable_unref(e->fdSource);
    }
}

static void siderLibsdeventAddWrite(void *userdata) {
    siderLibsdeventEvents *e = (siderLibsdeventEvents*)userdata;

    if (e->flags & EPOLLOUT) {
        return;
    }

    e->flags |= EPOLLOUT;

    if (e->flags & EPOLLIN) {
        sd_event_source_set_io_events(e->fdSource, e->flags);
    } else {
        sd_event_add_io(e->event, &e->fdSource, e->fd, e->flags, siderLibsdeventHandler, e);
    }
}

static void siderLibsdeventDelWrite(void *userdata) {
    siderLibsdeventEvents *e = (siderLibsdeventEvents*)userdata;

    e->flags &= ~EPOLLOUT;

    if (e->flags) {
        sd_event_source_set_io_events(e->fdSource, e->flags);
    } else {
        e->fdSource = sd_event_source_disable_unref(e->fdSource);
    }
}

static void siderLibsdeventCleanup(void *userdata) {
    siderLibsdeventEvents *e = (siderLibsdeventEvents*)userdata;

    if (!e) {
        return;
    }

    if (e->state & REDIS_LIBSDEVENT_ENTERED) {
        e->state |= REDIS_LIBSDEVENT_DELETED;
    } else {
        siderLibsdeventDestroy(e);
    }
}

static void siderLibsdeventSetTimeout(void *userdata, struct timeval tv) {
    siderLibsdeventEvents *e = (siderLibsdeventEvents *)userdata;

    uint64_t usec = tv.tv_sec * 1000000 + tv.tv_usec;
    if (!e->timerSource) {
        sd_event_add_time_relative(e->event, &e->timerSource, CLOCK_MONOTONIC, usec, 1, siderLibsdeventTimeoutHandler, e);
    } else {
        sd_event_source_set_time_relative(e->timerSource, usec);
    }
}

static int siderLibsdeventAttach(siderAsyncContext *ac, struct sd_event *event) {
    siderContext *c = &(ac->c);
    siderLibsdeventEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (siderLibsdeventEvents*)hi_calloc(1, sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    /* Initialize and increase event refcount */
    e->context = ac;
    e->event = event;
    e->fd = c->fd;
    sd_event_ref(event);

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = siderLibsdeventAddRead;
    ac->ev.delRead = siderLibsdeventDelRead;
    ac->ev.addWrite = siderLibsdeventAddWrite;
    ac->ev.delWrite = siderLibsdeventDelWrite;
    ac->ev.cleanup = siderLibsdeventCleanup;
    ac->ev.scheduleTimer = siderLibsdeventSetTimeout;
    ac->ev.data = e;

    return REDIS_OK;
}
#endif
