#include "sidermodule.h"

#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#define UNUSED(x) (void)(x)

static int n_events = 0;

static int KeySpace_NotificationModuleKeyMissExpired(SiderModuleCtx *ctx, int type, const char *event, SiderModuleString *key) {
    UNUSED(ctx);
    UNUSED(type);
    UNUSED(event);
    UNUSED(key);
    n_events++;
    return REDISMODULE_OK;
}

int test_clear_n_events(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    n_events = 0;
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int test_get_n_events(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    SiderModule_ReplyWithLongLong(ctx, n_events);
    return REDISMODULE_OK;
}

int test_open_key_no_effects(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc<2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    int supportedMode = SiderModule_GetOpenKeyModesAll();
    if (!(supportedMode & REDISMODULE_READ) || !(supportedMode & REDISMODULE_OPEN_KEY_NOEFFECTS)) {
        SiderModule_ReplyWithError(ctx, "OpenKey modes are not supported");
        return REDISMODULE_OK;
    }

    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_OPEN_KEY_NOEFFECTS);
    if (!key) {
        SiderModule_ReplyWithError(ctx, "key not found");
        return REDISMODULE_OK;
    }

    SiderModule_CloseKey(key);
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int test_call_generic(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc<2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    const char* cmdname = SiderModule_StringPtrLen(argv[1], NULL);
    SiderModuleCallReply *reply = SiderModule_Call(ctx, cmdname, "v", argv+2, argc-2);
    if (reply) {
        SiderModule_ReplyWithCallReply(ctx, reply);
        SiderModule_FreeCallReply(reply);
    } else {
        SiderModule_ReplyWithError(ctx, strerror(errno));
    }
    return REDISMODULE_OK;
}

int test_call_info(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    SiderModuleCallReply *reply;
    if (argc>1)
        reply = SiderModule_Call(ctx, "info", "s", argv[1]);
    else
        reply = SiderModule_Call(ctx, "info", "");
    if (reply) {
        SiderModule_ReplyWithCallReply(ctx, reply);
        SiderModule_FreeCallReply(reply);
    } else {
        SiderModule_ReplyWithError(ctx, strerror(errno));
    }
    return REDISMODULE_OK;
}

int test_ld_conv(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    long double ld = 0.00000000000000001L;
    const char *ldstr = "0.00000000000000001";
    SiderModuleString *s1 = SiderModule_CreateStringFromLongDouble(ctx, ld, 1);
    SiderModuleString *s2 =
        SiderModule_CreateString(ctx, ldstr, strlen(ldstr));
    if (SiderModule_StringCompare(s1, s2) != 0) {
        char err[4096];
        snprintf(err, 4096,
            "Failed to convert long double to string ('%s' != '%s')",
            SiderModule_StringPtrLen(s1, NULL),
            SiderModule_StringPtrLen(s2, NULL));
        SiderModule_ReplyWithError(ctx, err);
        goto final;
    }
    long double ld2 = 0;
    if (SiderModule_StringToLongDouble(s2, &ld2) == REDISMODULE_ERR) {
        SiderModule_ReplyWithError(ctx,
            "Failed to convert string to long double");
        goto final;
    }
    if (ld2 != ld) {
        char err[4096];
        snprintf(err, 4096,
            "Failed to convert string to long double (%.40Lf != %.40Lf)",
            ld2,
            ld);
        SiderModule_ReplyWithError(ctx, err);
        goto final;
    }

    /* Make sure we can't convert a string that has \0 in it */
    char buf[4] = "123";
    buf[1] = '\0';
    SiderModuleString *s3 = SiderModule_CreateString(ctx, buf, 3);
    long double ld3;
    if (SiderModule_StringToLongDouble(s3, &ld3) == REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "Invalid string successfully converted to long double");
        SiderModule_FreeString(ctx, s3);
        goto final;
    }
    SiderModule_FreeString(ctx, s3);

    SiderModule_ReplyWithLongDouble(ctx, ld2);
final:
    SiderModule_FreeString(ctx, s1);
    SiderModule_FreeString(ctx, s2);
    return REDISMODULE_OK;
}

int test_flushall(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModule_ResetDataset(1, 0);
    SiderModule_ReplyWithCString(ctx, "Ok");
    return REDISMODULE_OK;
}

int test_dbsize(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    long long ll = SiderModule_DbSize(ctx);
    SiderModule_ReplyWithLongLong(ctx, ll);
    return REDISMODULE_OK;
}

int test_randomkey(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModuleString *str = SiderModule_RandomKey(ctx);
    SiderModule_ReplyWithString(ctx, str);
    SiderModule_FreeString(ctx, str);
    return REDISMODULE_OK;
}

int test_keyexists(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc < 2) return SiderModule_WrongArity(ctx);
    SiderModuleString *key = argv[1];
    int exists = SiderModule_KeyExists(ctx, key);
    return SiderModule_ReplyWithBool(ctx, exists);
}

SiderModuleKey *open_key_or_reply(SiderModuleCtx *ctx, SiderModuleString *keyname, int mode) {
    SiderModuleKey *key = SiderModule_OpenKey(ctx, keyname, mode);
    if (!key) {
        SiderModule_ReplyWithError(ctx, "key not found");
        return NULL;
    }
    return key;
}

int test_getlru(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc<2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    SiderModuleKey *key = open_key_or_reply(ctx, argv[1], REDISMODULE_READ|REDISMODULE_OPEN_KEY_NOTOUCH);
    mstime_t lru;
    SiderModule_GetLRU(key, &lru);
    SiderModule_ReplyWithLongLong(ctx, lru);
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

int test_setlru(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc<3) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    SiderModuleKey *key = open_key_or_reply(ctx, argv[1], REDISMODULE_READ|REDISMODULE_OPEN_KEY_NOTOUCH);
    mstime_t lru;
    if (SiderModule_StringToLongLong(argv[2], &lru) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "invalid idle time");
        return REDISMODULE_OK;
    }
    int was_set = SiderModule_SetLRU(key, lru)==REDISMODULE_OK;
    SiderModule_ReplyWithLongLong(ctx, was_set);
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

int test_getlfu(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc<2) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    SiderModuleKey *key = open_key_or_reply(ctx, argv[1], REDISMODULE_READ|REDISMODULE_OPEN_KEY_NOTOUCH);
    mstime_t lfu;
    SiderModule_GetLFU(key, &lfu);
    SiderModule_ReplyWithLongLong(ctx, lfu);
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

int test_setlfu(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    if (argc<3) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }
    SiderModuleKey *key = open_key_or_reply(ctx, argv[1], REDISMODULE_READ|REDISMODULE_OPEN_KEY_NOTOUCH);
    mstime_t lfu;
    if (SiderModule_StringToLongLong(argv[2], &lfu) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "invalid freq");
        return REDISMODULE_OK;
    }
    int was_set = SiderModule_SetLFU(key, lfu)==REDISMODULE_OK;
    SiderModule_ReplyWithLongLong(ctx, was_set);
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

int test_siderversion(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    (void) argv;
    (void) argc;

    int version = SiderModule_GetServerVersion();
    int patch = version & 0x000000ff;
    int minor = (version & 0x0000ff00) >> 8;
    int major = (version & 0x00ff0000) >> 16;

    SiderModuleString* vStr = SiderModule_CreateStringPrintf(ctx, "%d.%d.%d", major, minor, patch);
    SiderModule_ReplyWithString(ctx, vStr);
    SiderModule_FreeString(ctx, vStr);
  
    return REDISMODULE_OK;
}

int test_getclientcert(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    (void) argv;
    (void) argc;

    SiderModuleString *cert = SiderModule_GetClientCertificate(ctx,
            SiderModule_GetClientId(ctx));
    if (!cert) {
        SiderModule_ReplyWithNull(ctx);
    } else {
        SiderModule_ReplyWithString(ctx, cert);
        SiderModule_FreeString(ctx, cert);
    }

    return REDISMODULE_OK;
}

int test_clientinfo(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    (void) argv;
    (void) argc;

    SiderModuleClientInfoV1 ci = REDISMODULE_CLIENTINFO_INITIALIZER_V1;
    uint64_t client_id = SiderModule_GetClientId(ctx);

    /* Check expected result from the V1 initializer. */
    assert(ci.version == 1);
    /* Trying to populate a future version of the struct should fail. */
    ci.version = REDISMODULE_CLIENTINFO_VERSION + 1;
    assert(SiderModule_GetClientInfoById(&ci, client_id) == REDISMODULE_ERR);

    ci.version = 1;
    if (SiderModule_GetClientInfoById(&ci, client_id) == REDISMODULE_ERR) {
            SiderModule_ReplyWithError(ctx, "failed to get client info");
            return REDISMODULE_OK;
    }

    SiderModule_ReplyWithArray(ctx, 10);
    char flags[512];
    snprintf(flags, sizeof(flags) - 1, "%s:%s:%s:%s:%s:%s",
        ci.flags & REDISMODULE_CLIENTINFO_FLAG_SSL ? "ssl" : "",
        ci.flags & REDISMODULE_CLIENTINFO_FLAG_PUBSUB ? "pubsub" : "",
        ci.flags & REDISMODULE_CLIENTINFO_FLAG_BLOCKED ? "blocked" : "",
        ci.flags & REDISMODULE_CLIENTINFO_FLAG_TRACKING ? "tracking" : "",
        ci.flags & REDISMODULE_CLIENTINFO_FLAG_UNIXSOCKET ? "unixsocket" : "",
        ci.flags & REDISMODULE_CLIENTINFO_FLAG_MULTI ? "multi" : "");

    SiderModule_ReplyWithCString(ctx, "flags");
    SiderModule_ReplyWithCString(ctx, flags);
    SiderModule_ReplyWithCString(ctx, "id");
    SiderModule_ReplyWithLongLong(ctx, ci.id);
    SiderModule_ReplyWithCString(ctx, "addr");
    SiderModule_ReplyWithCString(ctx, ci.addr);
    SiderModule_ReplyWithCString(ctx, "port");
    SiderModule_ReplyWithLongLong(ctx, ci.port);
    SiderModule_ReplyWithCString(ctx, "db");
    SiderModule_ReplyWithLongLong(ctx, ci.db);

    return REDISMODULE_OK;
}

int test_getname(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    (void)argv;
    if (argc != 1) return SiderModule_WrongArity(ctx);
    unsigned long long id = SiderModule_GetClientId(ctx);
    SiderModuleString *name = SiderModule_GetClientNameById(ctx, id);
    if (name == NULL)
        return SiderModule_ReplyWithError(ctx, "-ERR No name");
    SiderModule_ReplyWithString(ctx, name);
    SiderModule_FreeString(ctx, name);
    return REDISMODULE_OK;
}

int test_setname(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);
    unsigned long long id = SiderModule_GetClientId(ctx);
    if (SiderModule_SetClientNameById(id, argv[1]) == REDISMODULE_OK)
        return SiderModule_ReplyWithSimpleString(ctx, "OK");
    else
        return SiderModule_ReplyWithError(ctx, strerror(errno));
}

int test_log_tsctx(SiderModuleCtx *ctx, SiderModuleString **argv, int argc)
{
    SiderModuleCtx *tsctx = SiderModule_GetDetachedThreadSafeContext(ctx);

    if (argc != 3) {
        SiderModule_WrongArity(ctx);
        return REDISMODULE_OK;
    }

    char level[50];
    size_t level_len;
    const char *level_str = SiderModule_StringPtrLen(argv[1], &level_len);
    snprintf(level, sizeof(level) - 1, "%.*s", (int) level_len, level_str);

    size_t msg_len;
    const char *msg_str = SiderModule_StringPtrLen(argv[2], &msg_len);

    SiderModule_Log(tsctx, level, "%.*s", (int) msg_len, msg_str);
    SiderModule_FreeThreadSafeContext(tsctx);

    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int test_weird_cmd(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

int test_monotonic_time(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_ReplyWithLongLong(ctx, SiderModule_MonotonicMicroseconds());
    return REDISMODULE_OK;
}

/* wrapper for RM_Call */
int test_rm_call(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    if(argc < 2){
        return SiderModule_WrongArity(ctx);
    }

    const char* cmd = SiderModule_StringPtrLen(argv[1], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, "Ev", argv + 2, argc - 2);
    if(!rep){
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    }else{
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }

    return REDISMODULE_OK;
}

/* wrapper for RM_Call which also replicates the module command */
int test_rm_call_replicate(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    test_rm_call(ctx, argv, argc);
    SiderModule_ReplicateVerbatim(ctx);

    return REDISMODULE_OK;
}

/* wrapper for RM_Call with flags */
int test_rm_call_flags(SiderModuleCtx *ctx, SiderModuleString **argv, int argc){
    if(argc < 3){
        return SiderModule_WrongArity(ctx);
    }

    /* Append Ev to the provided flags. */
    SiderModuleString *flags = SiderModule_CreateStringFromString(ctx, argv[1]);
    SiderModule_StringAppendBuffer(ctx, flags, "Ev", 2);

    const char* flg = SiderModule_StringPtrLen(flags, NULL);
    const char* cmd = SiderModule_StringPtrLen(argv[2], NULL);

    SiderModuleCallReply* rep = SiderModule_Call(ctx, cmd, flg, argv + 3, argc - 3);
    if(!rep){
        SiderModule_ReplyWithError(ctx, "NULL reply returned");
    }else{
        SiderModule_ReplyWithCallReply(ctx, rep);
        SiderModule_FreeCallReply(rep);
    }
    SiderModule_FreeString(ctx, flags);

    return REDISMODULE_OK;
}

int test_ull_conv(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    unsigned long long ull = 18446744073709551615ULL;
    const char *ullstr = "18446744073709551615";

    SiderModuleString *s1 = SiderModule_CreateStringFromULongLong(ctx, ull);
    SiderModuleString *s2 =
        SiderModule_CreateString(ctx, ullstr, strlen(ullstr));
    if (SiderModule_StringCompare(s1, s2) != 0) {
        char err[4096];
        snprintf(err, 4096,
            "Failed to convert unsigned long long to string ('%s' != '%s')",
            SiderModule_StringPtrLen(s1, NULL),
            SiderModule_StringPtrLen(s2, NULL));
        SiderModule_ReplyWithError(ctx, err);
        goto final;
    }
    unsigned long long ull2 = 0;
    if (SiderModule_StringToULongLong(s2, &ull2) == REDISMODULE_ERR) {
        SiderModule_ReplyWithError(ctx,
            "Failed to convert string to unsigned long long");
        goto final;
    }
    if (ull2 != ull) {
        char err[4096];
        snprintf(err, 4096,
            "Failed to convert string to unsigned long long (%llu != %llu)",
            ull2,
            ull);
        SiderModule_ReplyWithError(ctx, err);
        goto final;
    }
    
    /* Make sure we can't convert a string more than ULLONG_MAX or less than 0 */
    ullstr = "18446744073709551616";
    SiderModuleString *s3 = SiderModule_CreateString(ctx, ullstr, strlen(ullstr));
    unsigned long long ull3;
    if (SiderModule_StringToULongLong(s3, &ull3) == REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "Invalid string successfully converted to unsigned long long");
        SiderModule_FreeString(ctx, s3);
        goto final;
    }
    SiderModule_FreeString(ctx, s3);
    ullstr = "-1";
    SiderModuleString *s4 = SiderModule_CreateString(ctx, ullstr, strlen(ullstr));
    unsigned long long ull4;
    if (SiderModule_StringToULongLong(s4, &ull4) == REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "Invalid string successfully converted to unsigned long long");
        SiderModule_FreeString(ctx, s4);
        goto final;
    }
    SiderModule_FreeString(ctx, s4);
   
    SiderModule_ReplyWithSimpleString(ctx, "ok");

final:
    SiderModule_FreeString(ctx, s1);
    SiderModule_FreeString(ctx, s2);
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx,"misc",1,REDISMODULE_APIVER_1)== REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if(SiderModule_SubscribeToKeyspaceEvents(ctx, REDISMODULE_NOTIFY_KEY_MISS | REDISMODULE_NOTIFY_EXPIRED, KeySpace_NotificationModuleKeyMissExpired) != REDISMODULE_OK){
        return REDISMODULE_ERR;
    }

    if (SiderModule_CreateCommand(ctx,"test.call_generic", test_call_generic,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.call_info", test_call_info,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.ld_conversion", test_ld_conv, "",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.ull_conversion", test_ull_conv, "",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.flushall", test_flushall,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.dbsize", test_dbsize,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.randomkey", test_randomkey,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.keyexists", test_keyexists,"",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.setlru", test_setlru,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.getlru", test_getlru,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.setlfu", test_setlfu,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.getlfu", test_getlfu,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.clientinfo", test_clientinfo,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.getname", test_getname,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.setname", test_setname,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.siderversion", test_siderversion,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.getclientcert", test_getclientcert,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.log_tsctx", test_log_tsctx,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    /* Add a command with ':' in it's name, so that we can check commandstats sanitization. */
    if (SiderModule_CreateCommand(ctx,"test.weird:cmd", test_weird_cmd,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx,"test.monotonic_time", test_monotonic_time,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx, "test.rm_call", test_rm_call,"allow-stale", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx, "test.rm_call_flags", test_rm_call_flags,"allow-stale", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx, "test.rm_call_replicate", test_rm_call_replicate,"allow-stale", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx, "test.silent_open_key", test_open_key_no_effects,"", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx, "test.get_n_events", test_get_n_events,"", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (SiderModule_CreateCommand(ctx, "test.clear_n_events", test_clear_n_events,"", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
