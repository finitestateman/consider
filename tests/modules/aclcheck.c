
#include "sidermodule.h"
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <strings.h>

/* A wrap for SET command with ACL check on the key. */
int set_aclcheck_key(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc < 4) {
        return SiderModule_WrongArity(ctx);
    }

    int permissions;
    const char *flags = SiderModule_StringPtrLen(argv[1], NULL);

    if (!strcasecmp(flags, "W")) {
        permissions = REDISMODULE_CMD_KEY_UPDATE;
    } else if (!strcasecmp(flags, "R")) {
        permissions = REDISMODULE_CMD_KEY_ACCESS;
    } else if (!strcasecmp(flags, "*")) {
        permissions = REDISMODULE_CMD_KEY_UPDATE | REDISMODULE_CMD_KEY_ACCESS;
    } else if (!strcasecmp(flags, "~")) {
        permissions = 0; /* Requires either read or write */
    } else {
        SiderModule_ReplyWithError(ctx, "INVALID FLAGS");
        return REDISMODULE_OK;
    }

    /* Check that the key can be accessed */
    SiderModuleString *user_name = SiderModule_GetCurrentUserName(ctx);
    SiderModuleUser *user = SiderModule_GetModuleUserFromUserName(user_name);
    int ret = SiderModule_ACLCheckKeyPermissions(user, argv[2], permissions);
    if (ret != 0) {
        SiderModule_ReplyWithError(ctx, "DENIED KEY");
        SiderModule_FreeModuleUser(user);
        SiderModule_FreeString(ctx, user_name);
        return REDISMODULE_OK;
    }

    SiderModuleCallReply *rep = SiderModule_Call(ctx, "SET", "v", argv + 2, argc - 2);
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }

    SiderModule_FreeModuleUser(user);
    SiderModule_FreeString(ctx, user_name);
    return REDISMODULE_OK;
}

/* A wrap for PUBLISH command with ACL check on the channel. */
int publish_aclcheck_channel(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) {
        return SiderModule_WrongArity(ctx);
    }

    /* Check that the pubsub channel can be accessed */
    SiderModuleString *user_name = SiderModule_GetCurrentUserName(ctx);
    SiderModuleUser *user = SiderModule_GetModuleUserFromUserName(user_name);
    int ret = SiderModule_ACLCheckChannelPermissions(user, argv[1], REDISMODULE_CMD_CHANNEL_SUBSCRIBE);
    if (ret != 0) {
        SiderModule_ReplyWithError(ctx, "DENIED CHANNEL");
        SiderModule_FreeModuleUser(user);
        SiderModule_FreeString(ctx, user_name);
        return REDISMODULE_OK;
    }

    SiderModuleCallReply *rep = SiderModule_Call(ctx, "PUBLISH", "v", argv + 1, argc - 1);
    if (!rep) {
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }

    SiderModule_FreeModuleUser(user);
    SiderModule_FreeString(ctx, user_name);
    return REDISMODULE_OK;
}

/* A wrap for RM_Call that check first that the command can be executed */
int rm_call_aclcheck_cmd(SiderModuleCtx *ctx, SiderModuleUser *user, SiderModuleString **argv, int argc) {
    if (argc < 2) {
        return SiderModule_WrongArity(ctx);
    }

    /* Check that the command can be executed */
    int ret = SiderModule_ACLCheckCommandPermissions(user, argv + 1, argc - 1);
    if (ret != 0) {
        SiderModule_ReplyWithError(ctx, "DENIED CMD");
        /* Add entry to ACL log */
        SiderModule_ACLAddLogEntry(ctx, user, argv[1], REDISMODULE_ACL_LOG_CMD);
        return REDISMODULE_OK;
    }

    const char* cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, "v", argv + 2, argc - 2);
    if(!rep){
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    }else{
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }

    return REDISMODULE_OK;
}

int rm_call_aclcheck_cmd_default_user(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModuleString *user_name = SiderModule_GetCurrentUserName(ctx);
    SiderModuleUser *user = SiderModule_GetModuleUserFromUserName(user_name);

    int res = rm_call_aclcheck_cmd(ctx, user, argv, argc);

    SiderModule_FreeModuleUser(user);
    SiderModule_FreeString(ctx, user_name);
    return res;
}

int rm_call_aclcheck_cmd_module_user(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    /* Create a user and authenticate */
    SiderModuleUser *user = SiderModule_CreateModuleUser("testuser1");
    SiderModule_SetModuleUserACL(user, "allcommands");
    SiderModule_SetModuleUserACL(user, "allkeys");
    SiderModule_SetModuleUserACL(user, "on");
    SiderModule_AuthenticateClientWithUser(ctx, user, NULL, NULL, NULL);

    int res = rm_call_aclcheck_cmd(ctx, user, argv, argc);

    /* authenticated back to "default" user (so once we free testuser1 we will not disconnected */
    SiderModule_AuthenticateClientWithACLUser(ctx, "default", 7, NULL, NULL, NULL);
    SiderModule_FreeModuleUser(user);
    return res;
}

int rm_call_aclcheck_with_errors(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if(argc < 2){
        return SiderModule_WrongArity(ctx);
    }

    const char* cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, "vEC", argv + 2, argc - 2);
    SiderModule_ReplyWithCallReply(ctx, rep);
    SiderModule_FreeCallReply(rep);
    return REDISMODULE_OK;
}

/* A wrap for RM_Call that pass the 'C' flag to do ACL check on the command. */
int rm_call_aclcheck(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if(argc < 2){
        return SiderModule_WrongArity(ctx);
    }

    const char* cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, "vC", argv + 2, argc - 2);
    if(!rep) {
        char err[100];
        switch (errno) {
            case EACCES:
                SiderModule_ReplyWithError(ctx, "ERR NOPERM");
                break;
            default:
                snprintf(err, sizeof(err) - 1, "ERR errno=%d", errno);
                SiderModule_ReplyWithError(ctx, err);
                break;
        }
    } else {
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }

    return REDISMODULE_OK;
}

int module_test_acl_category(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int commandBlockCheck(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    int response_ok = 0;
    int result = SiderModule_CreateCommand(ctx,"command.that.should.fail", module_test_acl_category, "", 0, 0, 0);
    response_ok |= (result == REDISMODULE_OK);

    SiderModuleCommand *parent = SiderModule_GetCommand(ctx,"block.commands.outside.onload");
    result = SiderModule_SetCommandACLCategories(parent, "write");
    response_ok |= (result == REDISMODULE_OK);

    result = SiderModule_CreateSubcommand(parent,"subcommand.that.should.fail",module_test_acl_category,"",0,0,0);
    response_ok |= (result == REDISMODULE_OK);
    
    /* This validates that it's not possible to create commands outside OnLoad,
     * thus returns an error if they succeed. */
    if (response_ok) {
        SiderModule_ReplyWithError(ctx, "UNEXPECTEDOK");
    } else {
        SiderModule_ReplyWithSimpleString(ctx, "OK");
    }
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"aclcheck",1,REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.set.check.key", set_aclcheck_key,"write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"block.commands.outside.onload", commandBlockCheck,"write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.module.command.aclcategories.write", module_test_acl_category,"write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    SiderModuleCommand *aclcategories_write = SiderModule_GetCommand(ctx,"aclcheck.module.command.aclcategories.write");

    if (SiderModule_SetCommandACLCategories(aclcategories_write, "write") == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.module.command.aclcategories.write.function.read.category", module_test_acl_category,"write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    SiderModuleCommand *read_category = SiderModule_GetCommand(ctx,"aclcheck.module.command.aclcategories.write.function.read.category");

    if (SiderModule_SetCommandACLCategories(read_category, "read") == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.module.command.aclcategories.read.only.category", module_test_acl_category,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    SiderModuleCommand *read_only_category = SiderModule_GetCommand(ctx,"aclcheck.module.command.aclcategories.read.only.category");

    if (SiderModule_SetCommandACLCategories(read_only_category, "read") == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.publish.check.channel", publish_aclcheck_channel,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.rm_call.check.cmd", rm_call_aclcheck_cmd_default_user,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.rm_call.check.cmd.module.user", rm_call_aclcheck_cmd_module_user,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.rm_call", rm_call_aclcheck,
                                  "write",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"aclcheck.rm_call_with_errors", rm_call_aclcheck_with_errors,
                                      "write",0,0,0) == REDISMODULE_ERR)
            return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
