#include "sidermodule.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <errno.h>

/* Sanity tests to verify inputs and return values. */
int sanity(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleRdbStream *s = SiderModule_RdbStreamCreateFromFile("dbnew.rdb");

    /* NULL stream should fail. */
    if (SiderModule_RdbLoad(ctx, NULL, 0) == REDISMODULE_OK || errno != EINVAL) {
        SiderModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    /* Invalid flags should fail. */
    if (SiderModule_RdbLoad(ctx, s, 188) == REDISMODULE_OK || errno != EINVAL) {
        SiderModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    /* Missing file should fail. */
    if (SiderModule_RdbLoad(ctx, s, 0) == REDISMODULE_OK || errno != ENOENT) {
        SiderModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    /* Save RDB file. */
    if (SiderModule_RdbSave(ctx, s, 0) != REDISMODULE_OK || errno != 0) {
        SiderModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    /* Load the saved RDB file. */
    if (SiderModule_RdbLoad(ctx, s, 0) != REDISMODULE_OK || errno != 0) {
        SiderModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    SiderModule_ReplyWithSimpleString(ctx, "OK");

 out:
    SiderModule_RdbStreamFree(s);
    return REDISMODULE_OK;
}

int cmd_rdbsave(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    size_t len;
    const char *filename = SiderModule_StringPtrLen(argv[1], &len);

    char tmp[len + 1];
    memcpy(tmp, filename, len);
    tmp[len] = '\0';

    SiderModuleRdbStream *stream = SiderModule_RdbStreamCreateFromFile(tmp);

    if (SiderModule_RdbSave(ctx, stream, 0) != REDISMODULE_OK || errno != 0) {
        SiderModule_ReplyWithError(ctx, strerror(errno));
        goto out;
    }

    SiderModule_ReplyWithSimpleString(ctx, "OK");

out:
    SiderModule_RdbStreamFree(stream);
    return REDISMODULE_OK;
}

/* Fork before calling RM_RdbSave(). */
int cmd_rdbsave_fork(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    size_t len;
    const char *filename = SiderModule_StringPtrLen(argv[1], &len);

    char tmp[len + 1];
    memcpy(tmp, filename, len);
    tmp[len] = '\0';

    int fork_child_pid = SiderModule_Fork(NULL, NULL);
    if (fork_child_pid < 0) {
        SiderModule_ReplyWithError(ctx, strerror(errno));
        return REDISMODULE_OK;
    } else if (fork_child_pid > 0) {
        /* parent */
        SiderModule_ReplyWithSimpleString(ctx, "OK");
        return REDISMODULE_OK;
    }

    SiderModuleRdbStream *stream = SiderModule_RdbStreamCreateFromFile(tmp);

    int ret = 0;
    if (SiderModule_RdbSave(ctx, stream, 0) != REDISMODULE_OK) {
        ret = errno;
    }
    SiderModule_RdbStreamFree(stream);

    SiderModule_ExitFromChild(ret);
    return REDISMODULE_OK;
}

int cmd_rdbload(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    size_t len;
    const char *filename = SiderModule_StringPtrLen(argv[1], &len);

    char tmp[len + 1];
    memcpy(tmp, filename, len);
    tmp[len] = '\0';

    SiderModuleRdbStream *stream = SiderModule_RdbStreamCreateFromFile(tmp);

    if (SiderModule_RdbLoad(ctx, stream, 0) != REDISMODULE_OK || errno != 0) {
        SiderModule_RdbStreamFree(stream);
        SiderModule_ReplyWithError(ctx, strerror(errno));
        return REDISMODULE_OK;
    }

    SiderModule_RdbStreamFree(stream);
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx, "rdbloadsave", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "test.sanity", sanity, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "test.rdbsave", cmd_rdbsave, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "test.rdbsave_fork", cmd_rdbsave_fork, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "test.rdbload", cmd_rdbload, "", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
