/* Helloblock module -- An example of blocking command implementation
 * with threads.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2016, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "../sidermodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

/* Reply callback for blocking command HELLO.BLOCK */
int HelloBlock_Reply(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    int *myint = SiderModule_GetBlockedClientPrivateData(ctx);
    return SiderModule_ReplyWithLongLong(ctx,*myint);
}

/* Timeout callback for blocking command HELLO.BLOCK */
int HelloBlock_Timeout(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    return SiderModule_ReplyWithSimpleString(ctx,"Request timedout");
}

/* Private data freeing callback for HELLO.BLOCK command. */
void HelloBlock_FreeData(SiderModuleCtx *ctx, void *privdata) {
    REDISMODULE_NOT_USED(ctx);
    SiderModule_Free(privdata);
}

/* The thread entry point that actually executes the blocking part
 * of the command HELLO.BLOCK. */
void *HelloBlock_ThreadMain(void *arg) {
    void **targ = arg;
    SiderModuleBlockedClient *bc = targ[0];
    long long delay = (unsigned long)targ[1];
    SiderModule_Free(targ);

    sleep(delay);
    int *r = SiderModule_Alloc(sizeof(int));
    *r = rand();
    SiderModule_UnblockClient(bc,r);
    return NULL;
}

/* An example blocked client disconnection callback.
 *
 * Note that in the case of the HELLO.BLOCK command, the blocked client is now
 * owned by the thread calling sleep(). In this specific case, there is not
 * much we can do, however normally we could instead implement a way to
 * signal the thread that the client disconnected, and sleep the specified
 * amount of seconds with a while loop calling sleep(1), so that once we
 * detect the client disconnection, we can terminate the thread ASAP. */
void HelloBlock_Disconnected(SiderModuleCtx *ctx, SiderModuleBlockedClient *bc) {
    SiderModule_Log(ctx,"warning","Blocked client %p disconnected!",
        (void*)bc);

    /* Here you should cleanup your state / threads, and if possible
     * call SiderModule_UnblockClient(), or notify the thread that will
     * call the function ASAP. */
}

/* HELLO.BLOCK <delay> <timeout> -- Block for <count> seconds, then reply with
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
    void **targ = SiderModule_Alloc(sizeof(void*)*2);
    targ[0] = bc;
    targ[1] = (void*)(unsigned long) delay;

    if (pthread_create(&tid,NULL,HelloBlock_ThreadMain,targ) != 0) {
        SiderModule_AbortBlock(bc);
        return SiderModule_ReplyWithError(ctx,"-ERR Can't start thread");
    }
    return REDISMODULE_OK;
}

/* The thread entry point that actually executes the blocking part
 * of the command HELLO.KEYS.
 *
 * Note: this implementation is very simple on purpose, so no duplicated
 * keys (returned by SCAN) are filtered. However adding such a functionality
 * would be trivial just using any data structure implementing a dictionary
 * in order to filter the duplicated items. */
void *HelloKeys_ThreadMain(void *arg) {
    SiderModuleBlockedClient *bc = arg;
    SiderModuleCtx *ctx = SiderModule_GetThreadSafeContext(bc);
    long long cursor = 0;
    size_t replylen = 0;

    SiderModule_ReplyWithArray(ctx,REDISMODULE_POSTPONED_LEN);
    do {
        SiderModule_ThreadSafeContextLock(ctx);
        SiderModuleCallReply *reply = SiderModule_Call(ctx,
            "SCAN","l",(long long)cursor);
        SiderModule_ThreadSafeContextUnlock(ctx);

        SiderModuleCallReply *cr_cursor =
            SiderModule_CallReplyArrayElement(reply,0);
        SiderModuleCallReply *cr_keys =
            SiderModule_CallReplyArrayElement(reply,1);

        SiderModuleString *s = SiderModule_CreateStringFromCallReply(cr_cursor);
        SiderModule_StringToLongLong(s,&cursor);
        SiderModule_FreeString(ctx,s);

        size_t items = SiderModule_CallReplyLength(cr_keys);
        for (size_t j = 0; j < items; j++) {
            SiderModuleCallReply *ele =
                SiderModule_CallReplyArrayElement(cr_keys,j);
            SiderModule_ReplyWithCallReply(ctx,ele);
            replylen++;
        }
        SiderModule_FreeCallReply(reply);
    } while (cursor != 0);
    SiderModule_ReplySetArrayLength(ctx,replylen);

    SiderModule_FreeThreadSafeContext(ctx);
    SiderModule_UnblockClient(bc,NULL);
    return NULL;
}

/* HELLO.KEYS -- Return all the keys in the current database without blocking
 * the server. The keys do not represent a point-in-time state so only the keys
 * that were in the database from the start to the end are guaranteed to be
 * there. */
int HelloKeys_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if (argc != 1) return SiderModule_WrongArity(ctx);

    pthread_t tid;

    /* Note that when blocking the client we do not set any callback: no
     * timeout is possible since we passed '0', nor we need a reply callback
     * because we'll use the thread safe context to accumulate a reply. */
    SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx,NULL,NULL,NULL,0);

    /* Now that we setup a blocking client, we need to pass the control
     * to the thread. However we need to pass arguments to the thread:
     * the reference to the blocked client handle. */
    if (pthread_create(&tid,NULL,HelloKeys_ThreadMain,bc) != 0) {
        SiderModule_AbortBlock(bc);
        return SiderModule_ReplyWithError(ctx,"-ERR Can't start thread");
    }
    return REDISMODULE_OK;
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"helloblock",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hello.block",
        HelloBlock_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"hello.keys",
        HelloKeys_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
