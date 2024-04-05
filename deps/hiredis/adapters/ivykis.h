#ifndef __HIREDIS_IVYKIS_H__
#define __HIREDIS_IVYKIS_H__
#include <iv.h>
#include "../hisider.h"
#include "../async.h"

typedef struct siderIvykisEvents {
    siderAsyncContext *context;
    struct iv_fd fd;
} siderIvykisEvents;

static void siderIvykisReadEvent(void *arg) {
    siderAsyncContext *context = (siderAsyncContext *)arg;
    siderAsyncHandleRead(context);
}

static void siderIvykisWriteEvent(void *arg) {
    siderAsyncContext *context = (siderAsyncContext *)arg;
    siderAsyncHandleWrite(context);
}

static void siderIvykisAddRead(void *privdata) {
    siderIvykisEvents *e = (siderIvykisEvents*)privdata;
    iv_fd_set_handler_in(&e->fd, siderIvykisReadEvent);
}

static void siderIvykisDelRead(void *privdata) {
    siderIvykisEvents *e = (siderIvykisEvents*)privdata;
    iv_fd_set_handler_in(&e->fd, NULL);
}

static void siderIvykisAddWrite(void *privdata) {
    siderIvykisEvents *e = (siderIvykisEvents*)privdata;
    iv_fd_set_handler_out(&e->fd, siderIvykisWriteEvent);
}

static void siderIvykisDelWrite(void *privdata) {
    siderIvykisEvents *e = (siderIvykisEvents*)privdata;
    iv_fd_set_handler_out(&e->fd, NULL);
}

static void siderIvykisCleanup(void *privdata) {
    siderIvykisEvents *e = (siderIvykisEvents*)privdata;

    iv_fd_unregister(&e->fd);
    hi_free(e);
}

static int siderIvykisAttach(siderAsyncContext *ac) {
    siderContext *c = &(ac->c);
    siderIvykisEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return REDIS_ERR;

    /* Create container for context and r/w events */
    e = (siderIvykisEvents*)hi_malloc(sizeof(*e));
    if (e == NULL)
        return REDIS_ERR;

    e->context = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = siderIvykisAddRead;
    ac->ev.delRead = siderIvykisDelRead;
    ac->ev.addWrite = siderIvykisAddWrite;
    ac->ev.delWrite = siderIvykisDelWrite;
    ac->ev.cleanup = siderIvykisCleanup;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    IV_FD_INIT(&e->fd);
    e->fd.fd = c->fd;
    e->fd.handler_in = siderIvykisReadEvent;
    e->fd.handler_out = siderIvykisWriteEvent;
    e->fd.handler_err = NULL;
    e->fd.cookie = e->context;

    iv_fd_register(&e->fd);

    return REDIS_OK;
}
#endif
