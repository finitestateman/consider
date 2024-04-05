#include "sidermodule.h"

#include <string.h>

void InfoFunc(SiderModuleInfoCtx *ctx, int for_crash_report) {
    SiderModule_InfoAddSection(ctx, "");
    SiderModule_InfoAddFieldLongLong(ctx, "global", -2);
    SiderModule_InfoAddFieldULongLong(ctx, "uglobal", (unsigned long long)-2);

    SiderModule_InfoAddSection(ctx, "Spanish");
    SiderModule_InfoAddFieldCString(ctx, "uno", "one");
    SiderModule_InfoAddFieldLongLong(ctx, "dos", 2);

    SiderModule_InfoAddSection(ctx, "Italian");
    SiderModule_InfoAddFieldLongLong(ctx, "due", 2);
    SiderModule_InfoAddFieldDouble(ctx, "tre", 3.3);

    SiderModule_InfoAddSection(ctx, "keyspace");
    SiderModule_InfoBeginDictField(ctx, "db0");
    SiderModule_InfoAddFieldLongLong(ctx, "keys", 3);
    SiderModule_InfoAddFieldLongLong(ctx, "expires", 1);
    SiderModule_InfoEndDictField(ctx);

    SiderModule_InfoAddSection(ctx, "unsafe");
    SiderModule_InfoBeginDictField(ctx, "unsafe:field");
    SiderModule_InfoAddFieldLongLong(ctx, "value", 1);
    SiderModule_InfoEndDictField(ctx);

    if (for_crash_report) {
        SiderModule_InfoAddSection(ctx, "Klingon");
        SiderModule_InfoAddFieldCString(ctx, "one", "wa’");
        SiderModule_InfoAddFieldCString(ctx, "two", "cha’");
        SiderModule_InfoAddFieldCString(ctx, "three", "wej");
    }

}

int info_get(SiderModuleCtx *ctx, SiderModuleString **argv, int argc, char field_type)
{
    if (argc != 3 && argc != 4) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    int err = REDISMODULE_OK;
    const char *section, *field;
    section = SiderModule_StringPtrLen(argv[1], NULL);
    field = SiderModule_StringPtrLen(argv[2], NULL);
    SiderModuleServerInfoData *info = SiderModule_GetServerInfo(ctx, section);
    if (field_type=='i') {
        long long ll = SiderModule_ServerInfoGetFieldSigned(info, field, &err);
        if (err==REDISMODULE_OK)
            SiderModule_ReplyWithLongLong(ctx, ll);
    } else if (field_type=='u') {
        unsigned long long ll = (unsigned long long)SiderModule_ServerInfoGetFieldUnsigned(info, field, &err);
        if (err==REDISMODULE_OK)
            SiderModule_ReplyWithLongLong(ctx, ll);
    } else if (field_type=='d') {
        double d = SiderModule_ServerInfoGetFieldDouble(info, field, &err);
        if (err==REDISMODULE_OK)
            SiderModule_ReplyWithDouble(ctx, d);
    } else if (field_type=='c') {
        const char *str = SiderModule_ServerInfoGetFieldC(info, field);
        if (str)
            SiderModule_ReplyWithCString(ctx, str);
    } else {
        SiderModuleString *str = SiderModule_ServerInfoGetField(ctx, info, field);
        if (str) {
            SiderModule_ReplyWithString(ctx, str);
            SiderModule_FreeString(ctx, str);
        } else
            err=REDISMODULE_ERR;
    }
    if (err!=REDISMODULE_OK)
        SiderModule_ReplyWithError(ctx, "not found");
    SiderModule_FreeServerInfo(ctx, info);
    return REDISMODULE_OK;
}

int info_gets(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 's');
}

int info_getc(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 'c');
}

int info_geti(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 'i');
}

int info_getu(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 'u');
}

int info_getd(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    return info_get(ctx, argv, argc, 'd');
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx,"infotest",1,REDISMODULE_APIVER_1)
            == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_RegisterInfoFunc(ctx, InfoFunc) == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"info.gets", info_gets,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"info.getc", info_getc,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"info.geti", info_geti,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"info.getu", info_getu,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"info.getd", info_getd,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
