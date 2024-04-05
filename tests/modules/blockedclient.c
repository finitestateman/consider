/* define macros for having usleep */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#include <unistd.h>

#include "sidermodule.h"
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <strings.h>

#define UNUSED(V) ((void) V)

/* used to test processing events during slow bg operation */
static volatile int g_slow_bg_operation = 0;
static volatile int g_is_in_slow_bg_operation = 0;

void *sub_worker(void *arg) {
    // Get Sider module context
    SiderModuleCtx *ctx = (SiderModuleCtx *)arg;

    // Try acquiring GIL
    int res = SiderModule_ThreadSafeContextTryLock(ctx);

    // GIL is already taken by the calling thread expecting to fail.
    assert(res != REDISMODULE_OK);

    return NULL;
}

void *worker(void *arg) {
    // Retrieve blocked client
    SiderModuleBlockedClient *bc = (SiderModuleBlockedClient *)arg;

    // Get Sider module context
    SiderModuleCtx *ctx = SiderModule_GetThreadSafeContext(bc);

    // Acquire GIL
    SiderModule_ThreadSafeContextLock(ctx);

    // Create another thread which will try to acquire the GIL
    pthread_t tid;
    int res = pthread_create(&tid, NULL, sub_worker, ctx);
    assert(res == 0);

    // Wait for thread
    pthread_join(tid, NULL);

    // Release GIL
    SiderModule_ThreadSafeContextUnlock(ctx);

    // Reply to client
    SiderModule_ReplyWithSimpleString(ctx, "OK");

    // Unblock client
    SiderModule_UnblockClient(bc, NULL);

    // Free the Sider module context
    SiderModule_FreeThreadSafeContext(ctx);

    return NULL;
}

int acquire_gil(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);

    int flags = SiderModule_GetContextFlags(ctx);
    int allFlags = SiderModule_GetContextFlagsAll();
    if ((allFlags & REDISMODULE_CTX_FLAGS_MULTI) &&
        (flags & REDISMODULE_CTX_FLAGS_MULTI)) {
        SiderModule_ReplyWithSimpleString(ctx, "Blocked client is not supported inside multi");
        return REDISMODULE_OK;
    }

    if ((allFlags & REDISMODULE_CTX_FLAGS_DENY_BLOCKING) &&
        (flags & REDISMODULE_CTX_FLAGS_DENY_BLOCKING)) {
        SiderModule_ReplyWithSimpleString(ctx, "Blocked client is not allowed");
        return REDISMODULE_OK;
    }

    /* This command handler tries to acquire the GIL twice
     * once in the worker thread using "SiderModule_ThreadSafeContextLock"
     * second in the sub-worker thread
     * using "SiderModule_ThreadSafeContextTryLock"
     * as the GIL is already locked. */
    SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx, NULL, NULL, NULL, 0);

    pthread_t tid;
    int res = pthread_create(&tid, NULL, worker, bc);
    assert(res == 0);

    return REDISMODULE_OK;
}

typedef struct {
    SiderModuleString **argv;
    int argc;
    SiderModuleBlockedClient *bc;
} bg_call_data;

void *bg_call_worker(void *arg) {
    bg_call_data *bg = arg;

    // Get Sider module context
    SiderModuleCtx *ctx = SiderModule_GetThreadSafeContext(bg->bc);

    // Acquire GIL
    SiderModule_ThreadSafeContextLock(ctx);

    // Test slow operation yielding
    if (g_slow_bg_operation) {
        g_is_in_slow_bg_operation = 1;
        while (g_slow_bg_operation) {
            SiderModule_Yield(ctx, REDISMODULE_YIELD_FLAG_CLIENTS, "Slow module operation");
            usleep(1000);
        }
        g_is_in_slow_bg_operation = 0;
    }

    // Call the command
    const char *module_cmd = SiderModule_StringPtrLen(bg->argv[0], NULL);
    int cmd_pos = 1;
    SiderModuleString *format_sider_str = SiderModule_CreateString(NULL, "v", 1);
    if (!strcasecmp(module_cmd, "do_bg_rm_call_format")) {
        cmd_pos = 2;
        size_t format_len;
        const char *format = SiderModule_StringPtrLen(bg->argv[1], &format_len);
        SiderModule_StringAppendBuffer(NULL, format_sider_str, format, format_len);
        SiderModule_StringAppendBuffer(NULL, format_sider_str, "E", 1);
    }
    const char *format = SiderModule_StringPtrLen(format_sider_str, NULL);
    const char *cmd = SiderModule_StringPtrLen(bg->argv[cmd_pos], NULL);
    SiderModuleCallReply *rep = SiderModule_Call(ctx, cmd, format, bg->argv + cmd_pos + 1, bg->argc - cmd_pos - 1);
    SiderModule_FreeString(NULL, format_sider_str);

    // Release GIL
    SiderModule_ThreadSafeContextUnlock(ctx);

    // Reply to client
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }

    // Unblock client
    SiderModule_UnblockClient(bg->bc, NULL);

    /* Free the arguments */
    for (int i=0; i<bg->argc; i++)
        SiderModule_FreeString(ctx, bg->argv[i]);
    SiderModule_Free(bg->argv);
    SiderModule_Free(bg);

    // Free the Sider module context
    SiderModule_FreeThreadSafeContext(ctx);

    return NULL;
}

int do_bg_rm_call(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);

    /* Make sure we're not trying to block a client when we shouldn't */
    int flags = SiderModule_GetContextFlags(ctx);
    int allFlags = SiderModule_GetContextFlagsAll();
    if ((allFlags & REDISMODULE_CTX_FLAGS_MULTI) &&
        (flags & REDISMODULE_CTX_FLAGS_MULTI)) {
        SiderModule_ReplyWithSimpleString(ctx, "Blocked client is not supported inside multi");
        return REDISMODULE_OK;
    }
    if ((allFlags & REDISMODULE_CTX_FLAGS_DENY_BLOCKING) &&
        (flags & REDISMODULE_CTX_FLAGS_DENY_BLOCKING)) {
        SiderModule_ReplyWithSimpleString(ctx, "Blocked client is not allowed");
        return REDISMODULE_OK;
    }

    /* Make a copy of the arguments and pass them to the thread. */
    bg_call_data *bg = SiderModule_Alloc(sizeof(bg_call_data));
    bg->argv = SiderModule_Alloc(sizeof(SiderModuleString*)*argc);
    bg->argc = argc;
    for (int i=0; i<argc; i++)
        bg->argv[i] = SiderModule_HoldString(ctx, argv[i]);

    /* Block the client */
    bg->bc = SiderModule_BlockClient(ctx, NULL, NULL, NULL, 0);

    /* Start a thread to handle the request */
    pthread_t tid;
    int res = pthread_create(&tid, NULL, bg_call_worker, bg);
    assert(res == 0);

    return REDISMODULE_OK;
}

int do_rm_call(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return SiderModule_WrongArity(ctx);
    }

    const char* cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, "Ev", argv + 2, argc - 2);
    if(!rep){
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    }else{
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }

    return REDISMODULE_OK;
}

static void rm_call_async_send_reply(SiderModuleCtx *ctx, SiderModuleCallReply *reply) {
    SiderModule_ReplyWithCallReply(ctx, reply);
    SiderModule_FreeCallReply(reply);
}

/* Called when the command that was blocked on 'RM_Call' gets unblocked
 * and send the reply to the blocked client. */
static void rm_call_async_on_unblocked(SiderModuleCtx *ctx, SiderModuleCallReply *reply, void *private_data) {
    UNUSED(ctx);
    SiderModuleBlockedClient *bc = private_data;
    SiderModuleCtx *bctx = SiderModule_GetThreadSafeContext(bc);
    rm_call_async_send_reply(bctx, reply);
    SiderModule_FreeThreadSafeContext(bctx);
    SiderModule_UnblockClient(bc, SiderModule_BlockClientGetPrivateData(bc));
}

int do_rm_call_async_fire_and_forget(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return SiderModule_WrongArity(ctx);
    }
    const char* cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, "!KEv", argv + 2, argc - 2);

    if(SiderModule_CallReplyType(rep) != REDISMODULE_REPLY_PROMISE) {
        SiderModule_ReplyWithCallReply(ctx, rep);
    } else {
        SiderModule_ReplyWithSimpleString(ctx, "Blocked");
    }
    SiderModule_FreeCallReply(rep);

    return REDISMODULE_OK;
}

static void do_rm_call_async_free_pd(SiderModuleCtx * ctx, void *pd) {
    UNUSED(ctx);
    SiderModule_FreeCallReply(pd);
}

static void do_rm_call_async_disconnect(SiderModuleCtx *ctx, struct SiderModuleBlockedClient *bc) {
    UNUSED(ctx);
    SiderModuleCallReply* rep = SiderModule_BlockClientGetPrivateData(bc);
    SiderModule_CallReplyPromiseAbort(rep, NULL);
    SiderModule_FreeCallReply(rep);
    SiderModule_AbortBlock(bc);
}

/*
 * Callback for do_rm_call_async / do_rm_call_async_script_mode
 * Gets the command to invoke as the first argument to the command and runs it,
 * passing the rest of the arguments to the command invocation.
 * If the command got blocked, blocks the client and unblock it when the command gets unblocked,
 * this allows check the K (allow blocking) argument to RM_Call.
 */
int do_rm_call_async(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return SiderModule_WrongArity(ctx);
    }

    size_t format_len = 0;
    char format[6] = {0};

    if (!(SiderModule_GetContextFlags(ctx) & REDISMODULE_CTX_FLAGS_DENY_BLOCKING)) {
        /* We are allowed to block the client so we can allow RM_Call to also block us */
        format[format_len++] = 'K';
    }

    const char* invoked_cmd = SiderModule_StringPtrLen(argv[0], NULL);
    if (strcasecmp(invoked_cmd, "do_rm_call_async_script_mode") == 0) {
        format[format_len++] = 'S';
    }

    format[format_len++] = 'E';
    format[format_len++] = 'v';
    if (strcasecmp(invoked_cmd, "do_rm_call_async_no_replicate") != 0) {
        /* Notice, without the '!' flag we will have inconsistency between master and replica.
         * This is used only to check '!' flag correctness on blocked commands. */
        format[format_len++] = '!';
    }

    const char* cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, format, argv + 2, argc - 2);

    if(SiderModule_CallReplyType(rep) != REDISMODULE_REPLY_PROMISE) {
        rm_call_async_send_reply(ctx, rep);
    } else {
        SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx, NULL, NULL, do_rm_call_async_free_pd, 0);
        SiderModule_SetDisconnectCallback(bc, do_rm_call_async_disconnect);
        SiderModule_BlockClientSetPrivateData(bc, rep);
        SiderModule_CallReplyPromiseSetUnblockHandler(rep, rm_call_async_on_unblocked, bc);
    }

    return REDISMODULE_OK;
}

typedef struct ThreadedAsyncRMCallCtx{
    SiderModuleBlockedClient *bc;
    SiderModuleCallReply *reply;
} ThreadedAsyncRMCallCtx;

void *send_async_reply(void *arg) {
    ThreadedAsyncRMCallCtx *ta_rm_call_ctx = arg;
    rm_call_async_on_unblocked(NULL, ta_rm_call_ctx->reply, ta_rm_call_ctx->bc);
    SiderModule_Free(ta_rm_call_ctx);
    return NULL;
}

/* Called when the command that was blocked on 'RM_Call' gets unblocked
 * and schedule a thread to send the reply to the blocked client. */
static void rm_call_async_reply_on_thread(SiderModuleCtx *ctx, SiderModuleCallReply *reply, void *private_data) {
    UNUSED(ctx);
    ThreadedAsyncRMCallCtx *ta_rm_call_ctx = SiderModule_Alloc(sizeof(*ta_rm_call_ctx));
    ta_rm_call_ctx->bc = private_data;
    ta_rm_call_ctx->reply = reply;
    pthread_t tid;
    int res = pthread_create(&tid, NULL, send_async_reply, ta_rm_call_ctx);
    assert(res == 0);
}

/*
 * Callback for do_rm_call_async_on_thread.
 * Gets the command to invoke as the first argument to the command and runs it,
 * passing the rest of the arguments to the command invocation.
 * If the command got blocked, blocks the client and unblock on a background thread.
 * this allows check the K (allow blocking) argument to RM_Call, and make sure that the reply
 * that passes to unblock handler is owned by the handler and are not attached to any
 * context that might be freed after the callback ends.
 */
int do_rm_call_async_on_thread(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return SiderModule_WrongArity(ctx);
    }

    const char* cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, "KEv", argv + 2, argc - 2);

    if(SiderModule_CallReplyType(rep) != REDISMODULE_REPLY_PROMISE) {
        rm_call_async_send_reply(ctx, rep);
    } else {
        SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx, NULL, NULL, NULL, 0);
        SiderModule_CallReplyPromiseSetUnblockHandler(rep, rm_call_async_reply_on_thread, bc);
        SiderModule_FreeCallReply(rep);
    }

    return REDISMODULE_OK;
}

/* Private data for wait_and_do_rm_call_async that holds information about:
 * 1. the block client, to unblock when done.
 * 2. the arguments, contains the command to run using RM_Call */
typedef struct WaitAndDoRMCallCtx {
    SiderModuleBlockedClient *bc;
    SiderModuleString **argv;
    int argc;
} WaitAndDoRMCallCtx;

/*
 * This callback will be called when the 'wait' command invoke on 'wait_and_do_rm_call_async' will finish.
 * This callback will continue the execution flow just like 'do_rm_call_async' command.
 */
static void wait_and_do_rm_call_async_on_unblocked(SiderModuleCtx *ctx, SiderModuleCallReply *reply, void *private_data) {
    WaitAndDoRMCallCtx *wctx = private_data;
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_INTEGER) {
        goto done;
    }

    if (SiderModule_CallReplyInteger(reply) != 1) {
        goto done;
    }

    SiderModule_FreeCallReply(reply);
    reply = NULL;

    const char* cmd = SiderModule_StringPtrLen(wctx->argv[0], NULL);
    reply = SiderModule_Call(ctx, cmd, "!EKv", wctx->argv + 1, wctx->argc - 1);

done:
    if(SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_PROMISE) {
        SiderModuleCtx *bctx = SiderModule_GetThreadSafeContext(wctx->bc);
        rm_call_async_send_reply(bctx, reply);
        SiderModule_FreeThreadSafeContext(bctx);
        SiderModule_UnblockClient(wctx->bc, NULL);
    } else {
        SiderModule_CallReplyPromiseSetUnblockHandler(reply, rm_call_async_on_unblocked, wctx->bc);
        SiderModule_FreeCallReply(reply);
    }
    for (int i = 0 ; i < wctx->argc ; ++i) {
        SiderModule_FreeString(NULL, wctx->argv[i]);
    }
    SiderModule_Free(wctx->argv);
    SiderModule_Free(wctx);
}

/*
 * Callback for wait_and_do_rm_call
 * Gets the command to invoke as the first argument, runs 'wait'
 * command (using the K flag to RM_Call). Once the wait finished, runs the
 * command that was given (just like 'do_rm_call_async').
 */
int wait_and_do_rm_call_async(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2){
        return SiderModule_WrongArity(ctx);
    }

    int flags = SiderModule_GetContextFlags(ctx);
    if (flags & REDISMODULE_CTX_FLAGS_DENY_BLOCKING) {
        return SiderModule_ReplyWithError(ctx, "Err can not run wait, blocking is not allowed.");
    }

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "wait", "!EKcc", "1", "0");
    if(SiderModule_CallReplyType(rep) != REDISMODULE_REPLY_PROMISE) {
        rm_call_async_send_reply(ctx, rep);
    } else {
        SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx, NULL, NULL, NULL, 0);
        WaitAndDoRMCallCtx *wctx = SiderModule_Alloc(sizeof(*wctx));
        *wctx = (WaitAndDoRMCallCtx){
                .bc = bc,
                .argv = SiderModule_Alloc((argc - 1) * sizeof(SiderModuleString*)),
                .argc = argc - 1,
        };

        for (int i = 1 ; i < argc ; ++i) {
            wctx->argv[i - 1] = SiderModule_HoldString(NULL, argv[i]);
        }
        SiderModule_CallReplyPromiseSetUnblockHandler(rep, wait_and_do_rm_call_async_on_unblocked, wctx);
        SiderModule_FreeCallReply(rep);
    }

    return REDISMODULE_OK;
}

static void blpop_and_set_multiple_keys_on_unblocked(SiderModuleCtx *ctx, SiderModuleCallReply *reply, void *private_data) {
    /* ignore the reply */
    SiderModule_FreeCallReply(reply);
    WaitAndDoRMCallCtx *wctx = private_data;
    for (int i = 0 ; i < wctx->argc ; i += 2) {
        SiderModuleCallReply* rep = SiderModule_Call(ctx, "set", "!ss", wctx->argv[i], wctx->argv[i + 1]);
        SiderModule_FreeCallReply(rep);
    }

    SiderModuleCtx *bctx = SiderModule_GetThreadSafeContext(wctx->bc);
    SiderModule_ReplyWithSimpleString(bctx, "OK");
    SiderModule_FreeThreadSafeContext(bctx);
    SiderModule_UnblockClient(wctx->bc, NULL);

    for (int i = 0 ; i < wctx->argc ; ++i) {
        SiderModule_FreeString(NULL, wctx->argv[i]);
    }
    SiderModule_Free(wctx->argv);
    SiderModule_Free(wctx);

}

/*
 * Performs a blpop command on a given list and when unblocked set multiple string keys.
 * This command allows checking that the unblock callback is performed as a unit
 * and its effect are replicated to the replica and AOF wrapped with multi exec.
 */
int blpop_and_set_multiple_keys(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    if(argc < 2 || argc % 2 != 0){
        return SiderModule_WrongArity(ctx);
    }

    int flags = SiderModule_GetContextFlags(ctx);
    if (flags & REDISMODULE_CTX_FLAGS_DENY_BLOCKING) {
        return SiderModule_ReplyWithError(ctx, "Err can not run wait, blocking is not allowed.");
    }

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "blpop", "!EKsc", argv[1], "0");
    if(SiderModule_CallReplyType(rep) != REDISMODULE_REPLY_PROMISE) {
        rm_call_async_send_reply(ctx, rep);
    } else {
        SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx, NULL, NULL, NULL, 0);
        WaitAndDoRMCallCtx *wctx = SiderModule_Alloc(sizeof(*wctx));
        *wctx = (WaitAndDoRMCallCtx){
                .bc = bc,
                .argv = SiderModule_Alloc((argc - 2) * sizeof(SiderModuleString*)),
                .argc = argc - 2,
        };

        for (int i = 0 ; i < argc - 2 ; ++i) {
            wctx->argv[i] = SiderModule_HoldString(NULL, argv[i + 2]);
        }
        SiderModule_CallReplyPromiseSetUnblockHandler(rep, blpop_and_set_multiple_keys_on_unblocked, wctx);
        SiderModule_FreeCallReply(rep);
    }

    return REDISMODULE_OK;
}

/* simulate a blocked client replying to a thread safe context without creating a thread */
int do_fake_bg_true(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx, NULL, NULL, NULL, 0);
    SiderModuleCtx *bctx = SiderModule_GetThreadSafeContext(bc);

    SiderModule_ReplyWithBool(bctx, 1);

    SiderModule_FreeThreadSafeContext(bctx);
    SiderModule_UnblockClient(bc, NULL);

    return REDISMODULE_OK;
}


/* this flag is used to work with busy commands, that might take a while
 * and ability to stop the busy work with a different command*/
static volatile int abort_flag = 0;

int slow_fg_command(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    long long block_time = 0;
    if (SiderModule_StringToLongLong(argv[1], &block_time) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "Invalid integer value");
        return REDISMODULE_OK;
    }

    uint64_t start_time = SiderModule_MonotonicMicroseconds();
    /* when not blocking indefinitely, we don't process client commands in this test. */
    int yield_flags = block_time? REDISMODULE_YIELD_FLAG_NONE: REDISMODULE_YIELD_FLAG_CLIENTS;
    while (!abort_flag) {
        SiderModule_Yield(ctx, yield_flags, "Slow module operation");
        usleep(1000);
        if (block_time && SiderModule_MonotonicMicroseconds() - start_time > (uint64_t)block_time)
            break;
    }

    abort_flag = 0;
    SiderModule_ReplyWithLongLong(ctx, 1);
    return REDISMODULE_OK;
}

int stop_slow_fg_command(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    abort_flag = 1;
    SiderModule_ReplyWithLongLong(ctx, 1);
    return REDISMODULE_OK;
}

/* used to enable or disable slow operation in do_bg_rm_call */
static int set_slow_bg_operation(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    long long ll;
    if (SiderModule_StringToLongLong(argv[1], &ll) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "Invalid integer value");
        return REDISMODULE_OK;
    }
    g_slow_bg_operation = ll;
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

/* used to test if we reached the slow operation in do_bg_rm_call */
static int is_in_slow_bg_operation(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    if (argc != 1) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    SiderModule_ReplyWithLongLong(ctx, g_is_in_slow_bg_operation);
    return REDISMODULE_OK;
}

static void timer_callback(SiderModuleCtx *ctx, void *data)
{
    UNUSED(ctx);

    SiderModuleBlockedClient *bc = data;

    // Get Sider module context
    SiderModuleCtx *reply_ctx = SiderModule_GetThreadSafeContext(bc);

    // Reply to client
    SiderModule_ReplyWithSimpleString(reply_ctx, "OK");

    // Unblock client
    SiderModule_UnblockClient(bc, NULL);

    // Free the Sider module context
    SiderModule_FreeThreadSafeContext(reply_ctx);
}

int unblock_by_timer(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2)
        return SiderModule_WrongArity(ctx);

    long long period;
    if (SiderModule_StringToLongLong(argv[1],&period) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx,"ERR invalid period");

    SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx, NULL, NULL, NULL, 0);
    SiderModule_CreateTimer(ctx, period, timer_callback, bc);
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx, "blockedclient", 1, REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "acquire_gil", acquire_gil, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_rm_call", do_rm_call,
                                  "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_rm_call_async", do_rm_call_async,
                                  "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_rm_call_async_on_thread", do_rm_call_async_on_thread,
                                      "write", 0, 0, 0) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_rm_call_async_script_mode", do_rm_call_async,
                                  "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_rm_call_async_no_replicate", do_rm_call_async,
                                  "write", 0, 0, 0) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_rm_call_fire_and_forget", do_rm_call_async_fire_and_forget,
                                  "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "wait_and_do_rm_call", wait_and_do_rm_call_async,
                                  "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "blpop_and_set_multiple_keys", blpop_and_set_multiple_keys,
                                      "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_bg_rm_call", do_bg_rm_call, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_bg_rm_call_format", do_bg_rm_call, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "do_fake_bg_true", do_fake_bg_true, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "slow_fg_command", slow_fg_command,"", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "stop_slow_fg_command", stop_slow_fg_command,"allow-busy", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "set_slow_bg_operation", set_slow_bg_operation, "allow-busy", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "is_in_slow_bg_operation", is_in_slow_bg_operation, "allow-busy", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "unblock_by_timer", unblock_by_timer, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
