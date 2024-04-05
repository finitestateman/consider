/* This module emulates a linked list for lazyfree testing of modules, which
 is a simplified version of 'hellotype.c'
 */
#include "sidermodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>

static SiderModuleType *LazyFreeLinkType;

struct LazyFreeLinkNode {
    int64_t value;
    struct LazyFreeLinkNode *next;
};

struct LazyFreeLinkObject {
    struct LazyFreeLinkNode *head;
    size_t len; /* Number of elements added. */
};

struct LazyFreeLinkObject *createLazyFreeLinkObject(void) {
    struct LazyFreeLinkObject *o;
    o = SiderModule_Alloc(sizeof(*o));
    o->head = NULL;
    o->len = 0;
    return o;
}

void LazyFreeLinkInsert(struct LazyFreeLinkObject *o, int64_t ele) {
    struct LazyFreeLinkNode *next = o->head, *newnode, *prev = NULL;

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

void LazyFreeLinkReleaseObject(struct LazyFreeLinkObject *o) {
    struct LazyFreeLinkNode *cur, *next;
    cur = o->head;
    while(cur) {
        next = cur->next;
        SiderModule_Free(cur);
        cur = next;
    }
    SiderModule_Free(o);
}

/* LAZYFREELINK.INSERT key value */
int LazyFreeLinkInsert_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 3) return SiderModule_WrongArity(ctx);
    SiderModuleKey *key = SiderModule_OpenKey(ctx,argv[1],
        REDISMODULE_READ|REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY &&
        SiderModule_ModuleTypeGetType(key) != LazyFreeLinkType)
    {
        return SiderModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    long long value;
    if ((SiderModule_StringToLongLong(argv[2],&value) != REDISMODULE_OK)) {
        return SiderModule_ReplyWithError(ctx,"ERR invalid value: must be a signed 64 bit integer");
    }

    struct LazyFreeLinkObject *hto;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        hto = createLazyFreeLinkObject();
        SiderModule_ModuleTypeSetValue(key,LazyFreeLinkType,hto);
    } else {
        hto = SiderModule_ModuleTypeGetValue(key);
    }

    LazyFreeLinkInsert(hto,value);
    SiderModule_SignalKeyAsReady(ctx,argv[1]);

    SiderModule_ReplyWithLongLong(ctx,hto->len);
    SiderModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;
}

/* LAZYFREELINK.LEN key */
int LazyFreeLinkLen_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx); /* Use automatic memory management. */

    if (argc != 2) return SiderModule_WrongArity(ctx);
    SiderModuleKey *key = SiderModule_OpenKey(ctx,argv[1],
                                              REDISMODULE_READ);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY &&
        SiderModule_ModuleTypeGetType(key) != LazyFreeLinkType)
    {
        return SiderModule_ReplyWithError(ctx,REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    struct LazyFreeLinkObject *hto = SiderModule_ModuleTypeGetValue(key);
    SiderModule_ReplyWithLongLong(ctx,hto ? hto->len : 0);
    return REDISMODULE_OK;
}

void *LazyFreeLinkRdbLoad(SiderModuleIO *rdb, int encver) {
    if (encver != 0) {
        return NULL;
    }
    uint64_t elements = SiderModule_LoadUnsigned(rdb);
    struct LazyFreeLinkObject *hto = createLazyFreeLinkObject();
    while(elements--) {
        int64_t ele = SiderModule_LoadSigned(rdb);
        LazyFreeLinkInsert(hto,ele);
    }
    return hto;
}

void LazyFreeLinkRdbSave(SiderModuleIO *rdb, void *value) {
    struct LazyFreeLinkObject *hto = value;
    struct LazyFreeLinkNode *node = hto->head;
    SiderModule_SaveUnsigned(rdb,hto->len);
    while(node) {
        SiderModule_SaveSigned(rdb,node->value);
        node = node->next;
    }
}

void LazyFreeLinkAofRewrite(SiderModuleIO *aof, SiderModuleString *key, void *value) {
    struct LazyFreeLinkObject *hto = value;
    struct LazyFreeLinkNode *node = hto->head;
    while(node) {
        SiderModule_EmitAOF(aof,"LAZYFREELINK.INSERT","sl",key,node->value);
        node = node->next;
    }
}

void LazyFreeLinkFree(void *value) {
    LazyFreeLinkReleaseObject(value);
}

size_t LazyFreeLinkFreeEffort(SiderModuleString *key, const void *value) {
    REDISMODULE_NOT_USED(key);
    const struct LazyFreeLinkObject *hto = value;
    return hto->len;
}

void LazyFreeLinkUnlink(SiderModuleString *key, const void *value) {
    REDISMODULE_NOT_USED(key);
    REDISMODULE_NOT_USED(value);
    /* Here you can know which key and value is about to be freed. */
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"lazyfreetest",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    /* We only allow our module to be loaded when the sider core version is greater than the version of my module */
    if (SiderModule_GetTypeMethodVersion() < REDISMODULE_TYPE_METHOD_VERSION) {
        return REDISMODULE_ERR;
    }

    SiderModuleTypeMethods tm = {
        .version = REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = LazyFreeLinkRdbLoad,
        .rdb_save = LazyFreeLinkRdbSave,
        .aof_rewrite = LazyFreeLinkAofRewrite,
        .free = LazyFreeLinkFree,
        .free_effort = LazyFreeLinkFreeEffort,
        .unlink = LazyFreeLinkUnlink,
    };

    LazyFreeLinkType = SiderModule_CreateDataType(ctx,"test_lazy",0,&tm);
    if (LazyFreeLinkType == NULL) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"lazyfreelink.insert",
        LazyFreeLinkInsert_SiderCommand,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"lazyfreelink.len",
        LazyFreeLinkLen_SiderCommand,"readonly",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
