#include "sidermodule.h"

#define UNUSED(V) ((void) V)

/* This function implements all commands in this module. All we care about is
 * the COMMAND metadata anyway. */
int kspec_impl(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);

    /* Handle getkeys-api introspection (for "kspec.nonewithgetkeys")  */
    if (SiderModule_IsKeysPositionRequest(ctx)) {
        for (int i = 1; i < argc; i += 2)
            SiderModule_KeyAtPosWithFlags(ctx, i, REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS);

        return REDISMODULE_OK;
    }

    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int createKspecNone(SiderModuleCtx *ctx) {
    /* A command without keyspecs; only the legacy (first,last,step) triple (MSET like spec). */
    if (SiderModule_CreateCommand(ctx,"kspec.none",kspec_impl,"",1,-1,2) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    return REDISMODULE_OK;
}

int createKspecNoneWithGetkeys(SiderModuleCtx *ctx) {
    /* A command without keyspecs; only the legacy (first,last,step) triple (MSET like spec), but also has a getkeys callback */
    if (SiderModule_CreateCommand(ctx,"kspec.nonewithgetkeys",kspec_impl,"getkeys-api",1,-1,2) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    return REDISMODULE_OK;
}

int createKspecTwoRanges(SiderModuleCtx *ctx) {
    /* Test that two position/range-based key specs are combined to produce the
     * legacy (first,last,step) values representing both keys. */
    if (SiderModule_CreateCommand(ctx,"kspec.tworanges",kspec_impl,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModuleCommand *command = SiderModule_GetCommand(ctx,"kspec.tworanges");
    SiderModuleCommandInfo info = {
        .version = REDISMODULE_COMMAND_INFO_VERSION,
        .arity = -2,
        .key_specs = (SiderModuleCommandKeySpec[]){
            {
                .flags = REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = REDISMODULE_CMD_KEY_RW | REDISMODULE_CMD_KEY_UPDATE,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 2,
                /* Omitted find_keys_type is shorthand for RANGE {0,1,0} */
            },
            {0}
        }
    };
    if (SiderModule_SetCommandInfo(command, &info) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}

int createKspecTwoRangesWithGap(SiderModuleCtx *ctx) {
    /* Test that two position/range-based key specs are combined to produce the
     * legacy (first,last,step) values representing just one key. */
    if (SiderModule_CreateCommand(ctx,"kspec.tworangeswithgap",kspec_impl,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModuleCommand *command = SiderModule_GetCommand(ctx,"kspec.tworangeswithgap");
    SiderModuleCommandInfo info = {
        .version = REDISMODULE_COMMAND_INFO_VERSION,
        .arity = -2,
        .key_specs = (SiderModuleCommandKeySpec[]){
            {
                .flags = REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = REDISMODULE_CMD_KEY_RW | REDISMODULE_CMD_KEY_UPDATE,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 3,
                /* Omitted find_keys_type is shorthand for RANGE {0,1,0} */
            },
            {0}
        }
    };
    if (SiderModule_SetCommandInfo(command, &info) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}

int createKspecKeyword(SiderModuleCtx *ctx) {
    /* Only keyword-based specs. The legacy triple is wiped and set to (0,0,0). */
    if (SiderModule_CreateCommand(ctx,"kspec.keyword",kspec_impl,"",3,-1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModuleCommand *command = SiderModule_GetCommand(ctx,"kspec.keyword");
    SiderModuleCommandInfo info = {
        .version = REDISMODULE_COMMAND_INFO_VERSION,
        .key_specs = (SiderModuleCommandKeySpec[]){
            {
                .flags = REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS,
                .begin_search_type = REDISMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "KEYS",
                .bs.keyword.startfrom = 1,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {-1,1,0}
            },
            {0}
        }
    };
    if (SiderModule_SetCommandInfo(command, &info) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}

int createKspecComplex1(SiderModuleCtx *ctx) {
    /* First is a range a single key. The rest are keyword-based specs. */
    if (SiderModule_CreateCommand(ctx,"kspec.complex1",kspec_impl,"",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModuleCommand *command = SiderModule_GetCommand(ctx,"kspec.complex1");
    SiderModuleCommandInfo info = {
        .version = REDISMODULE_COMMAND_INFO_VERSION,
        .key_specs = (SiderModuleCommandKeySpec[]){
            {
                .flags = REDISMODULE_CMD_KEY_RO,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
            },
            {
                .flags = REDISMODULE_CMD_KEY_RW | REDISMODULE_CMD_KEY_UPDATE,
                .begin_search_type = REDISMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "STORE",
                .bs.keyword.startfrom = 2,
            },
            {
                .flags = REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS,
                .begin_search_type = REDISMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "KEYS",
                .bs.keyword.startfrom = 2,
                .find_keys_type = REDISMODULE_KSPEC_FK_KEYNUM,
                .fk.keynum = {0,1,1}
            },
            {0}
        }
    };
    if (SiderModule_SetCommandInfo(command, &info) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}

int createKspecComplex2(SiderModuleCtx *ctx) {
    /* First is not legacy, more than STATIC_KEYS_SPECS_NUM specs */
    if (SiderModule_CreateCommand(ctx,"kspec.complex2",kspec_impl,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModuleCommand *command = SiderModule_GetCommand(ctx,"kspec.complex2");
    SiderModuleCommandInfo info = {
        .version = REDISMODULE_COMMAND_INFO_VERSION,
        .key_specs = (SiderModuleCommandKeySpec[]){
            {
                .flags = REDISMODULE_CMD_KEY_RW | REDISMODULE_CMD_KEY_UPDATE,
                .begin_search_type = REDISMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "STORE",
                .bs.keyword.startfrom = 5,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 1,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = REDISMODULE_CMD_KEY_RO | REDISMODULE_CMD_KEY_ACCESS,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 2,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {0,1,0}
            },
            {
                .flags = REDISMODULE_CMD_KEY_RW | REDISMODULE_CMD_KEY_UPDATE,
                .begin_search_type = REDISMODULE_KSPEC_BS_INDEX,
                .bs.index.pos = 3,
                .find_keys_type = REDISMODULE_KSPEC_FK_KEYNUM,
                .fk.keynum = {0,1,1}
            },
            {
                .flags = REDISMODULE_CMD_KEY_RW | REDISMODULE_CMD_KEY_UPDATE,
                .begin_search_type = REDISMODULE_KSPEC_BS_KEYWORD,
                .bs.keyword.keyword = "MOREKEYS",
                .bs.keyword.startfrom = 5,
                .find_keys_type = REDISMODULE_KSPEC_FK_RANGE,
                .fk.range = {-1,1,0}
            },
            {0}
        }
    };
    if (SiderModule_SetCommandInfo(command, &info) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx, "keyspecs", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (createKspecNone(ctx) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (createKspecNoneWithGetkeys(ctx) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (createKspecTwoRanges(ctx) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (createKspecTwoRangesWithGap(ctx) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (createKspecKeyword(ctx) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (createKspecComplex1(ctx) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (createKspecComplex2(ctx) == REDISMODULE_ERR) return REDISMODULE_ERR;
    return REDISMODULE_OK;
}
