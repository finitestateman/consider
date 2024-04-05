/* 
 * A module the tests RM_ReplyWith family of commands
 */

#include "sidermodule.h"
#include <math.h>

int rw_string(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    return SiderModule_ReplyWithString(ctx, argv[1]);
}

int rw_cstring(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if (argc != 1) return SiderModule_WrongArity(ctx);

    return SiderModule_ReplyWithSimpleString(ctx, "A simple string");
}

int rw_int(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    long long integer;
    if (SiderModule_StringToLongLong(argv[1], &integer) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx, "Arg cannot be parsed as an integer");

    return SiderModule_ReplyWithLongLong(ctx, integer);
}

/* When one argument is given, it is returned as a double,
 * when two arguments are given, it returns a/b. */
int rw_double(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc==1)
        return SiderModule_ReplyWithDouble(ctx, NAN);

    if (argc != 2 && argc != 3) return SiderModule_WrongArity(ctx);

    double dbl, dbl2;
    if (SiderModule_StringToDouble(argv[1], &dbl) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx, "Arg cannot be parsed as a double");
    if (argc == 3) {
        if (SiderModule_StringToDouble(argv[2], &dbl2) != REDISMODULE_OK)
            return SiderModule_ReplyWithError(ctx, "Arg cannot be parsed as a double");
        dbl /= dbl2;
    }

    return SiderModule_ReplyWithDouble(ctx, dbl);
}

int rw_longdouble(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    long double longdbl;
    if (SiderModule_StringToLongDouble(argv[1], &longdbl) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx, "Arg cannot be parsed as a double");

    return SiderModule_ReplyWithLongDouble(ctx, longdbl);
}

int rw_bignumber(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    size_t bignum_len;
    const char *bignum_str = SiderModule_StringPtrLen(argv[1], &bignum_len);

    return SiderModule_ReplyWithBigNumber(ctx, bignum_str, bignum_len);
}

int rw_array(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    long long integer;
    if (SiderModule_StringToLongLong(argv[1], &integer) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx, "Arg cannot be parsed as a integer");

    SiderModule_ReplyWithArray(ctx, integer);
    for (int i = 0; i < integer; ++i) {
        SiderModule_ReplyWithLongLong(ctx, i);
    }

    return REDISMODULE_OK;
}

int rw_map(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    long long integer;
    if (SiderModule_StringToLongLong(argv[1], &integer) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx, "Arg cannot be parsed as a integer");

    SiderModule_ReplyWithMap(ctx, integer);
    for (int i = 0; i < integer; ++i) {
        SiderModule_ReplyWithLongLong(ctx, i);
        SiderModule_ReplyWithDouble(ctx, i * 1.5);
    }

    return REDISMODULE_OK;
}

int rw_set(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    long long integer;
    if (SiderModule_StringToLongLong(argv[1], &integer) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx, "Arg cannot be parsed as a integer");

    SiderModule_ReplyWithSet(ctx, integer);
    for (int i = 0; i < integer; ++i) {
        SiderModule_ReplyWithLongLong(ctx, i);
    }

    return REDISMODULE_OK;
}

int rw_attribute(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    long long integer;
    if (SiderModule_StringToLongLong(argv[1], &integer) != REDISMODULE_OK)
        return SiderModule_ReplyWithError(ctx, "Arg cannot be parsed as a integer");

    if (SiderModule_ReplyWithAttribute(ctx, integer) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx, "Attributes aren't supported by RESP 2");
    }

    for (int i = 0; i < integer; ++i) {
        SiderModule_ReplyWithLongLong(ctx, i);
        SiderModule_ReplyWithDouble(ctx, i * 1.5);
    }

    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int rw_bool(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if (argc != 1) return SiderModule_WrongArity(ctx);

    SiderModule_ReplyWithArray(ctx, 2);
    SiderModule_ReplyWithBool(ctx, 0);
    return SiderModule_ReplyWithBool(ctx, 1);
}

int rw_null(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if (argc != 1) return SiderModule_WrongArity(ctx);

    return SiderModule_ReplyWithNull(ctx);
}

int rw_error(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if (argc != 1) return SiderModule_WrongArity(ctx);

    return SiderModule_ReplyWithError(ctx, "An error");
}

int rw_error_format(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) return SiderModule_WrongArity(ctx);

    return SiderModule_ReplyWithErrorFormat(ctx,
                                            SiderModule_StringPtrLen(argv[1], NULL),
                                            SiderModule_StringPtrLen(argv[2], NULL));
}

int rw_verbatim(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    size_t verbatim_len;
    const char *verbatim_str = SiderModule_StringPtrLen(argv[1], &verbatim_len);

    return SiderModule_ReplyWithVerbatimString(ctx, verbatim_str, verbatim_len);
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx, "replywith", 1, REDISMODULE_APIVER_1) != REDISMODULE_OK)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"rw.string",rw_string,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.cstring",rw_cstring,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.bignumber",rw_bignumber,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.int",rw_int,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.double",rw_double,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.longdouble",rw_longdouble,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.array",rw_array,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.map",rw_map,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.attribute",rw_attribute,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.set",rw_set,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.bool",rw_bool,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.null",rw_null,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.error",rw_error,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.error_format",rw_error_format,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"rw.verbatim",rw_verbatim,"",0,0,0) != REDISMODULE_OK)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
