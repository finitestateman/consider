#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <async.h>
#include <adapters/poll.h>

/* Put in the global scope, so that loop can be explicitly stopped */
static int exit_loop = 0;

void getCallback(siderAsyncContext *c, void *r, void *privdata) {
    siderReply *reply = r;
    if (reply == NULL) return;
    printf("argv[%s]: %s\n", (char*)privdata, reply->str);

    /* Disconnect after receiving the reply to GET */
    siderAsyncDisconnect(c);
}

void connectCallback(const siderAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        exit_loop = 1;
        return;
    }

    printf("Connected...\n");
}

void disconnectCallback(const siderAsyncContext *c, int status) {
    exit_loop = 1;
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }

    printf("Disconnected...\n");
}

int main (int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    siderAsyncContext *c = siderAsyncConnect("127.0.0.1", 6379);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    siderPollAttach(c);
    siderAsyncSetConnectCallback(c,connectCallback);
    siderAsyncSetDisconnectCallback(c,disconnectCallback);
    siderAsyncCommand(c, NULL, NULL, "SET key %b", argv[argc-1], strlen(argv[argc-1]));
    siderAsyncCommand(c, getCallback, (char*)"end-1", "GET key");
    while (!exit_loop)
    {
        siderPollTick(c, 0.1);
    }
    return 0;
}
