/* Production Bluetooth scheme I/O over a socketpair in place of RFCOMM.
 * No radio, pairing, or BlueZ: socket creation, connect, the connect wait, and
 * send are intercepted so the deadline and short-send paths run deterministically. */
#include <assert.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>

static int fake_socket(int domain, int type, int protocol);
static int fake_connect(int fd, const struct sockaddr *address, socklen_t length);
static int fake_poll(struct pollfd *fds, nfds_t n, int timeout);
static ssize_t fake_send(int fd, const void *buffer, size_t n, int flags);
#define socket fake_socket
#define connect fake_connect
#define poll fake_poll
#define send fake_send
#include "../src/device_bt.c"
#undef socket
#undef connect
#undef poll
#undef send

static int connect_wait_ms = -2, connect_ready, connect_sockets;
static uint8_t connected_address[6];
static size_t send_limit;
static unsigned sends;

/* An unconnected AF_UNIX socket stands in: SO_ERROR reads 0 and close works. */
static int fake_socket(int domain, int type, int protocol)
{
    assert(domain == AF_BLUETOOTH && protocol == BTPROTO_RFCOMM && (type & SOCK_NONBLOCK));
    connect_sockets++;
    return socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
}
static int fake_connect(int fd, const struct sockaddr *address, socklen_t length)
{
    (void)fd;
    const struct sockaddr_rc *rc = (const struct sockaddr_rc *)address;
    assert(length == sizeof *rc && rc->rc_family == AF_BLUETOOTH && rc->rc_channel == PT_BT_CHANNEL);
    for (int i = 0; i < 6; i++) connected_address[i] = rc->rc_bdaddr.b[5 - i];
    errno = EINPROGRESS;
    return -1;
}
/* The connect wait reports its deadline; data waits use the real poll. */
static int fake_poll(struct pollfd *fds, nfds_t n, int timeout)
{
    if (n == 1 && fds[0].events == POLLOUT) {
        connect_wait_ms = timeout;
        fds[0].revents = connect_ready ? POLLOUT : 0;
        return connect_ready;
    }
    return poll(fds, n, timeout);
}
static ssize_t fake_send(int fd, const void *buffer, size_t n, int flags)
{
    assert(flags & MSG_NOSIGNAL);
    sends++;
    return send(fd, buffer, send_limit && n > send_limit ? send_limit : n, flags);
}

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

    /* A malformed URI never creates a socket. An unanswered connect waits the
     * 10 s deadline, not the kernel's 30 s, then fails; a ready one opens. */
    pt_bt_register();
    assert(!papplDeviceOpen("ptouch-bt://Brother/PT-P710BT/zz", "bt-test", NULL, NULL));
    assert(connect_sockets == 0);
    const uint8_t expected[6] = {0xA4, 0xC1, 0x38, 0xAB, 0xCD, 0xEF};
    assert(!papplDeviceOpen(URI, "bt-test", NULL, NULL));
    assert(connect_sockets == 1 && connect_wait_ms == PT_BT_CONNECT_TIMEOUT_MS);
    assert(!memcmp(connected_address, expected, 6));
    connect_ready = 1;
    pappl_device_t *connected = papplDeviceOpen(URI, "bt-test", NULL, NULL); assert(connected);
    pt_bt_conn *conn = papplDeviceGetData(connected);
    assert(!(fcntl(conn->fd, F_GETFL) & O_NONBLOCK));
    papplDeviceClose(connected);
    struct timespec start;

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

    /* Short sends continue until every byte is written, in order. */
    unsigned char block[1000], received[sizeof block];
    for (unsigned i = 0; i < sizeof block; i++) block[i] = (unsigned char)(i * 7);
    send_limit = 7; sends = 0;
    assert(pt_bt_write(device, block, sizeof block) == (ssize_t)sizeof block);
    assert(sends == (sizeof block + 6) / 7);
    send_limit = 0;
    size_t used = 0;
    while (used < sizeof received) {
        ssize_t n = read(peer, received + used, sizeof received - used); assert(n > 0);
        used += (size_t)n;
    }
    assert(!memcmp(block, received, sizeof block));
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
