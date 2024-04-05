/* define macros for having usleep */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include "sidermodule.h"

#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define UNUSED(V) ((void) V)

// A simple global user
static SiderModuleUser *global = NULL;
static long long client_change_delta = 0;

void UserChangedCallback(uint64_t client_id, void *privdata) {
    REDISMODULE_NOT_USED(privdata);
    REDISMODULE_NOT_USED(client_id);
    client_change_delta++;
}

int Auth_CreateModuleUser(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (global) {
        SiderModule_FreeModuleUser(global);
    }

    global = SiderModule_CreateModuleUser("global");
    SiderModule_SetModuleUserACL(global, "allcommands");
    SiderModule_SetModuleUserACL(global, "allkeys");
    SiderModule_SetModuleUserACL(global, "on");

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

int Auth_AuthModuleUser(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    uint64_t client_id;
    SiderModule_AuthenticateClientWithUser(ctx, global, UserChangedCallback, NULL, &client_id);

    return SiderModule_ReplyWithLongLong(ctx, (uint64_t) client_id);
}

int Auth_AuthRealUser(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    size_t length;
    uint64_t client_id;

    SiderModuleString *user_string = argv[1];
    const char *name = SiderModule_StringPtrLen(user_string, &length);

    if (SiderModule_AuthenticateClientWithACLUser(ctx, name, length, 
            UserChangedCallback, NULL, &client_id) == REDISMODULE_ERR) {
        return SiderModule_ReplyWithError(ctx, "Invalid user");   
    }

    return SiderModule_ReplyWithLongLong(ctx, (uint64_t) client_id);
}

/* This command redacts every other arguments and returns OK */
int Auth_RedactedAPI(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    for(int i = argc - 1; i > 0; i -= 2) {
        int result = SiderModule_RedactClientCommandArgument(ctx, i);
        SiderModule_Assert(result == REDISMODULE_OK);
    }
    return SiderModule_ReplyWithSimpleString(ctx, "OK"); 
}

int Auth_ChangeCount(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    long long result = client_change_delta;
    client_change_delta = 0;
    return SiderModule_ReplyWithLongLong(ctx, result);
}

/* The Module functionality below validates that module authentication callbacks can be registered
 * to support both non-blocking and blocking module based authentication. */

/* Non Blocking Module Auth callback / implementation. */
int auth_cb(SiderModuleCtx *ctx, SiderModuleString *username, SiderModuleString *password, SiderModuleString **err) {
    const char *user = SiderModule_StringPtrLen(username, NULL);
    const char *pwd = SiderModule_StringPtrLen(password, NULL);
    if (!strcmp(user,"foo") && !strcmp(pwd,"allow")) {
        SiderModule_AuthenticateClientWithACLUser(ctx, "foo", 3, NULL, NULL, NULL);
        return REDISMODULE_AUTH_HANDLED;
    }
    else if (!strcmp(user,"foo") && !strcmp(pwd,"deny")) {
        SiderModuleString *log = SiderModule_CreateString(ctx, "Module Auth", 11);
        SiderModule_ACLAddLogEntryByUserName(ctx, username, log, REDISMODULE_ACL_LOG_AUTH);
        SiderModule_FreeString(ctx, log);
        const char *err_msg = "Auth denied by Misc Module.";
        *err = SiderModule_CreateString(ctx, err_msg, strlen(err_msg));
        return REDISMODULE_AUTH_HANDLED;
    }
    return REDISMODULE_AUTH_NOT_HANDLED;
}

int test_rm_register_auth_cb(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModule_RegisterAuthCallback(ctx, auth_cb);
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

/*
 * The thread entry point that actually executes the blocking part of the AUTH command.
 * This function sleeps for 0.5 seconds and then unblocks the client which will later call
 * `AuthBlock_Reply`.
 * `arg` is expected to contain the SiderModuleBlockedClient, username, and password.
 */
void *AuthBlock_ThreadMain(void *arg) {
    usleep(500000);
    void **targ = arg;
    SiderModuleBlockedClient *bc = targ[0];
    int result = 2;
    const char *user = SiderModule_StringPtrLen(targ[1], NULL);
    const char *pwd = SiderModule_StringPtrLen(targ[2], NULL);
    if (!strcmp(user,"foo") && !strcmp(pwd,"block_allow")) {
        result = 1;
    }
    else if (!strcmp(user,"foo") && !strcmp(pwd,"block_deny")) {
        result = 0;
    }
    else if (!strcmp(user,"foo") && !strcmp(pwd,"block_abort")) {
        SiderModule_BlockedClientMeasureTimeEnd(bc);
        SiderModule_AbortBlock(bc);
        goto cleanup;
    }
    /* Provide the result to the blocking reply cb. */
    void **replyarg = SiderModule_Alloc(sizeof(void*));
    replyarg[0] = (void *) (uintptr_t) result;
    SiderModule_BlockedClientMeasureTimeEnd(bc);
    SiderModule_UnblockClient(bc, replyarg);
cleanup:
    /* Free the username and password and thread / arg data. */
    SiderModule_FreeString(NULL, targ[1]);
    SiderModule_FreeString(NULL, targ[2]);
    SiderModule_Free(targ);
    return NULL;
}

/*
 * Reply callback for a blocking AUTH command. This is called when the client is unblocked.
 */
int AuthBlock_Reply(SiderModuleCtx *ctx, SiderModuleString *username, SiderModuleString *password, SiderModuleString **err) {
    REDISMODULE_NOT_USED(password);
    void **targ = SiderModule_GetBlockedClientPrivateData(ctx);
    int result = (uintptr_t) targ[0];
    size_t userlen = 0;
    const char *user = SiderModule_StringPtrLen(username, &userlen);
    /* Handle the success case by authenticating. */
    if (result == 1) {
        SiderModule_AuthenticateClientWithACLUser(ctx, user, userlen, NULL, NULL, NULL);
        return REDISMODULE_AUTH_HANDLED;
    }
    /* Handle the Error case by denying auth */
    else if (result == 0) {
        SiderModuleString *log = SiderModule_CreateString(ctx, "Module Auth", 11);
        SiderModule_ACLAddLogEntryByUserName(ctx, username, log, REDISMODULE_ACL_LOG_AUTH);
        SiderModule_FreeString(ctx, log);
        const char *err_msg = "Auth denied by Misc Module.";
        *err = SiderModule_CreateString(ctx, err_msg, strlen(err_msg));
        return REDISMODULE_AUTH_HANDLED;
    }
    /* "Skip" Authentication */
    return REDISMODULE_AUTH_NOT_HANDLED;
}

/* Private data freeing callback for Module Auth. */
void AuthBlock_FreeData(SiderModuleCtx *ctx, void *privdata) {
    REDISMODULE_NOT_USED(ctx);
    SiderModule_Free(privdata);
}

/* Callback triggered when the engine attempts module auth
 * Return code here is one of the following: Auth succeeded, Auth denied,
 * Auth not handled, Auth blocked.
 * The Module can have auth succeed / denied here itself, but this is an example
 * of blocking module auth.
 */
int blocking_auth_cb(SiderModuleCtx *ctx, SiderModuleString *username, SiderModuleString *password, SiderModuleString **err) {
    REDISMODULE_NOT_USED(username);
    REDISMODULE_NOT_USED(password);
    REDISMODULE_NOT_USED(err);
    /* Block the client from the Module. */
    SiderModuleBlockedClient *bc = SiderModule_BlockClientOnAuth(ctx, AuthBlock_Reply, AuthBlock_FreeData);
    int ctx_flags = SiderModule_GetContextFlags(ctx);
    if (ctx_flags & REDISMODULE_CTX_FLAGS_MULTI || ctx_flags & REDISMODULE_CTX_FLAGS_LUA) {
        /* Clean up by using SiderModule_UnblockClient since we attempted blocking the client. */
        SiderModule_UnblockClient(bc, NULL);
        return REDISMODULE_AUTH_HANDLED;
    }
    SiderModule_BlockedClientMeasureTimeStart(bc);
    pthread_t tid;
    /* Allocate memory for information needed. */
    void **targ = SiderModule_Alloc(sizeof(void*)*3);
    targ[0] = bc;
    targ[1] = SiderModule_CreateStringFromString(NULL, username);
    targ[2] = SiderModule_CreateStringFromString(NULL, password);
    /* Create bg thread and pass the blockedclient, username and password to it. */
    if (pthread_create(&tid, NULL, AuthBlock_ThreadMain, targ) != 0) {
        SiderModule_AbortBlock(bc);
    }
    return REDISMODULE_AUTH_HANDLED;
}

int test_rm_register_blocking_auth_cb(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModule_RegisterAuthCallback(ctx, blocking_auth_cb);
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"testacl",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"auth.authrealuser",
        Auth_AuthRealUser,"no-auth",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"auth.createmoduleuser",
        Auth_CreateModuleUser,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"auth.authmoduleuser",
        Auth_AuthModuleUser,"no-auth",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"auth.changecount",
        Auth_ChangeCount,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"auth.redact",
        Auth_RedactedAPI,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testmoduleone.rm_register_auth_cb",
        test_rm_register_auth_cb,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"testmoduleone.rm_register_blocking_auth_cb",
        test_rm_register_blocking_auth_cb,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}

int SiderModule_OnUnload(SiderModuleCtx *ctx) {
    UNUSED(ctx);

    if (global)
        SiderModule_FreeModuleUser(global);

    return REDISMODULE_OK;
}
