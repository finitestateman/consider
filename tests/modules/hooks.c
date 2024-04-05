/* This module is used to test the server events hooks API.
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
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <assert.h>

/* We need to store events to be able to test and see what we got, and we can't
 * store them in the key-space since that would mess up rdb loading (duplicates)
 * and be lost of flushdb. */
SiderModuleDict *event_log = NULL;
/* stores all the keys on which we got 'removed' event */
SiderModuleDict *removed_event_log = NULL;
/* stores all the subevent on which we got 'removed' event */
SiderModuleDict *removed_subevent_type = NULL;
/* stores all the keys on which we got 'removed' event with expiry information */
SiderModuleDict *removed_expiry_log = NULL;

typedef struct EventElement {
    long count;
    SiderModuleString *last_val_string;
    long last_val_int;
} EventElement;

void LogStringEvent(SiderModuleCtx *ctx, const char* keyname, const char* data) {
    EventElement *event = SiderModule_DictGetC(event_log, (void*)keyname, strlen(keyname), NULL);
    if (!event) {
        event = SiderModule_Alloc(sizeof(EventElement));
        memset(event, 0, sizeof(EventElement));
        SiderModule_DictSetC(event_log, (void*)keyname, strlen(keyname), event);
    }
    if (event->last_val_string) SiderModule_FreeString(ctx, event->last_val_string);
    event->last_val_string = SiderModule_CreateString(ctx, data, strlen(data));
    event->count++;
}

void LogNumericEvent(SiderModuleCtx *ctx, const char* keyname, long data) {
    REDISMODULE_NOT_USED(ctx);
    EventElement *event = SiderModule_DictGetC(event_log, (void*)keyname, strlen(keyname), NULL);
    if (!event) {
        event = SiderModule_Alloc(sizeof(EventElement));
        memset(event, 0, sizeof(EventElement));
        SiderModule_DictSetC(event_log, (void*)keyname, strlen(keyname), event);
    }
    event->last_val_int = data;
    event->count++;
}

void FreeEvent(SiderModuleCtx *ctx, EventElement *event) {
    if (event->last_val_string)
        SiderModule_FreeString(ctx, event->last_val_string);
    SiderModule_Free(event);
}

int cmdEventCount(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2){
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    EventElement *event = SiderModule_DictGet(event_log, argv[1], NULL);
    SiderModule_ReplyWithLongLong(ctx, event? event->count: 0);
    return REDISMODULE_OK;
}

int cmdEventLast(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2){
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    EventElement *event = SiderModule_DictGet(event_log, argv[1], NULL);
    if (event && event->last_val_string)
        SiderModule_ReplyWithString(ctx, event->last_val_string);
    else if (event)
        SiderModule_ReplyWithLongLong(ctx, event->last_val_int);
    else
        SiderModule_ReplyWithNull(ctx);
    return REDISMODULE_OK;
}

void clearEvents(SiderModuleCtx *ctx)
{
    SiderModuleString *key;
    EventElement *event;
    SiderModuleDictIter *iter = SiderModule_DictIteratorStart(event_log, "^", NULL);
    while((key = SiderModule_DictNext(ctx, iter, (void**)&event)) != NULL) {
        event->count = 0;
        event->last_val_int = 0;
        if (event->last_val_string) SiderModule_FreeString(ctx, event->last_val_string);
        event->last_val_string = NULL;
        SiderModule_DictDel(event_log, key, NULL);
        SiderModule_Free(event);
    }
    SiderModule_DictIteratorStop(iter);
}

int cmdEventsClear(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argc);
    REDISMODULE_NOT_USED(argv);
    clearEvents(ctx);
    return REDISMODULE_OK;
}

/* Client state change callback. */
void clientChangeCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);

    SiderModuleClientInfo *ci = data;
    char *keyname = (sub == REDISMODULE_SUBEVENT_CLIENT_CHANGE_CONNECTED) ?
        "client-connected" : "client-disconnected";
    LogNumericEvent(ctx, keyname, ci->id);
}

void flushdbCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);

    SiderModuleFlushInfo *fi = data;
    char *keyname = (sub == REDISMODULE_SUBEVENT_FLUSHDB_START) ?
        "flush-start" : "flush-end";
    LogNumericEvent(ctx, keyname, fi->dbnum);
}

void roleChangeCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(data);

    SiderModuleReplicationInfo *ri = data;
    char *keyname = (sub == REDISMODULE_EVENT_REPLROLECHANGED_NOW_MASTER) ?
        "role-master" : "role-replica";
    LogStringEvent(ctx, keyname, ri->masterhost);
}

void replicationChangeCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(data);

    char *keyname = (sub == REDISMODULE_SUBEVENT_REPLICA_CHANGE_ONLINE) ?
        "replica-online" : "replica-offline";
    LogNumericEvent(ctx, keyname, 0);
}

void rasterLinkChangeCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(data);

    char *keyname = (sub == REDISMODULE_SUBEVENT_MASTER_LINK_UP) ?
        "masterlink-up" : "masterlink-down";
    LogNumericEvent(ctx, keyname, 0);
}

void persistenceCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(data);

    char *keyname = NULL;
    switch (sub) {
        case REDISMODULE_SUBEVENT_PERSISTENCE_RDB_START: keyname = "persistence-rdb-start"; break;
        case REDISMODULE_SUBEVENT_PERSISTENCE_AOF_START: keyname = "persistence-aof-start"; break;
        case REDISMODULE_SUBEVENT_PERSISTENCE_SYNC_AOF_START: keyname = "persistence-syncaof-start"; break;
        case REDISMODULE_SUBEVENT_PERSISTENCE_SYNC_RDB_START: keyname = "persistence-syncrdb-start"; break;
        case REDISMODULE_SUBEVENT_PERSISTENCE_ENDED: keyname = "persistence-end"; break;
        case REDISMODULE_SUBEVENT_PERSISTENCE_FAILED: keyname = "persistence-failed"; break;
    }
    /* modifying the keyspace from the fork child is not an option, using log instead */
    SiderModule_Log(ctx, "warning", "module-event-%s", keyname);
    if (sub == REDISMODULE_SUBEVENT_PERSISTENCE_SYNC_RDB_START ||
        sub == REDISMODULE_SUBEVENT_PERSISTENCE_SYNC_AOF_START) 
    {
        LogNumericEvent(ctx, keyname, 0);
    }
}

void loadingCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(data);

    char *keyname = NULL;
    switch (sub) {
        case REDISMODULE_SUBEVENT_LOADING_RDB_START: keyname = "loading-rdb-start"; break;
        case REDISMODULE_SUBEVENT_LOADING_AOF_START: keyname = "loading-aof-start"; break;
        case REDISMODULE_SUBEVENT_LOADING_REPL_START: keyname = "loading-repl-start"; break;
        case REDISMODULE_SUBEVENT_LOADING_ENDED: keyname = "loading-end"; break;
        case REDISMODULE_SUBEVENT_LOADING_FAILED: keyname = "loading-failed"; break;
    }
    LogNumericEvent(ctx, keyname, 0);
}

void loadingProgressCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);

    SiderModuleLoadingProgress *ei = data;
    char *keyname = (sub == REDISMODULE_SUBEVENT_LOADING_PROGRESS_RDB) ?
        "loading-progress-rdb" : "loading-progress-aof";
    LogNumericEvent(ctx, keyname, ei->progress);
}

void shutdownCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(data);
    REDISMODULE_NOT_USED(sub);

    SiderModule_Log(ctx, "warning", "module-event-%s", "shutdown");
}

void cronLoopCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(sub);

    SiderModuleCronLoop *ei = data;
    LogNumericEvent(ctx, "cron-loop", ei->hz);
}

void moduleChangeCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);

    SiderModuleModuleChange *ei = data;
    char *keyname = (sub == REDISMODULE_SUBEVENT_MODULE_LOADED) ?
        "module-loaded" : "module-unloaded";
    LogStringEvent(ctx, keyname, ei->module_name);
}

void swapDbCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(sub);

    SiderModuleSwapDbInfo *ei = data;
    LogNumericEvent(ctx, "swapdb-first", ei->dbnum_first);
    LogNumericEvent(ctx, "swapdb-second", ei->dbnum_second);
}

void configChangeCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    if (sub != REDISMODULE_SUBEVENT_CONFIG_CHANGE) {
        return;
    }

    SiderModuleConfigChangeV1 *ei = data;
    LogNumericEvent(ctx, "config-change-count", ei->num_changes);
    LogStringEvent(ctx, "config-change-first", ei->config_names[0]);
}

void keyInfoCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);

    SiderModuleKeyInfoV1 *ei = data;
    SiderModuleKey *kp = ei->key;
    SiderModuleString *key = (SiderModuleString *) SiderModule_GetKeyNameFromModuleKey(kp);
    const char *keyname = SiderModule_StringPtrLen(key, NULL);
    SiderModuleString *event_keyname = SiderModule_CreateStringPrintf(ctx, "key-info-%s", keyname);
    LogStringEvent(ctx, SiderModule_StringPtrLen(event_keyname, NULL), keyname);
    SiderModule_FreeString(ctx, event_keyname);

    /* Despite getting a key object from the callback, we also try to re-open it
     * to make sure the callback is called before it is actually removed from the keyspace. */
    SiderModuleKey *kp_open = SiderModule_OpenKey(ctx, key, REDISMODULE_READ);
    assert(SiderModule_ValueLength(kp) == SiderModule_ValueLength(kp_open));
    SiderModule_CloseKey(kp_open);

    /* We also try to RM_Call a command that accesses that key, also to make sure it's still in the keyspace. */
    char *size_command = NULL;
    int key_type = SiderModule_KeyType(kp);
    if (key_type == REDISMODULE_KEYTYPE_STRING) {
        size_command = "STRLEN";
    } else if (key_type == REDISMODULE_KEYTYPE_LIST) {
        size_command = "LLEN";
    } else if (key_type == REDISMODULE_KEYTYPE_HASH) {
        size_command = "HLEN";
    } else if (key_type == REDISMODULE_KEYTYPE_SET) {
        size_command = "SCARD";
    } else if (key_type == REDISMODULE_KEYTYPE_ZSET) {
        size_command = "ZCARD";
    } else if (key_type == REDISMODULE_KEYTYPE_STREAM) {
        size_command = "XLEN";
    }
    if (size_command != NULL) {
        SiderModuleCallReply *reply = SiderModule_Call(ctx, size_command, "s", key);
        assert(reply != NULL);
        assert(SiderModule_ValueLength(kp) == (size_t) SiderModule_CallReplyInteger(reply));
        SiderModule_FreeCallReply(reply);
    }

    /* Now use the key object we got from the callback for various validations. */
    SiderModuleString *prev = SiderModule_DictGetC(removed_event_log, (void*)keyname, strlen(keyname), NULL);
    /* We keep object length */
    SiderModuleString *v = SiderModule_CreateStringPrintf(ctx, "%zd", SiderModule_ValueLength(kp));
    /* For string type, we keep value instead of length */
    if (SiderModule_KeyType(kp) == REDISMODULE_KEYTYPE_STRING) {
        SiderModule_FreeString(ctx, v);
        size_t len;
        /* We need to access the string value with SiderModule_StringDMA.
         * SiderModule_StringDMA may call dbUnshareStringValue to free the origin object,
         * so we also can test it. */
        char *s = SiderModule_StringDMA(kp, &len, REDISMODULE_READ);
        v = SiderModule_CreateString(ctx, s, len);
    }
    SiderModule_DictReplaceC(removed_event_log, (void*)keyname, strlen(keyname), v);
    if (prev != NULL) {
        SiderModule_FreeString(ctx, prev);
    }

    const char *subevent = "deleted";
    if (sub == REDISMODULE_SUBEVENT_KEY_EXPIRED) {
        subevent = "expired";
    } else if (sub == REDISMODULE_SUBEVENT_KEY_EVICTED) {
        subevent = "evicted";
    } else if (sub == REDISMODULE_SUBEVENT_KEY_OVERWRITTEN) {
        subevent = "overwritten";
    }
    SiderModule_DictReplaceC(removed_subevent_type, (void*)keyname, strlen(keyname), (void *)subevent);

    SiderModuleString *prevexpire = SiderModule_DictGetC(removed_expiry_log, (void*)keyname, strlen(keyname), NULL);
    SiderModuleString *expire = SiderModule_CreateStringPrintf(ctx, "%lld", SiderModule_GetAbsExpire(kp));
    SiderModule_DictReplaceC(removed_expiry_log, (void*)keyname, strlen(keyname), (void *)expire);
    if (prevexpire != NULL) {
        SiderModule_FreeString(ctx, prevexpire);
    }
}

static int cmdIsKeyRemoved(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    if(argc != 2){
        return SiderModule_WrongArity(ctx);
    }

    const char *key  = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleString *value = SiderModule_DictGetC(removed_event_log, (void*)key, strlen(key), NULL);

    if (value == NULL) {
        return SiderModule_ReplyWithError(ctx, "ERR Key was not removed");
    }

    const char *subevent = SiderModule_DictGetC(removed_subevent_type, (void*)key, strlen(key), NULL);
    SiderModule_ReplyWithArray(ctx, 2);
    SiderModule_ReplyWithString(ctx, value);
    SiderModule_ReplyWithSimpleString(ctx, subevent);

    return REDISMODULE_OK;
}

static int cmdKeyExpiry(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    if(argc != 2){
        return SiderModule_WrongArity(ctx);
    }

    const char* key  = SiderModule_StringPtrLen(argv[1], NULL);
    SiderModuleString *expire = SiderModule_DictGetC(removed_expiry_log, (void*)key, strlen(key), NULL);
    if (expire == NULL) {
        return SiderModule_ReplyWithError(ctx, "ERR Key was not removed");
    }
    SiderModule_ReplyWithString(ctx, expire);
    return REDISMODULE_OK;
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
#define VerifySubEventSupported(e, s) \
    if (!SiderModule_IsSubEventSupported(e, s)) { \
        return REDISMODULE_ERR; \
    }

    if (SiderModule_Init(ctx,"testhook",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    /* Example on how to check if a server sub event is supported */
    if (!SiderModule_IsSubEventSupported(SiderModuleEvent_ReplicationRoleChanged, REDISMODULE_EVENT_REPLROLECHANGED_NOW_MASTER)) {
        return REDISMODULE_ERR;
    }

    /* replication related hooks */
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_ReplicationRoleChanged, roleChangeCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_ReplicaChange, replicationChangeCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_MasterLinkChange, rasterLinkChangeCallback);

    /* persistence related hooks */
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_Persistence, persistenceCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_Loading, loadingCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_LoadingProgress, loadingProgressCallback);

    /* other hooks */
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_ClientChange, clientChangeCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_FlushDB, flushdbCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_Shutdown, shutdownCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_CronLoop, cronLoopCallback);

    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_ModuleChange, moduleChangeCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_SwapDB, swapDbCallback);

    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_Config, configChangeCallback);

    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_Key, keyInfoCallback);

    event_log = SiderModule_CreateDict(ctx);
    removed_event_log = SiderModule_CreateDict(ctx);
    removed_subevent_type = SiderModule_CreateDict(ctx);
    removed_expiry_log = SiderModule_CreateDict(ctx);

    if (SiderModule_CreateCommand(ctx,"hooks.event_count", cmdEventCount,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"hooks.event_last", cmdEventLast,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"hooks.clear", cmdEventsClear,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"hooks.is_key_removed", cmdIsKeyRemoved,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"hooks.pexpireat", cmdKeyExpiry,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (argc == 1) {
        const char *ptr = SiderModule_StringPtrLen(argv[0], NULL);
        if (!strcasecmp(ptr, "noload")) {
            /* This is a hint that we return ERR at the last moment of OnLoad. */
            SiderModule_FreeDict(ctx, event_log);
            SiderModule_FreeDict(ctx, removed_event_log);
            SiderModule_FreeDict(ctx, removed_subevent_type);
            SiderModule_FreeDict(ctx, removed_expiry_log);
            return REDISMODULE_ERR;
        }
    }

    return REDISMODULE_OK;
}

int SiderModule_OnUnload(SiderModuleCtx *ctx) {
    clearEvents(ctx);
    SiderModule_FreeDict(ctx, event_log);
    event_log = NULL;

    SiderModuleDictIter *iter = SiderModule_DictIteratorStartC(removed_event_log, "^", NULL, 0);
    char* key;
    size_t keyLen;
    SiderModuleString* val;
    while((key = SiderModule_DictNextC(iter, &keyLen, (void**)&val))){
        SiderModule_FreeString(ctx, val);
    }
    SiderModule_FreeDict(ctx, removed_event_log);
    SiderModule_DictIteratorStop(iter);
    removed_event_log = NULL;

    SiderModule_FreeDict(ctx, removed_subevent_type);
    removed_subevent_type = NULL;

    iter = SiderModule_DictIteratorStartC(removed_expiry_log, "^", NULL, 0);
    while((key = SiderModule_DictNextC(iter, &keyLen, (void**)&val))){
        SiderModule_FreeString(ctx, val);
    }
    SiderModule_FreeDict(ctx, removed_expiry_log);
    SiderModule_DictIteratorStop(iter);
    removed_expiry_log = NULL;

    return REDISMODULE_OK;
}

