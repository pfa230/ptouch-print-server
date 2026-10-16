/* Custom "ptouch-bt://" PAPPL device scheme over a kernel RFCOMM socket.
 * Pairing is a host step (bluetoothctl); this scheme only connects to the
 * configured address. Return conventions match the "ptouch://" USB scheme.
 * Replies are read only through pt_bt_read_status, so there is no read_cb. */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include "config.h"
#include "device_bt.h"

#define PT_BT_PREFIX             "ptouch-bt://Brother/PT-P710BT/"
#define PT_BT_CHANNEL            1
#define PT_BT_CONNECT_TIMEOUT_MS 10000
#define PT_BT_STATUS_TIMEOUT_MS  300

typedef struct { int fd; } pt_bt_conn;

static bool pt_bt_address_from_uri(const char *uri, uint8_t address[6])
{
    size_t n = strlen(PT_BT_PREFIX);
    return uri && !strncmp(uri, PT_BT_PREFIX, n) && pt_parse_bt_address(uri + n, address);
}

bool pt_bt_is_uri(const char *uri)
{
    uint8_t address[6];
    return pt_bt_address_from_uri(uri, address);
}

void pt_bt_format_uri(const char *address, char *buffer, size_t size)
{
    snprintf(buffer, size, PT_BT_PREFIX "%s", address);
}

static int pt_ms_left(const struct timespec *deadline)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (deadline->tv_sec - now.tv_sec) * 1000 + (deadline->tv_nsec - now.tv_nsec) / 1000000;
    return ms > 0 ? (int)ms : 0;
}

/* Non-blocking connect bounded by our deadline, replacing the kernel's 30 s
 * RFCOMM default; the connected socket is returned in blocking mode. */
static int pt_bt_connect(const uint8_t address[6])
{
    int fd = socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, BTPROTO_RFCOMM);
    if (fd < 0)
        return -1;
    struct sockaddr_rc peer = { .rc_family = AF_BLUETOOTH, .rc_channel = PT_BT_CHANNEL };
    for (int i = 0; i < 6; i++)
        peer.rc_bdaddr.b[i] = address[5 - i];  /* bdaddr_t is little-endian */
    struct pollfd p = { .fd = fd, .events = POLLOUT };
    int error = 0;
    socklen_t length = sizeof error;
    bool connected = connect(fd, (struct sockaddr *)&peer, sizeof peer) == 0 ||
        (errno == EINPROGRESS && poll(&p, 1, PT_BT_CONNECT_TIMEOUT_MS) == 1 &&
         getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0);
    if (connected && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK) == 0)
        return fd;
    close(fd);
    return -1;
}

/* Receive every requested byte within timeout_ms. Timeout yields the count so
 * far (0 = no data yet); EOF is a failure. */
static ssize_t pt_bt_recv(int fd, void *buffer, size_t bytes, int timeout_ms)
{
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
    size_t got = 0;
    while (got < bytes) {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        int ready = poll(&p, 1, pt_ms_left(&deadline));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return -1;
        if (ready == 0) break;
        ssize_t n = recv(fd, (char *)buffer + got, bytes - got, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static bool pt_bt_open(pappl_device_t *device, const char *device_uri, const char *name)
{
    (void)name;
    uint8_t address[6];
    if (!pt_bt_address_from_uri(device_uri, address)) {
        papplDeviceError(device, "Unsupported P-touch Bluetooth URI: %s", device_uri);
        return false;
    }
    int fd = pt_bt_connect(address);
    if (fd < 0) {
        papplDeviceError(device, "PT-P710BT at %s is not reachable over Bluetooth",
                         device_uri + strlen(PT_BT_PREFIX));
        return false;
    }
    pt_bt_conn *c = calloc(1, sizeof *c);
    if (!c) {
        close(fd);
        return false;
    }
    c->fd = fd;
    papplDeviceSetData(device, c);
    return true;
}

static void pt_bt_close(pappl_device_t *device)
{
    pt_bt_conn *c = papplDeviceGetData(device);
    if (!c)
        return;
    close(c->fd);
    free(c);
    papplDeviceSetData(device, NULL);
}

ssize_t pt_bt_read_status(pappl_device_t *device, const char *device_uri,
                          void *buffer, size_t bytes)
{
    if (!device || !pt_bt_is_uri(device_uri))
        return -1;
    pt_bt_conn *c = papplDeviceGetData(device);
    return c ? pt_bt_recv(c->fd, buffer, bytes, PT_BT_STATUS_TIMEOUT_MS) : -1;
}

static ssize_t pt_bt_write(pappl_device_t *device, const void *buffer, size_t bytes)
{
    pt_bt_conn *c = papplDeviceGetData(device);
    if (!c)
        return -1;
    const char *p = buffer;
    size_t sent = 0;
    while (sent < bytes) {  /* full-write: PAPPL won't retry the tail */
        ssize_t n = send(c->fd, p + sent, bytes - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return (ssize_t)bytes;
}

void pt_bt_register(void)
{
    papplDeviceAddScheme("ptouch-bt", PAPPL_DEVTYPE_CUSTOM_LOCAL,
                         NULL, pt_bt_open, pt_bt_close,
                         NULL, pt_bt_write, NULL, NULL);
}
