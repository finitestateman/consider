#include "sidermodule.h"
#include <strings.h>

/* Second module configs module, for testing.
 * Need to make sure that multiple modules with configs don't interfere with each other */
int bool_config;

int getBoolConfigCommand(const char *name, void *privdata) {
    REDISMODULE_NOT_USED(privdata);
    if (!strcasecmp(name, "test")) {
        return bool_config;
    }
    return 0;
}

int setBoolConfigCommand(const char *name, int new, void *privdata, SiderModuleString **err) {
    REDISMODULE_NOT_USED(privdata);
    REDISMODULE_NOT_USED(err);
    if (!strcasecmp(name, "test")) {
        bool_config = new;
        return REDISMODULE_OK;
    }
    return REDISMODULE_ERR;
}

/* No arguments are expected */ 
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx, "configs", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_RegisterBoolConfig(ctx, "test", 1, REDISMODULE_CONFIG_DEFAULT, getBoolConfigCommand, setBoolConfigCommand, NULL, &argc) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    if (SiderModule_LoadConfigs(ctx) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }
    return REDISMODULE_OK;
}