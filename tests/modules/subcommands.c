#include "sidermodule.h"

#define UNUSED(V) ((void) V)

int cmd_set(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int cmd_get(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);

    if (argc > 4) /* For testing */
        return SiderModule_WrongArity(ctx);

    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int cmd_get_fullname(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    const char *command_name = SiderModule_GetCurrentCommandName(ctx);
    SiderModule_ReplyWithSimpleString(ctx, command_name);
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx, "subcommands", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Module command names cannot contain special characters. */
    SiderModule_Assert(SiderModule_CreateCommand(ctx,"subcommands.char\r",NULL,"",0,0,0) == REDISMODULE_ERR);
    SiderModule_Assert(SiderModule_CreateCommand(ctx,"subcommands.char\n",NULL,"",0,0,0) == REDISMODULE_ERR);
    SiderModule_Assert(SiderModule_CreateCommand(ctx,"subcommands.char ",NULL,"",0,0,0) == REDISMODULE_ERR);

    if (SiderModule_CreateCommand(ctx,"subcommands.bitarray",NULL,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    SiderModuleCommand *parent = SiderModule_GetCommand(ctx,"subcommands.bitarray");

    if (SiderModule_CreateSubcommand(parent,"set",cmd_set,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Module subcommand names cannot contain special characters. */
    SiderModule_Assert(SiderModule_CreateSubcommand(parent,"char|",cmd_set,"",0,0,0) == REDISMODULE_ERR);
    SiderModule_Assert(SiderModule_CreateSubcommand(parent,"char@",cmd_set,"",0,0,0) == REDISMODULE_ERR);
    SiderModule_Assert(SiderModule_CreateSubcommand(parent,"char=",cmd_set,"",0,0,0) == REDISMODULE_ERR);

    SiderModuleCommand *subcmd = SiderModule_GetCommand(ctx,"subcommands.bitarray|set");
    SiderModuleCommandInfo cmd_set_info = {
        .version = REDISMODULE_COMMAND_INFO_VERSION,
        .key_specs = (SiderModuleCommandKeySpec[]){
            {
                .flags = REDISMODULE_CMD_KEY_RW | REDISMODULE_CMD_KEY_UPDATE,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {0}
        }
    };
    if (SiderModule_SetCommandInfo(subcmd, &cmd_set_info) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateSubcommand(parent,"get",cmd_get,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    subcmd = SiderModule_GetCommand(ctx,"subcommands.bitarray|get");
    SiderModuleCommandInfo cmd_get_info = {
        .version = REDISMODULE_COMMAND_INFO_VERSION,
        .key_specs = (SiderModuleCommandKeySpec[]){
            {
                .flags = REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {0}
        }
    };
    if (SiderModule_SetCommandInfo(subcmd, &cmd_get_info) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Get the name of the command currently running. */
    if (SiderModule_CreateCommand(ctx,"subcommands.parent_get_fullname",cmd_get_fullname,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Get the name of the subcommand currently running. */
    if (SiderModule_CreateCommand(ctx,"subcommands.sub",NULL,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModuleCommand *fullname_parent = SiderModule_GetCommand(ctx,"subcommands.sub");
    if (SiderModule_CreateSubcommand(fullname_parent,"get_fullname",cmd_get_fullname,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Sanity */

    /* Trying to create the same subcommand fails */
    SiderModule_Assert(SiderModule_CreateSubcommand(parent,"get",NULL,"",0,0,0) == REDISMODULE_ERR);

    /* Trying to create a sub-subcommand fails */
    SiderModule_Assert(SiderModule_CreateSubcommand(subcmd,"get",NULL,"",0,0,0) == REDISMODULE_ERR);

    return REDISMODULE_OK;
}
