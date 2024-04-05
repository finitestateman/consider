#include "sidermodule.h"

#include <string.h>
#include <assert.h>
#include <unistd.h>

typedef struct {
    size_t nkeys;
} scan_strings_pd;

void scan_strings_callback(SiderModuleCtx *ctx, SiderModuleString* keyname, SiderModuleKey* key, void *privdata) {
    scan_strings_pd* pd = privdata;
    int was_opened = 0;
    if (!key) {
        key = SiderModule_OpenKey(ctx, keyname, REDISMODULE_READ);
        was_opened = 1;
    }

    if (SiderModule_KeyType(key) == REDISMODULE_KEYTYPE_STRING) {
        size_t len;
        char * data = SiderModule_StringDMA(key, &len, REDISMODULE_READ);
        SiderModule_ReplyWithArray(ctx, 2);
        SiderModule_ReplyWithString(ctx, keyname);
        SiderModule_ReplyWithStringBuffer(ctx, data, len);
        pd->nkeys++;
    }
    if (was_opened)
        SiderModule_CloseKey(key);
}

int scan_strings(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    scan_strings_pd pd = {
        .nkeys = 0,
    };

    SiderModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_LEN);

    SiderModuleScanCursor* cursor = SiderModule_ScanCursorCreate();
    while(SiderModule_Scan(ctx, cursor, scan_strings_callback, &pd));
    SiderModule_ScanCursorDestroy(cursor);

    SiderModule_ReplySetArrayLength(ctx, pd.nkeys);
    return REDISMODULE_OK;
}

typedef struct {
    SiderModuleCtx *ctx;
    size_t nreplies;
} scan_key_pd;

void scan_key_callback(SiderModuleKey *key, SiderModuleString* field, SiderModuleString* value, void *privdata) {
    REDISMODULE_NOT_USED(key);
    scan_key_pd* pd = privdata;
    SiderModule_ReplyWithArray(pd->ctx, 2);
    size_t fieldCStrLen;

    // The implementation of SiderModuleString is robj with lots of encodings.
    // We want to make sure the robj that passes to this callback in
    // String encoded, this is why we use SiderModule_StringPtrLen and
    // SiderModule_ReplyWithStringBuffer instead of directly use
    // SiderModule_ReplyWithString.
    const char* fieldCStr = SiderModule_StringPtrLen(field, &fieldCStrLen);
    SiderModule_ReplyWithStringBuffer(pd->ctx, fieldCStr, fieldCStrLen);
    if(value){
        size_t valueCStrLen;
        const char* valueCStr = SiderModule_StringPtrLen(value, &valueCStrLen);
        SiderModule_ReplyWithStringBuffer(pd->ctx, valueCStr, valueCStrLen);
    } else {
        SiderModule_ReplyWithNull(pd->ctx);
    }

    pd->nreplies++;
}

int scan_key(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    scan_key_pd pd = {
        .ctx = ctx,
        .nreplies = 0,
    };

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    if (!key) {
        SiderModule_ReplyWithError(ctx, "not found");
        return REDISMODULE_OK;
    }

    SiderModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);

    SiderModuleScanCursor* cursor = SiderModule_ScanCursorCreate();
    while(SiderModule_ScanKey(key, cursor, scan_key_callback, &pd));
    SiderModule_ScanCursorDestroy(cursor);

    SiderModule_ReplySetArrayLength(ctx, pd.nreplies);
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx, "scan", 1, REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "scan.scan_strings", scan_strings, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "scan.scan_key", scan_key, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}


