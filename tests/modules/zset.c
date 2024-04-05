#include "sidermodule.h"
#include <math.h>
#include <errno.h>

/* ZSET.REM key element
 *
 * Removes an occurrence of an element from a sorted set. Replies with the
 * number of removed elements (0 or 1).
 */
int zset_rem(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) return SiderModule_WrongArity(ctx);
    SiderModule_AutoMemory(ctx);
    int keymode = REDISMODULE_READ | REDISMODULE_WRITE;
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], keymode);
    int deleted;
    if (SiderModule_ZsetRem(key, argv[2], &deleted) == REDISMODULE_OK)
        return SiderModule_ReplyWithLongLong(ctx, deleted);
    else
        return SiderModule_ReplyWithError(ctx, "ERR ZsetRem failed");
}

/* ZSET.ADD key score member
 *
 * Adds a specified member with the specified score to the sorted
 * set stored at key.
 */
int zset_add(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 4) return SiderModule_WrongArity(ctx);
    SiderModule_AutoMemory(ctx);
    int keymode = REDISMODULE_READ | REDISMODULE_WRITE;
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], keymode);

    size_t len;
    double score;
    char *endptr;
    const char *str = SiderModule_StringPtrLen(argv[2], &len);
    score = strtod(str, &endptr);
    if (*endptr != '\0' || errno == ERANGE)
        return SiderModule_ReplyWithError(ctx, "value is not a valid float");

    if (SiderModule_ZsetAdd(key, score, argv[3], NULL) == REDISMODULE_OK)
        return SiderModule_ReplyWithSimpleString(ctx, "OK");
    else
        return SiderModule_ReplyWithError(ctx, "ERR ZsetAdd failed");
}

/* ZSET.INCRBY key member increment
 *
 * Increments the score stored at member in the sorted set stored at key by increment.
 * Replies with the new score of this element.
 */
int zset_incrby(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 4) return SiderModule_WrongArity(ctx);
    SiderModule_AutoMemory(ctx);
    int keymode = REDISMODULE_READ | REDISMODULE_WRITE;
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], keymode);

    size_t len;
    double score, newscore;
    char *endptr;
    const char *str = SiderModule_StringPtrLen(argv[3], &len);
    score = strtod(str, &endptr);
    if (*endptr != '\0' || errno == ERANGE)
        return SiderModule_ReplyWithError(ctx, "value is not a valid float");

    if (SiderModule_ZsetIncrby(key, score, argv[2], NULL, &newscore) == REDISMODULE_OK)
        return SiderModule_ReplyWithDouble(ctx, newscore);
    else
        return SiderModule_ReplyWithError(ctx, "ERR ZsetIncrby failed");
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx, "zset", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "zset.rem", zset_rem, "write",
                                  1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "zset.add", zset_add, "write",
                                  1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "zset.incrby", zset_incrby, "write",
                                  1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
