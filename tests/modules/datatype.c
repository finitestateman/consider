/* This module current tests a small subset but should be extended in the future
 * for general ModuleDataType coverage.
 */

/* define macros for having usleep */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#include <unistd.h>

#include "sidermodule.h"

static SiderModuleType *datatype = NULL;
static int load_encver = 0;

/* used to test processing events during slow loading */
static volatile int slow_loading = 0;
static volatile int is_in_slow_loading = 0;

#define DATATYPE_ENC_VER 1

typedef struct {
    long long intval;
    SiderModuleString *strval;
} DataType;

static void *datatype_load(SiderModuleIO *io, int encver) {
    load_encver = encver;
    int intval = SiderModule_LoadSigned(io);
    if (SiderModule_IsIOError(io)) return NULL;

    SiderModuleString *strval = SiderModule_LoadString(io);
    if (SiderModule_IsIOError(io)) return NULL;

    DataType *dt = (DataType *) SiderModule_Alloc(sizeof(DataType));
    dt->intval = intval;
    dt->strval = strval;

    if (slow_loading) {
        SiderModuleCtx *ctx = SiderModule_GetContextFromIO(io);
        is_in_slow_loading = 1;
        while (slow_loading) {
            SiderModule_Yield(ctx, REDISMODULE_YIELD_FLAG_CLIENTS, "Slow module operation");
            usleep(1000);
        }
        is_in_slow_loading = 0;
    }

    return dt;
}

static void datatype_save(SiderModuleIO *io, void *value) {
    DataType *dt = (DataType *) value;
    SiderModule_SaveSigned(io, dt->intval);
    SiderModule_SaveString(io, dt->strval);
}

static void datatype_free(void *value) {
    if (value) {
        DataType *dt = (DataType *) value;

        if (dt->strval) SiderModule_FreeString(NULL, dt->strval);
        SiderModule_Free(dt);
    }
}

static void *datatype_copy(SiderModuleString *fromkey, SiderModuleString *tokey, const void *value) {
    const DataType *old = value;

    /* Answers to ultimate questions cannot be copied! */
    if (old->intval == 42)
        return NULL;

    DataType *new = (DataType *) SiderModule_Alloc(sizeof(DataType));

    new->intval = old->intval;
    new->strval = SiderModule_CreateStringFromString(NULL, old->strval);

    /* Breaking the rules here! We return a copy that also includes traces
     * of fromkey/tokey to confirm we get what we expect.
     */
    size_t len;
    const char *str = SiderModule_StringPtrLen(fromkey, &len);
    SiderModule_StringAppendBuffer(NULL, new->strval, "/", 1);
    SiderModule_StringAppendBuffer(NULL, new->strval, str, len);
    SiderModule_StringAppendBuffer(NULL, new->strval, "/", 1);
    str = SiderModule_StringPtrLen(tokey, &len);
    SiderModule_StringAppendBuffer(NULL, new->strval, str, len);

    return new;
}

static int datatype_set(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 4) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    long long intval;

    if (SiderModule_StringToLongLong(argv[2], &intval) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "Invalid integer value");
        return REDISMODULE_OK;
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);
    DataType *dt = SiderModule_Calloc(sizeof(DataType), 1);
    dt->intval = intval;
    dt->strval = argv[3];
    SiderModule_RetainString(ctx, dt->strval);

    SiderModule_ModuleTypeSetValue(key, datatype, dt);
    SiderModule_CloseKey(key);
    SiderModule_ReplyWithSimpleString(ctx, "OK");

    return REDISMODULE_OK;
}

static int datatype_restore(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 4) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    long long encver;
    if (SiderModule_StringToLongLong(argv[3], &encver) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "Invalid integer value");
        return REDISMODULE_OK;
    }

    DataType *dt = SiderModule_LoadDataTypeFromStringEncver(argv[2], datatype, encver);
    if (!dt) {
        SiderModule_ReplyWithError(ctx, "Invalid data");
        return REDISMODULE_OK;
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);
    SiderModule_ModuleTypeSetValue(key, datatype, dt);
    SiderModule_CloseKey(key);
    SiderModule_ReplyWithLongLong(ctx, load_encver);

    return REDISMODULE_OK;
}

static int datatype_get(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    DataType *dt = SiderModule_ModuleTypeGetValue(key);
    SiderModule_CloseKey(key);

    if (!dt) {
        SiderModule_ReplyWithNullArray(ctx);
    } else {
        SiderModule_ReplyWithArray(ctx, 2);
        SiderModule_ReplyWithLongLong(ctx, dt->intval);
        SiderModule_ReplyWithString(ctx, dt->strval);
    }
    return REDISMODULE_OK;
}

static int datatype_dump(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    DataType *dt = SiderModule_ModuleTypeGetValue(key);
    SiderModule_CloseKey(key);

    SiderModuleString *reply = SiderModule_SaveDataTypeToString(ctx, dt, datatype);
    if (!reply) {
        SiderModule_ReplyWithError(ctx, "Failed to save");
        return REDISMODULE_OK;
    }

    SiderModule_ReplyWithString(ctx, reply);
    SiderModule_FreeString(ctx, reply);
    return REDISMODULE_OK;
}

static int datatype_swap(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    SiderModuleKey *a = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);
    SiderModuleKey *b = SiderModule_OpenKey(ctx, argv[2], REDISMODULE_WRITE);
    void *val = SiderModule_ModuleTypeGetValue(a);

    int error = (SiderModule_ModuleTypeReplaceValue(b, datatype, val, &val) == REDISMODULE_ERR ||
                 SiderModule_ModuleTypeReplaceValue(a, datatype, val, NULL) == REDISMODULE_ERR);
    if (!error)
        SiderModule_ReplyWithSimpleString(ctx, "OK");
    else
        SiderModule_ReplyWithError(ctx, "ERR failed");

    SiderModule_CloseKey(a);
    SiderModule_CloseKey(b);

    return REDISMODULE_OK;
}

/* used to enable or disable slow loading */
static int datatype_slow_loading(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    long long ll;
    if (SiderModule_StringToLongLong(argv[1], &ll) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "Invalid integer value");
        return REDISMODULE_OK;
    }
    slow_loading = ll;
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

/* used to test if we reached the slow loading code */
static int datatype_is_in_slow_loading(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if (argc != 1) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    SiderModule_ReplyWithLongLong(ctx, is_in_slow_loading);
    return REDISMODULE_OK;
}

int createDataTypeBlockCheck(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    static SiderModuleType *datatype_outside_onload = NULL;

    SiderModuleTypeMethods datatype_methods = {
        .version = REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = datatype_load,
        .rdb_save = datatype_save,
        .free = datatype_free,
        .copy = datatype_copy
    };

    datatype_outside_onload = SiderModule_CreateDataType(ctx, "test_dt_outside_onload", 1, &datatype_methods);

    /* This validates that it's not possible to create datatype outside OnLoad,
     * thus returns an error if it succeeds. */
    if (datatype_outside_onload == NULL) {
        SiderModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        SiderModule_ReplyWithError(ctx, "UNEXPECTEDOK");
    }
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"datatype",DATATYPE_ENC_VER,REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Creates a command which creates a datatype outside OnLoad() function. */
    if (SiderModule_CreateCommand(ctx,"block.create.datatype.outside.onload", createDataTypeBlockCheck, "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModule_SetModuleOptions(ctx, REDISMODULE_OPTIONS_HANDLE_IO_ERRORS);

    SiderModuleTypeMethods datatype_methods = {
        .version = REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = datatype_load,
        .rdb_save = datatype_save,
        .free = datatype_free,
        .copy = datatype_copy
    };

    datatype = SiderModule_CreateDataType(ctx, "test___dt", 1, &datatype_methods);
    if (datatype == NULL)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"datatype.set", datatype_set,
                                  "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"datatype.get", datatype_get,"",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"datatype.restore", datatype_restore,
                                  "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"datatype.dump", datatype_dump,"",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "datatype.swap", datatype_swap,
                                  "write", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "datatype.slow_loading", datatype_slow_loading,
                                  "allow-loading", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "datatype.is_in_slow_loading", datatype_is_in_slow_loading,
                                  "allow-loading", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
