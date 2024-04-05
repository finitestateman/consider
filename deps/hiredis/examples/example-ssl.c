#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hisider.h>
#include <hisider_ssl.h>

#ifdef _MSC_VER
#include <winsock2.h> /* For struct timeval */
#endif

int main(int argc, char **argv) {
    unsigned int j;
    siderSSLContext *ssl;
    siderSSLContextError ssl_error = REDIS_SSL_CTX_NONE;
    siderContext *c;
    siderReply *reply;
    if (argc < 4) {
        printf("Usage: %s <host> <port> <cert> <key> [ca]\n", argv[0]);
        exit(1);
    }
    const char *hostname = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = atoi(argv[2]);
    const char *cert = argv[3];
    const char *key = argv[4];
    const char *ca = argc > 4 ? argv[5] : NULL;

    siderInitOpenSSL();
    ssl = siderCreateSSLContext(ca, NULL, cert, key, NULL, &ssl_error);
    if (!ssl || ssl_error != REDIS_SSL_CTX_NONE) {
        printf("SSL Context error: %s\n", siderSSLContextGetError(ssl_error));
        exit(1);
    }

    struct timeval tv = { 1, 500000 }; // 1.5 seconds
    siderOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, hostname, port);
    options.connect_timeout = &tv;
    c = siderConnectWithOptions(&options);

    if (c == NULL || c->err) {
        if (c) {
            printf("Connection error: %s\n", c->errstr);
            siderFree(c);
        } else {
            printf("Connection error: can't allocate sider context\n");
        }
        exit(1);
    }

    if (siderInitiateSSLWithContext(c, ssl) != REDIS_OK) {
        printf("Couldn't initialize SSL!\n");
        printf("Error: %s\n", c->errstr);
        siderFree(c);
        exit(1);
    }

    /* PING server */
    reply = siderCommand(c,"PING");
    printf("PING: %s\n", reply->str);
    freeReplyObject(reply);

    /* Set a key */
    reply = siderCommand(c,"SET %s %s", "foo", "hello world");
    printf("SET: %s\n", reply->str);
    freeReplyObject(reply);

    /* Set a key using binary safe API */
    reply = siderCommand(c,"SET %b %b", "bar", (size_t) 3, "hello", (size_t) 5);
    printf("SET (binary API): %s\n", reply->str);
    freeReplyObject(reply);

    /* Try a GET and two INCR */
    reply = siderCommand(c,"GET foo");
    printf("GET foo: %s\n", reply->str);
    freeReplyObject(reply);

    reply = siderCommand(c,"INCR counter");
    printf("INCR counter: %lld\n", reply->integer);
    freeReplyObject(reply);
    /* again ... */
    reply = siderCommand(c,"INCR counter");
    printf("INCR counter: %lld\n", reply->integer);
    freeReplyObject(reply);

    /* Create a list of numbers, from 0 to 9 */
    reply = siderCommand(c,"DEL mylist");
    freeReplyObject(reply);
    for (j = 0; j < 10; j++) {
        char buf[64];

        snprintf(buf,64,"%u",j);
        reply = siderCommand(c,"LPUSH mylist element-%s", buf);
        freeReplyObject(reply);
    }

    /* Let's check what we have inside the list */
    reply = siderCommand(c,"LRANGE mylist 0 -1");
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (j = 0; j < reply->elements; j++) {
            printf("%u) %s\n", j, reply->element[j]->str);
        }
    }
    freeReplyObject(reply);

    /* Disconnects and frees the context */
    siderFree(c);

    siderFreeSSLContext(ssl);

    return 0;
}
