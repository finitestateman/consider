#include "sidermodule.h"
#include <strings.h>
int mutable_bool_val;
int immutable_bool_val;
long long longval;
long long memval;
SiderModuleString *strval = NULL;
int enumval;
int flagsval;

/* Series of get and set callbacks for each type of config, these rely on the privdata ptr
 * to point to the config, and they register the configs as such. Note that one could also just
 * use names if they wanted, and store anything in privdata. */
int getBoolConfigCommand(const char *name, void *privdata) {
    REDISMODULE_NOT_USED(name);
    return (*(int *)privdata);
}

int setBoolConfigCommand(const char *name, int new, void *privdata, SiderModuleString **err) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(err);
    *(int *)privdata = new;
    return REDISMODULE_OK;
}

long long getNumericConfigCommand(const char *name, void *privdata) {
    REDISMODULE_NOT_USED(name);
    return (*(long long *) privdata);
}

int setNumericConfigCommand(const char *name, long long new, void *privdata, SiderModuleString **err) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(err);
    *(long long *)privdata = new;
    return REDISMODULE_OK;
}

SiderModuleString *getStringConfigCommand(const char *name, void *privdata) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(privdata);
    return strval;
}
int setStringConfigCommand(const char *name, SiderModuleString *new, void *privdata, SiderModuleString **err) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(err);
    REDISMODULE_NOT_USED(privdata);
    size_t len;
    if (!strcasecmp(SiderModule_StringPtrLen(new, &len), "rejectisfreed")) {
        *err = SiderModule_CreateString(NULL, "Cannot set string to 'rejectisfreed'", 36);
        return REDISMODULE_ERR;
    }
    if (strval) SiderModule_FreeString(NULL, strval);
    SiderModule_RetainString(NULL, new);
    strval = new;
    return REDISMODULE_OK;
}

int getEnumConfigCommand(const char *name, void *privdata) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(privdata);
    return enumval;
}

int setEnumConfigCommand(const char *name, int val, void *privdata, SiderModuleString **err) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(err);
    REDISMODULE_NOT_USED(privdata);
    enumval = val;
    return REDISMODULE_OK;
}

int getFlagsConfigCommand(const char *name, void *privdata) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(privdata);
    return flagsval;
}

int setFlagsConfigCommand(const char *name, int val, void *privdata, SiderModuleString **err) {
    REDISMODULE_NOT_USED(name);
    REDISMODULE_NOT_USED(err);
    REDISMODULE_NOT_USED(privdata);
    flagsval = val;
    return REDISMODULE_OK;
}

int boolApplyFunc(SiderModuleCtx *ctx, void *privdata, SiderModuleString **err) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(privdata);
    if (mutable_bool_val && immutable_bool_val) {
        *err = SiderModule_CreateString(NULL, "Bool configs cannot both be yes.", 32);
        return REDISMODULE_ERR;
    }
    return REDISMODULE_OK;
}

int longlongApplyFunc(SiderModuleCtx *ctx, void *privdata, SiderModuleString **err) {
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(privdata);
    if (longval == memval) {
        *err = SiderModule_CreateString(NULL, "These configs cannot equal each other.", 38);
        return REDISMODULE_ERR;
    }
    return REDISMODULE_OK;
}

int registerBlockCheck(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    int response_ok = 0;
    int result = SiderModule_RegisterBoolConfig(ctx, "mutable_bool", 1, REDISMODULE_CONFIG_DEFAULT, getBoolConfigCommand, setBoolConfigCommand, boolApplyFunc, &mutable_bool_val);
    response_ok |= (result == REDISMODULE_OK);

    result = SiderModule_RegisterStringConfig(ctx, "string", "secret password", REDISMODULE_CONFIG_DEFAULT, getStringConfigCommand, setStringConfigCommand, NULL, NULL);
    response_ok |= (result == REDISMODULE_OK);

    const char *enum_vals[] = {"none", "five", "one", "two", "four"};
    const int int_vals[] = {0, 5, 1, 2, 4};
    result = SiderModule_RegisterEnumConfig(ctx, "enum", 1, REDISMODULE_CONFIG_DEFAULT, enum_vals, int_vals, 5, getEnumConfigCommand, setEnumConfigCommand, NULL, NULL);
    response_ok |= (result == REDISMODULE_OK);

    result = SiderModule_RegisterNumericConfig(ctx, "numeric", -1, REDISMODULE_CONFIG_DEFAULT, -5, 2000, getNumericConfigCommand, setNumericConfigCommand, longlongApplyFunc, &longval);
    response_ok |= (result == REDISMODULE_OK);

    result = SiderModule_LoadConfigs(ctx);
    response_ok |= (result == REDISMODULE_OK);
    
    /* This validates that it's not possible to register/load configs outside OnLoad,
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

    if (SiderModule_Init(ctx, "moduleconfigs", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_RegisterBoolConfig(ctx, "mutable_bool", 1, REDISMODULE_CONFIG_DEFAULT, getBoolConfigCommand, setBoolConfigCommand, boolApplyFunc, &mutable_bool_val) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    /* Immutable config here. */
    if (SiderModule_RegisterBoolConfig(ctx, "immutable_bool", 0, REDISMODULE_CONFIG_IMMUTABLE, getBoolConfigCommand, setBoolConfigCommand, boolApplyFunc, &immutable_bool_val) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    if (SiderModule_RegisterStringConfig(ctx, "string", "secret password", REDISMODULE_CONFIG_DEFAULT, getStringConfigCommand, setStringConfigCommand, NULL, NULL) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    /* On the stack to make sure we're copying them. */
    const char *enum_vals[] = {"none", "five", "one", "two", "four"};
    const int int_vals[] = {0, 5, 1, 2, 4};

    if (SiderModule_RegisterEnumConfig(ctx, "enum", 1, REDISMODULE_CONFIG_DEFAULT, enum_vals, int_vals, 5, getEnumConfigCommand, setEnumConfigCommand, NULL, NULL) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    if (SiderModule_RegisterEnumConfig(ctx, "flags", 3, REDISMODULE_CONFIG_DEFAULT | REDISMODULE_CONFIG_BITFLAGS, enum_vals, int_vals, 5, getFlagsConfigCommand, setFlagsConfigCommand, NULL, NULL) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    /* Memory config here. */
    if (SiderModule_RegisterNumericConfig(ctx, "memory_numeric", 1024, REDISMODULE_CONFIG_DEFAULT | REDISMODULE_CONFIG_MEMORY, 0, 3000000, getNumericConfigCommand, setNumericConfigCommand, longlongApplyFunc, &memval) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    if (SiderModule_RegisterNumericConfig(ctx, "numeric", -1, REDISMODULE_CONFIG_DEFAULT, -5, 2000, getNumericConfigCommand, setNumericConfigCommand, longlongApplyFunc, &longval) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    size_t len;
    if (argc && !strcasecmp(SiderModule_StringPtrLen(argv[0], &len), "noload")) {
        return REDISMODULE_OK;
    } else if (SiderModule_LoadConfigs(ctx) == REDISMODULE_ERR) {
        if (strval) {
            SiderModule_FreeString(ctx, strval);
            strval = NULL;
        }
        return REDISMODULE_ERR;
    }
    /* Creates a command which registers configs outside OnLoad() function. */
    if (SiderModule_CreateCommand(ctx,"block.register.configs.outside.onload", registerBlockCheck, "write", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
  
    return REDISMODULE_OK;
}

int SiderModule_OnUnload(SiderModuleCtx *ctx) {
    REDISMODULE_NOT_USED(ctx);
    if (strval) {
        SiderModule_FreeString(ctx, strval);
        strval = NULL;
    }
    return REDISMODULE_OK;
}
