#define _XOPEN_SOURCE 700
#include "sidermodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define UNUSED(x) (void)(x)

/* Reply callback for blocking command BLOCK.DEBUG */
int HelloBlock_Reply(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    int *myint = SiderModule_GetBlockedClientPrivateData(ctx);
    return SiderModule_ReplyWithLongLong(ctx,*myint);
}

/* Timeout callback for blocking command BLOCK.DEBUG */
int HelloBlock_Timeout(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    SiderModuleBlockedClient *bc = SiderModule_GetBlockedClientHandle(ctx);
    SiderModule_BlockedClientMeasureTimeEnd(bc);
    return SiderModule_ReplyWithSimpleString(ctx,"Request timedout");
}

/* Private data freeing callback for BLOCK.DEBUG command. */
void HelloBlock_FreeData(SiderModuleCtx *ctx, void *privdata) {
    UNUSED(ctx);
    SiderModule_Free(privdata);
}

/* Private data freeing callback for BLOCK.BLOCK command. */
void HelloBlock_FreeStringData(SiderModuleCtx *ctx, void *privdata) {
    SiderModule_FreeString(ctx, (SiderModuleString*)privdata);
}

/* The thread entry point that actually executes the blocking part
 * of the command BLOCK.DEBUG. */
void *BlockDebug_ThreadMain(void *arg) {
    void **targ = arg;
    SiderModuleBlockedClient *bc = targ[0];
    long long delay = (unsigned long)targ[1];
    long long enable_time_track = (unsigned long)targ[2];
    if (enable_time_track)
        SiderModule_BlockedClientMeasureTimeStart(bc);
    SiderModule_Free(targ);

    struct timespec ts;
    ts.tv_sec = delay / 1000;
    ts.tv_nsec = (delay % 1000) * 1000000;
    nanosleep(&ts, NULL);
    int *r = SiderModule_Alloc(sizeof(int));
    *r = rand();
    if (enable_time_track)
        SiderModule_BlockedClientMeasureTimeEnd(bc);
    SiderModule_UnblockClient(bc,r);
    return NULL;
}

/* The thread entry point that actually executes the blocking part
 * of the command BLOCK.DOUBLE_DEBUG. */
void *DoubleBlock_ThreadMain(void *arg) {
    void **targ = arg;
    SiderModuleBlockedClient *bc = targ[0];
    long long delay = (unsigned long)targ[1];
    SiderModule_BlockedClientMeasureTimeStart(bc);
    SiderModule_Free(targ);
    struct timespec ts;
    ts.tv_sec = delay / 1000;
    ts.tv_nsec = (delay % 1000) * 1000000;
    nanosleep(&ts, NULL);
    int *r = SiderModule_Alloc(sizeof(int));
    *r = rand();
    SiderModule_BlockedClientMeasureTimeEnd(bc);
    /* call again SiderModule_BlockedClientMeasureTimeStart() and
     * SiderModule_BlockedClientMeasureTimeEnd and ensure that the
     * total execution time is 2x the delay. */
    SiderModule_BlockedClientMeasureTimeStart(bc);
    nanosleep(&ts, NULL);
    SiderModule_BlockedClientMeasureTimeEnd(bc);

    SiderModule_UnblockClient(bc,r);
    return NULL;
}

void HelloBlock_Disconnected(SiderModuleCtx *ctx, SiderModuleBlockedClient *bc) {
    SiderModule_Log(ctx,"warning","Blocked client %p disconnected!",
        (void*)bc);
}

/* BLOCK.DEBUG <delay_ms> <timeout_ms> -- Block for <count> milliseconds, then reply with
 * a random number. Timeout is the command timeout, so that you can test
 * what happens when the delay is greater than the timeout. */
int HelloBlock_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) return SiderModule_WrongArity(ctx);
    long long delay;
    long long timeout;

    if (SiderModule_StringToLongLong(argv[1],&delay) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx,"ERR invalid count");
    }

    if (SiderModule_StringToLongLong(argv[2],&timeout) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx,"ERR invalid count");
    }

    pthread_t tid;
    SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx,HelloBlock_Reply,HelloBlock_Timeout,HelloBlock_FreeData,timeout);

    /* Here we set a disconnection handler, however since this module will
     * block in sleep() in a thread, there is not much we can do in the
     * callback, so this is just to show you the API. */
    SiderModule_SetDisconnectCallback(bc,HelloBlock_Disconnected);

    /* Now that we setup a blocking client, we need to pass the control
     * to the thread. However we need to pass arguments to the thread:
     * the delay and a reference to the blocked client handle. */
    void **targ = SiderModule_Alloc(sizeof(void*)*3);
    targ[0] = bc;
    targ[1] = (void*)(unsigned long) delay;
    // pass 1 as flag to enable time tracking
    targ[2] = (void*)(unsigned long) 1;

    if (pthread_create(&tid,NULL,BlockDebug_ThreadMain,targ) != 0) {
        SiderModule_AbortBlock(bc);
        return SiderModule_ReplyWithError(ctx,"-ERR Can't start thread");
    }
    return REDISMODULE_OK;
}

/* BLOCK.DEBUG_NOTRACKING <delay_ms> <timeout_ms> -- Block for <count> milliseconds, then reply with
 * a random number. Timeout is the command timeout, so that you can test
 * what happens when the delay is greater than the timeout.
 * this command does not track background time so the background time should no appear in stats*/
int HelloBlockNoTracking_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) return SiderModule_WrongArity(ctx);
    long long delay;
    long long timeout;

    if (SiderModule_StringToLongLong(argv[1],&delay) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx,"ERR invalid count");
    }

    if (SiderModule_StringToLongLong(argv[2],&timeout) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx,"ERR invalid count");
    }

    pthread_t tid;
    SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx,HelloBlock_Reply,HelloBlock_Timeout,HelloBlock_FreeData,timeout);

    /* Here we set a disconnection handler, however since this module will
     * block in sleep() in a thread, there is not much we can do in the
     * callback, so this is just to show you the API. */
    SiderModule_SetDisconnectCallback(bc,HelloBlock_Disconnected);

    /* Now that we setup a blocking client, we need to pass the control
     * to the thread. However we need to pass arguments to the thread:
     * the delay and a reference to the blocked client handle. */
    void **targ = SiderModule_Alloc(sizeof(void*)*3);
    targ[0] = bc;
    targ[1] = (void*)(unsigned long) delay;
    // pass 0 as flag to enable time tracking
    targ[2] = (void*)(unsigned long) 0;

    if (pthread_create(&tid,NULL,BlockDebug_ThreadMain,targ) != 0) {
        SiderModule_AbortBlock(bc);
        return SiderModule_ReplyWithError(ctx,"-ERR Can't start thread");
    }
    return REDISMODULE_OK;
}

/* BLOCK.DOUBLE_DEBUG <delay_ms> -- Block for 2 x <count> milliseconds,
 * then reply with a random number.
 * This command is used to test multiple calls to SiderModule_BlockedClientMeasureTimeStart()
 * and SiderModule_BlockedClientMeasureTimeEnd() within the same execution. */
int HelloDoubleBlock_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);
    long long delay;

    if (SiderModule_StringToLongLong(argv[1],&delay) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx,"ERR invalid count");
    }

    pthread_t tid;
    SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx,HelloBlock_Reply,HelloBlock_Timeout,HelloBlock_FreeData,0);

    /* Now that we setup a blocking client, we need to pass the control
     * to the thread. However we need to pass arguments to the thread:
     * the delay and a reference to the blocked client handle. */
    void **targ = SiderModule_Alloc(sizeof(void*)*2);
    targ[0] = bc;
    targ[1] = (void*)(unsigned long) delay;

    if (pthread_create(&tid,NULL,DoubleBlock_ThreadMain,targ) != 0) {
        SiderModule_AbortBlock(bc);
        return SiderModule_ReplyWithError(ctx,"-ERR Can't start thread");
    }
    return REDISMODULE_OK;
}

SiderModuleBlockedClient *blocked_client = NULL;

/* BLOCK.BLOCK [TIMEOUT] -- Blocks the current client until released
 * or TIMEOUT seconds. If TIMEOUT is zero, no timeout function is
 * registered.
 */
int Block_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (SiderModule_IsBlockedReplyRequest(ctx)) {
        SiderModuleString *r = SiderModule_GetBlockedClientPrivateData(ctx);
        return SiderModule_ReplyWithString(ctx, r);
    } else if (SiderModule_IsBlockedTimeoutRequest(ctx)) {
        SiderModule_UnblockClient(blocked_client, NULL); /* Must be called to avoid leaks. */
        blocked_client = NULL;
        return SiderModule_ReplyWithSimpleString(ctx, "Timed out");
    }

    if (argc != 2) return SiderModule_WrongArity(ctx);
    long long timeout;

    if (SiderModule_StringToLongLong(argv[1], &timeout) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid timeout");
    }
    if (blocked_client) {
        return SiderModule_ReplyWithError(ctx, "ERR another client already blocked");
    }

    /* Block client. We use this function as both a reply and optional timeout
     * callback and differentiate the different code flows above.
     */
    blocked_client = SiderModule_BlockClient(ctx, Block_SiderCommand,
            timeout > 0 ? Block_SiderCommand : NULL, HelloBlock_FreeStringData, timeout);
    return REDISMODULE_OK;
}

/* BLOCK.IS_BLOCKED -- Returns 1 if we have a blocked client, or 0 otherwise.
 */
int IsBlocked_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    SiderModule_ReplyWithLongLong(ctx, blocked_client ? 1 : 0);
    return REDISMODULE_OK;
}

/* BLOCK.RELEASE [reply] -- Releases the blocked client and produce the specified reply.
 */
int Release_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);
    if (!blocked_client) {
        return SiderModule_ReplyWithError(ctx, "ERR No blocked client");
    }

    SiderModuleString *replystr = argv[1];
    SiderModule_RetainString(ctx, replystr);
    SiderModule_UnblockClient(blocked_client, replystr);
    blocked_client = NULL;

    SiderModule_ReplyWithSimpleString(ctx, "OK");

    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    if (SiderModule_Init(ctx,"block",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"block.debug",
        HelloBlock_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"block.double_debug",
        HelloDoubleBlock_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"block.debug_no_track",
        HelloBlockNoTracking_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "block.block",
        Block_SiderCommand, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"block.is_blocked",
        IsBlocked_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"block.release",
        Release_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
