/* This file implements a new module native data type called "HELLOTYPE".
 * The data structure implemented is a very simple ordered linked list of
 * 64 bit integers, in order to have something that is real world enough, but
 * at the same time, extremely simple to understand, to show how the API
 * works, how a new data type is created, and how to write basic methods
 * for RDB loading, saving and AOF rewriting.
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
#include <ctype.h>
#include <string.h>
#include <stdint.h>

static SiderModuleType *HelloType;

/* ========================== Internal data structure  =======================
 * This is just a linked list of 64 bit integers where elements are inserted
 * in-place, so it's ordered. There is no pop/push operation but just insert
 * because it is enough to show the implementation of new data types without
 * making things complex. */

struct HelloTypeNode {
    int64_t value;
    struct HelloTypeNode *next;
};

struct HelloTypeObject {
    struct HelloTypeNode *head;
    size_t len; /* Number of elements added. */
};

struct HelloTypeObject *createHelloTypeObject(void) {
    struct HelloTypeObject *o;
    o = SiderModule_Alloc(sizeof(*o));
    o->head = NULL;
    o->len = 0;
    return o;
}

void HelloTypeInsert(struct HelloTypeObject *o, int64_t ele) {
    struct HelloTypeNode *next = o->head, *newnode, *prev = NULL;

    while(next && next->value < ele) {
        prev = next;
        next = next->next;
    }
    newnode = SiderModule_Alloc(sizeof(*newnode));
    newnode->value = ele;
    newnode->next = next;
    if (prev) {
        prev->next = newnode;
    } else {
        o->head = newnode;
    }
    o->len++;
}

void HelloTypeReleaseObject(struct HelloTypeObject *o) {
    struct HelloTypeNode *cur, *next;
    cur = o->head;
    while(cur) {
        next = cur->next;
        SiderModule_Free(cur);
        cur = next;
    }
    SiderModule_Free(o);
}

/* ========================= "hellotype" type commands ======================= */

/* HELLOTYPE.INSERT key value */
int HelloTypeInsert_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 3) return SiderModule_WrongArity(ctx);
    SiderModuleKey *key = SiderModule_OpenKey(ctx,argv[1],
        REDISMODULE_READ|REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY &&
        SiderModule_ModuleTypeGetType(key) != HelloType)
    {
        return SiderModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    long long value;
    if ((SiderModule_StringToLongLong(argv[2],&value) != REDISMODULE_OK)) {
        return SiderModule_ReplyWithError(ctx,"ERR invalid value: must be a signed 64 bit integer");
    }

    /* Create an empty value object if the key is currently empty. */
    struct HelloTypeObject *hto;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        hto = createHelloTypeObject();
        SiderModule_ModuleTypeSetValue(key,HelloType,hto);
    } else {
        hto = SiderModule_ModuleTypeGetValue(key);
    }

    /* Insert the new element. */
    HelloTypeInsert(hto,value);
    SiderModule_SignalKeyAsReady(ctx,argv[1]);

    SiderModule_ReplyWithLongLong(ctx,hto->len);
    SiderModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;
}

/* HELLOTYPE.RANGE key first count */
int HelloTypeRange_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 4) return SiderModule_WrongArity(ctx);
    SiderModuleKey *key = SiderModule_OpenKey(ctx,argv[1],
        REDISMODULE_READ|REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY &&
        SiderModule_ModuleTypeGetType(key) != HelloType)
    {
        return SiderModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    long long first, count;
    if (SiderModule_StringToLongLong(argv[2],&first) != REDISMODULE_OK ||
        SiderModule_StringToLongLong(argv[3],&count) != REDISMODULE_OK ||
        first < 0 || count < 0)
    {
        return SiderModule_ReplyWithError(ctx,
            "ERR invalid first or count parameters");
    }

    struct HelloTypeObject *hto = SiderModule_ModuleTypeGetValue(key);
    struct HelloTypeNode *node = hto ? hto->head : NULL;
    SiderModule_ReplyWithArray(ctx,REDISMODULE_POSTPONED_LEN);
    long long arraylen = 0;
    while(node && count--) {
        SiderModule_ReplyWithLongLong(ctx,node->value);
        arraylen++;
        node = node->next;
    }
    SiderModule_ReplySetArrayLength(ctx,arraylen);
    return REDISMODULE_OK;
}

/* HELLOTYPE.LEN key */
int HelloTypeLen_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 2) return SiderModule_WrongArity(ctx);
    SiderModuleKey *key = SiderModule_OpenKey(ctx,argv[1],
        REDISMODULE_READ|REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY &&
        SiderModule_ModuleTypeGetType(key) != HelloType)
    {
        return SiderModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    struct HelloTypeObject *hto = SiderModule_ModuleTypeGetValue(key);
    SiderModule_ReplyWithLongLong(ctx,hto ? hto->len : 0);
    return REDISMODULE_OK;
}

/* ====================== Example of a blocking command ==================== */

/* Reply callback for blocking command HELLOTYPE.BRANGE, this will get
 * called when the key we blocked for is ready: we need to check if we
 * can really serve the client, and reply OK or ERR accordingly. */
int HelloBlock_Reply(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleString *keyname = SiderModule_GetBlockedClientReadyKey(ctx);
    SiderModuleKey *key = SiderModule_OpenKey(ctx,keyname,REDISMODULE_READ);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_MODULE ||
        SiderModule_ModuleTypeGetType(key) != HelloType)
    {
        SiderModule_CloseKey(key);
        return REDISMODULE_ERR;
    }

    /* In case the key is able to serve our blocked client, let's directly
     * use our original command implementation to make this example simpler. */
    SiderModule_CloseKey(key);
    return HelloTypeRange_SiderCommand(ctx,argv,argc-1);
}

/* Timeout callback for blocking command HELLOTYPE.BRANGE */
int HelloBlock_Timeout(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    return SiderModule_ReplyWithSimpleString(ctx,"Request timedout");
}

/* Private data freeing callback for HELLOTYPE.BRANGE command. */
void HelloBlock_FreeData(SiderModuleCtx *ctx, void *privdata) {
    REDISMODULE_NOT_USED(ctx);
    SiderModule_Free(privdata);
}

/* HELLOTYPE.BRANGE key first count timeout -- This is a blocking version of
 * the RANGE operation, in order to show how to use the API
 * SiderModule_BlockClientOnKeys(). */
int HelloTypeBRange_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 5) return SiderModule_WrongArity(ctx);
    SiderModule_AutoMemory(ctx); /* Use automatic memory management. */
    SiderModuleKey *key = SiderModule_OpenKey(ctx,argv[1],
        REDISMODULE_READ|REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY &&
        SiderModule_ModuleTypeGetType(key) != HelloType)
    {
        return SiderModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    /* Parse the timeout before even trying to serve the client synchronously,
     * so that we always fail ASAP on syntax errors. */
    long long timeout;
    if (SiderModule_StringToLongLong(argv[4],&timeout) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx,
            "ERR invalid timeout parameter");
    }

    /* Can we serve the reply synchronously? */
    if (type != REDISMODULE_KEYTYPE_EMPTY) {
        return HelloTypeRange_SiderCommand(ctx,argv,argc-1);
    }

    /* Otherwise let's block on the key. */
    void *privdata = SiderModule_Alloc(100);
    SiderModule_BlockClientOnKeys(ctx,HelloBlock_Reply,HelloBlock_Timeout,HelloBlock_FreeData,timeout,argv+1,1,privdata);
    return REDISMODULE_OK;
}

/* ========================== "hellotype" type methods ======================= */

void *HelloTypeRdbLoad(SiderModuleIO *rdb, int encver) {
    if (encver != 0) {
        /* SiderModule_Log("warning","Can't load data with version %d", encver);*/
        return NULL;
    }
    uint64_t elements = SiderModule_LoadUnsigned(rdb);
    struct HelloTypeObject *hto = createHelloTypeObject();
    while(elements--) {
        int64_t ele = SiderModule_LoadSigned(rdb);
        HelloTypeInsert(hto,ele);
    }
    return hto;
}

void HelloTypeRdbSave(SiderModuleIO *rdb, void *value) {
    struct HelloTypeObject *hto = value;
    struct HelloTypeNode *node = hto->head;
    SiderModule_SaveUnsigned(rdb,hto->len);
    while(node) {
        SiderModule_SaveSigned(rdb,node->value);
        node = node->next;
    }
}

void HelloTypeAofRewrite(SiderModuleIO *aof, SiderModuleString *key, void *value) {
    struct HelloTypeObject *hto = value;
    struct HelloTypeNode *node = hto->head;
    while(node) {
        SiderModule_EmitAOF(aof,"HELLOTYPE.INSERT","sl",key,node->value);
        node = node->next;
    }
}

/* The goal of this function is to return the amount of memory used by
 * the HelloType value. */
size_t HelloTypeMemUsage(const void *value) {
    const struct HelloTypeObject *hto = value;
    struct HelloTypeNode *node = hto->head;
    return sizeof(*hto) + sizeof(*node)*hto->len;
}

void HelloTypeFree(void *value) {
    HelloTypeReleaseObject(value);
}

void HelloTypeDigest(SiderModuleDigest *md, void *value) {
    struct HelloTypeObject *hto = value;
    struct HelloTypeNode *node = hto->head;
    while(node) {
        SiderModule_DigestAddLongLong(md,node->value);
        node = node->next;
    }
    SiderModule_DigestEndSequence(md);
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"hellotype",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    SiderModuleTypeMethods tm = {
        .version = REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = HelloTypeRdbLoad,
        .rdb_save = HelloTypeRdbSave,
        .aof_rewrite = HelloTypeAofRewrite,
        .mem_usage = HelloTypeMemUsage,
        .free = HelloTypeFree,
        .digest = HelloTypeDigest
    };

    HelloType = SiderModule_CreateDataType(ctx,"hellotype",0,&tm);
    if (HelloType == NULL) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellotype.insert",
        HelloTypeInsert_SiderCommand,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellotype.range",
        HelloTypeRange_SiderCommand,"readonly",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellotype.len",
        HelloTypeLen_SiderCommand,"readonly",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellotype.brange",
        HelloTypeBRange_SiderCommand,"readonly",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
