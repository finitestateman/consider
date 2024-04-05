/* This module is used to test a use case of a module that stores information
 * about keys in global memory, and relies on the enhanced data type callbacks to
 * get key name and dbid on various operations.
 *
 * it simulates a simple memory allocator. The smallest allocation unit of 
 * the allocator is a mem block with a size of 4KB. Multiple mem blocks are combined 
 * using a linked list. These linked lists are placed in a global dict named 'mem_pool'.
 * Each db has a 'mem_pool'. You can use the 'mem.alloc' command to allocate a specified 
 * number of mem blocks, and use 'mem.free' to release the memory. Use 'mem.write', 'mem.read'
 * to write and read the specified mem block (note that each mem block can only be written once).
 * Use 'mem.usage' to get the memory usage under different dbs, and it will return the size 
 * mem blocks and used mem blocks under the db.
 * The specific structure diagram is as follows:
 * 
 * 
 * Global variables of the module:
 * 
 *                                           mem blocks link
 *                          ┌─────┬─────┐
 *                          │     │     │    ┌───┐    ┌───┐    ┌───┐
 *                          │ k1  │  ───┼───►│4KB├───►│4KB├───►│4KB│
 *                          │     │     │    └───┘    └───┘    └───┘
 *                          ├─────┼─────┤
 *    ┌───────┐      ┌────► │     │     │    ┌───┐    ┌───┐
 *    │       │      │      │ k2  │  ───┼───►│4KB├───►│4KB│
 *    │ db0   ├──────┘      │     │     │    └───┘    └───┘
 *    │       │             ├─────┼─────┤
 *    ├───────┤             │     │     │    ┌───┐    ┌───┐    ┌───┐
 *    │       │             │ k3  │  ───┼───►│4KB├───►│4KB├───►│4KB│
 *    │ db1   ├──►null      │     │     │    └───┘    └───┘    └───┘
 *    │       │             └─────┴─────┘
 *    ├───────┤                  dict
 *    │       │
 *    │ db2   ├─────────┐
 *    │       │         │
 *    ├───────┤         │   ┌─────┬─────┐
 *    │       │         │   │     │     │    ┌───┐    ┌───┐    ┌───┐
 *    │ db3   ├──►null  │   │ k1  │  ───┼───►│4KB├───►│4KB├───►│4KB│
 *    │       │         │   │     │     │    └───┘    └───┘    └───┘
 *    └───────┘         │   ├─────┼─────┤
 * mem_pool[MAX_DB]     │   │     │     │    ┌───┐    ┌───┐
 *                      └──►│ k2  │  ───┼───►│4KB├───►│4KB│
 *                          │     │     │    └───┘    └───┘
 *                          └─────┴─────┘
 *                               dict
 * 
 * 
 * Keys in sider database:
 * 
 *                                ┌───────┐
 *                                │ size  │
 *                   ┌───────────►│ used  │
 *                   │            │ mask  │
 *     ┌─────┬─────┐ │            └───────┘                                   ┌───────┐
 *     │     │     │ │          MemAllocObject                                │ size  │
 *     │ k1  │  ───┼─┘                                           ┌───────────►│ used  │
 *     │     │     │                                             │            │ mask  │
 *     ├─────┼─────┤              ┌───────┐        ┌─────┬─────┐ │            └───────┘
 *     │     │     │              │ size  │        │     │     │ │          MemAllocObject
 *     │ k2  │  ───┼─────────────►│ used  │        │ k1  │  ───┼─┘
 *     │     │     │              │ mask  │        │     │     │
 *     ├─────┼─────┤              └───────┘        ├─────┼─────┤
 *     │     │     │            MemAllocObject     │     │     │
 *     │ k3  │  ───┼─┐                             │ k2  │  ───┼─┐
 *     │     │     │ │                             │     │     │ │
 *     └─────┴─────┘ │            ┌───────┐        └─────┴─────┘ │            ┌───────┐
 *      sider db[0]  │            │ size  │          sider db[1] │            │ size  │
 *                   └───────────►│ used  │                      └───────────►│ used  │
 *                                │ mask  │                                   │ mask  │
 *                                └───────┘                                   └───────┘
 *                              MemAllocObject                              MemAllocObject
 *
 **/

#include "sidermodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>

static SiderModuleType *MemAllocType;

#define MAX_DB 16
SiderModuleDict *mem_pool[MAX_DB];
typedef struct MemAllocObject {
    long long size;
    long long used;
    uint64_t mask;
} MemAllocObject;

MemAllocObject *createMemAllocObject(void) {
    MemAllocObject *o = SiderModule_Calloc(1, sizeof(*o));
    return o;
}

/*---------------------------- mem block apis ------------------------------------*/
#define BLOCK_SIZE 4096
struct MemBlock {
    char block[BLOCK_SIZE];
    struct MemBlock *next;
};

void MemBlockFree(struct MemBlock *head) {
    if (head) {
        struct MemBlock *block = head->next, *next;
        SiderModule_Free(head);
        while (block) {
            next = block->next;
            SiderModule_Free(block);
            block = next;
        }
    }
}
struct MemBlock *MemBlockCreate(long long num) {
    if (num <= 0) {
        return NULL;
    }

    struct MemBlock *head = SiderModule_Calloc(1, sizeof(struct MemBlock));
    struct MemBlock *block = head;
    while (--num) {
        block->next = SiderModule_Calloc(1, sizeof(struct MemBlock));
        block = block->next;
    }

    return head;
}

long long MemBlockNum(const struct MemBlock *head) {
    long long num = 0;
    const struct MemBlock *block = head;
    while (block) {
        num++;
        block = block->next;
    }

    return num;
}

size_t MemBlockWrite(struct MemBlock *head, long long block_index, const char *data, size_t size) {
    size_t w_size = 0;
    struct MemBlock *block = head;
    while (block_index-- && block) {
        block = block->next;
    }

    if (block) {
        size = size > BLOCK_SIZE ? BLOCK_SIZE:size;
        memcpy(block->block, data, size);
        w_size += size;
    }

    return w_size;
}

int MemBlockRead(struct MemBlock *head, long long block_index, char *data, size_t size) {
    size_t r_size = 0;
    struct MemBlock *block = head;
    while (block_index-- && block) {
        block = block->next;
    }

    if (block) {
        size = size > BLOCK_SIZE ? BLOCK_SIZE:size;
        memcpy(data, block->block, size);
        r_size += size;
    }

    return r_size;
}

void MemPoolFreeDb(SiderModuleCtx *ctx, int dbid) {
    SiderModuleString *key;
    void *tdata;
    SiderModuleDictIter *iter = SiderModule_DictIteratorStartC(mem_pool[dbid], "^", NULL, 0);
    while((key = SiderModule_DictNext(ctx, iter, &tdata)) != NULL) {
        MemBlockFree((struct MemBlock *)tdata);
    }
    SiderModule_DictIteratorStop(iter);
    SiderModule_FreeDict(NULL, mem_pool[dbid]);
    mem_pool[dbid] = SiderModule_CreateDict(NULL);
}

struct MemBlock *MemBlockClone(const struct MemBlock *head) {
    struct MemBlock *newhead = NULL;
    if (head) {
        newhead = SiderModule_Calloc(1, sizeof(struct MemBlock));
        memcpy(newhead->block, head->block, BLOCK_SIZE);
        struct MemBlock *newblock = newhead;
        const struct MemBlock *oldblock = head->next;
        while (oldblock) {
            newblock->next = SiderModule_Calloc(1, sizeof(struct MemBlock));
            newblock = newblock->next;
            memcpy(newblock->block, oldblock->block, BLOCK_SIZE);
            oldblock = oldblock->next;
        }
    }

    return newhead;
}

/*---------------------------- event handler ------------------------------------*/
void swapDbCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(sub);

    SiderModuleSwapDbInfo *ei = data;

    // swap
    SiderModuleDict *tmp = mem_pool[ei->dbnum_first];
    mem_pool[ei->dbnum_first] = mem_pool[ei->dbnum_second];
    mem_pool[ei->dbnum_second] = tmp;
}

void flushdbCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(e);
    int i;
    SiderModuleFlushInfo *fi = data;

    SiderModule_AutoMemory(ctx);

    if (sub == REDISMODULE_SUBEVENT_FLUSHDB_START) {
        if (fi->dbnum != -1) {
           MemPoolFreeDb(ctx, fi->dbnum);
        } else {
            for (i = 0; i < MAX_DB; i++) {
                MemPoolFreeDb(ctx, i);
            }
        }
    }
}

/*---------------------------- command implementation ------------------------------------*/

/* MEM.ALLOC key block_num */
int MemAlloc_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);  

    if (argc != 3) {
        return SiderModule_WrongArity(ctx);
    }

    long long block_num;
    if ((SiderModule_StringToLongLong(argv[2], &block_num) != REDISMODULE_OK) || block_num <= 0) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid block_num: must be a value greater than 0");
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY && SiderModule_ModuleTypeGetType(key) != MemAllocType) {
        return SiderModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    MemAllocObject *o;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        o = createMemAllocObject();
        SiderModule_ModuleTypeSetValue(key, MemAllocType, o);
    } else {
        o = SiderModule_ModuleTypeGetValue(key);
    }

    struct MemBlock *mem = MemBlockCreate(block_num);
    SiderModule_Assert(mem != NULL);
    SiderModule_DictSet(mem_pool[SiderModule_GetSelectedDb(ctx)], argv[1], mem);
    o->size = block_num;
    o->used = 0;
    o->mask = 0;

    SiderModule_ReplyWithLongLong(ctx, block_num);
    SiderModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;
}

/* MEM.FREE key */
int MemFree_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);  

    if (argc != 2) {
        return SiderModule_WrongArity(ctx);
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY && SiderModule_ModuleTypeGetType(key) != MemAllocType) {
        return SiderModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    int ret = 0;
    MemAllocObject *o;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        SiderModule_ReplyWithLongLong(ctx, ret);
        return REDISMODULE_OK;
    } else {
        o = SiderModule_ModuleTypeGetValue(key);
    }

    int nokey;
    struct MemBlock *mem = (struct MemBlock *)SiderModule_DictGet(mem_pool[SiderModule_GetSelectedDb(ctx)], argv[1], &nokey);
    if (!nokey && mem) {
        SiderModule_DictDel(mem_pool[SiderModule_GetSelectedDb(ctx)], argv[1], NULL);
        MemBlockFree(mem);
        o->used = 0;
        o->size = 0;
        o->mask = 0;
        ret = 1;
    }

    SiderModule_ReplyWithLongLong(ctx, ret);
    SiderModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;
}

/* MEM.WRITE key block_index data */
int MemWrite_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);  

    if (argc != 4) {
        return SiderModule_WrongArity(ctx);
    }

    long long block_index;
    if ((SiderModule_StringToLongLong(argv[2], &block_index) != REDISMODULE_OK) || block_index < 0) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid block_index: must be a value greater than 0");
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY && SiderModule_ModuleTypeGetType(key) != MemAllocType) {
        return SiderModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    MemAllocObject *o;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        return SiderModule_ReplyWithError(ctx, "ERR Memory has not been allocated");
    } else {
        o = SiderModule_ModuleTypeGetValue(key);
    }

    if (o->mask & (1UL << block_index)) {
        return SiderModule_ReplyWithError(ctx, "ERR block is busy");
    }

    int ret = 0;
    int nokey;
    struct MemBlock *mem = (struct MemBlock *)SiderModule_DictGet(mem_pool[SiderModule_GetSelectedDb(ctx)], argv[1], &nokey);
    if (!nokey && mem) {
        size_t len;
        const char *buf = SiderModule_StringPtrLen(argv[3], &len);
        ret = MemBlockWrite(mem, block_index, buf, len);
        o->mask |= (1UL << block_index);
        o->used++;
    }

    SiderModule_ReplyWithLongLong(ctx, ret);
    SiderModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;
}

/* MEM.READ key block_index */
int MemRead_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);  

    if (argc != 3) {
        return SiderModule_WrongArity(ctx);
    }

    long long block_index;
    if ((SiderModule_StringToLongLong(argv[2], &block_index) != REDISMODULE_OK) || block_index < 0) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid block_index: must be a value greater than 0");
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY && SiderModule_ModuleTypeGetType(key) != MemAllocType) {
        return SiderModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    MemAllocObject *o;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        return SiderModule_ReplyWithError(ctx, "ERR Memory has not been allocated");
    } else {
        o = SiderModule_ModuleTypeGetValue(key);
    }

    if (!(o->mask & (1UL << block_index))) {
        return SiderModule_ReplyWithNull(ctx);
    }

    int nokey;
    struct MemBlock *mem = (struct MemBlock *)SiderModule_DictGet(mem_pool[SiderModule_GetSelectedDb(ctx)], argv[1], &nokey);
    SiderModule_Assert(nokey == 0 && mem != NULL);
     
    char buf[BLOCK_SIZE];
    MemBlockRead(mem, block_index, buf, sizeof(buf));
    
    /* Assuming that the contents are all c-style strings */
    SiderModule_ReplyWithStringBuffer(ctx, buf, strlen(buf));
    return REDISMODULE_OK;
}

/* MEM.USAGE dbid */
int MemUsage_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);  

    if (argc != 2) {
        return SiderModule_WrongArity(ctx);
    }

    long long dbid;
    if ((SiderModule_StringToLongLong(argv[1], (long long *)&dbid) != REDISMODULE_OK)) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid value: must be a integer");
    }

    if (dbid < 0 || dbid >= MAX_DB) {
        return SiderModule_ReplyWithError(ctx, "ERR dbid out of range");
    }


    long long size = 0, used = 0;

    void *data;
    SiderModuleString *key;
    SiderModuleDictIter *iter = SiderModule_DictIteratorStartC(mem_pool[dbid], "^", NULL, 0);
    while((key = SiderModule_DictNext(ctx, iter, &data)) != NULL) {
        int dbbackup = SiderModule_GetSelectedDb(ctx);
        SiderModule_SelectDb(ctx, dbid);
        SiderModuleKey *openkey = SiderModule_OpenKey(ctx, key, REDISMODULE_READ);
        int type = SiderModule_KeyType(openkey);
        SiderModule_Assert(type != REDISMODULE_KEYTYPE_EMPTY && SiderModule_ModuleTypeGetType(openkey) == MemAllocType);
        MemAllocObject *o = SiderModule_ModuleTypeGetValue(openkey);
        used += o->used;
        size += o->size;
        SiderModule_CloseKey(openkey);
        SiderModule_SelectDb(ctx, dbbackup);
    }
    SiderModule_DictIteratorStop(iter);

    SiderModule_ReplyWithArray(ctx, 4);
    SiderModule_ReplyWithSimpleString(ctx, "total");
    SiderModule_ReplyWithLongLong(ctx, size);
    SiderModule_ReplyWithSimpleString(ctx, "used");
    SiderModule_ReplyWithLongLong(ctx, used);
    return REDISMODULE_OK;
}

/* MEM.ALLOCANDWRITE key block_num block_index data block_index data ... */
int MemAllocAndWrite_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);  

    if (argc < 3) {
        return SiderModule_WrongArity(ctx);
    }

    long long block_num;
    if ((SiderModule_StringToLongLong(argv[2], &block_num) != REDISMODULE_OK) || block_num <= 0) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid block_num: must be a value greater than 0");
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY && SiderModule_ModuleTypeGetType(key) != MemAllocType) {
        return SiderModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    MemAllocObject *o;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        o = createMemAllocObject();
        SiderModule_ModuleTypeSetValue(key, MemAllocType, o);
    } else {
        o = SiderModule_ModuleTypeGetValue(key);
    }

    struct MemBlock *mem = MemBlockCreate(block_num);
    SiderModule_Assert(mem != NULL);
    SiderModule_DictSet(mem_pool[SiderModule_GetSelectedDb(ctx)], argv[1], mem);
    o->used = 0;
    o->mask = 0;
    o->size = block_num;

    int i = 3;
    long long block_index;
    for (; i < argc; i++) {
        /* Security is guaranteed internally, so no security check. */
        SiderModule_StringToLongLong(argv[i], &block_index);
        size_t len;
        const char * buf = SiderModule_StringPtrLen(argv[i + 1], &len);
        MemBlockWrite(mem, block_index, buf, len);
        o->used++;
        o->mask |= (1UL << block_index);
    }

    SiderModule_ReplyWithSimpleString(ctx, "OK");
    SiderModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;
}

/*---------------------------- type callbacks ------------------------------------*/

void *MemAllocRdbLoad(SiderModuleIO *rdb, int encver) {
    if (encver != 0) {
        return NULL;
    }

    MemAllocObject *o = createMemAllocObject();
    o->size = SiderModule_LoadSigned(rdb);
    o->used = SiderModule_LoadSigned(rdb);
    o->mask = SiderModule_LoadUnsigned(rdb);

    const SiderModuleString *key = SiderModule_GetKeyNameFromIO(rdb);
    int dbid = SiderModule_GetDbIdFromIO(rdb);

    if (o->size) {
        size_t size;
        char *tmpbuf;
        long long num = o->size;
        struct MemBlock *head = SiderModule_Calloc(1, sizeof(struct MemBlock));
        tmpbuf = SiderModule_LoadStringBuffer(rdb, &size);
        memcpy(head->block, tmpbuf, size > BLOCK_SIZE ? BLOCK_SIZE:size);
        SiderModule_Free(tmpbuf);
        struct MemBlock *block = head;
        while (--num) {
            block->next = SiderModule_Calloc(1, sizeof(struct MemBlock));
            block = block->next;

            tmpbuf = SiderModule_LoadStringBuffer(rdb, &size);
            memcpy(block->block, tmpbuf, size > BLOCK_SIZE ? BLOCK_SIZE:size);
            SiderModule_Free(tmpbuf);
        }

        SiderModule_DictSet(mem_pool[dbid], (SiderModuleString *)key, head);
    }
     
    return o;
}

void MemAllocRdbSave(SiderModuleIO *rdb, void *value) {
    MemAllocObject *o = value;
    SiderModule_SaveSigned(rdb, o->size);
    SiderModule_SaveSigned(rdb, o->used);
    SiderModule_SaveUnsigned(rdb, o->mask);

    const SiderModuleString *key = SiderModule_GetKeyNameFromIO(rdb);
    int dbid = SiderModule_GetDbIdFromIO(rdb);

    if (o->size) {
        int nokey;
        struct MemBlock *mem = (struct MemBlock *)SiderModule_DictGet(mem_pool[dbid], (SiderModuleString *)key, &nokey);
        SiderModule_Assert(nokey == 0 && mem != NULL);

        struct MemBlock *block = mem; 
        while (block) {
            SiderModule_SaveStringBuffer(rdb, block->block, BLOCK_SIZE);
            block = block->next;
        }
    }
}

void MemAllocAofRewrite(SiderModuleIO *aof, SiderModuleString *key, void *value) {
    MemAllocObject *o = (MemAllocObject *)value;
    if (o->size) {
        int dbid = SiderModule_GetDbIdFromIO(aof);
        int nokey;
        size_t i = 0, j = 0;
        struct MemBlock *mem = (struct MemBlock *)SiderModule_DictGet(mem_pool[dbid], (SiderModuleString *)key, &nokey);
        SiderModule_Assert(nokey == 0 && mem != NULL);
        size_t array_size = o->size * 2;
        SiderModuleString ** string_array = SiderModule_Calloc(array_size, sizeof(SiderModuleString *));
        while (mem) {
            string_array[i] = SiderModule_CreateStringFromLongLong(NULL, j);
            string_array[i + 1] = SiderModule_CreateString(NULL, mem->block, BLOCK_SIZE);
            mem = mem->next;
            i += 2;
            j++;
        }
        SiderModule_EmitAOF(aof, "mem.allocandwrite", "slv", key, o->size, string_array, array_size);
        for (i = 0; i < array_size; i++) {
            SiderModule_FreeString(NULL, string_array[i]);
        }
        SiderModule_Free(string_array);
    } else {
        SiderModule_EmitAOF(aof, "mem.allocandwrite", "sl", key, o->size);
    }
}

void MemAllocFree(void *value) {
    SiderModule_Free(value);
}

void MemAllocUnlink(SiderModuleString *key, const void *value) {
    REDISMODULE_NOT_USED(key);
    REDISMODULE_NOT_USED(value);

    /* When unlink and unlink2 exist at the same time, we will only call unlink2. */
    SiderModule_Assert(0);
}

void MemAllocUnlink2(SiderModuleKeyOptCtx *ctx, const void *value) {
    MemAllocObject *o = (MemAllocObject *)value;

    const SiderModuleString *key = SiderModule_GetKeyNameFromOptCtx(ctx);
    int dbid = SiderModule_GetDbIdFromOptCtx(ctx);
    
    if (o->size) {
        void *oldval;
        SiderModule_DictDel(mem_pool[dbid], (SiderModuleString *)key, &oldval);
        SiderModule_Assert(oldval != NULL);
        MemBlockFree((struct MemBlock *)oldval);
    }
}

void MemAllocDigest(SiderModuleDigest *md, void *value) {
    MemAllocObject *o = (MemAllocObject *)value;
    SiderModule_DigestAddLongLong(md, o->size);
    SiderModule_DigestAddLongLong(md, o->used);
    SiderModule_DigestAddLongLong(md, o->mask);

    int dbid = SiderModule_GetDbIdFromDigest(md);
    const SiderModuleString *key = SiderModule_GetKeyNameFromDigest(md);
    
    if (o->size) {
        int nokey;
        struct MemBlock *mem = (struct MemBlock *)SiderModule_DictGet(mem_pool[dbid], (SiderModuleString *)key, &nokey);
        SiderModule_Assert(nokey == 0 && mem != NULL);

        struct MemBlock *block = mem;
        while (block) {
            SiderModule_DigestAddStringBuffer(md, (const char *)block->block, BLOCK_SIZE);
            block = block->next;
        }
    }
}

void *MemAllocCopy2(SiderModuleKeyOptCtx *ctx, const void *value) {
    const MemAllocObject *old = value;
    MemAllocObject *new = createMemAllocObject();
    new->size = old->size;
    new->used = old->used;
    new->mask = old->mask;

    int from_dbid = SiderModule_GetDbIdFromOptCtx(ctx);
    int to_dbid = SiderModule_GetToDbIdFromOptCtx(ctx);
    const SiderModuleString *fromkey = SiderModule_GetKeyNameFromOptCtx(ctx);
    const SiderModuleString *tokey = SiderModule_GetToKeyNameFromOptCtx(ctx);

    if (old->size) {
        int nokey;
        struct MemBlock *oldmem = (struct MemBlock *)SiderModule_DictGet(mem_pool[from_dbid], (SiderModuleString *)fromkey, &nokey);
        SiderModule_Assert(nokey == 0 && oldmem != NULL);
        struct MemBlock *newmem = MemBlockClone(oldmem);
        SiderModule_Assert(newmem != NULL);
        SiderModule_DictSet(mem_pool[to_dbid], (SiderModuleString *)tokey, newmem);
    }   

    return new;
}

size_t MemAllocMemUsage2(SiderModuleKeyOptCtx *ctx, const void *value, size_t sample_size) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(sample_size);
    uint64_t size = 0;
    MemAllocObject *o = (MemAllocObject *)value;

    size += sizeof(*o);
    size += o->size * sizeof(struct MemBlock);

    return size;
}

size_t MemAllocMemFreeEffort2(SiderModuleKeyOptCtx *ctx, const void *value) {
    REDISMODULE_NOT_USED(ctx);
    MemAllocObject *o = (MemAllocObject *)value;
    return o->size;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx, "datatype2", 1,REDISMODULE_APIVER_1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    SiderModuleTypeMethods tm = {
        .version = REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = MemAllocRdbLoad,
        .rdb_save = MemAllocRdbSave,
        .aof_rewrite = MemAllocAofRewrite,
        .free = MemAllocFree,
        .digest = MemAllocDigest,
        .unlink = MemAllocUnlink,
        // .defrag = MemAllocDefrag, // Tested in defragtest.c
        .unlink2 = MemAllocUnlink2,
        .copy2 = MemAllocCopy2,
        .mem_usage2 = MemAllocMemUsage2,
        .free_effort2 = MemAllocMemFreeEffort2,
    };

    MemAllocType = SiderModule_CreateDataType(ctx, "mem_alloc", 0, &tm);
    if (MemAllocType == NULL) {
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx, "mem.alloc", MemAlloc_SiderCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx, "mem.free", MemFree_SiderCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx, "mem.write", MemWrite_SiderCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx, "mem.read", MemRead_SiderCommand, "readonly", 1, 1, 1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx, "mem.usage", MemUsage_SiderCommand, "readonly", 1, 1, 1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    /* used for internal aof rewrite */
    if (SiderModule_CreateCommand(ctx, "mem.allocandwrite", MemAllocAndWrite_SiderCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    for(int i = 0; i < MAX_DB; i++){
        mem_pool[i] = SiderModule_CreateDict(NULL);
    }

    SiderModule_SubscribeToServerEvent(ctx, SiderModuleEvent_FlushDB, flushdbCallback);
    SiderModule_SubscribeToServerEvent(ctx, SiderModuleEvent_SwapDB, swapDbCallback);
  
    return REDISMODULE_OK;
}
