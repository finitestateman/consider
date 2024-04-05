#include "sidermodule.h"
#include <pthread.h>
#include <assert.h>

#define UNUSED(V) ((void) V)

SiderModuleUser *user = NULL;

int call_without_user(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc < 2) {
        return SiderModule_WrongArity(ctx);
    }

    const char *cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply *rep = SiderModule_Call(ctx, cmd, "Ev", argv + 2, argc - 2);
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }
    return REDISMODULE_OK;
}

int call_with_user_flag(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc < 3) {
        return SiderModule_WrongArity(ctx);
    }

    SiderModule_SetContextUser(ctx, user);

    /* Append Ev to the provided flags. */
    SiderModuleString *flags = SiderModule_CreateStringFromString(ctx, argv[1]);
    SiderModule_StringAppendBuffer(ctx, flags, "Ev", 2);

    const char* flg = SiderModule_StringPtrLen(flags, NULL);
    const char* cmd = SiderModule_StringPtrLen(argv[2], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, flg, argv + 3, argc - 3);
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }
    SiderModule_FreeString(ctx, flags);

    return REDISMODULE_OK;
}

int add_to_acl(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) {
        return SiderModule_WrongArity(ctx);
    }

    size_t acl_len;
    const char *acl = SiderModule_StringPtrLen(argv[1], &acl_len);

    SiderModuleString *error;
    int ret = SiderModule_SetModuleUserACLString(ctx, user, acl, &error);
    if (ret) {
        size_t len;
        const char * e = SiderModule_StringPtrLen(error, &len);
        SiderModule_ReplyWithError(ctx, e);
        return REDISMODULE_OK;
    }

    SiderModule_ReplyWithSimpleString(ctx, "OK");

    return REDISMODULE_OK;
}

int get_acl(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);

    if (argc != 1) {
        return SiderModule_WrongArity(ctx);
    }

    SiderModule_Assert(user != NULL);

    SiderModuleString *acl = SiderModule_GetModuleUserACLString(user);

    SiderModule_ReplyWithString(ctx, acl);

    SiderModule_FreeString(NULL, acl);

    return REDISMODULE_OK;
}

int reset_user(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);

    if (argc != 1) {
        return SiderModule_WrongArity(ctx);
    }

    if (user != NULL) {
        SiderModule_FreeModuleUser(user);
    }

    user = SiderModule_CreateModuleUser("module_user");

    SiderModule_ReplyWithSimpleString(ctx, "OK");

    return REDISMODULE_OK;
}

typedef struct {
    SiderModuleString **argv;
    int argc;
    SiderModuleBlockedClient *bc;
} bg_call_data;

void *bg_call_worker(void *arg) {
    bg_call_data *bg = arg;

    // Get Sider module context
    SiderModuleCtx *ctx = SiderModule_GetThreadSafeContext(bg->bc);

    // Acquire GIL
    SiderModule_ThreadSafeContextLock(ctx);

    // Set user
    SiderModule_SetContextUser(ctx, user);

    // Call the command
    size_t format_len;
    SiderModuleString *format_sider_str = SiderModule_CreateString(NULL, "v", 1);
    const char *format = SiderModule_StringPtrLen(bg->argv[1], &format_len);
    SiderModule_StringAppendBuffer(NULL, format_sider_str, format, format_len);
    SiderModule_StringAppendBuffer(NULL, format_sider_str, "E", 1);
    format = SiderModule_StringPtrLen(format_sider_str, NULL);
    const char *cmd = SiderModule_StringPtrLen(bg->argv[2], NULL);
    SiderModuleCallReply *rep = SiderModule_Call(ctx, cmd, format, bg->argv + 3, bg->argc - 3);
    SiderModule_FreeString(NULL, format_sider_str);

    // Release GIL
    SiderModule_ThreadSafeContextUnlock(ctx);

    // Reply to client
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }

    // Unblock client
    SiderModule_UnblockClient(bg->bc, NULL);

    /* Free the arguments */
    for (int i=0; i<bg->argc; i++)
        SiderModule_FreeString(ctx, bg->argv[i]);
    SiderModule_Free(bg->argv);
    SiderModule_Free(bg);

    // Free the Sider module context
    SiderModule_FreeThreadSafeContext(ctx);

    return NULL;
}

int call_with_user_bg(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    UNUSED(argv);
    UNUSED(argc);

    /* Make sure we're not trying to block a client when we shouldn't */
    int flags = SiderModule_GetContextFlags(ctx);
    int allFlags = SiderModule_GetContextFlagsAll();
    if ((allFlags & REDISMODULE_CTX_FLAGS_MULTI) &&
        (flags & REDISMODULE_CTX_FLAGS_MULTI)) {
        SiderModule_ReplyWithSimpleString(ctx, "Blocked client is not supported inside multi");
        return REDISMODULE_OK;
    }
    if ((allFlags & REDISMODULE_CTX_FLAGS_DENY_BLOCKING) &&
        (flags & REDISMODULE_CTX_FLAGS_DENY_BLOCKING)) {
        SiderModule_ReplyWithSimpleString(ctx, "Blocked client is not allowed");
        return REDISMODULE_OK;
    }

    /* Make a copy of the arguments and pass them to the thread. */
    bg_call_data *bg = SiderModule_Alloc(sizeof(bg_call_data));
    bg->argv = SiderModule_Alloc(sizeof(SiderModuleString*)*argc);
    bg->argc = argc;
    for (int i=0; i<argc; i++)
        bg->argv[i] = SiderModule_HoldString(ctx, argv[i]);

    /* Block the client */
    bg->bc = SiderModule_BlockClient(ctx, NULL, NULL, NULL, 0);

    /* Start a thread to handle the request */
    pthread_t tid;
    int res = pthread_create(&tid, NULL, bg_call_worker, bg);
    assert(res == 0);

    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"usercall",1,REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"usercall.call_without_user", call_without_user,"write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"usercall.call_with_user_flag", call_with_user_flag,"write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "usercall.call_with_user_bg", call_with_user_bg, "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "usercall.add_to_acl", add_to_acl, "write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"usercall.reset_user", reset_user,"write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"usercall.get_acl", get_acl,"write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
