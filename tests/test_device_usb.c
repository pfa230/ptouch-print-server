/* Production USB selection and I/O boundary with fake enumeration only.
 * No USB hardware, string descriptors, serial probes, or control transfers. */
#include <assert.h>
#include <arpa/inet.h>
#include <unistd.h>

#define libusb_init fake_usb_init
#define libusb_exit fake_usb_exit
#define libusb_get_device_list fake_usb_get_device_list
#define libusb_free_device_list fake_usb_free_device_list
#define libusb_get_device_descriptor fake_usb_get_device_descriptor
#define libusb_open fake_usb_open
#define libusb_close fake_usb_close
#define libusb_kernel_driver_active fake_usb_kernel_driver_active
#define libusb_detach_kernel_driver fake_usb_detach_kernel_driver
#define libusb_attach_kernel_driver fake_usb_attach_kernel_driver
#define libusb_claim_interface fake_usb_claim_interface
#define libusb_release_interface fake_usb_release_interface
#define libusb_bulk_transfer fake_usb_bulk_transfer
#define papplDeviceGetData tracked_device_data
#include "../src/device_usb.c"
#undef papplDeviceGetData
extern void *papplDeviceGetData(pappl_device_t *device);

static const char *uri = "ptouch://Brother/PT-2730";
static struct { uint16_t vid, pid; int node; } devices[4];
static libusb_device *device_list[5];
static ssize_t num_devices;
static int contexts, init_calls, opens, claims, closes, transfers, data_reads;
static int selected_node, claim_result, enumeration_error;
static bool detached;
static int listed, errors;
static char last_error[256];

int fake_usb_init(libusb_context **ctx)
{
    contexts++; init_calls++;
    *ctx = (libusb_context *)&contexts;
    return 0;
}
void fake_usb_exit(libusb_context *ctx) { assert(ctx); contexts--; }
ssize_t fake_usb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
    assert(ctx);
    if (enumeration_error) return -1;
    for (ssize_t i = 0; i < num_devices; i++) device_list[i] = (libusb_device *)&devices[i];
    device_list[num_devices] = NULL;
    *list = device_list;
    return num_devices;
}
void fake_usb_free_device_list(libusb_device **list, int unref)
{ assert(list == device_list && unref == 1); }
int fake_usb_get_device_descriptor(libusb_device *device, struct libusb_device_descriptor *d)
{
    for (ssize_t i = 0; i < num_devices; i++) if (device == (libusb_device *)&devices[i]) {
        memset(d, 0, sizeof *d); d->idVendor = devices[i].vid; d->idProduct = devices[i].pid;
        return 0;
    }
    assert(false); return -1;
}
int fake_usb_open(libusb_device *device, libusb_device_handle **handle)
{
    for (ssize_t i = 0; i < num_devices; i++) if (device == (libusb_device *)&devices[i]) {
        opens++; selected_node = devices[i].node;
        *handle = (libusb_device_handle *)device;
        return 0;
    }
    assert(false); return -1;
}
void fake_usb_close(libusb_device_handle *h) { assert(h); closes++; }
int fake_usb_kernel_driver_active(libusb_device_handle *h, int interface)
{ assert(h && interface == 0); return 1; }
int fake_usb_detach_kernel_driver(libusb_device_handle *h, int interface)
{ assert(h && interface == 0); detached = true; return 0; }
int fake_usb_attach_kernel_driver(libusb_device_handle *h, int interface)
{ assert(h && interface == 0 && detached); detached = false; return 0; }
int fake_usb_claim_interface(libusb_device_handle *h, int interface)
{ assert(h && interface == 0); claims++; return claim_result; }
int fake_usb_release_interface(libusb_device_handle *h, int interface)
{ assert(h && interface == 0); return 0; }
int fake_usb_bulk_transfer(libusb_device_handle *h, unsigned char endpoint,
                           unsigned char *data, int size, int *transferred, unsigned int timeout)
{
    assert(h && size > 0);
    assert(endpoint == PT_EP_IN || endpoint == PT_EP_OUT);
    assert(timeout == PT_USB_TIMEOUT_MS || timeout == PT_USB_STATUS_TIMEOUT_MS);
    transfers++;
    if (endpoint == PT_EP_IN) memset(data, 0, (size_t)size);
    *transferred = size;
    return 0;
}
void *tracked_device_data(pappl_device_t *device)
{ data_reads++; return papplDeviceGetData(device); }

static bool found(const char *info, const char *device_uri, const char *id, void *data)
{
    (void)data;
    assert(!strcmp(info, "PT-2730 (USB)") && !strcmp(device_uri, uri));
    assert(strstr(id, "MDL:PT-2730;")); listed++;
    return false;
}
static void error(const char *message, void *data)
{ (void)data; errors++; snprintf(last_error, sizeof last_error, "%s", message); }
static void inventory(int supported, bool unsupported)
{
    num_devices = supported + (unsupported ? 1 : 0);
    for (int i = 0; i < supported; i++) {
        devices[i].vid = 0x04f9; devices[i].pid = 0x2041; devices[i].node = i + 1;
    }
    if (unsupported) {
        devices[supported].vid = 0x04f9; devices[supported].pid = 0x2061;
        devices[supported].node = 99;
    }
    listed = errors = 0; last_error[0] = '\0';
}

int main(void)
{
    assert(pt_usb_supported_model(NULL) && pt_usb_supported_model(""));
    assert(pt_usb_supported_model("PT-2730"));
    assert(!pt_usb_supported_model("PT-P700") && !pt_usb_supported_model("typo"));
    assert(pt_usb_is_uri(uri) && !pt_usb_is_uri("ptouch://Brother/"));
    assert(!pt_usb_is_uri("ptouch://Brother/PT-P700") && !pt_usb_is_uri(NULL));
    pt_usb_register();

    for (int count = 0; count <= 2; count++) {
        inventory(count, false);
        assert(pt_usb_count(0x04f9, 0x2041) == count);
        assert(pt_usb_present(0x04f9, 0x2041) == (count == 1));
        assert(!pt_usb_list(found, NULL, error, NULL));
        assert(listed == (count == 1));
        if (count == 2) assert(errors && strstr(last_error, "ambiguous"));
        int previous_opens = opens;
        pappl_device_t *device = papplDeviceOpen(uri, "selection-test", error, NULL);
        assert((device != NULL) == (count == 1));
        if (device) {
            uint8_t bytes[32];
            assert(pt_usb_read_status(device, uri, bytes, sizeof bytes) == 32);
            assert(opens == previous_opens + 1 && selected_node == 1);
            papplDeviceClose(device);
        } else assert(opens == previous_opens);
        assert(contexts == 0 && !detached);
    }

    inventory(1, true);
    assert(!pt_usb_list(found, NULL, error, NULL));
    assert(listed == 1 && errors == 1 && strstr(last_error, "PT-P700"));
    int previous_opens = opens, previous_inits = init_calls;
    assert(!papplDeviceOpen("ptouch://Brother/PT-P700", "unsupported-test", error, NULL));
    assert(opens == previous_opens && init_calls == previous_inits);
    inventory(0, true);
    assert(!pt_usb_list(found, NULL, error, NULL) && !listed && errors);
    assert(!papplDeviceOpen(uri, "unsupported-only-test", error, NULL));
    assert(opens == previous_opens);

    /* Replug changes both the node and enumeration position, never the URI. */
    inventory(1, true);
    devices[0].pid = 0x2061; devices[0].node = 99;
    devices[1].pid = 0x2041; devices[1].node = 42;
    pappl_device_t *device = papplDeviceOpen(uri, "replug-test", error, NULL);
    assert(device && selected_node == 42); papplDeviceClose(device);
    inventory(0, false);
    assert(!papplDeviceOpen(uri, "unplug-test", error, NULL));
    inventory(1, false); devices[0].node = 87;
    device = papplDeviceOpen(uri, "replug-test", error, NULL);
    assert(device && selected_node == 87); papplDeviceClose(device);
    assert(contexts == 0 && !detached);

    claim_result = LIBUSB_ERROR_BUSY;
    assert(!papplDeviceOpen(uri, "unclaimable-test", error, NULL));
    assert(contexts == 0 && !detached && closes == opens);
    claim_result = 0;
    enumeration_error = 1;
    assert(pt_usb_count(0x04f9, 0x2041) == -1 && !pt_usb_present(0x04f9, 0x2041));
    assert(!papplDeviceOpen(uri, "enumeration-error-test", error, NULL));
    assert(contexts == 0);
    enumeration_error = 0;

    /* Real PAPPL socket connection while a Brother is in the fake inventory.
     * The guard must return before accessing opaque network connection data. */
    int listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    assert(bind(listener, (struct sockaddr *)&address, sizeof address) == 0);
    assert(listen(listener, 1) == 0);
    socklen_t length = sizeof address;
    assert(getsockname(listener, (struct sockaddr *)&address, &length) == 0);
    char socket_uri[80];
    snprintf(socket_uri, sizeof socket_uri, "socket://127.0.0.1:%u", ntohs(address.sin_port));
    device = papplDeviceOpen(socket_uri, "socket-guard-test", error, NULL); assert(device);
    int previous_reads = data_reads, previous_transfers = transfers;
    uint8_t packet[32];
    assert(pt_usb_present(0x04f9, 0x2041));
    assert(pt_usb_read_status(device, socket_uri, packet, sizeof packet) == -1);
    assert(pt_usb_read_status(device, NULL, packet, sizeof packet) == -1);
    assert(data_reads == previous_reads && transfers == previous_transfers);
    papplDeviceClose(device); close(listener);
    assert(contexts == 0 && !detached && claims == opens);
    puts("USB selection, model support, replug, and socket guard passed");
    return 0;
}
