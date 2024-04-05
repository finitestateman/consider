#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <hisider.h>
#include <hisider_ssl.h>
#include <async.h>
#include <adapters/libevent.h>

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

int main (int argc, char **argv) {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    struct event_base *base = event_base_new();
    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s <key> <host> <port> <cert> <certKey> [ca]\n", argv[0]);
        exit(1);
    }

    const char *value = argv[1];
    size_t nvalue = strlen(value);

    const char *hostname = argv[2];
    int port = atoi(argv[3]);

    const char *cert = argv[4];
    const char *certKey = argv[5];
    const char *caCert = argc > 5 ? argv[6] : NULL;

    siderSSLContext *ssl;
    siderSSLContextError ssl_error = REDIS_SSL_CTX_NONE;

    siderInitOpenSSL();

    ssl = siderCreateSSLContext(caCert, NULL,
            cert, certKey, NULL, &ssl_error);
    if (!ssl) {
        printf("Error: %s\n", siderSSLContextGetError(ssl_error));
        return 1;
    }

    siderAsyncContext *c = siderAsyncConnect(hostname, port);
    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }
    if (siderInitiateSSLWithContext(&c->c, ssl) != REDIS_OK) {
        printf("SSL Error!\n");
        exit(1);
    }

    siderLibeventAttach(c,base);
    siderAsyncSetConnectCallback(c,connectCallback);
    siderAsyncSetDisconnectCallback(c,disconnectCallback);
    siderAsyncCommand(c, NULL, NULL, "SET key %b", value, nvalue);
    siderAsyncCommand(c, getCallback, (char*)"end-1", "GET key");
    event_base_dispatch(base);

    siderFreeSSLContext(ssl);
    return 0;
}
