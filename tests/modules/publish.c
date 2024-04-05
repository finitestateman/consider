#include "sidermodule.h"
#include <string.h>
#include <assert.h>
#include <unistd.h>

#define UNUSED(V) ((void) V)

int cmd_publish_classic_multi(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc < 3)
        return SiderModule_WrongArity(ctx);
    SiderModule_ReplyWithArray(ctx, argc-2);
    for (int i = 2; i < argc; i++) {
        int receivers = SiderModule_PublishMessage(ctx, argv[1], argv[i]);
        SiderModule_ReplyWithLongLong(ctx, receivers);
    }
    return REDISMODULE_OK;
}

int cmd_publish_classic(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 3)
        return SiderModule_WrongArity(ctx);
    
    int receivers = SiderModule_PublishMessage(ctx, argv[1], argv[2]);
    SiderModule_ReplyWithLongLong(ctx, receivers);
    return REDISMODULE_OK;
}

int cmd_publish_shard(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc != 3)
        return SiderModule_WrongArity(ctx);
    
    int receivers = SiderModule_PublishMessageShard(ctx, argv[1], argv[2]);
    SiderModule_ReplyWithLongLong(ctx, receivers);
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    
    if (SiderModule_Init(ctx,"publish",1,REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"publish.classic",cmd_publish_classic,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"publish.classic_multi",cmd_publish_classic_multi,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"publish.shard",cmd_publish_shard,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
        
    return REDISMODULE_OK;
}
