#ifndef __HIREDIS_REDISMODULEAPI_H__
#define __HIREDIS_REDISMODULEAPI_H__

#include "sidermodule.h"

#include "../async.h"
#include "../hisider.h"

#include <sys/types.h>

typedef struct siderModuleEvents {
    siderAsyncContext *context;
    SiderModuleCtx *module_ctx;
    int fd;
    int reading, writing;
    int timer_active;
    SiderModuleTimerID timer_id;
} siderModuleEvents;

static inline void siderModuleReadEvent(int fd, void *privdata, int mask) {
    (void) fd;
    (void) mask;

    siderModuleEvents *e = (siderModuleEvents*)privdata;
    siderAsyncHandleRead(e->context);
}

static inline void siderModuleWriteEvent(int fd, void *privdata, int mask) {
    (void) fd;
    (void) mask;

    siderModuleEvents *e = (siderModuleEvents*)privdata;
    siderAsyncHandleWrite(e->context);
}

static inline void siderModuleAddRead(void *privdata) {
    siderModuleEvents *e = (siderModuleEvents*)privdata;
    if (!e->reading) {
        e->reading = 1;
        SiderModule_EventLoopAdd(e->fd, REDISMODULE_EVENTLOOP_READABLE, siderModuleReadEvent, e);
    }
}

static inline void siderModuleDelRead(void *privdata) {
    siderModuleEvents *e = (siderModuleEvents*)privdata;
    if (e->reading) {
        e->reading = 0;
        SiderModule_EventLoopDel(e->fd, REDISMODULE_EVENTLOOP_READABLE);
    }
}

static inline void siderModuleAddWrite(void *privdata) {
    siderModuleEvents *e = (siderModuleEvents*)privdata;
    if (!e->writing) {
        e->writing = 1;
        SiderModule_EventLoopAdd(e->fd, REDISMODULE_EVENTLOOP_WRITABLE, siderModuleWriteEvent, e);
    }
}

static inline void siderModuleDelWrite(void *privdata) {
    siderModuleEvents *e = (siderModuleEvents*)privdata;
    if (e->writing) {
        e->writing = 0;
        SiderModule_EventLoopDel(e->fd, REDISMODULE_EVENTLOOP_WRITABLE);
    }
}

static inline void siderModuleStopTimer(void *privdata) {
    siderModuleEvents *e = (siderModuleEvents*)privdata;
    if (e->timer_active) {
        SiderModule_StopTimer(e->module_ctx, e->timer_id, NULL);
    }
    e->timer_active = 0;
}

static inline void siderModuleCleanup(void *privdata) {
    siderModuleEvents *e = (siderModuleEvents*)privdata;
    siderModuleDelRead(privdata);
    siderModuleDelWrite(privdata);
    siderModuleStopTimer(privdata);
    hi_free(e);
}

static inline void siderModuleTimeout(SiderModuleCtx *ctx, void *privdata) {
    (void) ctx;

    siderModuleEvents *e = (siderModuleEvents*)privdata;
    e->timer_active = 0;
    siderAsyncHandleTimeout(e->context);
}

static inline void siderModuleSetTimeout(void *privdata, struct timeval tv) {
    siderModuleEvents* e = (siderModuleEvents*)privdata;

    siderModuleStopTimer(privdata);

    mstime_t millis = tv.tv_sec * 1000 + tv.tv_usec / 1000.0;
    e->timer_id = SiderModule_CreateTimer(e->module_ctx, millis, siderModuleTimeout, e);
    e->timer_active = 1;
}

/* Check if Sider version is compatible with the adapter. */
static inline int siderModuleCompatibilityCheck(void) {
    if (!SiderModule_EventLoopAdd ||
        !SiderModule_EventLoopDel ||
        !SiderModule_CreateTimer ||
        !SiderModule_StopTimer) {
        return REDIS_ERR;
    }
    return REDIS_OK;
}

static inline int siderModuleAttach(siderAsyncContext *ac, SiderModuleCtx *module_ctx) {
    siderContext *c = &(ac->c);
    siderModuleEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (siderModuleEvents*)hi_malloc(sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    e->context = ac;
    e->module_ctx = module_ctx;
    e->fd = c->fd;
    e->reading = e->writing = 0;
    e->timer_active = 0;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = siderModuleAddRead;
    ac->ev.delRead = siderModuleDelRead;
    ac->ev.addWrite = siderModuleAddWrite;
    ac->ev.delWrite = siderModuleDelWrite;
    ac->ev.cleanup = siderModuleCleanup;
    ac->ev.scheduleTimer = siderModuleSetTimeout;
    ac->ev.data = e;

    return REDIS_OK;
}

#endif
