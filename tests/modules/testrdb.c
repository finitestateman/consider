#include "sidermodule.h"

#include <string.h>
#include <assert.h>

/* Module configuration, save aux or not? */
#define CONF_AUX_OPTION_NO_AUX           0
#define CONF_AUX_OPTION_SAVE2            1 << 0
#define CONF_AUX_OPTION_BEFORE_KEYSPACE  1 << 1
#define CONF_AUX_OPTION_AFTER_KEYSPACE   1 << 2
#define CONF_AUX_OPTION_NO_DATA          1 << 3
long long conf_aux_count = 0;

/* Registered type */
SiderModuleType *testrdb_type = NULL;

/* Global values to store and persist to aux */
SiderModuleString *before_str = NULL;
SiderModuleString *after_str = NULL;

/* Global values used to keep aux from db being loaded (in case of async_loading) */
SiderModuleString *before_str_temp = NULL;
SiderModuleString *after_str_temp = NULL;

/* Indicates whether there is an async replication in progress.
 * We control this value from SiderModuleEvent_ReplAsyncLoad events. */
int async_loading = 0;

int n_aux_load_called = 0;

void replAsyncLoadCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(e);
    REDISMODULE_NOT_USED(data);

    switch (sub) {
    case REDISMODULE_SUBEVENT_REPL_ASYNC_LOAD_STARTED:
        assert(async_loading == 0);
        async_loading = 1;
        break;
    case REDISMODULE_SUBEVENT_REPL_ASYNC_LOAD_ABORTED:
        /* Discard temp aux */
        if (before_str_temp)
            SiderModule_FreeString(ctx, before_str_temp);
        if (after_str_temp)
            SiderModule_FreeString(ctx, after_str_temp);
        before_str_temp = NULL;
        after_str_temp = NULL;

        async_loading = 0;
        break;
    case REDISMODULE_SUBEVENT_REPL_ASYNC_LOAD_COMPLETED:
        if (before_str)
            SiderModule_FreeString(ctx, before_str);
        if (after_str)
            SiderModule_FreeString(ctx, after_str);
        before_str = before_str_temp;
        after_str = after_str_temp;

        before_str_temp = NULL;
        after_str_temp = NULL;

        async_loading = 0;
        break;
    default:
        assert(0);
    }
}

void *testrdb_type_load(SiderModuleIO *rdb, int encver) {
    int count = SiderModule_LoadSigned(rdb);
    SiderModuleString *str = SiderModule_LoadString(rdb);
    float f = SiderModule_LoadFloat(rdb);
    long double ld = SiderModule_LoadLongDouble(rdb);
    if (SiderModule_IsIOError(rdb)) {
        SiderModuleCtx *ctx = SiderModule_GetContextFromIO(rdb);
        if (str)
            SiderModule_FreeString(ctx, str);
        return NULL;
    }
    /* Using the values only after checking for io errors. */
    assert(count==1);
    assert(encver==1);
    assert(f==1.5f);
    assert(ld==0.333333333333333333L);
    return str;
}

void testrdb_type_save(SiderModuleIO *rdb, void *value) {
    SiderModuleString *str = (SiderModuleString*)value;
    SiderModule_SaveSigned(rdb, 1);
    SiderModule_SaveString(rdb, str);
    SiderModule_SaveFloat(rdb, 1.5);
    SiderModule_SaveLongDouble(rdb, 0.333333333333333333L);
}

void testrdb_aux_save(SiderModuleIO *rdb, int when) {
    if (!(conf_aux_count & CONF_AUX_OPTION_BEFORE_KEYSPACE)) assert(when == REDISMODULE_AUX_AFTER_RDB);
    if (!(conf_aux_count & CONF_AUX_OPTION_AFTER_KEYSPACE)) assert(when == REDISMODULE_AUX_BEFORE_RDB);
    assert(conf_aux_count!=CONF_AUX_OPTION_NO_AUX);
    if (when == REDISMODULE_AUX_BEFORE_RDB) {
        if (before_str) {
            SiderModule_SaveSigned(rdb, 1);
            SiderModule_SaveString(rdb, before_str);
        } else {
            SiderModule_SaveSigned(rdb, 0);
        }
    } else {
        if (after_str) {
            SiderModule_SaveSigned(rdb, 1);
            SiderModule_SaveString(rdb, after_str);
        } else {
            SiderModule_SaveSigned(rdb, 0);
        }
    }
}

int testrdb_aux_load(SiderModuleIO *rdb, int encver, int when) {
    assert(encver == 1);
    if (!(conf_aux_count & CONF_AUX_OPTION_BEFORE_KEYSPACE)) assert(when == REDISMODULE_AUX_AFTER_RDB);
    if (!(conf_aux_count & CONF_AUX_OPTION_AFTER_KEYSPACE)) assert(when == REDISMODULE_AUX_BEFORE_RDB);
    assert(conf_aux_count!=CONF_AUX_OPTION_NO_AUX);
    SiderModuleCtx *ctx = SiderModule_GetContextFromIO(rdb);
    if (when == REDISMODULE_AUX_BEFORE_RDB) {
        if (async_loading == 0) {
            if (before_str)
                SiderModule_FreeString(ctx, before_str);
            before_str = NULL;
            int count = SiderModule_LoadSigned(rdb);
            if (SiderModule_IsIOError(rdb))
                return REDISMODULE_ERR;
            if (count)
                before_str = SiderModule_LoadString(rdb);
        } else {
            if (before_str_temp)
                SiderModule_FreeString(ctx, before_str_temp);
            before_str_temp = NULL;
            int count = SiderModule_LoadSigned(rdb);
            if (SiderModule_IsIOError(rdb))
                return REDISMODULE_ERR;
            if (count)
                before_str_temp = SiderModule_LoadString(rdb);
        }
    } else {
        if (async_loading == 0) {
            if (after_str)
                SiderModule_FreeString(ctx, after_str);
            after_str = NULL;
            int count = SiderModule_LoadSigned(rdb);
            if (SiderModule_IsIOError(rdb))
                return REDISMODULE_ERR;
            if (count)
                after_str = SiderModule_LoadString(rdb);
        } else {
            if (after_str_temp)
                SiderModule_FreeString(ctx, after_str_temp);
            after_str_temp = NULL;
            int count = SiderModule_LoadSigned(rdb);
            if (SiderModule_IsIOError(rdb))
                return REDISMODULE_ERR;
            if (count)
                after_str_temp = SiderModule_LoadString(rdb);
        }
    }

    if (SiderModule_IsIOError(rdb))
        return REDISMODULE_ERR;
    return REDISMODULE_OK;
}

void testrdb_type_free(void *value) {
    if (value)
        SiderModule_FreeString(NULL, (SiderModuleString*)value);
}

int testrdb_set_before(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    if (before_str)
        SiderModule_FreeString(ctx, before_str);
    before_str = argv[1];
    SiderModule_RetainString(ctx, argv[1]);
    SiderModule_ReplyWithLongLong(ctx, 1);
    return REDISMODULE_OK;
}

int testrdb_get_before(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    if (argc != 1){
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    if (before_str)
        SiderModule_ReplyWithString(ctx, before_str);
    else
        SiderModule_ReplyWithStringBuffer(ctx, "", 0);
    return REDISMODULE_OK;
}

/* For purpose of testing module events, expose variable state during async_loading. */
int testrdb_async_loading_get_before(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    if (argc != 1){
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    if (before_str_temp)
        SiderModule_ReplyWithString(ctx, before_str_temp);
    else
        SiderModule_ReplyWithStringBuffer(ctx, "", 0);
    return REDISMODULE_OK;
}

int testrdb_set_after(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2){
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    if (after_str)
        SiderModule_FreeString(ctx, after_str);
    after_str = argv[1];
    SiderModule_RetainString(ctx, argv[1]);
    SiderModule_ReplyWithLongLong(ctx, 1);
    return REDISMODULE_OK;
}

int testrdb_get_after(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    if (argc != 1){
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    if (after_str)
        SiderModule_ReplyWithString(ctx, after_str);
    else
        SiderModule_ReplyWithStringBuffer(ctx, "", 0);
    return REDISMODULE_OK;
}

int testrdb_set_key(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 3){
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);
    SiderModuleString *str = SiderModule_ModuleTypeGetValue(key);
    if (str)
        SiderModule_FreeString(ctx, str);
    SiderModule_ModuleTypeSetValue(key, testrdb_type, argv[2]);
    SiderModule_RetainString(ctx, argv[2]);
    SiderModule_CloseKey(key);
    SiderModule_ReplyWithLongLong(ctx, 1);
    return REDISMODULE_OK;
}

int testrdb_get_key(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2){
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    SiderModuleString *str = SiderModule_ModuleTypeGetValue(key);
    SiderModule_CloseKey(key);
    SiderModule_ReplyWithString(ctx, str);
    return REDISMODULE_OK;
}

int testrdb_get_n_aux_load_called(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModule_ReplyWithLongLong(ctx, n_aux_load_called);
    return REDISMODULE_OK;
}

int test2rdb_aux_load(SiderModuleIO *rdb, int encver, int when) {
    REDISMODULE_NOT_USED(rdb);
    REDISMODULE_NOT_USED(encver);
    REDISMODULE_NOT_USED(when);
    n_aux_load_called++;
    return REDISMODULE_OK;
}

void test2rdb_aux_save(SiderModuleIO *rdb, int when) {
    REDISMODULE_NOT_USED(rdb);
    REDISMODULE_NOT_USED(when);
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx,"testrdb",1,REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModule_SetModuleOptions(ctx, REDISMODULE_OPTIONS_HANDLE_IO_ERRORS | REDISMODULE_OPTIONS_HANDLE_REPL_ASYNC_LOAD);

    if (argc > 0)
        SiderModule_StringToLongLong(argv[0], &conf_aux_count);

    if (conf_aux_count==CONF_AUX_OPTION_NO_AUX) {
        SiderModuleTypeMethods datatype_methods = {
            .version = 1,
            .rdb_load = testrdb_type_load,
            .rdb_save = testrdb_type_save,
            .aof_rewrite = NULL,
            .digest = NULL,
            .free = testrdb_type_free,
        };

        testrdb_type = SiderModule_CreateDataType(ctx, "test__rdb", 1, &datatype_methods);
        if (testrdb_type == NULL)
            return REDISMODULE_ERR;
    } else if (!(conf_aux_count & CONF_AUX_OPTION_NO_DATA)) {
        SiderModuleTypeMethods datatype_methods = {
            .version = REDISMODULE_TYPE_METHOD_VERSION,
            .rdb_load = testrdb_type_load,
            .rdb_save = testrdb_type_save,
            .aof_rewrite = NULL,
            .digest = NULL,
            .free = testrdb_type_free,
            .aux_load = testrdb_aux_load,
            .aux_save = testrdb_aux_save,
            .aux_save_triggers = ((conf_aux_count & CONF_AUX_OPTION_BEFORE_KEYSPACE) ? REDISMODULE_AUX_BEFORE_RDB : 0) |
                                 ((conf_aux_count & CONF_AUX_OPTION_AFTER_KEYSPACE)  ? REDISMODULE_AUX_AFTER_RDB : 0)
        };

        if (conf_aux_count & CONF_AUX_OPTION_SAVE2) {
            datatype_methods.aux_save2 = testrdb_aux_save;
        }

        testrdb_type = SiderModule_CreateDataType(ctx, "test__rdb", 1, &datatype_methods);
        if (testrdb_type == NULL)
            return REDISMODULE_ERR;
    } else {

        /* Used to verify that aux_save2 api without any data, saves nothing to the RDB. */
        SiderModuleTypeMethods datatype_methods = {
            .version = REDISMODULE_TYPE_METHOD_VERSION,
            .aux_load = test2rdb_aux_load,
            .aux_save = test2rdb_aux_save,
            .aux_save_triggers = ((conf_aux_count & CONF_AUX_OPTION_BEFORE_KEYSPACE) ? REDISMODULE_AUX_BEFORE_RDB : 0) |
                                 ((conf_aux_count & CONF_AUX_OPTION_AFTER_KEYSPACE)  ? REDISMODULE_AUX_AFTER_RDB : 0)
        };
        if (conf_aux_count & CONF_AUX_OPTION_SAVE2) {
            datatype_methods.aux_save2 = test2rdb_aux_save;
        }

        SiderModule_CreateDataType(ctx, "test__rdb", 1, &datatype_methods);
    }

    if (SiderModule_CreateCommand(ctx,"testrdb.set.before", testrdb_set_before,"deny-oom",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testrdb.get.before", testrdb_get_before,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testrdb.async_loading.get.before", testrdb_async_loading_get_before,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testrdb.set.after", testrdb_set_after,"deny-oom",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testrdb.get.after", testrdb_get_after,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testrdb.set.key", testrdb_set_key,"deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testrdb.get.key", testrdb_get_key,"",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testrdb.get.n_aux_load_called", testrdb_get_n_aux_load_called,"",1,1,1) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_ReplAsyncLoad, replAsyncLoadCallback);

    return REDISMODULE_OK;
}

int SiderModule_OnUnload(SiderModuleCtx *ctx) {
    if (before_str)
        SiderModule_FreeString(ctx, before_str);
    if (after_str)
        SiderModule_FreeString(ctx, after_str);
    if (before_str_temp)
        SiderModule_FreeString(ctx, before_str_temp);
    if (after_str_temp)
        SiderModule_FreeString(ctx, after_str_temp);
    return REDISMODULE_OK;
}
