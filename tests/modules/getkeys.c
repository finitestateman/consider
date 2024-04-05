
#include "sidermodule.h"
#include <strings.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#define UNUSED(V) ((void) V)

/* A sample movable keys command that returns a list of all
 * arguments that follow a KEY argument, i.e.
 */
int getkeys_command(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    int i;
    int count = 0;

    /* Handle getkeys-api introspection */
    if (SiderModule_IsKeysPositionRequest(ctx)) {
        for (i = 0; i < argc; i++) {
            size_t len;
            const char *str = SiderModule_StringPtrLen(argv[i], &len);

            if (len == 3 && !strncasecmp(str, "key", 3) && i + 1 < argc)
                SiderModule_KeyAtPos(ctx, i + 1);
        }

        return REDISMODULE_OK;
    }

    /* Handle real command invocation */
    SiderModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_LEN);
    for (i = 0; i < argc; i++) {
        size_t len;
        const char *str = SiderModule_StringPtrLen(argv[i], &len);

        if (len == 3 && !strncasecmp(str, "key", 3) && i + 1 < argc) {
            SiderModule_ReplyWithString(ctx, argv[i+1]);
            count++;
        }
    }
    SiderModule_ReplySetArrayLength(ctx, count);

    return REDISMODULE_OK;
}

int getkeys_command_with_flags(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    int i;
    int count = 0;

    /* Handle getkeys-api introspection */
    if (SiderModule_IsKeysPositionRequest(ctx)) {
        for (i = 0; i < argc; i++) {
            size_t len;
            const char *str = SiderModule_StringPtrLen(argv[i], &len);

            if (len == 3 && !strncasecmp(str, "key", 3) && i + 1 < argc)
                SiderModule_KeyAtPosWithFlags(ctx, i + 1, REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS);
        }

        return REDISMODULE_OK;
    }

    /* Handle real command invocation */
    SiderModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_LEN);
    for (i = 0; i < argc; i++) {
        size_t len;
        const char *str = SiderModule_StringPtrLen(argv[i], &len);

        if (len == 3 && !strncasecmp(str, "key", 3) && i + 1 < argc) {
            SiderModule_ReplyWithString(ctx, argv[i+1]);
            count++;
        }
    }
    SiderModule_ReplySetArrayLength(ctx, count);

    return REDISMODULE_OK;
}

int getkeys_fixed(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    int i;

    SiderModule_ReplyWithArray(ctx, argc - 1);
    for (i = 1; i < argc; i++) {
        SiderModule_ReplyWithString(ctx, argv[i]);
    }
    return REDISMODULE_OK;
}

/* Introspect a command using RM_GetCommandKeys() and returns the list
 * of keys. Essentially this is COMMAND GETKEYS implemented in a module.
 * INTROSPECT <with-flags> <cmd> <args>
 */
int getkeys_introspect(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    long long with_flags = 0;

    if (argc < 4) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    if (SiderModule_StringToLongLong(argv[1],&with_flags) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx,"ERR invalid integer");

    int num_keys, *keyflags = NULL;
    int *keyidx = SiderModule_GetCommandKeysWithFlags(ctx, &argv[2], argc - 2, &num_keys, with_flags ? &keyflags : NULL);

    if (!keyidx) {
        if (!errno)
            SiderModule_ReplyWithEmptyArray(ctx);
        else {
            char err[100];
            switch (errno) {
                case ENOENT:
                    SiderModule_ReplyWithError(ctx, "ERR ENOENT");
                    break;
                case EINVAL:
                    SiderModule_ReplyWithError(ctx, "ERR EINVAL");
                    break;
                default:
                    snprintf(err, sizeof(err) - 1, "ERR errno=%d", errno);
                    SiderModule_ReplyWithError(ctx, err);
                    break;
            }
        }
    } else {
        int i;

        SiderModule_ReplyWithArray(ctx, num_keys);
        for (i = 0; i < num_keys; i++) {
            if (!with_flags) {
                SiderModule_ReplyWithString(ctx, argv[2 + keyidx[i]]);
                continue;
            }
            SiderModule_ReplyWithArray(ctx, 2);
            SiderModule_ReplyWithString(ctx, argv[2 + keyidx[i]]);
            char* sflags = "";
            if (keyflags[i] & REDISMODULE_CMD_KEY_RO)
                sflags = "RO";
            else if (keyflags[i] & REDISMODULE_CMD_KEY_RW)
                sflags = "RW";
            else if (keyflags[i] & REDISMODULE_CMD_KEY_OW)
                sflags = "OW";
            else if (keyflags[i] & REDISMODULE_CMD_KEY_RM)
                sflags = "RM";
            SiderModule_ReplyWithCString(ctx, sflags);
        }

        SiderModule_Free(keyidx);
        SiderModule_Free(keyflags);
    }

    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    if (SiderModule_Init(ctx,"getkeys",1,REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"getkeys.command", getkeys_command,"getkeys-api",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"getkeys.command_with_flags", getkeys_command_with_flags,"getkeys-api",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"getkeys.fixed", getkeys_fixed,"",2,4,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"getkeys.introspect", getkeys_introspect,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
