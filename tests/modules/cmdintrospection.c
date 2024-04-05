#include "sidermodule.h"

#define UNUSED(V) ((void) V)

int cmd_xadd(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx, "cmdintrospection", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"cmdintrospection.xadd",cmd_xadd,"write deny-oom random fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModuleCommand *xadd = SiderModule_GetCommand(ctx,"cmdintrospection.xadd");

    SiderModuleCommandInfo info = {
        .version = REDISMODULE_COMMAND_INFO_VERSION,
        .arity = -5,
        .summary = "Appends a new message to a stream. Creates the key if it doesn't exist.",
        .since = "5.0.0",
        .complexity = "O(1) when adding a new entry, O(N) when trimming where N being the number of entries evicted.",
        .tips = "nondeterministic_output",
        .history = (SiderModuleCommandHistoryEntry[]){
            /* NOTE: All versions specified should be the module's versions, not
             * Sider'! We use Sider versions in this example for the purpose of
             * testing (comparing the output with the output of the vanilla
             * XADD). */
            {"6.2.0", "Added the `NOMKSTREAM` option, `MINID` trimming strategy and the `LIMIT` option."},
            {"7.0.0", "Added support for the `<ms>-*` explicit ID form."},
            {0}
        },
        .key_specs = (SiderModuleCommandKeySpec[]){
            {
                .notes = "UPDATE instead of INSERT because of the optional trimming feature",
                .flags = REDISMODULE_CMD_KEY_RW | REDISMODULE_CMD_KEY_UPDATE,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {0}
        },
        .args = (SiderModuleCommandArg[]){
            {
                .name = "key",
                .type = REDISMODULE_ARG_TYPE_KEY,
                .key_spec_index = 0
            },
            {
                .name = "nomkstream",
                .type = REDISMODULE_ARG_TYPE_PURE_TOKEN,
                .token = "NOMKSTREAM",
                .since = "6.2.0",
                .flags = REDISMODULE_CMD_ARG_OPTIONAL
            },
            {
                .name = "trim",
                .type = REDISMODULE_ARG_TYPE_BLOCK,
                .flags = REDISMODULE_CMD_ARG_OPTIONAL,
                .subargs = (SiderModuleCommandArg[]){
                    {
                        .name = "strategy",
                        .type = REDISMODULE_ARG_TYPE_ONEOF,
                        .subargs = (SiderModuleCommandArg[]){
                            {
                                .name = "maxlen",
                                .type = REDISMODULE_ARG_TYPE_PURE_TOKEN,
                                .token = "MAXLEN",
                            },
                            {
                                .name = "minid",
                                .type = REDISMODULE_ARG_TYPE_PURE_TOKEN,
                                .token = "MINID",
                                .since = "6.2.0",
                            },
                            {0}
                        }
                    },
                    {
                        .name = "operator",
                        .type = REDISMODULE_ARG_TYPE_ONEOF,
                        .flags = REDISMODULE_CMD_ARG_OPTIONAL,
                        .subargs = (SiderModuleCommandArg[]){
                            {
                                .name = "equal",
                                .type = REDISMODULE_ARG_TYPE_PURE_TOKEN,
                                .token = "="
                            },
                            {
                                .name = "approximately",
                                .type = REDISMODULE_ARG_TYPE_PURE_TOKEN,
                                .token = "~"
                            },
                            {0}
                        }
                    },
                    {
                        .name = "threshold",
                        .type = REDISMODULE_ARG_TYPE_STRING,
                        .display_text = "threshold" /* Just for coverage, doesn't have a visible effect */
                    },
                    {
                        .name = "count",
                        .type = REDISMODULE_ARG_TYPE_INTEGER,
                        .token = "LIMIT",
                        .since = "6.2.0",
                        .flags = REDISMODULE_CMD_ARG_OPTIONAL
                    },
                    {0}
                }
            },
            {
                .name = "id-selector",
                .type = REDISMODULE_ARG_TYPE_ONEOF,
                .subargs = (SiderModuleCommandArg[]){
                    {
                        .name = "auto-id",
                        .type = REDISMODULE_ARG_TYPE_PURE_TOKEN,
                        .token = "*"
                    },
                    {
                        .name = "id",
                        .type = REDISMODULE_ARG_TYPE_STRING,
                    },
                    {0}
                }
            },
            {
                .name = "data",
                .type = REDISMODULE_ARG_TYPE_BLOCK,
                .flags = REDISMODULE_CMD_ARG_MULTIPLE,
                .subargs = (SiderModuleCommandArg[]){
                    {
                        .name = "field",
                        .type = REDISMODULE_ARG_TYPE_STRING,
                    },
                    {
                        .name = "value",
                        .type = REDISMODULE_ARG_TYPE_STRING,
                    },
                    {0}
                }
            },
            {0}
        }
    };
    if (SiderModule_SetCommandInfo(xadd, &info) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
