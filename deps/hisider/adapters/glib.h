#ifndef __HIREDIS_GLIB_H__
#define __HIREDIS_GLIB_H__

#include <glib.h>

#include "../hisider.h"
#include "../async.h"

typedef struct
{
    GSource source;
    siderAsyncContext *ac;
    GPollFD poll_fd;
} SiderSource;

static void
sider_source_add_read (gpointer data)
{
    SiderSource *source = (SiderSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events |= G_IO_IN;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
sider_source_del_read (gpointer data)
{
    SiderSource *source = (SiderSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events &= ~G_IO_IN;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
sider_source_add_write (gpointer data)
{
    SiderSource *source = (SiderSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events |= G_IO_OUT;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
sider_source_del_write (gpointer data)
{
    SiderSource *source = (SiderSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events &= ~G_IO_OUT;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
sider_source_cleanup (gpointer data)
{
    SiderSource *source = (SiderSource *)data;

    g_return_if_fail(source);

    sider_source_del_read(source);
    sider_source_del_write(source);
    /*
     * It is not our responsibility to remove ourself from the
     * current main loop. However, we will remove the GPollFD.
     */
    if (source->poll_fd.fd >= 0) {
        g_source_remove_poll((GSource *)data, &source->poll_fd);
        source->poll_fd.fd = -1;
    }
}

static gboolean
sider_source_prepare (GSource *source,
                      gint    *timeout_)
{
    SiderSource *sider = (SiderSource *)source;
    *timeout_ = -1;
    return !!(sider->poll_fd.events & sider->poll_fd.revents);
}

static gboolean
sider_source_check (GSource *source)
{
    SiderSource *sider = (SiderSource *)source;
    return !!(sider->poll_fd.events & sider->poll_fd.revents);
}

static gboolean
sider_source_dispatch (GSource      *source,
                       GSourceFunc   callback,
                       gpointer      user_data)
{
    SiderSource *sider = (SiderSource *)source;

    if ((sider->poll_fd.revents & G_IO_OUT)) {
        siderAsyncHandleWrite(sider->ac);
        sider->poll_fd.revents &= ~G_IO_OUT;
    }

    if ((sider->poll_fd.revents & G_IO_IN)) {
        siderAsyncHandleRead(sider->ac);
        sider->poll_fd.revents &= ~G_IO_IN;
    }

    if (callback) {
        return callback(user_data);
    }

    return TRUE;
}

static void
sider_source_finalize (GSource *source)
{
    SiderSource *sider = (SiderSource *)source;

    if (sider->poll_fd.fd >= 0) {
        g_source_remove_poll(source, &sider->poll_fd);
        sider->poll_fd.fd = -1;
    }
}

static GSource *
sider_source_new (siderAsyncContext *ac)
{
    static GSourceFuncs source_funcs = {
        .prepare  = sider_source_prepare,
        .check     = sider_source_check,
        .dispatch = sider_source_dispatch,
        .finalize = sider_source_finalize,
    };
    siderContext *c = &ac->c;
    SiderSource *source;

    g_return_val_if_fail(ac != NULL, NULL);

    source = (SiderSource *)g_source_new(&source_funcs, sizeof *source);
    if (source == NULL)
        return NULL;

    source->ac = ac;
    source->poll_fd.fd = c->fd;
    source->poll_fd.events = 0;
    source->poll_fd.revents = 0;
    g_source_add_poll((GSource *)source, &source->poll_fd);

    ac->ev.addRead = sider_source_add_read;
    ac->ev.delRead = sider_source_del_read;
    ac->ev.addWrite = sider_source_add_write;
    ac->ev.delWrite = sider_source_del_write;
    ac->ev.cleanup = sider_source_cleanup;
    ac->ev.data = source;

    return (GSource *)source;
}

#endif /* __HIREDIS_GLIB_H__ */
