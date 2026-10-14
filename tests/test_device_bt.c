/* Production Bluetooth scheme I/O over a socketpair in place of RFCOMM.
 * No radio, pairing, or BlueZ: only URI checks, the bounded connect's failure
 * path, and the stream read/write conventions the driver depends on. */
#include <assert.h>
#include <pthread.h>
#include "../src/device_bt.c"

#define URI "ptouch-bt://Brother/PT-P710BT/A4:C1:38:AB:CD:EF"

static int peer = -1;

/* Test scheme: production callbacks on one end of a socketpair. */
static bool pair_open(pappl_device_t *device, const char *uri, const char *name)
{
    (void)uri; (void)name;
    int fds[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    pt_bt_conn *c = calloc(1, sizeof *c); assert(c);
    c->fd = fds[0]; peer = fds[1];
    papplDeviceSetData(device, c);
    return true;
}

static long ms_since(const struct timespec *t0)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - t0->tv_sec) * 1000 + (now.tv_nsec - t0->tv_nsec) / 1000000;
}

static void sleep_ms(long ms)
{
    struct timespec delay = {0, ms * 1000000L};
    nanosleep(&delay, NULL);
}

static size_t chunk, delay_ms, total;
static unsigned char reply[32];

static void *send_reply(void *arg)
{
    (void)arg;
    sleep_ms((long)delay_ms);
    for (size_t sent = 0; sent < total; sent += chunk) {
        assert(write(peer, reply + sent, chunk) == (ssize_t)chunk);
        sleep_ms(5);
    }
    return NULL;
}

static ssize_t read_with(pappl_device_t *device, size_t piece, size_t wait, size_t bytes, unsigned char *out)
{
    chunk = piece; delay_ms = wait; total = bytes;
    pthread_t writer; assert(!pthread_create(&writer, NULL, send_reply, NULL));
    ssize_t n = pt_bt_read_status(device, URI, out, 32);
    pthread_join(writer, NULL);
    return n;
}

static size_t drained;
static void *drain(void *arg)
{
    (void)arg;
    char buffer[4096];
    ssize_t n;
    while ((n = read(peer, buffer, sizeof buffer)) > 0) drained += (size_t)n;
    return NULL;
}

int main(void)
{
    assert(pt_bt_is_uri(URI));
    assert(pt_bt_is_uri("ptouch-bt://Brother/PT-P710BT/a4:c1:38:ab:cd:ef"));
    const char *bad[] = {NULL, "", "ptouch-bt://Brother/PT-P710BT/", "ptouch-bt://Brother/PT-2730/A4:C1:38:AB:CD:EF",
                         "ptouch-bt://Brother/PT-P710BT/A4:C1:38:AB:CD:EF/", "ptouch-bt://Brother/PT-P710BT/A4:C1:38:AB:CD",
                         "ptouch://Brother/PT-P710BT", "socket://127.0.0.1:9100"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) assert(!pt_bt_is_uri(bad[i]));
    char uri[80];
    pt_bt_format_uri("A4:C1:38:AB:CD:EF", uri, sizeof uri);
    assert(!strcmp(uri, URI));

    /* A malformed URI never reaches a socket; an unreachable address fails
     * within the connect deadline (here no adapter exists at all). */
    pt_bt_register();
    assert(!papplDeviceOpen("ptouch-bt://Brother/PT-P710BT/zz", "bt-test", NULL, NULL));
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(!papplDeviceOpen(URI, "bt-test", NULL, NULL));
    assert(ms_since(&start) < PT_BT_CONNECT_TIMEOUT_MS + 1000);

    papplDeviceAddScheme("btpair", PAPPL_DEVTYPE_CUSTOM_LOCAL, NULL, pair_open, pt_bt_close,
                         NULL, pt_bt_write, NULL, NULL);
    pappl_device_t *device = papplDeviceOpen("btpair://test", "bt-test", NULL, NULL); assert(device);
    for (unsigned i = 0; i < sizeof reply; i++) reply[i] = (unsigned char)(i + 1);
    unsigned char got[32];

    /* A reply split across many frames is assembled by one status read. */
    memset(got, 0, sizeof got);
    assert(read_with(device, 2, 0, 32, got) == 32 && !memcmp(got, reply, 32));
    /* A delayed reply within the budget still arrives whole. */
    memset(got, 0, sizeof got);
    assert(read_with(device, 32, 100, 32, got) == 32 && !memcmp(got, reply, 32));
    /* Silence is "no data yet" after the 300 ms budget, not an error. */
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(read_with(device, 1, 0, 0, got) == 0);
    long waited = ms_since(&start);
    assert(waited >= PT_BT_STATUS_TIMEOUT_MS - 10 && waited < PT_BT_STATUS_TIMEOUT_MS + 500);
    /* A partial reply returns its count at timeout for the caller to continue. */
    assert(read_with(device, 10, 0, 10, got) == 10);
    /* The status guard rejects other URIs before touching connection data. */
    assert(pt_bt_read_status(device, "socket://127.0.0.1:9100", got, sizeof got) == -1);
    assert(pt_bt_read_status(device, NULL, got, sizeof got) == -1);

    /* Full write across short sends: a tiny send buffer forces partial writes. */
    int small = 1024;
    pt_bt_conn *c = papplDeviceGetData(device);
    assert(!setsockopt(c->fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof small));
    static unsigned char block[256 * 1024];
    drained = 0;
    pthread_t reader; assert(!pthread_create(&reader, NULL, drain, NULL));
    assert(pt_bt_write(device, block, sizeof block) == (ssize_t)sizeof block);
    shutdown(c->fd, SHUT_WR);
    pthread_join(reader, NULL);
    assert(drained == sizeof block);
    close(peer);
    papplDeviceClose(device);

    /* A closed link is a failure for reads and writes, never "no data". */
    device = papplDeviceOpen("btpair://closed", "bt-test", NULL, NULL); assert(device);
    close(peer);
    assert(pt_bt_read_status(device, URI, got, sizeof got) == -1);
    assert(pt_bt_write(device, reply, sizeof reply) == -1);
    papplDeviceClose(device);
    puts("Bluetooth device checks passed");
    return 0;
}
