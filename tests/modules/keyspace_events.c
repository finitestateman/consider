/* This module is used to test the server keyspace events API.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2020, Meir Shpilraien <meir at siderlabs dot com>
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

#define _BSD_SOURCE
#define _DEFAULT_SOURCE /* For usleep */

#include "sidermodule.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

ustime_t cached_time = 0;

/** stores all the keys on which we got 'loaded' keyspace notification **/
SiderModuleDict *loaded_event_log = NULL;
/** stores all the keys on which we got 'module' keyspace notification **/
SiderModuleDict *module_event_log = NULL;

/** Counts how many deleted KSN we got on keys with a prefix of "count_dels_" **/
static size_t dels = 0;

static int KeySpace_NotificationLoaded(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key){
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(type);

    if(strcmp(event, "loaded") == 0){
        const char* keyName = SiderModule_StringPtrLen(key, NULL);
        int nokey;
        SiderModule_DictGetC(loaded_event_log, (void*)keyName, strlen(keyName), &nokey);
        if(nokey){
            SiderModule_DictSetC(loaded_event_log, (void*)keyName, strlen(keyName), SiderModule_HoldString(ctx, key));
        }
    }

    return REDISMODULE_OK;
}

static int KeySpace_NotificationGeneric(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key) {
    REDISMODULE_NOT_USED(type);
    const char *key_str = SiderModule_StringPtrLen(key, NULL);
    if (strncmp(key_str, "count_dels_", 11) == 0 && strcmp(event, "del") == 0) {
        if (SiderModule_GetContextFlags(ctx) & REDISMODULE_CTX_FLAGS_MASTER) {
            dels++;
            SiderModule_Replicate(ctx, "keyspace.incr_dels", "");
        }
        return REDISMODULE_OK;
    }
    if (cached_time) {
        SiderModule_Assert(cached_time == SiderModule_CachedMicroseconds());
        usleep(1);
        SiderModule_Assert(cached_time != SiderModule_Microseconds());
    }

    if (strcmp(event, "del") == 0) {
        SiderModuleString *copykey = SiderModule_CreateStringPrintf(ctx, "%s_copy", SiderModule_StringPtrLen(key, NULL));
        SiderModuleCallReply* rep = SiderModule_Call(ctx, "DEL", "s!", copykey);
        SiderModule_FreeString(ctx, copykey);
        SiderModule_FreeCallReply(rep);

        int ctx_flags = SiderModule_GetContextFlags(ctx);
        if (ctx_flags & REDISMODULE_CTX_FLAGS_LUA) {
            SiderModuleCallReply* rep = SiderModule_Call(ctx, "INCR", "c", "lua");
            SiderModule_FreeCallReply(rep);
        }
        if (ctx_flags & REDISMODULE_CTX_FLAGS_MULTI) {
            SiderModuleCallReply* rep = SiderModule_Call(ctx, "INCR", "c", "multi");
            SiderModule_FreeCallReply(rep);
        }
    }

    return REDISMODULE_OK;
}

static int KeySpace_NotificationExpired(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key) {
    REDISMODULE_NOT_USED(type);
    REDISMODULE_NOT_USED(event);
    REDISMODULE_NOT_USED(key);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "INCR", "c!", "testkeyspace:expired");
    SiderModule_FreeCallReply(rep);

    return REDISMODULE_OK;
}

/* This key miss notification handler is performing a write command inside the notification callback.
 * Notice, it is discourage and currently wrong to perform a write command inside key miss event.
 * It can cause read commands to be replicated to the replica/aof. This test is here temporary (for coverage and
 * verification that it's not crashing). */
static int KeySpace_NotificationModuleKeyMiss(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key) {
    REDISMODULE_NOT_USED(type);
    REDISMODULE_NOT_USED(event);
    REDISMODULE_NOT_USED(key);

    int flags = SiderModule_GetContextFlags(ctx);
    if (!(flags & REDISMODULE_CTX_FLAGS_MASTER)) {
        return REDISMODULE_OK; // ignore the event on replica
    }

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "incr", "!c", "missed");
    SiderModule_FreeCallReply(rep);

    return REDISMODULE_OK;
}

static int KeySpace_NotificationModuleString(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key) {
    REDISMODULE_NOT_USED(type);
    REDISMODULE_NOT_USED(event);
    SiderModuleKey *sider_key = SiderModule_OpenKey(ctx, key, REDISMODULE_READ);

    size_t len = 0;
    /* SiderModule_StringDMA could change the data format and cause the old robj to be freed.
     * This code verifies that such format change will not cause any crashes.*/
    char *data = SiderModule_StringDMA(sider_key, &len, REDISMODULE_READ);
    int res = strncmp(data, "dummy", 5);
    REDISMODULE_NOT_USED(res);

    SiderModule_CloseKey(sider_key);

    return REDISMODULE_OK;
}

static void KeySpace_PostNotificationStringFreePD(void *pd) {
    SiderModule_FreeString(NULL, pd);
}

static void KeySpace_PostNotificationString(SiderModuleCtx *ctx, void *pd) {
    REDISMODULE_NOT_USED(ctx);
    SiderModuleCallReply* rep = SiderModule_Call(ctx, "incr", "!s", pd);
    SiderModule_FreeCallReply(rep);
}

static int KeySpace_NotificationModuleStringPostNotificationJob(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(type);
    REDISMODULE_NOT_USED(event);

    const char *key_str = SiderModule_StringPtrLen(key, NULL);

    if (strncmp(key_str, "string1_", 8) != 0) {
        return REDISMODULE_OK;
    }

    SiderModuleString *new_key = SiderModule_CreateStringPrintf(NULL, "string_changed{%s}", key_str);
    SiderModule_AddPostNotificationJob(ctx, KeySpace_PostNotificationString, new_key, KeySpace_PostNotificationStringFreePD);
    return REDISMODULE_OK;
}

static int KeySpace_NotificationModule(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(type);
    REDISMODULE_NOT_USED(event);

    const char* keyName = SiderModule_StringPtrLen(key, NULL);
    int nokey;
    SiderModule_DictGetC(module_event_log, (void*)keyName, strlen(keyName), &nokey);
    if(nokey){
        SiderModule_DictSetC(module_event_log, (void*)keyName, strlen(keyName), SiderModule_HoldString(ctx, key));
    }
    return REDISMODULE_OK;
}

static int cmdNotify(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    if(argc != 2){
        return SiderModule_WrongArity(ctx);
    }

    SiderModule_NotifyKeyspaceEvent(ctx, REDISMODULE_NOTIFY_MODULE, "notify", argv[1]);
    SiderModule_ReplyWithNull(ctx);
    return REDISMODULE_OK;
}

static int cmdIsModuleKeyNotified(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    if(argc != 2){
        return SiderModule_WrongArity(ctx);
    }

    const char* key  = SiderModule_StringPtrLen(argv[1], NULL);

    int nokey;
    SiderModuleString* keyStr = SiderModule_DictGetC(module_event_log, (void*)key, strlen(key), &nokey);

    SiderModule_ReplyWithArray(ctx, 2);
    SiderModule_ReplyWithLongLong(ctx, !nokey);
    if(nokey){
        SiderModule_ReplyWithNull(ctx);
    }else{
        SiderModule_ReplyWithString(ctx, keyStr);
    }
    return REDISMODULE_OK;
}

static int cmdIsKeyLoaded(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    if(argc != 2){
        return SiderModule_WrongArity(ctx);
    }

    const char* key  = SiderModule_StringPtrLen(argv[1], NULL);

    int nokey;
    SiderModuleString* keyStr = SiderModule_DictGetC(loaded_event_log, (void*)key, strlen(key), &nokey);

    SiderModule_ReplyWithArray(ctx, 2);
    SiderModule_ReplyWithLongLong(ctx, !nokey);
    if(nokey){
        SiderModule_ReplyWithNull(ctx);
    }else{
        SiderModule_ReplyWithString(ctx, keyStr);
    }
    return REDISMODULE_OK;
}

static int cmdDelKeyCopy(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2)
        return SiderModule_WrongArity(ctx);

    cached_time = SiderModule_CachedMicroseconds();

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "DEL", "s!", argv[1]);
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }
    cached_time = 0;
    return REDISMODULE_OK;
}

/* Call INCR and propagate using RM_Call with `!`. */
static int cmdIncrCase1(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2)
        return SiderModule_WrongArity(ctx);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "INCR", "s!", argv[1]);
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }
    return REDISMODULE_OK;
}

/* Call INCR and propagate using RM_Replicate. */
static int cmdIncrCase2(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2)
        return SiderModule_WrongArity(ctx);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "INCR", "s", argv[1]);
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }
    SiderModule_Replicate(ctx, "INCR", "s", argv[1]);
    return REDISMODULE_OK;
}

/* Call INCR and propagate using RM_ReplicateVerbatim. */
static int cmdIncrCase3(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2)
        return SiderModule_WrongArity(ctx);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, "INCR", "s", argv[1]);
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }
    SiderModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;
}

static int cmdIncrDels(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    dels++;
    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

static int cmdGetDels(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    return SiderModule_ReplyWithLongLong(ctx, dels);
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (SiderModule_Init(ctx,"testkeyspace",1,REDISMODULE_APIVER_1) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }

    loaded_event_log = SiderModule_CreateDict(ctx);
    module_event_log = SiderModule_CreateDict(ctx);

    int keySpaceAll = SiderModule_GetKeyspaceNotificationFlagsAll();

    if (!(keySpaceAll & REDISMODULE_NOTIFY_LOADED)) {
        // REDISMODULE_NOTIFY_LOADED event are not supported we can not start
        return REDISMODULE_ERR;
    }

    if(SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_LOADED, KeySpace_NotificationLoaded) != REDISMODULE_OK){
        return REDISMODULE_ERR;
    }

    if(SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_GENERIC, KeySpace_NotificationGeneric) != REDISMODULE_OK){
        return REDISMODULE_ERR;
    }

    if(SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_EXPIRED, KeySpace_NotificationExpired) != REDISMODULE_OK){
        return REDISMODULE_ERR;
    }

    if(SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_MODULE, KeySpace_NotificationModule) != REDISMODULE_OK){
        return REDISMODULE_ERR;
    }

    if(SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_KEY_MISS, KeySpace_NotificationModuleKeyMiss) != REDISMODULE_OK){
        return REDISMODULE_ERR;
    }

    if(SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_STRING, KeySpace_NotificationModuleString) != REDISMODULE_OK){
        return REDISMODULE_ERR;
    }

    if(SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_STRING, KeySpace_NotificationModuleStringPostNotificationJob) != REDISMODULE_OK){
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx,"keyspace.notify", cmdNotify,"",0,0,0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx,"keyspace.is_module_key_notified", cmdIsModuleKeyNotified,"",0,0,0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx,"keyspace.is_key_loaded", cmdIsKeyLoaded,"",0,0,0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx, "keyspace.del_key_copy", cmdDelKeyCopy,
                                  "write", 0, 0, 0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }
    
    if (SiderModule_CreateCommand(ctx, "keyspace.incr_case1", cmdIncrCase1,
                                  "write", 0, 0, 0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }
    
    if (SiderModule_CreateCommand(ctx, "keyspace.incr_case2", cmdIncrCase2,
                                  "write", 0, 0, 0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }
    
    if (SiderModule_CreateCommand(ctx, "keyspace.incr_case3", cmdIncrCase3,
                                  "write", 0, 0, 0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx, "keyspace.incr_dels", cmdIncrDels,
                                  "write", 0, 0, 0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx, "keyspace.get_dels", cmdGetDels,
                                  "readonly", 0, 0, 0) == REDISMODULE_ERR){
        return REDISMODULE_ERR;
    }

    if (argc == 1) {
        const char *ptr = SiderModule_StringPtrLen(argv[0], NULL);
        if (!strcasecmp(ptr, "noload")) {
            /* This is a hint that we return ERR at the last moment of OnLoad. */
            SiderModule_FreeDict(ctx, loaded_event_log);
            SiderModule_FreeDict(ctx, module_event_log);
            return REDISMODULE_ERR;
        }
    }

    return REDISMODULE_OK;
}

int SiderModule_OnUnload(SiderModuleCtx *ctx) {
    SiderModuleDictIter *iter = SiderModule_DictIteratorStartC(loaded_event_log, "^", NULL, 0);
    char* key;
    size_t keyLen;
    SiderModuleString* val;
    while((key = SiderModule_DictNextC(iter, &keyLen, (void**)&val))){
        SiderModule_FreeString(ctx, val);
    }
    SiderModule_FreeDict(ctx, loaded_event_log);
    SiderModule_DictIteratorStop(iter);
    loaded_event_log = NULL;

    iter = SiderModule_DictIteratorStartC(module_event_log, "^", NULL, 0);
    while((key = SiderModule_DictNextC(iter, &keyLen, (void**)&val))){
        SiderModule_FreeString(ctx, val);
    }
    SiderModule_FreeDict(ctx, module_event_log);
    SiderModule_DictIteratorStop(iter);
    module_event_log = NULL;

    return REDISMODULE_OK;
}
