#include "fmacros.h"
#include "sockcompat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#include <sys/time.h>
#endif
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#include "hisider.h"
#include "async.h"
#include "adapters/poll.h"
#ifdef HIREDIS_TEST_SSL
#include "hisider_ssl.h"
#endif
#ifdef HIREDIS_TEST_ASYNC
#include "adapters/libevent.h"
#include <event2/event.h>
#endif
#include "net.h"
#include "win32.h"

enum connection_type {
    CONN_TCP,
    CONN_UNIX,
    CONN_FD,
    CONN_SSL
};

struct config {
    enum connection_type type;
    struct timeval connect_timeout;

    struct {
        const char *host;
        int port;
    } tcp;

    struct {
        const char *path;
    } unix_sock;

    struct {
        const char *host;
        int port;
        const char *ca_cert;
        const char *cert;
        const char *key;
    } ssl;
};

struct privdata {
    int dtor_counter;
};

struct pushCounters {
    int nil;
    int str;
};

static int insecure_calloc_calls;

#ifdef HIREDIS_TEST_SSL
siderSSLContext *_ssl_ctx = NULL;
#endif

/* The following lines make up our testing "framework" :) */
static int tests = 0, fails = 0, skips = 0;
#define test(_s) { printf("#%02d ", ++tests); printf(_s); }
#define test_cond(_c) if(_c) printf("\033[0;32mPASSED\033[0;0m\n"); else {printf("\033[0;31mFAILED\033[0;0m\n"); fails++;}
#define test_skipped() { printf("\033[01;33mSKIPPED\033[0;0m\n"); skips++; }

static void millisleep(int ms)
{
#ifdef _MSC_VER
    Sleep(ms);
#else
    usleep(ms*1000);
#endif
}

static long long usec(void) {
#ifndef _MSC_VER
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
#else
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (((long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10;
#endif
}

/* The assert() calls below have side effects, so we need assert()
 * even if we are compiling without asserts (-DNDEBUG). */
#ifdef NDEBUG
#undef assert
#define assert(e) (void)(e)
#endif

/* Helper to extract Sider version information.  Aborts on any failure. */
#define REDIS_VERSION_FIELD "sider_version:"
void get_sider_version(siderContext *c, int *majorptr, int *minorptr) {
    siderReply *reply;
    char *eptr, *s, *e;
    int major, minor;

    reply = siderCommand(c, "INFO");
    if (reply == NULL || c->err || reply->type != REDIS_REPLY_STRING)
        goto abort;
    if ((s = strstr(reply->str, REDIS_VERSION_FIELD)) == NULL)
        goto abort;

    s += strlen(REDIS_VERSION_FIELD);

    /* We need a field terminator and at least 'x.y.z' (5) bytes of data */
    if ((e = strstr(s, "\r\n")) == NULL || (e - s) < 5)
        goto abort;

    /* Extract version info */
    major = strtol(s, &eptr, 10);
    if (*eptr != '.') goto abort;
    minor = strtol(eptr+1, NULL, 10);

    /* Push info the caller wants */
    if (majorptr) *majorptr = major;
    if (minorptr) *minorptr = minor;

    freeReplyObject(reply);
    return;

abort:
    freeReplyObject(reply);
    fprintf(stderr, "Error:  Cannot determine Sider version, aborting\n");
    exit(1);
}

static siderContext *select_database(siderContext *c) {
    siderReply *reply;

    /* Switch to DB 9 for testing, now that we know we can chat. */
    reply = siderCommand(c,"SELECT 9");
    assert(reply != NULL);
    freeReplyObject(reply);

    /* Make sure the DB is emtpy */
    reply = siderCommand(c,"DBSIZE");
    assert(reply != NULL);
    if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0) {
        /* Awesome, DB 9 is empty and we can continue. */
        freeReplyObject(reply);
    } else {
        printf("Database #9 is not empty, test can not continue\n");
        exit(1);
    }

    return c;
}

/* Switch protocol */
static void send_hello(siderContext *c, int version) {
    siderReply *reply;
    int expected;

    reply = siderCommand(c, "HELLO %d", version);
    expected = version == 3 ? REDIS_REPLY_MAP : REDIS_REPLY_ARRAY;
    assert(reply != NULL && reply->type == expected);
    freeReplyObject(reply);
}

/* Togggle client tracking */
static void send_client_tracking(siderContext *c, const char *str) {
    siderReply *reply;

    reply = siderCommand(c, "CLIENT TRACKING %s", str);
    assert(reply != NULL && reply->type == REDIS_REPLY_STATUS);
    freeReplyObject(reply);
}

static int disconnect(siderContext *c, int keep_fd) {
    siderReply *reply;

    /* Make sure we're on DB 9. */
    reply = siderCommand(c,"SELECT 9");
    assert(reply != NULL);
    freeReplyObject(reply);
    reply = siderCommand(c,"FLUSHDB");
    assert(reply != NULL);
    freeReplyObject(reply);

    /* Free the context as well, but keep the fd if requested. */
    if (keep_fd)
        return siderFreeKeepFd(c);
    siderFree(c);
    return -1;
}

static void do_ssl_handshake(siderContext *c) {
#ifdef HIREDIS_TEST_SSL
    siderInitiateSSLWithContext(c, _ssl_ctx);
    if (c->err) {
        printf("SSL error: %s\n", c->errstr);
        siderFree(c);
        exit(1);
    }
#else
    (void) c;
#endif
}

static siderContext *do_connect(struct config config) {
    siderContext *c = NULL;

    if (config.type == CONN_TCP) {
        c = siderConnect(config.tcp.host, config.tcp.port);
    } else if (config.type == CONN_SSL) {
        c = siderConnect(config.ssl.host, config.ssl.port);
    } else if (config.type == CONN_UNIX) {
        c = siderConnectUnix(config.unix_sock.path);
    } else if (config.type == CONN_FD) {
        /* Create a dummy connection just to get an fd to inherit */
        siderContext *dummy_ctx = siderConnectUnix(config.unix_sock.path);
        if (dummy_ctx) {
            int fd = disconnect(dummy_ctx, 1);
            printf("Connecting to inherited fd %d\n", fd);
            c = siderConnectFd(fd);
        }
    } else {
        assert(NULL);
    }

    if (c == NULL) {
        printf("Connection error: can't allocate sider context\n");
        exit(1);
    } else if (c->err) {
        printf("Connection error: %s\n", c->errstr);
        siderFree(c);
        exit(1);
    }

    if (config.type == CONN_SSL) {
        do_ssl_handshake(c);
    }

    return select_database(c);
}

static void do_reconnect(siderContext *c, struct config config) {
    siderReconnect(c);

    if (config.type == CONN_SSL) {
        do_ssl_handshake(c);
    }
}

static void test_format_commands(void) {
    char *cmd;
    int len;

    test("Format command without interpolation: ");
    len = siderFormatCommand(&cmd,"SET foo bar");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    hi_free(cmd);

    test("Format command with %%s string interpolation: ");
    len = siderFormatCommand(&cmd,"SET %s %s","foo","bar");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    hi_free(cmd);

    test("Format command with %%s and an empty string: ");
    len = siderFormatCommand(&cmd,"SET %s %s","foo","");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(0+2));
    hi_free(cmd);

    test("Format command with an empty string in between proper interpolations: ");
    len = siderFormatCommand(&cmd,"SET %s %s","","foo");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$0\r\n\r\n$3\r\nfoo\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(0+2)+4+(3+2));
    hi_free(cmd);

    test("Format command with %%b string interpolation: ");
    len = siderFormatCommand(&cmd,"SET %b %b","foo",(size_t)3,"b\0r",(size_t)3);
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nb\0r\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    hi_free(cmd);

    test("Format command with %%b and an empty string: ");
    len = siderFormatCommand(&cmd,"SET %b %b","foo",(size_t)3,"",(size_t)0);
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(0+2));
    hi_free(cmd);

    test("Format command with literal %%: ");
    len = siderFormatCommand(&cmd,"SET %% %%");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$1\r\n%\r\n$1\r\n%\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(1+2)+4+(1+2));
    hi_free(cmd);

    /* Vararg width depends on the type. These tests make sure that the
     * width is correctly determined using the format and subsequent varargs
     * can correctly be interpolated. */
#define INTEGER_WIDTH_TEST(fmt, type) do {                                                \
    type value = 123;                                                                     \
    test("Format command with printf-delegation (" #type "): ");                          \
    len = siderFormatCommand(&cmd,"key:%08" fmt " str:%s", value, "hello");               \
    test_cond(strncmp(cmd,"*2\r\n$12\r\nkey:00000123\r\n$9\r\nstr:hello\r\n",len) == 0 && \
        len == 4+5+(12+2)+4+(9+2));                                                       \
    hi_free(cmd);                                                                         \
} while(0)

#define FLOAT_WIDTH_TEST(type) do {                                                       \
    type value = 123.0;                                                                   \
    test("Format command with printf-delegation (" #type "): ");                          \
    len = siderFormatCommand(&cmd,"key:%08.3f str:%s", value, "hello");                   \
    test_cond(strncmp(cmd,"*2\r\n$12\r\nkey:0123.000\r\n$9\r\nstr:hello\r\n",len) == 0 && \
        len == 4+5+(12+2)+4+(9+2));                                                       \
    hi_free(cmd);                                                                         \
} while(0)

    INTEGER_WIDTH_TEST("d", int);
    INTEGER_WIDTH_TEST("hhd", char);
    INTEGER_WIDTH_TEST("hd", short);
    INTEGER_WIDTH_TEST("ld", long);
    INTEGER_WIDTH_TEST("lld", long long);
    INTEGER_WIDTH_TEST("u", unsigned int);
    INTEGER_WIDTH_TEST("hhu", unsigned char);
    INTEGER_WIDTH_TEST("hu", unsigned short);
    INTEGER_WIDTH_TEST("lu", unsigned long);
    INTEGER_WIDTH_TEST("llu", unsigned long long);
    FLOAT_WIDTH_TEST(float);
    FLOAT_WIDTH_TEST(double);

    test("Format command with unhandled printf format (specifier 'p' not supported): ");
    len = siderFormatCommand(&cmd,"key:%08p %b",(void*)1234,"foo",(size_t)3);
    test_cond(len == -1);

    test("Format command with invalid printf format (specifier missing): ");
    len = siderFormatCommand(&cmd,"%-");
    test_cond(len == -1);

    const char *argv[3];
    argv[0] = "SET";
    argv[1] = "foo\0xxx";
    argv[2] = "bar";
    size_t lens[3] = { 3, 7, 3 };
    int argc = 3;

    test("Format command by passing argc/argv without lengths: ");
    len = siderFormatCommandArgv(&cmd,argc,argv,NULL);
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    hi_free(cmd);

    test("Format command by passing argc/argv with lengths: ");
    len = siderFormatCommandArgv(&cmd,argc,argv,lens);
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$7\r\nfoo\0xxx\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(7+2)+4+(3+2));
    hi_free(cmd);

    hisds sds_cmd;

    sds_cmd = NULL;
    test("Format command into hisds by passing argc/argv without lengths: ");
    len = siderFormatSdsCommandArgv(&sds_cmd,argc,argv,NULL);
    test_cond(strncmp(sds_cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    hi_sdsfree(sds_cmd);

    sds_cmd = NULL;
    test("Format command into hisds by passing argc/argv with lengths: ");
    len = siderFormatSdsCommandArgv(&sds_cmd,argc,argv,lens);
    test_cond(strncmp(sds_cmd,"*3\r\n$3\r\nSET\r\n$7\r\nfoo\0xxx\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(7+2)+4+(3+2));
    hi_sdsfree(sds_cmd);
}

static void test_append_formatted_commands(struct config config) {
    siderContext *c;
    siderReply *reply;
    char *cmd;
    int len;

    c = do_connect(config);

    test("Append format command: ");

    len = siderFormatCommand(&cmd, "SET foo bar");

    test_cond(siderAppendFormattedCommand(c, cmd, len) == REDIS_OK);

    assert(siderGetReply(c, (void*)&reply) == REDIS_OK);

    hi_free(cmd);
    freeReplyObject(reply);

    disconnect(c, 0);
}

static void test_tcp_options(struct config cfg) {
    siderContext *c;

    c = do_connect(cfg);

    test("We can enable TCP_KEEPALIVE: ");
    test_cond(siderEnableKeepAlive(c) == REDIS_OK);

#ifdef TCP_USER_TIMEOUT
    test("We can set TCP_USER_TIMEOUT: ");
    test_cond(siderSetTcpUserTimeout(c, 100) == REDIS_OK);
#else
    test("Setting TCP_USER_TIMEOUT errors when unsupported: ");
    test_cond(siderSetTcpUserTimeout(c, 100) == REDIS_ERR && c->err == REDIS_ERR_IO);
#endif

    siderFree(c);
}

static void test_reply_reader(void) {
    siderReader *reader;
    void *reply, *root;
    int ret;
    int i;

    test("Error handling in reply parser: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader,(char*)"@foo\r\n",6);
    ret = siderReaderGetReply(reader,NULL);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Protocol error, got \"@\" as reply type byte") == 0);
    siderReaderFree(reader);

    /* when the reply already contains multiple items, they must be free'd
     * on an error. valgrind will bark when this doesn't happen. */
    test("Memory cleanup in reply parser: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader,(char*)"*2\r\n",4);
    siderReaderFeed(reader,(char*)"$5\r\nhello\r\n",11);
    siderReaderFeed(reader,(char*)"@foo\r\n",6);
    ret = siderReaderGetReply(reader,NULL);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Protocol error, got \"@\" as reply type byte") == 0);
    siderReaderFree(reader);

    reader = siderReaderCreate();
    test("Can handle arbitrarily nested multi-bulks: ");
    for (i = 0; i < 128; i++) {
        siderReaderFeed(reader,(char*)"*1\r\n", 4);
    }
    siderReaderFeed(reader,(char*)"$6\r\nLOLWUT\r\n",12);
    ret = siderReaderGetReply(reader,&reply);
    root = reply; /* Keep track of the root reply */
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_ARRAY &&
        ((siderReply*)reply)->elements == 1);

    test("Can parse arbitrarily nested multi-bulks correctly: ");
    while(i--) {
        assert(reply != NULL && ((siderReply*)reply)->type == REDIS_REPLY_ARRAY);
        reply = ((siderReply*)reply)->element[0];
    }
    test_cond(((siderReply*)reply)->type == REDIS_REPLY_STRING &&
        !memcmp(((siderReply*)reply)->str, "LOLWUT", 6));
    freeReplyObject(root);
    siderReaderFree(reader);

    test("Correctly parses LLONG_MAX: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ":9223372036854775807\r\n",22);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
            ((siderReply*)reply)->type == REDIS_REPLY_INTEGER &&
            ((siderReply*)reply)->integer == LLONG_MAX);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Set error when > LLONG_MAX: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ":9223372036854775808\r\n",22);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Bad integer value") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Correctly parses LLONG_MIN: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ":-9223372036854775808\r\n",23);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
            ((siderReply*)reply)->type == REDIS_REPLY_INTEGER &&
            ((siderReply*)reply)->integer == LLONG_MIN);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Set error when < LLONG_MIN: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ":-9223372036854775809\r\n",23);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Bad integer value") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Set error when array < -1: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "*-2\r\n+asdf\r\n",12);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Multi-bulk length out of range") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Set error when bulk < -1: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "$-2\r\nasdf\r\n",11);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Bulk string length out of range") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can configure maximum multi-bulk elements: ");
    reader = siderReaderCreate();
    reader->maxelements = 1024;
    siderReaderFeed(reader, "*1025\r\n", 7);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr, "Multi-bulk length out of range") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Multi-bulk never overflows regardless of maxelements: ");
    size_t bad_mbulk_len = (SIZE_MAX / sizeof(void *)) + 3;
    char bad_mbulk_reply[100];
    snprintf(bad_mbulk_reply, sizeof(bad_mbulk_reply), "*%llu\r\n+asdf\r\n",
        (unsigned long long) bad_mbulk_len);

    reader = siderReaderCreate();
    reader->maxelements = 0;    /* Don't rely on default limit */
    siderReaderFeed(reader, bad_mbulk_reply, strlen(bad_mbulk_reply));
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR && strcasecmp(reader->errstr, "Out of memory") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

#if LLONG_MAX > SIZE_MAX
    test("Set error when array > SIZE_MAX: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "*9223372036854775807\r\n+asdf\r\n",29);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
            strcasecmp(reader->errstr,"Multi-bulk length out of range") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Set error when bulk > SIZE_MAX: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "$9223372036854775807\r\nasdf\r\n",28);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
            strcasecmp(reader->errstr,"Bulk string length out of range") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);
#endif

    test("Works with NULL functions for reply: ");
    reader = siderReaderCreate();
    reader->fn = NULL;
    siderReaderFeed(reader,(char*)"+OK\r\n",5);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK && reply == (void*)REDIS_REPLY_STATUS);
    siderReaderFree(reader);

    test("Works when a single newline (\\r\\n) covers two calls to feed: ");
    reader = siderReaderCreate();
    reader->fn = NULL;
    siderReaderFeed(reader,(char*)"+OK\r",4);
    ret = siderReaderGetReply(reader,&reply);
    assert(ret == REDIS_OK && reply == NULL);
    siderReaderFeed(reader,(char*)"\n",1);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK && reply == (void*)REDIS_REPLY_STATUS);
    siderReaderFree(reader);

    test("Don't reset state after protocol error: ");
    reader = siderReaderCreate();
    reader->fn = NULL;
    siderReaderFeed(reader,(char*)"x",1);
    ret = siderReaderGetReply(reader,&reply);
    assert(ret == REDIS_ERR);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR && reply == NULL);
    siderReaderFree(reader);

    test("Don't reset state after protocol error(not segfault): ");
    reader = siderReaderCreate();
    siderReaderFeed(reader,(char*)"*3\r\n$3\r\nSET\r\n$5\r\nhello\r\n$", 25);
    ret = siderReaderGetReply(reader,&reply);
    assert(ret == REDIS_OK);
    siderReaderFeed(reader,(char*)"3\r\nval\r\n", 8);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_ARRAY &&
        ((siderReply*)reply)->elements == 3);
    freeReplyObject(reply);
    siderReaderFree(reader);

    /* Regression test for issue #45 on GitHub. */
    test("Don't do empty allocation for empty multi bulk: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader,(char*)"*0\r\n",4);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_ARRAY &&
        ((siderReply*)reply)->elements == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    /* RESP3 verbatim strings (GitHub issue #802) */
    test("Can parse RESP3 verbatim strings: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader,(char*)"=10\r\ntxt:LOLWUT\r\n",17);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_VERB &&
         !memcmp(((siderReply*)reply)->str,"LOLWUT", 6));
    freeReplyObject(reply);
    siderReaderFree(reader);

    /* RESP3 push messages (Github issue #815) */
    test("Can parse RESP3 push messages: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader,(char*)">2\r\n$6\r\nLOLWUT\r\n:42\r\n",21);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_PUSH &&
        ((siderReply*)reply)->elements == 2 &&
        ((siderReply*)reply)->element[0]->type == REDIS_REPLY_STRING &&
        !memcmp(((siderReply*)reply)->element[0]->str,"LOLWUT",6) &&
        ((siderReply*)reply)->element[1]->type == REDIS_REPLY_INTEGER &&
        ((siderReply*)reply)->element[1]->integer == 42);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can parse RESP3 doubles: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ",3.14159265358979323846\r\n",25);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
              ((siderReply*)reply)->type == REDIS_REPLY_DOUBLE &&
              fabs(((siderReply*)reply)->dval - 3.14159265358979323846) < 0.00000001 &&
              ((siderReply*)reply)->len == 22 &&
              strcmp(((siderReply*)reply)->str, "3.14159265358979323846") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Set error on invalid RESP3 double: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ",3.14159\000265358979323846\r\n",26);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Bad double value") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Correctly parses RESP3 double INFINITY: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ",inf\r\n",6);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
              ((siderReply*)reply)->type == REDIS_REPLY_DOUBLE &&
              isinf(((siderReply*)reply)->dval) &&
              ((siderReply*)reply)->dval > 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Correctly parses RESP3 double NaN: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ",nan\r\n",6);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
              ((siderReply*)reply)->type == REDIS_REPLY_DOUBLE &&
              isnan(((siderReply*)reply)->dval));
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Correctly parses RESP3 double -Nan: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, ",-nan\r\n", 7);
    ret = siderReaderGetReply(reader, &reply);
    test_cond(ret == REDIS_OK &&
              ((siderReply*)reply)->type == REDIS_REPLY_DOUBLE &&
              isnan(((siderReply*)reply)->dval));
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can parse RESP3 nil: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "_\r\n",3);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
              ((siderReply*)reply)->type == REDIS_REPLY_NIL);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Set error on invalid RESP3 nil: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "_nil\r\n",6);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Bad nil value") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can parse RESP3 bool (true): ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "#t\r\n",4);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
              ((siderReply*)reply)->type == REDIS_REPLY_BOOL &&
              ((siderReply*)reply)->integer);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can parse RESP3 bool (false): ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "#f\r\n",4);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
              ((siderReply*)reply)->type == REDIS_REPLY_BOOL &&
              !((siderReply*)reply)->integer);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Set error on invalid RESP3 bool: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "#foobar\r\n",9);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Bad bool value") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can parse RESP3 map: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "%2\r\n+first\r\n:123\r\n$6\r\nsecond\r\n#t\r\n",34);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_MAP &&
        ((siderReply*)reply)->elements == 4 &&
        ((siderReply*)reply)->element[0]->type == REDIS_REPLY_STATUS &&
        ((siderReply*)reply)->element[0]->len == 5 &&
        !strcmp(((siderReply*)reply)->element[0]->str,"first") &&
        ((siderReply*)reply)->element[1]->type == REDIS_REPLY_INTEGER &&
        ((siderReply*)reply)->element[1]->integer == 123 &&
        ((siderReply*)reply)->element[2]->type == REDIS_REPLY_STRING &&
        ((siderReply*)reply)->element[2]->len == 6 &&
        !strcmp(((siderReply*)reply)->element[2]->str,"second") &&
        ((siderReply*)reply)->element[3]->type == REDIS_REPLY_BOOL &&
        ((siderReply*)reply)->element[3]->integer);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can parse RESP3 set: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "~5\r\n+orange\r\n$5\r\napple\r\n#f\r\n:100\r\n:999\r\n",40);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_SET &&
        ((siderReply*)reply)->elements == 5 &&
        ((siderReply*)reply)->element[0]->type == REDIS_REPLY_STATUS &&
        ((siderReply*)reply)->element[0]->len == 6 &&
        !strcmp(((siderReply*)reply)->element[0]->str,"orange") &&
        ((siderReply*)reply)->element[1]->type == REDIS_REPLY_STRING &&
        ((siderReply*)reply)->element[1]->len == 5 &&
        !strcmp(((siderReply*)reply)->element[1]->str,"apple") &&
        ((siderReply*)reply)->element[2]->type == REDIS_REPLY_BOOL &&
        !((siderReply*)reply)->element[2]->integer &&
        ((siderReply*)reply)->element[3]->type == REDIS_REPLY_INTEGER &&
        ((siderReply*)reply)->element[3]->integer == 100 &&
        ((siderReply*)reply)->element[4]->type == REDIS_REPLY_INTEGER &&
        ((siderReply*)reply)->element[4]->integer == 999);
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can parse RESP3 bignum: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader,"(3492890328409238509324850943850943825024385\r\n",46);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_BIGNUM &&
        ((siderReply*)reply)->len == 43 &&
        !strcmp(((siderReply*)reply)->str,"3492890328409238509324850943850943825024385"));
    freeReplyObject(reply);
    siderReaderFree(reader);

    test("Can parse RESP3 doubles in an array: ");
    reader = siderReaderCreate();
    siderReaderFeed(reader, "*1\r\n,3.14159265358979323846\r\n",31);
    ret = siderReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((siderReply*)reply)->type == REDIS_REPLY_ARRAY &&
        ((siderReply*)reply)->elements == 1 &&
        ((siderReply*)reply)->element[0]->type == REDIS_REPLY_DOUBLE &&
        fabs(((siderReply*)reply)->element[0]->dval - 3.14159265358979323846) < 0.00000001 &&
        ((siderReply*)reply)->element[0]->len == 22 &&
        strcmp(((siderReply*)reply)->element[0]->str, "3.14159265358979323846") == 0);
    freeReplyObject(reply);
    siderReaderFree(reader);
}

static void test_free_null(void) {
    void *siderCtx = NULL;
    void *reply = NULL;

    test("Don't fail when siderFree is passed a NULL value: ");
    siderFree(siderCtx);
    test_cond(siderCtx == NULL);

    test("Don't fail when freeReplyObject is passed a NULL value: ");
    freeReplyObject(reply);
    test_cond(reply == NULL);
}

static void *hi_malloc_fail(size_t size) {
    (void)size;
    return NULL;
}

static void *hi_calloc_fail(size_t nmemb, size_t size) {
    (void)nmemb;
    (void)size;
    return NULL;
}

static void *hi_calloc_insecure(size_t nmemb, size_t size) {
    (void)nmemb;
    (void)size;
    insecure_calloc_calls++;
    return (void*)0xdeadc0de;
}

static void *hi_realloc_fail(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
    return NULL;
}

static void test_allocator_injection(void) {
    void *ptr;

    hisiderAllocFuncs ha = {
        .mallocFn = hi_malloc_fail,
        .callocFn = hi_calloc_fail,
        .reallocFn = hi_realloc_fail,
        .strdupFn = strdup,
        .freeFn = free,
    };

    // Override hisider allocators
    hisiderSetAllocators(&ha);

    test("siderContext uses injected allocators: ");
    siderContext *c = siderConnect("localhost", 6379);
    test_cond(c == NULL);

    test("siderReader uses injected allocators: ");
    siderReader *reader = siderReaderCreate();
    test_cond(reader == NULL);

    /* Make sure hisider itself protects against a non-overflow checking calloc */
    test("hisider calloc wrapper protects against overflow: ");
    ha.callocFn = hi_calloc_insecure;
    hisiderSetAllocators(&ha);
    ptr = hi_calloc((SIZE_MAX / sizeof(void*)) + 3, sizeof(void*));
    test_cond(ptr == NULL && insecure_calloc_calls == 0);

    // Return allocators to default
    hisiderResetAllocators();
}

#define HIREDIS_BAD_DOMAIN "idontexist-noreally.com"
static void test_blocking_connection_errors(void) {
    struct addrinfo hints = {.ai_family = AF_INET};
    struct addrinfo *ai_tmp = NULL;
    siderContext *c;

    int rv = getaddrinfo(HIREDIS_BAD_DOMAIN, "6379", &hints, &ai_tmp);
    if (rv != 0) {
        // Address does *not* exist
        test("Returns error when host cannot be resolved: ");
        // First see if this domain name *actually* resolves to NXDOMAIN
        c = siderConnect(HIREDIS_BAD_DOMAIN, 6379);
        test_cond(
            c->err == REDIS_ERR_OTHER &&
            (strcmp(c->errstr, "Name or service not known") == 0 ||
             strcmp(c->errstr, "Can't resolve: " HIREDIS_BAD_DOMAIN) == 0 ||
             strcmp(c->errstr, "Name does not resolve") == 0 ||
             strcmp(c->errstr, "nodename nor servname provided, or not known") == 0 ||
             strcmp(c->errstr, "node name or service name not known") == 0 ||
             strcmp(c->errstr, "No address associated with hostname") == 0 ||
             strcmp(c->errstr, "Temporary failure in name resolution") == 0 ||
             strcmp(c->errstr, "hostname nor servname provided, or not known") == 0 ||
             strcmp(c->errstr, "no address associated with name") == 0 ||
             strcmp(c->errstr, "No such host is known. ") == 0));
        siderFree(c);
    } else {
        printf("Skipping NXDOMAIN test. Found evil ISP!\n");
        freeaddrinfo(ai_tmp);
    }

#ifndef _WIN32
    siderOptions opt = {0};
    struct timeval tv;

    test("Returns error when the port is not open: ");
    c = siderConnect((char*)"localhost", 1);
    test_cond(c->err == REDIS_ERR_IO &&
        strcmp(c->errstr,"Connection refused") == 0);
    siderFree(c);


    /* Verify we don't regress from the fix in PR #1180 */
    test("We don't clobber connection exception with setsockopt error: ");
    tv = (struct timeval){.tv_sec = 0, .tv_usec = 500000};
    opt.command_timeout = opt.connect_timeout = &tv;
    REDIS_OPTIONS_SET_TCP(&opt, "localhost", 10337);
    c = siderConnectWithOptions(&opt);
    test_cond(c->err == REDIS_ERR_IO &&
              strcmp(c->errstr, "Connection refused") == 0);
    siderFree(c);

    test("Returns error when the unix_sock socket path doesn't accept connections: ");
    c = siderConnectUnix((char*)"/tmp/idontexist.sock");
    test_cond(c->err == REDIS_ERR_IO); /* Don't care about the message... */
    siderFree(c);
#endif
}

/* Test push handler */
void push_handler(void *privdata, void *r) {
    struct pushCounters *pcounts = privdata;
    siderReply *reply = r, *payload;

    assert(reply && reply->type == REDIS_REPLY_PUSH && reply->elements == 2);

    payload = reply->element[1];
    if (payload->type == REDIS_REPLY_ARRAY) {
        payload = payload->element[0];
    }

    if (payload->type == REDIS_REPLY_STRING) {
        pcounts->str++;
    } else if (payload->type == REDIS_REPLY_NIL) {
        pcounts->nil++;
    }

    freeReplyObject(reply);
}

/* Dummy function just to test setting a callback with siderOptions */
void push_handler_async(siderAsyncContext *ac, void *reply) {
    (void)ac;
    (void)reply;
}

static void test_resp3_push_handler(siderContext *c) {
    struct pushCounters pc = {0};
    siderPushFn *old = NULL;
    siderReply *reply;
    void *privdata;

    /* Switch to RESP3 and turn on client tracking */
    send_hello(c, 3);
    send_client_tracking(c, "ON");
    privdata = c->privdata;
    c->privdata = &pc;

    reply = siderCommand(c, "GET key:0");
    assert(reply != NULL);
    freeReplyObject(reply);

    test("RESP3 PUSH messages are handled out of band by default: ");
    reply = siderCommand(c, "SET key:0 val:0");
    test_cond(reply != NULL && reply->type == REDIS_REPLY_STATUS);
    freeReplyObject(reply);

    assert((reply = siderCommand(c, "GET key:0")) != NULL);
    freeReplyObject(reply);

    old = siderSetPushCallback(c, push_handler);
    test("We can set a custom RESP3 PUSH handler: ");
    reply = siderCommand(c, "SET key:0 val:0");
    /* We need another command because depending on the version of Sider, the
     * notification may be delivered after the command's reply. */
    assert(reply != NULL);
    freeReplyObject(reply);
    reply = siderCommand(c, "PING");
    test_cond(reply != NULL && reply->type == REDIS_REPLY_STATUS && pc.str == 1);
    freeReplyObject(reply);

    test("We properly handle a NIL invalidation payload: ");
    reply = siderCommand(c, "FLUSHDB");
    assert(reply != NULL);
    freeReplyObject(reply);
    reply = siderCommand(c, "PING");
    test_cond(reply != NULL && reply->type == REDIS_REPLY_STATUS && pc.nil == 1);
    freeReplyObject(reply);

    /* Unset the push callback and generate an invalidate message making
     * sure it is not handled out of band. */
    test("With no handler, PUSH replies come in-band: ");
    siderSetPushCallback(c, NULL);
    assert((reply = siderCommand(c, "GET key:0")) != NULL);
    freeReplyObject(reply);
    assert((reply = siderCommand(c, "SET key:0 invalid")) != NULL);
    /* Depending on Sider version, we may receive either push notification or
     * status reply. Both cases are valid. */
    if (reply->type == REDIS_REPLY_STATUS) {
        freeReplyObject(reply);
        reply = siderCommand(c, "PING");
    }
    test_cond(reply->type == REDIS_REPLY_PUSH);
    freeReplyObject(reply);

    test("With no PUSH handler, no replies are lost: ");
    assert(siderGetReply(c, (void**)&reply) == REDIS_OK);
    test_cond(reply != NULL && reply->type == REDIS_REPLY_STATUS);
    freeReplyObject(reply);

    /* Return to the originally set PUSH handler */
    assert(old != NULL);
    siderSetPushCallback(c, old);

    /* Switch back to RESP2 and disable tracking */
    c->privdata = privdata;
    send_client_tracking(c, "OFF");
    send_hello(c, 2);
}

siderOptions get_sider_tcp_options(struct config config) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, config.tcp.host, config.tcp.port);
    return options;
}

static void test_resp3_push_options(struct config config) {
    siderAsyncContext *ac;
    siderContext *c;
    siderOptions options;

    test("We set a default RESP3 handler for siderContext: ");
    options = get_sider_tcp_options(config);
    assert((c = siderConnectWithOptions(&options)) != NULL);
    test_cond(c->push_cb != NULL);
    siderFree(c);

    test("We don't set a default RESP3 push handler for siderAsyncContext: ");
    options = get_sider_tcp_options(config);
    assert((ac = siderAsyncConnectWithOptions(&options)) != NULL);
    test_cond(ac->c.push_cb == NULL);
    siderAsyncFree(ac);

    test("Our REDIS_OPT_NO_PUSH_AUTOFREE flag works: ");
    options = get_sider_tcp_options(config);
    options.options |= REDIS_OPT_NO_PUSH_AUTOFREE;
    assert((c = siderConnectWithOptions(&options)) != NULL);
    test_cond(c->push_cb == NULL);
    siderFree(c);

    test("We can use siderOptions to set a custom PUSH handler for siderContext: ");
    options = get_sider_tcp_options(config);
    options.push_cb = push_handler;
    assert((c = siderConnectWithOptions(&options)) != NULL);
    test_cond(c->push_cb == push_handler);
    siderFree(c);

    test("We can use siderOptions to set a custom PUSH handler for siderAsyncContext: ");
    options = get_sider_tcp_options(config);
    options.async_push_cb = push_handler_async;
    assert((ac = siderAsyncConnectWithOptions(&options)) != NULL);
    test_cond(ac->push_cb == push_handler_async);
    siderAsyncFree(ac);
}

void free_privdata(void *privdata) {
    struct privdata *data = privdata;
    data->dtor_counter++;
}

static void test_privdata_hooks(struct config config) {
    struct privdata data = {0};
    siderOptions options;
    siderContext *c;

    test("We can use siderOptions to set privdata: ");
    options = get_sider_tcp_options(config);
    REDIS_OPTIONS_SET_PRIVDATA(&options, &data, free_privdata);
    assert((c = siderConnectWithOptions(&options)) != NULL);
    test_cond(c->privdata == &data);

    test("Our privdata destructor fires when we free the context: ");
    siderFree(c);
    test_cond(data.dtor_counter == 1);
}

static void test_blocking_connection(struct config config) {
    siderContext *c;
    siderReply *reply;
    int major;

    c = do_connect(config);

    test("Is able to deliver commands: ");
    reply = siderCommand(c,"PING");
    test_cond(reply->type == REDIS_REPLY_STATUS &&
        strcasecmp(reply->str,"pong") == 0)
    freeReplyObject(reply);

    test("Is a able to send commands verbatim: ");
    reply = siderCommand(c,"SET foo bar");
    test_cond (reply->type == REDIS_REPLY_STATUS &&
        strcasecmp(reply->str,"ok") == 0)
    freeReplyObject(reply);

    test("%%s String interpolation works: ");
    reply = siderCommand(c,"SET %s %s","foo","hello world");
    freeReplyObject(reply);
    reply = siderCommand(c,"GET foo");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        strcmp(reply->str,"hello world") == 0);
    freeReplyObject(reply);

    test("%%b String interpolation works: ");
    reply = siderCommand(c,"SET %b %b","foo",(size_t)3,"hello\x00world",(size_t)11);
    freeReplyObject(reply);
    reply = siderCommand(c,"GET foo");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        memcmp(reply->str,"hello\x00world",11) == 0)

    test("Binary reply length is correct: ");
    test_cond(reply->len == 11)
    freeReplyObject(reply);

    test("Can parse nil replies: ");
    reply = siderCommand(c,"GET nokey");
    test_cond(reply->type == REDIS_REPLY_NIL)
    freeReplyObject(reply);

    /* test 7 */
    test("Can parse integer replies: ");
    reply = siderCommand(c,"INCR mycounter");
    test_cond(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1)
    freeReplyObject(reply);

    test("Can parse multi bulk replies: ");
    freeReplyObject(siderCommand(c,"LPUSH mylist foo"));
    freeReplyObject(siderCommand(c,"LPUSH mylist bar"));
    reply = siderCommand(c,"LRANGE mylist 0 -1");
    test_cond(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              !memcmp(reply->element[0]->str,"bar",3) &&
              !memcmp(reply->element[1]->str,"foo",3))
    freeReplyObject(reply);

    /* m/e with multi bulk reply *before* other reply.
     * specifically test ordering of reply items to parse. */
    test("Can handle nested multi bulk replies: ");
    freeReplyObject(siderCommand(c,"MULTI"));
    freeReplyObject(siderCommand(c,"LRANGE mylist 0 -1"));
    freeReplyObject(siderCommand(c,"PING"));
    reply = (siderCommand(c,"EXEC"));
    test_cond(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              reply->element[0]->type == REDIS_REPLY_ARRAY &&
              reply->element[0]->elements == 2 &&
              !memcmp(reply->element[0]->element[0]->str,"bar",3) &&
              !memcmp(reply->element[0]->element[1]->str,"foo",3) &&
              reply->element[1]->type == REDIS_REPLY_STATUS &&
              strcasecmp(reply->element[1]->str,"pong") == 0);
    freeReplyObject(reply);

    test("Send command by passing argc/argv: ");
    const char *argv[3] = {"SET", "foo", "bar"};
    size_t argvlen[3] = {3, 3, 3};
    reply = siderCommandArgv(c,3,argv,argvlen);
    test_cond(reply->type == REDIS_REPLY_STATUS);
    freeReplyObject(reply);

    /* Make sure passing NULL to siderGetReply is safe */
    test("Can pass NULL to siderGetReply: ");
    assert(siderAppendCommand(c, "PING") == REDIS_OK);
    test_cond(siderGetReply(c, NULL) == REDIS_OK);

    get_sider_version(c, &major, NULL);
    if (major >= 6) test_resp3_push_handler(c);
    test_resp3_push_options(config);

    test_privdata_hooks(config);

    disconnect(c, 0);
}

/* Send DEBUG SLEEP 0 to detect if we have this command */
static int detect_debug_sleep(siderContext *c) {
    int detected;
    siderReply *reply = siderCommand(c, "DEBUG SLEEP 0\r\n");

    if (reply == NULL || c->err) {
        const char *cause = c->err ? c->errstr : "(none)";
        fprintf(stderr, "Error testing for DEBUG SLEEP (Sider error: %s), exiting\n", cause);
        exit(-1);
    }

    detected = reply->type == REDIS_REPLY_STATUS;
    freeReplyObject(reply);

    return detected;
}

static void test_blocking_connection_timeouts(struct config config) {
    siderContext *c;
    siderReply *reply;
    ssize_t s;
    const char *sleep_cmd = "DEBUG SLEEP 3\r\n";
    struct timeval tv;

    c = do_connect(config);
    test("Successfully completes a command when the timeout is not exceeded: ");
    reply = siderCommand(c,"SET foo fast");
    freeReplyObject(reply);
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    siderSetTimeout(c, tv);
    reply = siderCommand(c, "GET foo");
    test_cond(reply != NULL && reply->type == REDIS_REPLY_STRING && memcmp(reply->str, "fast", 4) == 0);
    freeReplyObject(reply);
    disconnect(c, 0);

    c = do_connect(config);
    test("Does not return a reply when the command times out: ");
    if (detect_debug_sleep(c)) {
        siderAppendFormattedCommand(c, sleep_cmd, strlen(sleep_cmd));

        // flush connection buffer without waiting for the reply
        s = c->funcs->write(c);
        assert(s == (ssize_t)hi_sdslen(c->obuf));
        hi_sdsfree(c->obuf);
        c->obuf = hi_sdsempty();

        tv.tv_sec = 0;
        tv.tv_usec = 10000;
        siderSetTimeout(c, tv);
        reply = siderCommand(c, "GET foo");
#ifndef _WIN32
        test_cond(s > 0 && reply == NULL && c->err == REDIS_ERR_IO &&
                  strcmp(c->errstr, "Resource temporarily unavailable") == 0);
#else
        test_cond(s > 0 && reply == NULL && c->err == REDIS_ERR_TIMEOUT &&
                  strcmp(c->errstr, "recv timeout") == 0);
#endif
        freeReplyObject(reply);

        // wait for the DEBUG SLEEP to complete so that Sider server is unblocked for the following tests
        millisleep(3000);
    } else {
        test_skipped();
    }

    test("Reconnect properly reconnects after a timeout: ");
    do_reconnect(c, config);
    reply = siderCommand(c, "PING");
    test_cond(reply != NULL && reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "PONG") == 0);
    freeReplyObject(reply);

    test("Reconnect properly uses owned parameters: ");
    config.tcp.host = "foo";
    config.unix_sock.path = "foo";
    do_reconnect(c, config);
    reply = siderCommand(c, "PING");
    test_cond(reply != NULL && reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "PONG") == 0);
    freeReplyObject(reply);

    disconnect(c, 0);
}

static void test_blocking_io_errors(struct config config) {
    siderContext *c;
    siderReply *reply;
    void *_reply;
    int major, minor;

    /* Connect to target given by config. */
    c = do_connect(config);
    get_sider_version(c, &major, &minor);

    test("Returns I/O error when the connection is lost: ");
    reply = siderCommand(c,"QUIT");
    if (major > 2 || (major == 2 && minor > 0)) {
        /* > 2.0 returns OK on QUIT and read() should be issued once more
         * to know the descriptor is at EOF. */
        test_cond(strcasecmp(reply->str,"OK") == 0 &&
            siderGetReply(c,&_reply) == REDIS_ERR);
        freeReplyObject(reply);
    } else {
        test_cond(reply == NULL);
    }

#ifndef _WIN32
    /* On 2.0, QUIT will cause the connection to be closed immediately and
     * the read(2) for the reply on QUIT will set the error to EOF.
     * On >2.0, QUIT will return with OK and another read(2) needed to be
     * issued to find out the socket was closed by the server. In both
     * conditions, the error will be set to EOF. */
    assert(c->err == REDIS_ERR_EOF &&
        strcmp(c->errstr,"Server closed the connection") == 0);
#endif
    siderFree(c);

    c = do_connect(config);
    test("Returns I/O error on socket timeout: ");
    struct timeval tv = { 0, 1000 };
    assert(siderSetTimeout(c,tv) == REDIS_OK);
    int respcode = siderGetReply(c,&_reply);
#ifndef _WIN32
    test_cond(respcode == REDIS_ERR && c->err == REDIS_ERR_IO && errno == EAGAIN);
#else
    test_cond(respcode == REDIS_ERR && c->err == REDIS_ERR_TIMEOUT);
#endif
    siderFree(c);
}

static void test_invalid_timeout_errors(struct config config) {
    siderContext *c;

    test("Set error when an invalid timeout usec value is used during connect: ");

    config.connect_timeout.tv_sec = 0;
    config.connect_timeout.tv_usec = 10000001;

    if (config.type == CONN_TCP || config.type == CONN_SSL) {
        c = siderConnectWithTimeout(config.tcp.host, config.tcp.port, config.connect_timeout);
    } else if(config.type == CONN_UNIX) {
        c = siderConnectUnixWithTimeout(config.unix_sock.path, config.connect_timeout);
    } else {
        assert(NULL);
    }

    test_cond(c->err == REDIS_ERR_IO && strcmp(c->errstr, "Invalid timeout specified") == 0);
    siderFree(c);

    test("Set error when an invalid timeout sec value is used during connect: ");

    config.connect_timeout.tv_sec = (((LONG_MAX) - 999) / 1000) + 1;
    config.connect_timeout.tv_usec = 0;

    if (config.type == CONN_TCP || config.type == CONN_SSL) {
        c = siderConnectWithTimeout(config.tcp.host, config.tcp.port, config.connect_timeout);
    } else if(config.type == CONN_UNIX) {
        c = siderConnectUnixWithTimeout(config.unix_sock.path, config.connect_timeout);
    } else {
        assert(NULL);
    }

    test_cond(c->err == REDIS_ERR_IO && strcmp(c->errstr, "Invalid timeout specified") == 0);
    siderFree(c);
}

/* Wrap malloc to abort on failure so OOM checks don't make the test logic
 * harder to follow. */
void *hi_malloc_safe(size_t size) {
    void *ptr = hi_malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Error:  Out of memory\n");
        exit(-1);
    }

    return ptr;
}

static void test_throughput(struct config config) {
    siderContext *c = do_connect(config);
    siderReply **replies;
    int i, num;
    long long t1, t2;

    test("Throughput:\n");
    for (i = 0; i < 500; i++)
        freeReplyObject(siderCommand(c,"LPUSH mylist foo"));

    num = 1000;
    replies = hi_malloc_safe(sizeof(siderReply*)*num);
    t1 = usec();
    for (i = 0; i < num; i++) {
        replies[i] = siderCommand(c,"PING");
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_STATUS);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    hi_free(replies);
    printf("\t(%dx PING: %.3fs)\n", num, (t2-t1)/1000000.0);

    replies = hi_malloc_safe(sizeof(siderReply*)*num);
    t1 = usec();
    for (i = 0; i < num; i++) {
        replies[i] = siderCommand(c,"LRANGE mylist 0 499");
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_ARRAY);
        assert(replies[i] != NULL && replies[i]->elements == 500);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    hi_free(replies);
    printf("\t(%dx LRANGE with 500 elements: %.3fs)\n", num, (t2-t1)/1000000.0);

    replies = hi_malloc_safe(sizeof(siderReply*)*num);
    t1 = usec();
    for (i = 0; i < num; i++) {
        replies[i] = siderCommand(c, "INCRBY incrkey %d", 1000000);
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_INTEGER);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    hi_free(replies);
    printf("\t(%dx INCRBY: %.3fs)\n", num, (t2-t1)/1000000.0);

    num = 10000;
    replies = hi_malloc_safe(sizeof(siderReply*)*num);
    for (i = 0; i < num; i++)
        siderAppendCommand(c,"PING");
    t1 = usec();
    for (i = 0; i < num; i++) {
        assert(siderGetReply(c, (void*)&replies[i]) == REDIS_OK);
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_STATUS);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    hi_free(replies);
    printf("\t(%dx PING (pipelined): %.3fs)\n", num, (t2-t1)/1000000.0);

    replies = hi_malloc_safe(sizeof(siderReply*)*num);
    for (i = 0; i < num; i++)
        siderAppendCommand(c,"LRANGE mylist 0 499");
    t1 = usec();
    for (i = 0; i < num; i++) {
        assert(siderGetReply(c, (void*)&replies[i]) == REDIS_OK);
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_ARRAY);
        assert(replies[i] != NULL && replies[i]->elements == 500);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    hi_free(replies);
    printf("\t(%dx LRANGE with 500 elements (pipelined): %.3fs)\n", num, (t2-t1)/1000000.0);

    replies = hi_malloc_safe(sizeof(siderReply*)*num);
    for (i = 0; i < num; i++)
        siderAppendCommand(c,"INCRBY incrkey %d", 1000000);
    t1 = usec();
    for (i = 0; i < num; i++) {
        assert(siderGetReply(c, (void*)&replies[i]) == REDIS_OK);
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_INTEGER);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    hi_free(replies);
    printf("\t(%dx INCRBY (pipelined): %.3fs)\n", num, (t2-t1)/1000000.0);

    disconnect(c, 0);
}

// static long __test_callback_flags = 0;
// static void __test_callback(siderContext *c, void *privdata) {
//     ((void)c);
//     /* Shift to detect execution order */
//     __test_callback_flags <<= 8;
//     __test_callback_flags |= (long)privdata;
// }
//
// static void __test_reply_callback(siderContext *c, siderReply *reply, void *privdata) {
//     ((void)c);
//     /* Shift to detect execution order */
//     __test_callback_flags <<= 8;
//     __test_callback_flags |= (long)privdata;
//     if (reply) freeReplyObject(reply);
// }
//
// static siderContext *__connect_nonblock() {
//     /* Reset callback flags */
//     __test_callback_flags = 0;
//     return siderConnectNonBlock("127.0.0.1", port, NULL);
// }
//
// static void test_nonblocking_connection() {
//     siderContext *c;
//     int wdone = 0;
//
//     test("Calls command callback when command is issued: ");
//     c = __connect_nonblock();
//     siderSetCommandCallback(c,__test_callback,(void*)1);
//     siderCommand(c,"PING");
//     test_cond(__test_callback_flags == 1);
//     siderFree(c);
//
//     test("Calls disconnect callback on siderDisconnect: ");
//     c = __connect_nonblock();
//     siderSetDisconnectCallback(c,__test_callback,(void*)2);
//     siderDisconnect(c);
//     test_cond(__test_callback_flags == 2);
//     siderFree(c);
//
//     test("Calls disconnect callback and free callback on siderFree: ");
//     c = __connect_nonblock();
//     siderSetDisconnectCallback(c,__test_callback,(void*)2);
//     siderSetFreeCallback(c,__test_callback,(void*)4);
//     siderFree(c);
//     test_cond(__test_callback_flags == ((2 << 8) | 4));
//
//     test("siderBufferWrite against empty write buffer: ");
//     c = __connect_nonblock();
//     test_cond(siderBufferWrite(c,&wdone) == REDIS_OK && wdone == 1);
//     siderFree(c);
//
//     test("siderBufferWrite against not yet connected fd: ");
//     c = __connect_nonblock();
//     siderCommand(c,"PING");
//     test_cond(siderBufferWrite(c,NULL) == REDIS_ERR &&
//               strncmp(c->error,"write:",6) == 0);
//     siderFree(c);
//
//     test("siderBufferWrite against closed fd: ");
//     c = __connect_nonblock();
//     siderCommand(c,"PING");
//     siderDisconnect(c);
//     test_cond(siderBufferWrite(c,NULL) == REDIS_ERR &&
//               strncmp(c->error,"write:",6) == 0);
//     siderFree(c);
//
//     test("Process callbacks in the right sequence: ");
//     c = __connect_nonblock();
//     siderCommandWithCallback(c,__test_reply_callback,(void*)1,"PING");
//     siderCommandWithCallback(c,__test_reply_callback,(void*)2,"PING");
//     siderCommandWithCallback(c,__test_reply_callback,(void*)3,"PING");
//
//     /* Write output buffer */
//     wdone = 0;
//     while(!wdone) {
//         usleep(500);
//         siderBufferWrite(c,&wdone);
//     }
//
//     /* Read until at least one callback is executed (the 3 replies will
//      * arrive in a single packet, causing all callbacks to be executed in
//      * a single pass). */
//     while(__test_callback_flags == 0) {
//         assert(siderBufferRead(c) == REDIS_OK);
//         siderProcessCallbacks(c);
//     }
//     test_cond(__test_callback_flags == 0x010203);
//     siderFree(c);
//
//     test("siderDisconnect executes pending callbacks with NULL reply: ");
//     c = __connect_nonblock();
//     siderSetDisconnectCallback(c,__test_callback,(void*)1);
//     siderCommandWithCallback(c,__test_reply_callback,(void*)2,"PING");
//     siderDisconnect(c);
//     test_cond(__test_callback_flags == 0x0201);
//     siderFree(c);
// }

#ifdef HIREDIS_TEST_ASYNC

#pragma GCC diagnostic ignored "-Woverlength-strings"   /* required on gcc 4.8.x due to assert statements */

struct event_base *base;

typedef struct TestState {
    siderOptions *options;
    int           checkpoint;
    int           resp3;
    int           disconnect;
} TestState;

/* Helper to disconnect and stop event loop */
void async_disconnect(siderAsyncContext *ac) {
    siderAsyncDisconnect(ac);
    event_base_loopbreak(base);
}

/* Testcase timeout, will trigger a failure */
void timeout_cb(int fd, short event, void *arg) {
    (void) fd; (void) event; (void) arg;
    printf("Timeout in async testing!\n");
    exit(1);
}

/* Unexpected call, will trigger a failure */
void unexpected_cb(siderAsyncContext *ac, void *r, void *privdata) {
    (void) ac; (void) r;
    printf("Unexpected call: %s\n",(char*)privdata);
    exit(1);
}

/* Helper function to publish a message via own client. */
void publish_msg(siderOptions *options, const char* channel, const char* msg) {
    siderContext *c = siderConnectWithOptions(options);
    assert(c != NULL);
    siderReply *reply = siderCommand(c,"PUBLISH %s %s",channel,msg);
    assert(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    disconnect(c, 0);
}

/* Expect a reply of type INTEGER */
void integer_cb(siderAsyncContext *ac, void *r, void *privdata) {
    siderReply *reply = r;
    TestState *state = privdata;
    assert(reply != NULL && reply->type == REDIS_REPLY_INTEGER);
    state->checkpoint++;
    if (state->disconnect) async_disconnect(ac);
}

/* Subscribe callback for test_pubsub_handling and test_pubsub_handling_resp3:
 * - a published message triggers an unsubscribe
 * - a command is sent before the unsubscribe response is received. */
void subscribe_cb(siderAsyncContext *ac, void *r, void *privdata) {
    siderReply *reply = r;
    TestState *state = privdata;

    assert(reply != NULL &&
           reply->type == (state->resp3 ? REDIS_REPLY_PUSH : REDIS_REPLY_ARRAY) &&
           reply->elements == 3);

    if (strcmp(reply->element[0]->str,"subscribe") == 0) {
        assert(strcmp(reply->element[1]->str,"mychannel") == 0 &&
               reply->element[2]->str == NULL);
        publish_msg(state->options,"mychannel","Hello!");
    } else if (strcmp(reply->element[0]->str,"message") == 0) {
        assert(strcmp(reply->element[1]->str,"mychannel") == 0 &&
               strcmp(reply->element[2]->str,"Hello!") == 0);
        state->checkpoint++;

        /* Unsubscribe after receiving the published message. Send unsubscribe
         * which should call the callback registered during subscribe */
        siderAsyncCommand(ac,unexpected_cb,
                          (void*)"unsubscribe should call subscribe_cb()",
                          "unsubscribe");
        /* Send a regular command after unsubscribing, then disconnect */
        state->disconnect = 1;
        siderAsyncCommand(ac,integer_cb,state,"LPUSH mylist foo");

    } else if (strcmp(reply->element[0]->str,"unsubscribe") == 0) {
        assert(strcmp(reply->element[1]->str,"mychannel") == 0 &&
               reply->element[2]->str == NULL);
    } else {
        printf("Unexpected pubsub command: %s\n", reply->element[0]->str);
        exit(1);
    }
}

/* Expect a reply of type ARRAY */
void array_cb(siderAsyncContext *ac, void *r, void *privdata) {
    siderReply *reply = r;
    TestState *state = privdata;
    assert(reply != NULL && reply->type == REDIS_REPLY_ARRAY);
    state->checkpoint++;
    if (state->disconnect) async_disconnect(ac);
}

/* Expect a NULL reply */
void null_cb(siderAsyncContext *ac, void *r, void *privdata) {
    (void) ac;
    assert(r == NULL);
    TestState *state = privdata;
    state->checkpoint++;
}

static void test_pubsub_handling(struct config config) {
    test("Subscribe, handle published message and unsubscribe: ");
    /* Setup event dispatcher with a testcase timeout */
    base = event_base_new();
    struct event *timeout = evtimer_new(base, timeout_cb, NULL);
    assert(timeout != NULL);

    evtimer_assign(timeout,base,timeout_cb,NULL);
    struct timeval timeout_tv = {.tv_sec = 10};
    evtimer_add(timeout, &timeout_tv);

    /* Connect */
    siderOptions options = get_sider_tcp_options(config);
    siderAsyncContext *ac = siderAsyncConnectWithOptions(&options);
    assert(ac != NULL && ac->err == 0);
    siderLibeventAttach(ac,base);

    /* Start subscribe */
    TestState state = {.options = &options};
    siderAsyncCommand(ac,subscribe_cb,&state,"subscribe mychannel");

    /* Make sure non-subscribe commands are handled */
    siderAsyncCommand(ac,array_cb,&state,"PING");

    /* Start event dispatching loop */
    test_cond(event_base_dispatch(base) == 0);
    event_free(timeout);
    event_base_free(base);

    /* Verify test checkpoints */
    assert(state.checkpoint == 3);
}

/* Unexpected push message, will trigger a failure */
void unexpected_push_cb(siderAsyncContext *ac, void *r) {
    (void) ac; (void) r;
    printf("Unexpected call to the PUSH callback!\n");
    exit(1);
}

static void test_pubsub_handling_resp3(struct config config) {
    test("Subscribe, handle published message and unsubscribe using RESP3: ");
    /* Setup event dispatcher with a testcase timeout */
    base = event_base_new();
    struct event *timeout = evtimer_new(base, timeout_cb, NULL);
    assert(timeout != NULL);

    evtimer_assign(timeout,base,timeout_cb,NULL);
    struct timeval timeout_tv = {.tv_sec = 10};
    evtimer_add(timeout, &timeout_tv);

    /* Connect */
    siderOptions options = get_sider_tcp_options(config);
    siderAsyncContext *ac = siderAsyncConnectWithOptions(&options);
    assert(ac != NULL && ac->err == 0);
    siderLibeventAttach(ac,base);

    /* Not expecting any push messages in this test */
    siderAsyncSetPushCallback(ac, unexpected_push_cb);

    /* Switch protocol */
    siderAsyncCommand(ac,NULL,NULL,"HELLO 3");

    /* Start subscribe */
    TestState state = {.options = &options, .resp3 = 1};
    siderAsyncCommand(ac,subscribe_cb,&state,"subscribe mychannel");

    /* Make sure non-subscribe commands are handled in RESP3 */
    siderAsyncCommand(ac,integer_cb,&state,"LPUSH mylist foo");
    siderAsyncCommand(ac,integer_cb,&state,"LPUSH mylist foo");
    siderAsyncCommand(ac,integer_cb,&state,"LPUSH mylist foo");
    /* Handle an array with 3 elements as a non-subscribe command */
    siderAsyncCommand(ac,array_cb,&state,"LRANGE mylist 0 2");

    /* Start event dispatching loop */
    test_cond(event_base_dispatch(base) == 0);
    event_free(timeout);
    event_base_free(base);

    /* Verify test checkpoints */
    assert(state.checkpoint == 6);
}

/* Subscribe callback for test_command_timeout_during_pubsub:
 * - a subscribe response triggers a published message
 * - the published message triggers a command that times out
 * - the command timeout triggers a disconnect */
void subscribe_with_timeout_cb(siderAsyncContext *ac, void *r, void *privdata) {
    siderReply *reply = r;
    TestState *state = privdata;

    /* The non-clean disconnect should trigger the
     * subscription callback with a NULL reply. */
    if (reply == NULL) {
        state->checkpoint++;
        event_base_loopbreak(base);
        return;
    }

    assert(reply->type == (state->resp3 ? REDIS_REPLY_PUSH : REDIS_REPLY_ARRAY) &&
           reply->elements == 3);

    if (strcmp(reply->element[0]->str,"subscribe") == 0) {
        assert(strcmp(reply->element[1]->str,"mychannel") == 0 &&
               reply->element[2]->str == NULL);
        publish_msg(state->options,"mychannel","Hello!");
        state->checkpoint++;
    } else if (strcmp(reply->element[0]->str,"message") == 0) {
        assert(strcmp(reply->element[1]->str,"mychannel") == 0 &&
               strcmp(reply->element[2]->str,"Hello!") == 0);
        state->checkpoint++;

        /* Send a command that will trigger a timeout */
        siderAsyncCommand(ac,null_cb,state,"DEBUG SLEEP 3");
        siderAsyncCommand(ac,null_cb,state,"LPUSH mylist foo");
    } else {
        printf("Unexpected pubsub command: %s\n", reply->element[0]->str);
        exit(1);
    }
}

static void test_command_timeout_during_pubsub(struct config config) {
    test("Command timeout during Pub/Sub: ");
    /* Setup event dispatcher with a testcase timeout */
    base = event_base_new();
    struct event *timeout = evtimer_new(base,timeout_cb,NULL);
    assert(timeout != NULL);

    evtimer_assign(timeout,base,timeout_cb,NULL);
    struct timeval timeout_tv = {.tv_sec = 10};
    evtimer_add(timeout,&timeout_tv);

    /* Connect */
    siderOptions options = get_sider_tcp_options(config);
    siderAsyncContext *ac = siderAsyncConnectWithOptions(&options);
    assert(ac != NULL && ac->err == 0);
    siderLibeventAttach(ac,base);

    /* Configure a command timout */
    struct timeval command_timeout = {.tv_sec = 2};
    siderAsyncSetTimeout(ac,command_timeout);

    /* Not expecting any push messages in this test */
    siderAsyncSetPushCallback(ac,unexpected_push_cb);

    /* Switch protocol */
    siderAsyncCommand(ac,NULL,NULL,"HELLO 3");

    /* Start subscribe */
    TestState state = {.options = &options, .resp3 = 1};
    siderAsyncCommand(ac,subscribe_with_timeout_cb,&state,"subscribe mychannel");

    /* Start event dispatching loop */
    assert(event_base_dispatch(base) == 0);
    event_free(timeout);
    event_base_free(base);

    /* Verify test checkpoints */
    test_cond(state.checkpoint == 5);
}

/* Subscribe callback for test_pubsub_multiple_channels */
void subscribe_channel_a_cb(siderAsyncContext *ac, void *r, void *privdata) {
    siderReply *reply = r;
    TestState *state = privdata;

    assert(reply != NULL && reply->type == REDIS_REPLY_ARRAY &&
           reply->elements == 3);

    if (strcmp(reply->element[0]->str,"subscribe") == 0) {
        assert(strcmp(reply->element[1]->str,"A") == 0);
        publish_msg(state->options,"A","Hello!");
        state->checkpoint++;
    } else if (strcmp(reply->element[0]->str,"message") == 0) {
        assert(strcmp(reply->element[1]->str,"A") == 0 &&
               strcmp(reply->element[2]->str,"Hello!") == 0);
        state->checkpoint++;

        /* Unsubscribe to channels, including channel X & Z which we don't subscribe to */
        siderAsyncCommand(ac,unexpected_cb,
                          (void*)"unsubscribe should not call unexpected_cb()",
                          "unsubscribe B X A A Z");
        /* Unsubscribe to patterns, none which we subscribe to */
        siderAsyncCommand(ac,unexpected_cb,
                          (void*)"punsubscribe should not call unexpected_cb()",
                          "punsubscribe");
        /* Send a regular command after unsubscribing, then disconnect */
        state->disconnect = 1;
        siderAsyncCommand(ac,integer_cb,state,"LPUSH mylist foo");
    } else if (strcmp(reply->element[0]->str,"unsubscribe") == 0) {
        assert(strcmp(reply->element[1]->str,"A") == 0);
        state->checkpoint++;
    } else {
        printf("Unexpected pubsub command: %s\n", reply->element[0]->str);
        exit(1);
    }
}

/* Subscribe callback for test_pubsub_multiple_channels */
void subscribe_channel_b_cb(siderAsyncContext *ac, void *r, void *privdata) {
    siderReply *reply = r;
    TestState *state = privdata;
    (void)ac;

    assert(reply != NULL && reply->type == REDIS_REPLY_ARRAY &&
           reply->elements == 3);

    if (strcmp(reply->element[0]->str,"subscribe") == 0) {
        assert(strcmp(reply->element[1]->str,"B") == 0);
        state->checkpoint++;
    } else if (strcmp(reply->element[0]->str,"unsubscribe") == 0) {
        assert(strcmp(reply->element[1]->str,"B") == 0);
        state->checkpoint++;
    } else {
        printf("Unexpected pubsub command: %s\n", reply->element[0]->str);
        exit(1);
    }
}

/* Test handling of multiple channels
 * - subscribe to channel A and B
 * - a published message on A triggers an unsubscribe of channel B, X, A and Z
 *   where channel X and Z are not subscribed to.
 * - the published message also triggers an unsubscribe to patterns. Since no
 *   pattern is subscribed to the responded pattern element type is NIL.
 * - a command sent after unsubscribe triggers a disconnect */
static void test_pubsub_multiple_channels(struct config config) {
    test("Subscribe to multiple channels: ");
    /* Setup event dispatcher with a testcase timeout */
    base = event_base_new();
    struct event *timeout = evtimer_new(base,timeout_cb,NULL);
    assert(timeout != NULL);

    evtimer_assign(timeout,base,timeout_cb,NULL);
    struct timeval timeout_tv = {.tv_sec = 10};
    evtimer_add(timeout,&timeout_tv);

    /* Connect */
    siderOptions options = get_sider_tcp_options(config);
    siderAsyncContext *ac = siderAsyncConnectWithOptions(&options);
    assert(ac != NULL && ac->err == 0);
    siderLibeventAttach(ac,base);

    /* Not expecting any push messages in this test */
    siderAsyncSetPushCallback(ac,unexpected_push_cb);

    /* Start subscribing to two channels */
    TestState state = {.options = &options};
    siderAsyncCommand(ac,subscribe_channel_a_cb,&state,"subscribe A");
    siderAsyncCommand(ac,subscribe_channel_b_cb,&state,"subscribe B");

    /* Start event dispatching loop */
    assert(event_base_dispatch(base) == 0);
    event_free(timeout);
    event_base_free(base);

    /* Verify test checkpoints */
    test_cond(state.checkpoint == 6);
}

/* Command callback for test_monitor() */
void monitor_cb(siderAsyncContext *ac, void *r, void *privdata) {
    siderReply *reply = r;
    TestState *state = privdata;

    /* NULL reply is received when BYE triggers a disconnect. */
    if (reply == NULL) {
        event_base_loopbreak(base);
        return;
    }

    assert(reply != NULL && reply->type == REDIS_REPLY_STATUS);
    state->checkpoint++;

    if (state->checkpoint == 1) {
        /* Response from MONITOR */
        siderContext *c = siderConnectWithOptions(state->options);
        assert(c != NULL);
        siderReply *reply = siderCommand(c,"SET first 1");
        assert(reply->type == REDIS_REPLY_STATUS);
        freeReplyObject(reply);
        siderFree(c);
    } else if (state->checkpoint == 2) {
        /* Response for monitored command 'SET first 1' */
        assert(strstr(reply->str,"first") != NULL);
        siderContext *c = siderConnectWithOptions(state->options);
        assert(c != NULL);
        siderReply *reply = siderCommand(c,"SET second 2");
        assert(reply->type == REDIS_REPLY_STATUS);
        freeReplyObject(reply);
        siderFree(c);
    } else if (state->checkpoint == 3) {
        /* Response for monitored command 'SET second 2' */
        assert(strstr(reply->str,"second") != NULL);
        /* Send QUIT to disconnect */
        siderAsyncCommand(ac,NULL,NULL,"QUIT");
    }
}

/* Test handling of the monitor command
 * - sends MONITOR to enable monitoring.
 * - sends SET commands via separate clients to be monitored.
 * - sends QUIT to stop monitoring and disconnect. */
static void test_monitor(struct config config) {
    test("Enable monitoring: ");
    /* Setup event dispatcher with a testcase timeout */
    base = event_base_new();
    struct event *timeout = evtimer_new(base, timeout_cb, NULL);
    assert(timeout != NULL);

    evtimer_assign(timeout,base,timeout_cb,NULL);
    struct timeval timeout_tv = {.tv_sec = 10};
    evtimer_add(timeout, &timeout_tv);

    /* Connect */
    siderOptions options = get_sider_tcp_options(config);
    siderAsyncContext *ac = siderAsyncConnectWithOptions(&options);
    assert(ac != NULL && ac->err == 0);
    siderLibeventAttach(ac,base);

    /* Not expecting any push messages in this test */
    siderAsyncSetPushCallback(ac,unexpected_push_cb);

    /* Start monitor */
    TestState state = {.options = &options};
    siderAsyncCommand(ac,monitor_cb,&state,"monitor");

    /* Start event dispatching loop */
    test_cond(event_base_dispatch(base) == 0);
    event_free(timeout);
    event_base_free(base);

    /* Verify test checkpoints */
    assert(state.checkpoint == 3);
}
#endif /* HIREDIS_TEST_ASYNC */

/* tests for async api using polling adapter, requires no extra libraries*/

/* enum for the test cases, the callbacks have different logic based on them */
typedef enum astest_no
{
    ASTEST_CONNECT=0,
    ASTEST_CONN_TIMEOUT,
    ASTEST_PINGPONG,
    ASTEST_PINGPONG_TIMEOUT,
    ASTEST_ISSUE_931,
    ASTEST_ISSUE_931_PING
}astest_no;

/* a static context for the async tests */
struct _astest {
    siderAsyncContext *ac;
    astest_no testno;
    int counter;
    int connects;
    int connect_status;
    int disconnects;
    int pongs;
    int disconnect_status;
    int connected;
    int err;
    char errstr[256];
};
static struct _astest astest;

/* async callbacks */
static void asCleanup(void* data)
{
    struct _astest *t = (struct _astest *)data;
    t->ac = NULL;
}

static void commandCallback(struct siderAsyncContext *ac, void* _reply, void* _privdata);

static void connectCallback(siderAsyncContext *c, int status) {
    struct _astest *t = (struct _astest *)c->data;
    assert(t == &astest);
    assert(t->connects == 0);
    t->err = c->err;
    strcpy(t->errstr, c->errstr);
    t->connects++;
    t->connect_status = status;
    t->connected = status == REDIS_OK ? 1 : -1;

    if (t->testno == ASTEST_ISSUE_931) {
        /* disconnect again */
        siderAsyncDisconnect(c);
    }
    else if (t->testno == ASTEST_ISSUE_931_PING)
    {
        siderAsyncCommand(c, commandCallback, NULL, "PING");
    }
}
static void disconnectCallback(const siderAsyncContext *c, int status) {
    assert(c->data == (void*)&astest);
    assert(astest.disconnects == 0);
    astest.err = c->err;
    strcpy(astest.errstr, c->errstr);
    astest.disconnects++;
    astest.disconnect_status = status;
    astest.connected = 0;
}

static void commandCallback(struct siderAsyncContext *ac, void* _reply, void* _privdata)
{
    siderReply *reply = (siderReply*)_reply;
    struct _astest *t = (struct _astest *)ac->data;
    assert(t == &astest);
    (void)_privdata;
    t->err = ac->err;
    strcpy(t->errstr, ac->errstr);
    t->counter++;
    if (t->testno == ASTEST_PINGPONG ||t->testno == ASTEST_ISSUE_931_PING)
    {
        assert(reply != NULL && reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "PONG") == 0);
        t->pongs++;
        siderAsyncFree(ac);
    }
    if (t->testno == ASTEST_PINGPONG_TIMEOUT)
    {
        /* two ping pongs */
        assert(reply != NULL && reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "PONG") == 0);
        t->pongs++;
        if (t->counter == 1) {
            int status = siderAsyncCommand(ac, commandCallback, NULL, "PING");
            assert(status == REDIS_OK);
        } else {
            siderAsyncFree(ac);
        }
    }
}

static siderAsyncContext *do_aconnect(struct config config, astest_no testno)
{
    siderOptions options = {0};
    memset(&astest, 0, sizeof(astest));

    astest.testno = testno;
    astest.connect_status = astest.disconnect_status = -2;

    if (config.type == CONN_TCP) {
        options.type = REDIS_CONN_TCP;
        options.connect_timeout = &config.connect_timeout;
        REDIS_OPTIONS_SET_TCP(&options, config.tcp.host, config.tcp.port);
    } else if (config.type == CONN_SSL) {
        options.type = REDIS_CONN_TCP;
        options.connect_timeout = &config.connect_timeout;
        REDIS_OPTIONS_SET_TCP(&options, config.ssl.host, config.ssl.port);
    } else if (config.type == CONN_UNIX) {
        options.type = REDIS_CONN_UNIX;
        options.endpoint.unix_socket = config.unix_sock.path;
    } else if (config.type == CONN_FD) {
        options.type = REDIS_CONN_USERFD;
        /* Create a dummy connection just to get an fd to inherit */
        siderContext *dummy_ctx = siderConnectUnix(config.unix_sock.path);
        if (dummy_ctx) {
            siderFD fd = disconnect(dummy_ctx, 1);
            printf("Connecting to inherited fd %d\n", (int)fd);
            options.endpoint.fd = fd;
        }
    }
    siderAsyncContext *c = siderAsyncConnectWithOptions(&options);
    assert(c);
    astest.ac = c;
    c->data = &astest;
    c->dataCleanup = asCleanup;
    siderPollAttach(c);
    siderAsyncSetConnectCallbackNC(c, connectCallback);
    siderAsyncSetDisconnectCallback(c, disconnectCallback);
    return c;
}

static void as_printerr(void) {
    printf("Async err %d : %s\n", astest.err, astest.errstr);
}

#define ASASSERT(e) do { \
    if (!(e)) \
        as_printerr(); \
    assert(e); \
} while (0);

static void test_async_polling(struct config config) {
    int status;
    siderAsyncContext *c;
    struct config defaultconfig = config;

    test("Async connect: ");
    c = do_aconnect(config, ASTEST_CONNECT);
    assert(c);
    while(astest.connected == 0)
        siderPollTick(c, 0.1);
    assert(astest.connects == 1);
    ASASSERT(astest.connect_status == REDIS_OK);
    assert(astest.disconnects == 0);
    test_cond(astest.connected == 1);

    test("Async free after connect: ");
    assert(astest.ac != NULL);
    siderAsyncFree(c);
    assert(astest.disconnects == 1);
    assert(astest.ac == NULL);
    test_cond(astest.disconnect_status == REDIS_OK);

    if (config.type == CONN_TCP || config.type == CONN_SSL) {
        /* timeout can only be simulated with network */
        test("Async connect timeout: ");
        config.tcp.host = "192.168.254.254";  /* blackhole ip */
        config.connect_timeout.tv_usec = 100000;
        c = do_aconnect(config, ASTEST_CONN_TIMEOUT);
        assert(c);
        assert(c->err == 0);
        while(astest.connected == 0)
            siderPollTick(c, 0.1);
        assert(astest.connected == -1);
        /*
         * freeing should not be done, clearing should have happened.
         *siderAsyncFree(c);
         */
        assert(astest.ac == NULL);
        test_cond(astest.connect_status == REDIS_ERR);
        config = defaultconfig;
    }

    /* Test a ping/pong after connection */
    test("Async PING/PONG: ");
    c = do_aconnect(config, ASTEST_PINGPONG);
    while(astest.connected == 0)
        siderPollTick(c, 0.1);
    status = siderAsyncCommand(c, commandCallback, NULL, "PING");
    assert(status == REDIS_OK);
    while(astest.ac)
        siderPollTick(c, 0.1);
    test_cond(astest.pongs == 1);

    /* Test a ping/pong after connection that didn't time out.
     * see https://github.com/sider/hisider/issues/945
     */
    if (config.type == CONN_TCP || config.type == CONN_SSL) {
        test("Async PING/PONG after connect timeout: ");
        config.connect_timeout.tv_usec = 10000; /* 10ms  */
        c = do_aconnect(config, ASTEST_PINGPONG_TIMEOUT);
        while(astest.connected == 0)
            siderPollTick(c, 0.1);
        /* sleep 0.1 s, allowing old timeout to arrive */
        millisleep(10);
        status = siderAsyncCommand(c, commandCallback, NULL, "PING");
        assert(status == REDIS_OK);
        while(astest.ac)
            siderPollTick(c, 0.1);
        test_cond(astest.pongs == 2);
        config = defaultconfig;
    }

    /* Test disconnect from an on_connect callback
     * see https://github.com/sider/hisider/issues/931
     */
    test("Disconnect from onConnected callback (Issue #931): ");
    c = do_aconnect(config, ASTEST_ISSUE_931);
    while(astest.disconnects == 0)
        siderPollTick(c, 0.1);
    assert(astest.connected == 0);
    assert(astest.connects == 1);
    test_cond(astest.disconnects == 1);

    /* Test ping/pong from an on_connect callback
     * see https://github.com/sider/hisider/issues/931
     */
    test("Ping/Pong from onConnected callback (Issue #931): ");
    c = do_aconnect(config, ASTEST_ISSUE_931_PING);
    /* connect callback issues ping, reponse callback destroys context */
    while(astest.ac)
        siderPollTick(c, 0.1);
    assert(astest.connected == 0);
    assert(astest.connects == 1);
    assert(astest.disconnects == 1);
    test_cond(astest.pongs == 1);
}
/* End of Async polling_adapter driven tests */

int main(int argc, char **argv) {
    struct config cfg = {
        .tcp = {
            .host = "127.0.0.1",
            .port = 6379
        },
        .unix_sock = {
            .path = "/tmp/sider.sock"
        }
    };
    int throughput = 1;
    int test_inherit_fd = 1;
    int skips_as_fails = 0;
    int test_unix_socket;

    /* Parse command line options. */
    argv++; argc--;
    while (argc) {
        if (argc >= 2 && !strcmp(argv[0],"-h")) {
            argv++; argc--;
            cfg.tcp.host = argv[0];
        } else if (argc >= 2 && !strcmp(argv[0],"-p")) {
            argv++; argc--;
            cfg.tcp.port = atoi(argv[0]);
        } else if (argc >= 2 && !strcmp(argv[0],"-s")) {
            argv++; argc--;
            cfg.unix_sock.path = argv[0];
        } else if (argc >= 1 && !strcmp(argv[0],"--skip-throughput")) {
            throughput = 0;
        } else if (argc >= 1 && !strcmp(argv[0],"--skip-inherit-fd")) {
            test_inherit_fd = 0;
        } else if (argc >= 1 && !strcmp(argv[0],"--skips-as-fails")) {
            skips_as_fails = 1;
#ifdef HIREDIS_TEST_SSL
        } else if (argc >= 2 && !strcmp(argv[0],"--ssl-port")) {
            argv++; argc--;
            cfg.ssl.port = atoi(argv[0]);
        } else if (argc >= 2 && !strcmp(argv[0],"--ssl-host")) {
            argv++; argc--;
            cfg.ssl.host = argv[0];
        } else if (argc >= 2 && !strcmp(argv[0],"--ssl-ca-cert")) {
            argv++; argc--;
            cfg.ssl.ca_cert  = argv[0];
        } else if (argc >= 2 && !strcmp(argv[0],"--ssl-cert")) {
            argv++; argc--;
            cfg.ssl.cert = argv[0];
        } else if (argc >= 2 && !strcmp(argv[0],"--ssl-key")) {
            argv++; argc--;
            cfg.ssl.key = argv[0];
#endif
        } else {
            fprintf(stderr, "Invalid argument: %s\n", argv[0]);
            exit(1);
        }
        argv++; argc--;
    }

#ifndef _WIN32
    /* Ignore broken pipe signal (for I/O error tests). */
    signal(SIGPIPE, SIG_IGN);

    test_unix_socket = access(cfg.unix_sock.path, F_OK) == 0;

#else
    /* Unix sockets don't exist in Windows */
    test_unix_socket = 0;
#endif

    test_allocator_injection();

    test_format_commands();
    test_reply_reader();
    test_blocking_connection_errors();
    test_free_null();

    printf("\nTesting against TCP connection (%s:%d):\n", cfg.tcp.host, cfg.tcp.port);
    cfg.type = CONN_TCP;
    test_blocking_connection(cfg);
    test_blocking_connection_timeouts(cfg);
    test_blocking_io_errors(cfg);
    test_invalid_timeout_errors(cfg);
    test_append_formatted_commands(cfg);
    test_tcp_options(cfg);
    if (throughput) test_throughput(cfg);

    printf("\nTesting against Unix socket connection (%s): ", cfg.unix_sock.path);
    if (test_unix_socket) {
        printf("\n");
        cfg.type = CONN_UNIX;
        test_blocking_connection(cfg);
        test_blocking_connection_timeouts(cfg);
        test_blocking_io_errors(cfg);
        test_invalid_timeout_errors(cfg);
        if (throughput) test_throughput(cfg);
    } else {
        test_skipped();
    }

#ifdef HIREDIS_TEST_SSL
    if (cfg.ssl.port && cfg.ssl.host) {

        siderInitOpenSSL();
        _ssl_ctx = siderCreateSSLContext(cfg.ssl.ca_cert, NULL, cfg.ssl.cert, cfg.ssl.key, NULL, NULL);
        assert(_ssl_ctx != NULL);

        printf("\nTesting against SSL connection (%s:%d):\n", cfg.ssl.host, cfg.ssl.port);
        cfg.type = CONN_SSL;

        test_blocking_connection(cfg);
        test_blocking_connection_timeouts(cfg);
        test_blocking_io_errors(cfg);
        test_invalid_timeout_errors(cfg);
        test_append_formatted_commands(cfg);
        if (throughput) test_throughput(cfg);

        siderFreeSSLContext(_ssl_ctx);
        _ssl_ctx = NULL;
    }
#endif

#ifdef HIREDIS_TEST_ASYNC
    cfg.type = CONN_TCP;
    printf("\nTesting asynchronous API against TCP connection (%s:%d):\n", cfg.tcp.host, cfg.tcp.port);
    cfg.type = CONN_TCP;

    int major;
    siderContext *c = do_connect(cfg);
    get_sider_version(c, &major, NULL);
    disconnect(c, 0);

    test_pubsub_handling(cfg);
    test_pubsub_multiple_channels(cfg);
    test_monitor(cfg);
    if (major >= 6) {
        test_pubsub_handling_resp3(cfg);
        test_command_timeout_during_pubsub(cfg);
    }
#endif /* HIREDIS_TEST_ASYNC */

    cfg.type = CONN_TCP;
    printf("\nTesting asynchronous API using polling_adapter TCP (%s:%d):\n", cfg.tcp.host, cfg.tcp.port);
    test_async_polling(cfg);
    if (test_unix_socket) {
        cfg.type = CONN_UNIX;
        printf("\nTesting asynchronous API using polling_adapter UNIX (%s):\n", cfg.unix_sock.path);
        test_async_polling(cfg);
    }

    if (test_inherit_fd) {
        printf("\nTesting against inherited fd (%s): ", cfg.unix_sock.path);
        if (test_unix_socket) {
            printf("\n");
            cfg.type = CONN_FD;
            test_blocking_connection(cfg);
        } else {
            test_skipped();
        }
    }

    if (fails || (skips_as_fails && skips)) {
        printf("*** %d TESTS FAILED ***\n", fails);
        if (skips) {
            printf("*** %d TESTS SKIPPED ***\n", skips);
        }
        return 1;
    }

    printf("ALL TESTS PASSED (%d skipped)\n", skips);
    return 0;
}
