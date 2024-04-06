//
//  Created by Дмитрий Бахвалов on 13.07.15.
//  Copyright (c) 2015 Dmitry Bakhvalov. All rights reserved.
//

#ifndef __HIREDIS_MACOSX_H__
#define __HIREDIS_MACOSX_H__

#include <CoreFoundation/CoreFoundation.h>

#include "../hisider.h"
#include "../async.h"

typedef struct {
    siderAsyncContext *context;
    CFSocketRef socketRef;
    CFRunLoopSourceRef sourceRef;
} SiderRunLoop;

static int freeSiderRunLoop(SiderRunLoop* siderRunLoop) {
    if( siderRunLoop != NULL ) {
        if( siderRunLoop->sourceRef != NULL ) {
            CFRunLoopSourceInvalidate(siderRunLoop->sourceRef);
            CFRelease(siderRunLoop->sourceRef);
        }
        if( siderRunLoop->socketRef != NULL ) {
            CFSocketInvalidate(siderRunLoop->socketRef);
            CFRelease(siderRunLoop->socketRef);
        }
        hi_free(siderRunLoop);
    }
    return REDIS_ERR;
}

static void siderMacOSAddRead(void *privdata) {
    SiderRunLoop *siderRunLoop = (SiderRunLoop*)privdata;
    CFSocketEnableCallBacks(siderRunLoop->socketRef, kCFSocketReadCallBack);
}

static void siderMacOSDelRead(void *privdata) {
    SiderRunLoop *siderRunLoop = (SiderRunLoop*)privdata;
    CFSocketDisableCallBacks(siderRunLoop->socketRef, kCFSocketReadCallBack);
}

static void siderMacOSAddWrite(void *privdata) {
    SiderRunLoop *siderRunLoop = (SiderRunLoop*)privdata;
    CFSocketEnableCallBacks(siderRunLoop->socketRef, kCFSocketWriteCallBack);
}

static void siderMacOSDelWrite(void *privdata) {
    SiderRunLoop *siderRunLoop = (SiderRunLoop*)privdata;
    CFSocketDisableCallBacks(siderRunLoop->socketRef, kCFSocketWriteCallBack);
}

static void siderMacOSCleanup(void *privdata) {
    SiderRunLoop *siderRunLoop = (SiderRunLoop*)privdata;
    freeSiderRunLoop(siderRunLoop);
}

static void siderMacOSAsyncCallback(CFSocketRef __unused s, CFSocketCallBackType callbackType, CFDataRef __unused address, const void __unused *data, void *info) {
    siderAsyncContext* context = (siderAsyncContext*) info;

    switch (callbackType) {
        case kCFSocketReadCallBack:
            siderAsyncHandleRead(context);
            break;

        case kCFSocketWriteCallBack:
            siderAsyncHandleWrite(context);
            break;

        default:
            break;
    }
}

static int siderMacOSAttach(siderAsyncContext *siderAsyncCtx, CFRunLoopRef runLoop) {
    siderContext *siderCtx = &(siderAsyncCtx->c);

    /* Nothing should be attached when something is already attached */
    if( siderAsyncCtx->ev.data != NULL ) return REDIS_ERR;

    SiderRunLoop* siderRunLoop = (SiderRunLoop*) hi_calloc(1, sizeof(SiderRunLoop));
    if (siderRunLoop == NULL)
        return REDIS_ERR;

    /* Setup sider stuff */
    siderRunLoop->context = siderAsyncCtx;

    siderAsyncCtx->ev.addRead  = siderMacOSAddRead;
    siderAsyncCtx->ev.delRead  = siderMacOSDelRead;
    siderAsyncCtx->ev.addWrite = siderMacOSAddWrite;
    siderAsyncCtx->ev.delWrite = siderMacOSDelWrite;
    siderAsyncCtx->ev.cleanup  = siderMacOSCleanup;
    siderAsyncCtx->ev.data     = siderRunLoop;

    /* Initialize and install read/write events */
    CFSocketContext socketCtx = { 0, siderAsyncCtx, NULL, NULL, NULL };

    siderRunLoop->socketRef = CFSocketCreateWithNative(NULL, siderCtx->fd,
                                                       kCFSocketReadCallBack | kCFSocketWriteCallBack,
                                                       siderMacOSAsyncCallback,
                                                       &socketCtx);
    if( !siderRunLoop->socketRef ) return freeSiderRunLoop(siderRunLoop);

    siderRunLoop->sourceRef = CFSocketCreateRunLoopSource(NULL, siderRunLoop->socketRef, 0);
    if( !siderRunLoop->sourceRef ) return freeSiderRunLoop(siderRunLoop);

    CFRunLoopAddSource(runLoop, siderRunLoop->sourceRef, kCFRunLoopDefaultMode);

    return REDIS_OK;
}

#endif

