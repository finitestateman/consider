/* This module is used to test the propagation (replication + AOF) of
 * commands, via the SiderModule_Replicate() interface, in asynchronous
 * contexts, such as callbacks not implementing commands, and thread safe
 * contexts.
 *
 * We create a timer callback and a threads using a thread safe context.
 * Using both we try to propagate counters increments, and later we check
 * if the replica contains the changes as expected.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2019, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "sidermodule.h"
#include <pthread.h>
#include <errno.h>

#define UNUSED(V) ((void) V)

SiderModuleCtx *detached_ctx = NULL;

static int KeySpace_NotificationGeneric(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key) {
    REDISMODULE_NOT_USED(type);
    REDISMODULE_NOT_USED(event);
    REDISMODULE_NOT_USED(key);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "INCR", "c!", "notifications");
    SiderModule_FreeCallReply(rep);

    return REDISMODULE_OK;
}

/* Timer callback. */
void timerHandler(SiderModuleCtx *ctx, void *data) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(data);

    static int times = 0;

    SiderModule_Replicate(ctx,"INCR","c","timer");
    times++;

    if (times < 3)
        SiderModule_CreateTimer(ctx,100,timerHandler,NULL);
    else
        times = 0;
}

int propagateTestTimerCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleTimerID timer_id =
        SiderModule_CreateTimer(ctx,100,timerHandler,NULL);
    REDISMODULE_NOT_USED(timer_id);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

/* Timer callback. */
void timerNestedHandler(SiderModuleCtx *ctx, void *data) {
    int repl = (long long)data;

    /* The goal is the trigger a module command that calls RM_Replicate
     * in order to test MULTI/EXEC structure */
    SiderModule_Replicate(ctx,"INCRBY","cc","timer-nested-start","1");
    SiderModuleCallReply *reply = SiderModule_Call(ctx,"propagate-test.nested", repl? "!" : "");
    SiderModule_FreeCallReply(reply);
    reply = SiderModule_Call(ctx, "INCR", repl? "c!" : "c", "timer-nested-middle");
    SiderModule_FreeCallReply(reply);
    SiderModule_Replicate(ctx,"INCRBY","cc","timer-nested-end","1");
}

int propagateTestTimerNestedCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleTimerID timer_id =
        SiderModule_CreateTimer(ctx,100,timerNestedHandler,(void*)0);
    REDISMODULE_NOT_USED(timer_id);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

int propagateTestTimerNestedReplCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleTimerID timer_id =
        SiderModule_CreateTimer(ctx,100,timerNestedHandler,(void*)1);
    REDISMODULE_NOT_USED(timer_id);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

void timerHandlerMaxmemory(SiderModuleCtx *ctx, void *data) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(data);

    SiderModuleCallReply *reply = SiderModule_Call(ctx,"SETEX","ccc!","timer-maxmemory-volatile-start","100","1");
    SiderModule_FreeCallReply(reply);
    reply = SiderModule_Call(ctx, "CONFIG", "ccc!", "SET", "maxmemory", "1");
    SiderModule_FreeCallReply(reply);

    SiderModule_Replicate(ctx, "INCR", "c", "timer-maxmemory-middle");

    reply = SiderModule_Call(ctx,"SETEX","ccc!","timer-maxmemory-volatile-end","100","1");
    SiderModule_FreeCallReply(reply);
}

int propagateTestTimerMaxmemoryCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleTimerID timer_id =
        SiderModule_CreateTimer(ctx,100,timerHandlerMaxmemory,(void*)1);
    REDISMODULE_NOT_USED(timer_id);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

void timerHandlerEval(SiderModuleCtx *ctx, void *data) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(data);

    SiderModuleCallReply *reply = SiderModule_Call(ctx,"INCRBY","cc!","timer-eval-start","1");
    SiderModule_FreeCallReply(reply);
    reply = SiderModule_Call(ctx, "EVAL", "cccc!", "sider.call('set',KEYS[1],ARGV[1])", "1", "foo", "bar");
    SiderModule_FreeCallReply(reply);

    SiderModule_Replicate(ctx, "INCR", "c", "timer-eval-middle");

    reply = SiderModule_Call(ctx,"INCRBY","cc!","timer-eval-end","1");
    SiderModule_FreeCallReply(reply);
}

int propagateTestTimerEvalCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleTimerID timer_id =
        SiderModule_CreateTimer(ctx,100,timerHandlerEval,(void*)1);
    REDISMODULE_NOT_USED(timer_id);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

/* The thread entry point. */
void *threadMain(void *arg) {
    REDISMODULE_NOT_USED(arg);
    SiderModuleCtx *ctx = SiderModule_GetThreadSafeContext(NULL);
    SiderModule_SelectDb(ctx,9); /* Tests ran in database number 9. */
    for (int i = 0; i < 3; i++) {
        SiderModule_ThreadSafeContextLock(ctx);
        SiderModule_Replicate(ctx,"INCR","c","a-from-thread");
        SiderModuleCallReply *reply = SiderModule_Call(ctx,"INCR","c!","thread-call");
        SiderModule_FreeCallReply(reply);
        SiderModule_Replicate(ctx,"INCR","c","b-from-thread");
        SiderModule_ThreadSafeContextUnlock(ctx);
    }
    SiderModule_FreeThreadSafeContext(ctx);
    return NULL;
}

int propagateTestThreadCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    pthread_t tid;
    if (pthread_create(&tid,NULL,threadMain,NULL) != 0)
        return SiderModule_ReplyWithError(ctx,"-ERR Can't start thread");
    REDISMODULE_NOT_USED(tid);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

/* The thread entry point. */
void *threadDetachedMain(void *arg) {
    REDISMODULE_NOT_USED(arg);
    SiderModule_SelectDb(detached_ctx,9); /* Tests ran in database number 9. */

    SiderModule_ThreadSafeContextLock(detached_ctx);
    SiderModule_Replicate(detached_ctx,"INCR","c","thread-detached-before");
    SiderModuleCallReply *reply = SiderModule_Call(detached_ctx,"INCR","c!","thread-detached-1");
    SiderModule_FreeCallReply(reply);
    reply = SiderModule_Call(detached_ctx,"INCR","c!","thread-detached-2");
    SiderModule_FreeCallReply(reply);
    SiderModule_Replicate(detached_ctx,"INCR","c","thread-detached-after");
    SiderModule_ThreadSafeContextUnlock(detached_ctx);

    return NULL;
}

int propagateTestDetachedThreadCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    pthread_t tid;
    if (pthread_create(&tid,NULL,threadDetachedMain,NULL) != 0)
        return SiderModule_ReplyWithError(ctx,"-ERR Can't start thread");
    REDISMODULE_NOT_USED(tid);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

int propagateTestSimpleCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    /* Replicate two commands to test MULTI/EXEC wrapping. */
    SiderModule_Replicate(ctx,"INCR","c","counter-1");
    SiderModule_Replicate(ctx,"INCR","c","counter-2");
    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

int propagateTestMixedCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModuleCallReply *reply;

    /* This test mixes multiple propagation systems. */
    reply = SiderModule_Call(ctx, "INCR", "c!", "using-call");
    SiderModule_FreeCallReply(reply);

    SiderModule_Replicate(ctx,"INCR","c","counter-1");
    SiderModule_Replicate(ctx,"INCR","c","counter-2");

    reply = SiderModule_Call(ctx, "INCR", "c!", "after-call");
    SiderModule_FreeCallReply(reply);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

int propagateTestNestedCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModuleCallReply *reply;

    /* This test mixes multiple propagation systems. */
    reply = SiderModule_Call(ctx, "INCR", "c!", "using-call");
    SiderModule_FreeCallReply(reply);

    reply = SiderModule_Call(ctx,"propagate-test.simple", "!");
    SiderModule_FreeCallReply(reply);

    SiderModule_Replicate(ctx,"INCR","c","counter-3");
    SiderModule_Replicate(ctx,"INCR","c","counter-4");

    reply = SiderModule_Call(ctx, "INCR", "c!", "after-call");
    SiderModule_FreeCallReply(reply);

    reply = SiderModule_Call(ctx, "INCR", "c!", "before-call-2");
    SiderModule_FreeCallReply(reply);

    reply = SiderModule_Call(ctx, "keyspace.incr_case1", "c!", "asdf"); /* Propagates INCR */
    SiderModule_FreeCallReply(reply);

    reply = SiderModule_Call(ctx, "keyspace.del_key_copy", "c!", "asdf"); /* Propagates DEL */
    SiderModule_FreeCallReply(reply);

    reply = SiderModule_Call(ctx, "INCR", "c!", "after-call-2");
    SiderModule_FreeCallReply(reply);

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;
}

int propagateTestIncr(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argc);
    SiderModuleCallReply *reply;

    /* This test propagates the module command, not the INCR it executes. */
    reply = SiderModule_Call(ctx, "INCR", "s", argv[1]);
    SiderModule_ReplyWithCallReply(ctx,reply);
    SiderModule_FreeCallReply(reply);
    SiderModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"propagate-test",1,REDISMODULE_APIVER_1)
            == REDISMODULE_ERR) return REDISMODULE_ERR;

    detached_ctx = SiderModule_GetDetachedThreadSafeContext(ctx);

    if (SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_ALL, KeySpace_NotificationGeneric) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.timer",
                propagateTestTimerCommand,
                "",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.timer-nested",
                propagateTestTimerNestedCommand,
                "",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.timer-nested-repl",
                propagateTestTimerNestedReplCommand,
                "",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.timer-maxmemory",
                propagateTestTimerMaxmemoryCommand,
                "",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.timer-eval",
                propagateTestTimerEvalCommand,
                "",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.thread",
                propagateTestThreadCommand,
                "",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.detached-thread",
                propagateTestDetachedThreadCommand,
                "",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.simple",
                propagateTestSimpleCommand,
                "",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.mixed",
                propagateTestMixedCommand,
                "write",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.nested",
                propagateTestNestedCommand,
                "write",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"propagate-test.incr",
                propagateTestIncr,
                "write",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    return REDISMODULE_OK;
}

int SiderModule_OnUnload(SiderModuleCtx *ctx) {
    UNUSED(ctx);

    if (detached_ctx)
        SiderModule_FreeThreadSafeContext(detached_ctx);

    return REDISMODULE_OK;
}
