#include "sidermodule.h"

#include <string.h>

/* This is a second sample module to validate that module authentication callbacks can be registered
 * from multiple modules. */

/* Non Blocking Module Auth callback / implementation. */
int auth_cb(SiderModuleCtx *ctx, SiderModuleString *username, SiderModuleString *password, SiderModuleString **err) {
    const char *user = SiderModule_StringPtrLen(username, NULL);
    const char *pwd = SiderModule_StringPtrLen(password, NULL);
    if (!strcmp(user,"foo") && !strcmp(pwd,"allow_two")) {
        SiderModule_AuthenticateClientWithACLUser(ctx, "foo", 3, NULL, NULL, NULL);
        return REDISMODULE_AUTH_HANDLED;
    }
    else if (!strcmp(user,"foo") && !strcmp(pwd,"deny_two")) {
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

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx,"moduleauthtwo",1,REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"testmoduletwo.rm_register_auth_cb", test_rm_register_auth_cb,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    return REDISMODULE_OK;
}