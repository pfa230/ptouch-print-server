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

/* Include the production diagnostic callbacks, with queue creation captured so
 * startup identity can be checked without listeners or printer commands. */
static pappl_printer_t *capture_printer_create(pappl_system_t *, int, const char *,
                                               const char *, const char *, const char *);
static bool no_listeners(pappl_system_t *, const char *);
#define papplPrinterCreate capture_printer_create
#define papplSystemAddListeners no_listeners
#define pt_driver_cb unused_driver_cb
#define pt_filter_png unused_filter_png
#define main pt_application_main
#include "../src/main.c"
#undef main
#undef papplPrinterCreate
#undef papplSystemAddListeners

bool unused_driver_cb(pappl_system_t *s, const char *name, const char *device_uri,
                      const char *id, pappl_pr_driver_data_t *data, ipp_t **attrs, void *cb)
{ (void)s; (void)name; (void)device_uri; (void)id; (void)data; (void)attrs; (void)cb; return false; }
bool unused_filter_png(pappl_job_t *job, pappl_device_t *device, void *data)
{ (void)job; (void)device; (void)data; return false; }
static char queue_id[100], queue_uri[100];
static pappl_printer_t *capture_printer_create(pappl_system_t *system, int printer_id,
                                               const char *printer_name, const char *driver_name,
                                               const char *id, const char *device_uri)
{
    (void)system; (void)printer_id; (void)printer_name; (void)driver_name;
    snprintf(queue_id, sizeof queue_id, "%s", id);
    snprintf(queue_uri, sizeof queue_uri, "%s", device_uri);
    return NULL;
}
static bool no_listeners(pappl_system_t *s, const char *name)
{ (void)s; (void)name; return true; }

static const pt_dev *selected;
static char uri[80];
static struct { uint16_t vid, pid; int node; } devices[4];
static libusb_device *device_list[5];
static ssize_t num_devices;
static int contexts, init_calls, opens, claims, closes, transfers, data_reads;
static int selected_node, claim_result, enumeration_error;
static bool detached;
static int listed, errors;
static char last_error[256];
static unsigned char last_output[32];
static int output_bytes;

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
    else {
        assert(size <= (int)sizeof last_output);
        memcpy(last_output, data, (size_t)size); output_bytes += size;
    }
    *transferred = size;
    return 0;
}
void *tracked_device_data(pappl_device_t *device)
{ data_reads++; return papplDeviceGetData(device); }

static bool found(const char *info, const char *device_uri, const char *id, void *data)
{
    (void)data;
    char expected_info[80], expected_id[100];
    snprintf(expected_info, sizeof expected_info, "%s (USB)", selected->name);
    pt_usb_format_id(selected, expected_id, sizeof expected_id);
    assert(!strcmp(info, expected_info) && !strcmp(device_uri, uri));
    assert(!strcmp(id, expected_id)); listed++;
    return false;
}
static void error(const char *message, void *data)
{ (void)data; errors++; snprintf(last_error, sizeof last_error, "%s", message); }
static void inventory(int supported, bool unsupported)
{
    num_devices = supported + (unsupported ? 1 : 0);
    for (int i = 0; i < supported; i++) {
        devices[i].vid = 0x04f9; devices[i].pid = selected->pid; devices[i].node = i + 1;
    }
    if (unsupported) {
        devices[supported].vid = 0x04f9; devices[supported].pid = 0x2061;
        devices[supported].node = 99;
    }
    listed = errors = 0; last_error[0] = '\0';
}

static void test_model(const char *name)
{
    setenv("PTOUCH_MODEL", name, 1);
    selected = pt_usb_supported_model(name);
    pt_usb_format_uri(selected, uri, sizeof uri);
    assert(pt_usb_supported_model(NULL) && pt_usb_supported_model(""));
    assert(pt_usb_supported_model("PT-2730") && pt_usb_supported_model("PT-P710BT"));
    assert(!pt_usb_supported_model("PT-P700") && !pt_usb_supported_model("typo"));
    assert(pt_usb_is_uri(uri) && !pt_usb_is_uri("ptouch://Brother/"));
    assert(!pt_usb_is_uri("ptouch://Brother/PT-P700") && !pt_usb_is_uri(NULL));
    pt_usb_register();

    for (int count = 0; count <= 2; count++) {
        inventory(count, false);
        assert(pt_usb_count(selected->vid, selected->pid) == count);
        assert(pt_usb_present(selected->vid, selected->pid) == (count == 1));
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
    devices[1].pid = selected->pid; devices[1].node = 42;
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
    assert(pt_usb_count(selected->vid, selected->pid) == -1 && !pt_usb_present(selected->vid, selected->pid));
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
    assert(pt_usb_present(selected->vid, selected->pid));
    assert(pt_usb_read_status(device, socket_uri, packet, sizeof packet) == -1);
    assert(pt_usb_read_status(device, NULL, packet, sizeof packet) == -1);
    assert(data_reads == previous_reads && transfers == previous_transfers);
    papplDeviceClose(device); close(listener);
    assert(contexts == 0 && !detached && claims == opens);
    /* Both supported models can coexist; absence never opens the other one. */
    inventory(1, false);
    num_devices = 2;
    devices[1].vid = 0x04f9;
    devices[1].pid = selected->pid == 0x2041 ? 0x20af : 0x2041;
    devices[1].node = 98;
    const pt_dev *other = pt_lookup_dev(devices[1].vid, devices[1].pid);
    assert(!pt_usb_list(found, NULL, error, NULL));
    assert(listed == 1 && errors == 1 && strstr(last_error, "attached but unselected"));
    assert(strstr(last_error, other->name) && strstr(last_error, "PTOUCH_MODEL="));
    device = papplDeviceOpen(uri, "both-models-test", error, NULL);
    assert(device && selected_node == 1);
    char actual_id[100], expected_id[100];
    pt_usb_format_id(selected, expected_id, sizeof expected_id);
    assert(!strcmp(pt_usb_id(device, actual_id, sizeof actual_id), expected_id));
    papplDeviceClose(device);
    devices[0] = devices[1]; num_devices = 1; listed = errors = 0;
    assert(!pt_usb_list(found, NULL, error, NULL) && listed == 0 && errors == 1);
    int prior_opens = opens, prior_output = output_bytes;
    assert(!papplDeviceOpen(uri, "other-only-test", error, NULL));
    smoke_saw_error = false;
    assert(run_smoke() == 2);
    assert(opens == prior_opens && output_bytes == prior_output);

    inventory(1, false);
    /* probe enumerates the selected model but does not open or write it. */
    assert(probe_cb(NULL, 0, NULL, 0, NULL, NULL) == 0);
    assert(opens == prior_opens && output_bytes == prior_output);
    smoke_saw_error = false;
    assert(run_smoke() == 0);
    assert(selected_node == 1 && output_bytes == prior_output + 2);
    assert(last_output[0] == 0x1b && last_output[1] == 0x40);
    assert(contexts == 0 && !detached);

    unsetenv("PTOUCH_DEVICE_URI");
    pappl_system_t *sys = system_cb(0, NULL, NULL); assert(sys);
    assert(!strcmp(queue_uri, uri) && !strcmp(queue_id, expected_id));
    papplSystemDelete(sys);
    setenv("PTOUCH_DEVICE_URI", "socket://127.0.0.1:9100", 1);
    sys = system_cb(0, NULL, NULL); assert(sys);
    assert(!strcmp(queue_uri, "socket://127.0.0.1:9100") && !strcmp(queue_id, expected_id));
    papplSystemDelete(sys);
    char other_uri[80]; pt_usb_format_uri(other, other_uri, sizeof other_uri);
    setenv("PTOUCH_DEVICE_URI", other_uri, 1);
    char *argv[] = { "ptouch-app", "smoke", NULL };
    prior_output = output_bytes; prior_opens = opens;
    assert(pt_application_main(2, argv) == EXIT_FAILURE);
    assert(output_bytes == prior_output && opens == prior_opens);
    unsetenv("PTOUCH_DEVICE_URI");
}

int main(void)
{
    unsetenv("PTOUCH_MODEL");
    assert(pt_usb_supported_model(NULL)->pid == 0x2041);
    assert(pt_usb_supported_model("")->pid == 0x2041);
    assert(!pt_usb_model_from_uri("ptouch://Brother/"));
    assert(!pt_usb_model_from_uri("ptouch://Brother/PT-P710BT/"));
    assert(!pt_usb_model_from_uri("ptouch://Brother/PT-P710BT?x=1"));
    assert(!pt_usb_model_from_uri("PT-P710BT"));
    assert(!pt_usb_model_from_uri(""));
    test_model("PT-2730");
    test_model("PT-P710BT");
    /* Invalid configuration is rejected equally for server and diagnostics,
     * before enumeration, opening, or command I/O. */
    int previous_inits = init_calls, previous_opens = opens, previous_output = output_bytes;
    const char *bad_models[] = { "PT-P700", "typo" };
    const char *commands[] = { "server", "probe", "smoke" };
    for (size_t i = 0; i < sizeof bad_models / sizeof bad_models[0]; i++) {
        setenv("PTOUCH_MODEL", bad_models[i], 1);
        for (size_t j = 0; j < sizeof commands / sizeof commands[0]; j++) {
            char *args[] = { "ptouch-app", (char *)commands[j], NULL };
            assert(pt_application_main(2, args) == EXIT_FAILURE);
        }
    }
    assert(init_calls == previous_inits && opens == previous_opens && output_bytes == previous_output);

    /* Bluetooth and polling settings fail before any USB access. */
    unsetenv("PTOUCH_MODEL");
    setenv("PTOUCH_BT_ADDRESS", "A4:C1:38:AB:CD:EF", 1);
    char *server[] = { "ptouch-app", "server", NULL };
    assert(pt_application_main(2, server) == EXIT_FAILURE);
    setenv("PTOUCH_MODEL", "PT-P710BT", 1);
    for (size_t j = 1; j < sizeof commands / sizeof commands[0]; j++) {
        char *args[] = { "ptouch-app", (char *)commands[j], NULL };
        assert(pt_application_main(2, args) == EXIT_FAILURE);
    }
    setenv("PTOUCH_BT_ADDRESS", "A4:C1:38:AB:CD", 1);
    assert(pt_application_main(2, server) == EXIT_FAILURE);
    setenv("PTOUCH_BT_ADDRESS", "A4:C1:38:AB:CD:EF", 1);
    setenv("PTOUCH_DEVICE_URI", "socket://127.0.0.1:9100", 1);
    assert(pt_application_main(2, server) == EXIT_FAILURE);
    unsetenv("PTOUCH_DEVICE_URI");
    const char *bad_polls[] = { "-1", "1.5", "soon" };
    for (size_t i = 0; i < sizeof bad_polls / sizeof bad_polls[0]; i++) {
        setenv("PTOUCH_IDLE_POLL_SECONDS", bad_polls[i], 1);
        assert(pt_application_main(2, server) == EXIT_FAILURE);
    }
    unsetenv("PTOUCH_IDLE_POLL_SECONDS");
    assert(init_calls == previous_inits && opens == previous_opens && output_bytes == previous_output);
    /* A configured address creates only the Bluetooth queue, even with the
     * same model attached over USB, and never enumerates USB. */
    selected = pt_lookup_name("PT-P710BT"); inventory(1, false);
    char expected_id[100]; pt_usb_format_id(selected, expected_id, sizeof expected_id);
    pappl_system_t *sys = system_cb(0, NULL, NULL); assert(sys);
    assert(!strcmp(queue_uri, "ptouch-bt://Brother/PT-P710BT/A4:C1:38:AB:CD:EF"));
    assert(!strcmp(queue_id, expected_id));
    assert(init_calls == previous_inits && opens == previous_opens && output_bytes == previous_output);
    papplSystemDelete(sys);
    unsetenv("PTOUCH_BT_ADDRESS");
    puts("Both USB models, production diagnostics, startup identity, replug, and socket guards passed");
    return 0;
}
