#include "sidermodule.h"
#include <string.h>
#include <assert.h>
#include <unistd.h>

#define UNUSED(V) ((void) V)

/* Registered type */
SiderModuleType *mallocsize_type = NULL;

typedef enum {
    UDT_RAW,
    UDT_STRING,
    UDT_DICT
} udt_type_t;

typedef struct {
    void *ptr;
    size_t len;
} raw_t;

typedef struct {
    udt_type_t type;
    union {
        raw_t raw;
        SiderModuleString *str;
        SiderModuleDict *dict;
    } data;
} udt_t;

void udt_free(void *value) {
    udt_t *udt = value;
    switch (udt->type) {
        case (UDT_RAW): {
            SiderModule_Free(udt->data.raw.ptr);
            break;
        }
        case (UDT_STRING): {
            SiderModule_FreeString(NULL, udt->data.str);
            break;
        }
        case (UDT_DICT): {
            SiderModuleString *dk, *dv;
            SiderModuleDictIter *iter = SiderModule_DictIteratorStartC(udt->data.dict, "^", NULL, 0);
            while((dk = SiderModule_DictNext(NULL, iter, (void **)&dv)) != NULL) {
                SiderModule_FreeString(NULL, dk);
                SiderModule_FreeString(NULL, dv);
            }
            SiderModule_DictIteratorStop(iter);
            SiderModule_FreeDict(NULL, udt->data.dict);
            break;
        }
    }
    SiderModule_Free(udt);
}

void udt_rdb_save(SiderModuleIO *rdb, void *value) {
    udt_t *udt = value;
    SiderModule_SaveUnsigned(rdb, udt->type);
    switch (udt->type) {
        case (UDT_RAW): {
            SiderModule_SaveStringBuffer(rdb, udt->data.raw.ptr, udt->data.raw.len);
            break;
        }
        case (UDT_STRING): {
            SiderModule_SaveString(rdb, udt->data.str);
            break;
        }
        case (UDT_DICT): {
            SiderModule_SaveUnsigned(rdb, SiderModule_DictSize(udt->data.dict));
            SiderModuleString *dk, *dv;
            SiderModuleDictIter *iter = SiderModule_DictIteratorStartC(udt->data.dict, "^", NULL, 0);
            while((dk = SiderModule_DictNext(NULL, iter, (void **)&dv)) != NULL) {
                SiderModule_SaveString(rdb, dk);
                SiderModule_SaveString(rdb, dv);
                SiderModule_FreeString(NULL, dk); /* Allocated by SiderModule_DictNext */
            }
            SiderModule_DictIteratorStop(iter);
            break;
        }
    }
}

void *udt_rdb_load(SiderModuleIO *rdb, int encver) {
    if (encver != 0)
        return NULL;
    udt_t *udt = SiderModule_Alloc(sizeof(*udt));
    udt->type = SiderModule_LoadUnsigned(rdb);
    switch (udt->type) {
        case (UDT_RAW): {
            udt->data.raw.ptr = SiderModule_LoadStringBuffer(rdb, &udt->data.raw.len);
            break;
        }
        case (UDT_STRING): {
            udt->data.str = SiderModule_LoadString(rdb);
            break;
        }
        case (UDT_DICT): {
            long long dict_len = SiderModule_LoadUnsigned(rdb);
            udt->data.dict = SiderModule_CreateDict(NULL);
            for (int i = 0; i < dict_len; i += 2) {
                SiderModuleString *key = SiderModule_LoadString(rdb);
                SiderModuleString *val = SiderModule_LoadString(rdb);
                SiderModule_DictSet(udt->data.dict, key, val);
            }
            break;
        }
    }

    return udt;
}

size_t udt_mem_usage(SiderModuleKeyOptCtx *ctx, const void *value, size_t sample_size) {
    UNUSED(ctx);
    UNUSED(sample_size);
    
    const udt_t *udt = value;
    size_t size = sizeof(*udt);
    
    switch (udt->type) {
        case (UDT_RAW): {
            size += SiderModule_MallocSize(udt->data.raw.ptr);
            break;
        }
        case (UDT_STRING): {
            size += SiderModule_MallocSizeString(udt->data.str);
            break;
        }
        case (UDT_DICT): {
            void *dk;
            size_t keylen;
            SiderModuleString *dv;
            SiderModuleDictIter *iter = SiderModule_DictIteratorStartC(udt->data.dict, "^", NULL, 0);
            while((dk = SiderModule_DictNextC(iter, &keylen, (void **)&dv)) != NULL) {
                size += keylen;
                size += SiderModule_MallocSizeString(dv);
            }
            SiderModule_DictIteratorStop(iter);
            break;
        }
    }
    
    return size;
}

/* MALLOCSIZE.SETRAW key len */
int cmd_setraw(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3)
        return SiderModule_WrongArity(ctx);
        
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);

    udt_t *udt = SiderModule_Alloc(sizeof(*udt));
    udt->type = UDT_RAW;
    
    long long raw_len;
    SiderModule_StringToLongLong(argv[2], &raw_len);
    udt->data.raw.ptr = SiderModule_Alloc(raw_len);
    udt->data.raw.len = raw_len;
    
    SiderModule_ModuleTypeSetValue(key, mallocsize_type, udt);
    SiderModule_CloseKey(key);

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

/* MALLOCSIZE.SETSTR key string */
int cmd_setstr(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3)
        return SiderModule_WrongArity(ctx);
        
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);

    udt_t *udt = SiderModule_Alloc(sizeof(*udt));
    udt->type = UDT_STRING;
    
    udt->data.str = argv[2];
    SiderModule_RetainString(ctx, argv[2]);
    
    SiderModule_ModuleTypeSetValue(key, mallocsize_type, udt);
    SiderModule_CloseKey(key);

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

/* MALLOCSIZE.SETDICT key field value [field value ...] */
int cmd_setdict(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc < 4 || argc % 2)
        return SiderModule_WrongArity(ctx);
        
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);

    udt_t *udt = SiderModule_Alloc(sizeof(*udt));
    udt->type = UDT_DICT;
    
    udt->data.dict = SiderModule_CreateDict(ctx);
    for (int i = 2; i < argc; i += 2) {
        SiderModule_DictSet(udt->data.dict, argv[i], argv[i+1]);
        /* No need to retain argv[i], it is copied as the rax key */
        SiderModule_RetainString(ctx, argv[i+1]);   
    }
    
    SiderModule_ModuleTypeSetValue(key, mallocsize_type, udt);
    SiderModule_CloseKey(key);

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    if (SiderModule_Init(ctx,"mallocsize",1,REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;
        
    SiderModuleTypeMethods tm = {
        .version = REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = udt_rdb_load,
        .rdb_save = udt_rdb_save,
        .free = udt_free,
        .mem_usage2 = udt_mem_usage,
    };

    mallocsize_type = SiderModule_CreateDataType(ctx, "allocsize", 0, &tm);
    if (mallocsize_type == NULL)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "mallocsize.setraw", cmd_setraw, "", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
        
    if (SiderModule_CreateCommand(ctx, "mallocsize.setstr", cmd_setstr, "", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
        
    if (SiderModule_CreateCommand(ctx, "mallocsize.setdict", cmd_setdict, "", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
