#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hisider.h>
#include <async.h>
#include <adapters/sidermoduleapi.h>

void debugCallback(siderAsyncContext *c, void *r, void *privdata) {
    (void)privdata; //unused
    siderReply *reply = r;
    if (reply == NULL) {
        /* The DEBUG SLEEP command will almost always fail, because we have set a 1 second timeout */
        printf("`DEBUG SLEEP` error: %s\n", c->errstr ? c->errstr : "unknown error");
        return;
    }
    /* Disconnect after receiving the reply of DEBUG SLEEP (which will not)*/
    siderAsyncDisconnect(c);
}

void getCallback(siderAsyncContext *c, void *r, void *privdata) {
    siderReply *reply = r;
    if (reply == NULL) {
        if (c->errstr) {
            printf("errstr: %s\n", c->errstr);
        }
        return;
    }
    printf("argv[%s]: %s\n", (char*)privdata, reply->str);

    /* start another request that demonstrate timeout */
    siderAsyncCommand(c, debugCallback, NULL, "DEBUG SLEEP %f", 1.5);
}

void connectCallback(const siderAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Connected...\n");
}

void disconnectCallback(const siderAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

/*
 * This example requires Sider 7.0 or above.
 *
 * 1- Compile this file as a shared library. Directory of "sidermodule.h" must
 *    be in the include path.
 *       gcc -fPIC -shared -I../../sider/src/ -I.. example-sidermoduleapi.c -o example-sidermoduleapi.so
 *
 * 2- Load module:
 *       sider-server --loadmodule ./example-sidermoduleapi.so value
 */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {

    int ret = SiderModule_Init(ctx, "example-sidermoduleapi", 1, REDISMODULE_APIVER_1);
    if (ret != REDISMODULE_OK) {
        printf("error module init \n");
        return REDISMODULE_ERR;
    }

    if (siderModuleCompatibilityCheck() != REDIS_OK) {
        printf("Sider 7.0 or above is required! \n");
        return REDISMODULE_ERR;
    }

    siderAsyncContext *c = siderAsyncConnect("127.0.0.1", 6379);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    size_t len;
    const char *val = SiderModule_StringPtrLen(argv[argc-1], &len);

    SiderModuleCtx *module_ctx = SiderModule_GetDetachedThreadSafeContext(ctx);
    siderModuleAttach(c, module_ctx);
    siderAsyncSetConnectCallback(c,connectCallback);
    siderAsyncSetDisconnectCallback(c,disconnectCallback);
    siderAsyncSetTimeout(c, (struct timeval){ .tv_sec = 1, .tv_usec = 0});

    /*
    In this demo, we first `set key`, then `get key` to demonstrate the basic usage of the adapter.
    Then in `getCallback`, we start a `debug sleep` command to create 1.5 second long request.
    Because we have set a 1 second timeout to the connection, the command will always fail with a
    timeout error, which is shown in the `debugCallback`.
    */

    siderAsyncCommand(c, NULL, NULL, "SET key %b", val, len);
    siderAsyncCommand(c, getCallback, (char*)"end-1", "GET key");
    return 0;
}
