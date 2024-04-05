
#include "sidermodule.h"

static void timer_callback(SiderModuleCtx *ctx, void *data)
{
    SiderModuleString *keyname = data;
    SiderModuleCallReply *reply;

    reply = SiderModule_Call(ctx, "INCR", "s", keyname);
    if (reply != NULL)
        SiderModule_FreeCallReply(reply);
    SiderModule_FreeString(ctx, keyname);
}

int test_createtimer(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 3) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    long long period;
    if (SiderModule_StringToLongLong(argv[1], &period) == REDISMODULE_ERR) {
        SiderModule_ReplyWithError(ctx, "Invalid time specified.");
        return REDISMODULE_OK;
    }

    SiderModuleString *keyname = argv[2];
    SiderModule_RetainString(ctx, keyname);

    SiderModuleTimerID id = SiderModule_CreateTimer(ctx, period, timer_callback, keyname);
    SiderModule_ReplyWithLongLong(ctx, id);

    return REDISMODULE_OK;
}

int test_gettimer(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    long long id;
    if (SiderModule_StringToLongLong(argv[1], &id) == REDISMODULE_ERR) {
        SiderModule_ReplyWithError(ctx, "Invalid id specified.");
        return REDISMODULE_OK;
    }

    uint64_t remaining;
    SiderModuleString *keyname;
    if (SiderModule_GetTimerInfo(ctx, id, &remaining, (void **)&keyname) == REDISMODULE_ERR) {
        SiderModule_ReplyWithNull(ctx);
    } else {
        SiderModule_ReplyWithArray(ctx, 2);
        SiderModule_ReplyWithString(ctx, keyname);
        SiderModule_ReplyWithLongLong(ctx, remaining);
    }

    return REDISMODULE_OK;
}

int test_stoptimer(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    long long id;
    if (SiderModule_StringToLongLong(argv[1], &id) == REDISMODULE_ERR) {
        SiderModule_ReplyWithError(ctx, "Invalid id specified.");
        return REDISMODULE_OK;
    }

    int ret = 0;
    SiderModuleString *keyname;
    if (SiderModule_StopTimer(ctx, id, (void **) &keyname) == REDISMODULE_OK) {
        SiderModule_FreeString(ctx, keyname);
        ret = 1;
    }

    SiderModule_ReplyWithLongLong(ctx, ret);
    return REDISMODULE_OK;
}


int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx,"timer",1,REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.createtimer", test_createtimer,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.gettimer", test_gettimer,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.stoptimer", test_stoptimer,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
