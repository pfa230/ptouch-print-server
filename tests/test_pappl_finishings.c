/* Real IPP boundary tests, independent of Brother command policy. */
#include <assert.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pappl/pappl.h>

static pappl_system_t *fixture_system;
static pappl_printer_t *printer;
static http_t *http;
static int port;
static bool start(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d)
{ (void)j; (void)o; (void)d; return true; }
static bool page(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned p)
{ (void)j; (void)o; (void)d; (void)p; return true; }
static bool line(pappl_job_t *j, pappl_pr_options_t *o, pappl_device_t *d, unsigned y, const unsigned char *b)
{ (void)j; (void)o; (void)d; (void)y; (void)b; return true; }
static bool status(pappl_printer_t *p) { (void)p; return true; }
static ipp_t *collection(const char *name)
{
    ipp_t *col = ippNew();
    ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "finishing-template", NULL, name);
    return col;
}
static bool driver(pappl_system_t *s, const char *n, const char *u, const char *id,
                   pappl_pr_driver_data_t *d, ipp_t **attrs, void *v)
{
    (void)s; (void)u; (void)id; (void)v;
    strcpy(d->make_and_model, "Finishing bridge fixture"); d->kind = PAPPL_KIND_LABEL; d->ppm = 20;
    d->rstartjob_cb = start; d->rstartpage_cb = page; d->rwriteline_cb = line;
    d->rendpage_cb = page; d->rendjob_cb = start; d->status_cb = status;
    d->finishings = PAPPL_FINISHINGS_TRIM;
    d->color_supported = PAPPL_COLOR_MODE_BI_LEVEL | PAPPL_COLOR_MODE_MONOCHROME;
    d->color_default = PAPPL_COLOR_MODE_BI_LEVEL;
    d->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_BLACK_8;
    d->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;
    d->num_resolution = 1; d->x_resolution[0] = d->y_resolution[0] = 180;
    d->x_default = d->y_default = 180;
    d->num_source = 1; d->source[0] = "main-roll"; d->num_type = 1; d->type[0] = "labels";
    d->num_media = 3; d->media[0] = "custom_12x100mm_12x100mm";
    d->media[1] = "roll_min_12x5mm"; d->media[2] = "roll_max_12x300mm";
    strcpy(d->media_default.size_name, d->media[0]); strcpy(d->media_default.source, "main-roll");
    strcpy(d->media_default.type, "labels"); d->media_default.tracking = PAPPL_MEDIA_TRACKING_CONTINUOUS;
    d->media_default.size_width = 1200; d->media_default.size_length = 10000;
    d->media_ready[0] = d->media_default;
    if (!strcmp(n, "legacy")) return true;
    *attrs = ippNew();
    int enums[] = {3, 11, 60, 63};
    const char *names[] = {"trim", "trim-after-pages", "trim-after-job", "ptouch-chain"};
    ipp_t *cols[4];
    for (int i = 0; i < 4; i++) cols[i] = collection(names[i]);
    ippAddIntegers(*attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-supported", 4, enums);
    ippAddIntegers(*attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-ready", 4, enums);
    ippAddCollections(*attrs, IPP_TAG_PRINTER, "finishings-col-database", 4, (const ipp_t **)cols);
    ippAddCollections(*attrs, IPP_TAG_PRINTER, "finishings-col-ready", 4, (const ipp_t **)cols);
    ippAddStrings(*attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishing-template-supported", 4, NULL, names);
    ippAddString(*attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "finishings-col-supported", NULL, "finishing-template");
    ippAddInteger(*attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-default", 11);
    ippAddCollection(*attrs, IPP_TAG_PRINTER, "finishings-col-default", cols[0]);
    for (int i = 0; i < 4; i++) ippDelete(cols[i]);
    return true;
}
static void *run(void *s) { papplSystemRun(s); return NULL; }
static ipp_t *request(ipp_op_t op, const char *name)
{
    ipp_t *q = ippNewRequest(op); char uri[256];
    snprintf(uri, sizeof uri, "ipp://localhost:%d/ipp/print/%s", port, name);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "finishing-test");
    return q;
}
static ipp_t *submit(ipp_t *q, const char *name, bool success)
{
    char resource[128]; snprintf(resource, sizeof resource, "/ipp/print/%s", name);
    ipp_t *r = cupsDoRequest(http, q, resource); assert(r);
    fprintf(stderr, "%s: status=%s\n", name, ippErrorString(ippGetStatusCode(r)));
    assert(success ? ippGetStatusCode(r) <= IPP_STATUS_OK_EVENTS_COMPLETE : ippGetStatusCode(r) == IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
    return r;
}
static unsigned named_count(ipp_t *attrs, const char *name)
{
    unsigned count = 0;
    for (ipp_attribute_t *a = ippFirstAttribute(attrs); a; a = ippNextAttribute(attrs))
        if (ippGetName(a) && !strcmp(ippGetName(a), name)) count++;
    return count;
}
static void defaults(int value, const char *name)
{
    ipp_t *r = submit(request(IPP_OP_GET_PRINTER_ATTRIBUTES, "test"), "test", true);
    assert(named_count(r, "finishings-supported") == 1);
    assert(named_count(r, "finishings-col-database") == 1);
    assert(named_count(r, "finishings-default") == 1);
    assert(named_count(r, "finishings-col-default") == 1);
    assert(!ippFindAttribute(r, "finishing-col-database", IPP_TAG_ZERO));
    assert(ippGetInteger(ippFindAttribute(r, "finishings-default", IPP_TAG_ENUM), 0) == value);
    ipp_attribute_t *col = ippFindAttribute(r, "finishings-col-default", IPP_TAG_ZERO); assert(col);
    if (!name) assert(ippGetValueTag(col) == IPP_TAG_NOVALUE);
    else assert(!strcmp(ippGetString(ippFindAttribute(ippGetCollection(col, 0), "finishing-template", IPP_TAG_KEYWORD), 0, NULL), name));
    ippDelete(r);
}
static void job(const int *enums, int count, const char *template, bool no_value, bool success, const char *queue)
{
    ipp_t *q = request(IPP_OP_CREATE_JOB, queue);
    ippAddString(q, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-hold-until", NULL, "indefinite");
    if (count) ippAddIntegers(q, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings", count, enums);
    if (template) { ipp_t *col = collection(template); ippAddCollection(q, IPP_TAG_JOB, "finishings-col", col); ippDelete(col); }
    if (no_value) ippAddOutOfBand(q, IPP_TAG_JOB, IPP_TAG_NOVALUE, "finishings-col");
    ipp_t *r = submit(q, queue, success);
    if (success) {
        pappl_printer_t *p = !strcmp(queue, "test") ? printer : papplSystemFindPrinter(fixture_system, !strcmp(queue,"legacy") ? "/ipp/print/legacy" : "/ipp/print/test", 0, NULL);
        pappl_job_t *j = papplPrinterFindJob(p, ippGetInteger(ippFindAttribute(r, "job-id", IPP_TAG_INTEGER), 0)); assert(j);
        if (count) { ipp_attribute_t *a = papplJobGetAttribute(j, "finishings"); assert(ippGetCount(a) == count); for (int i=0;i<count;i++) assert(ippGetInteger(a,i)==enums[i]); }
        if (template) assert(!strcmp(ippGetString(ippFindAttribute(ippGetCollection(papplJobGetAttribute(j, "finishings-col"), 0), "finishing-template", IPP_TAG_KEYWORD), 0, NULL), template));
        pappl_pr_options_t *options = papplJobCreatePrintOptions(j, 0, false); assert(options);
        if (no_value) assert(options->finishings == PAPPL_FINISHINGS_NONE);
        papplJobDeletePrintOptions(options); papplJobCancel(j);
    }
    ippDelete(r);
}
static void update(const int *enums, int count, const char *name, bool no_value, bool success)
{
    ipp_t *q = request(IPP_OP_SET_PRINTER_ATTRIBUTES, "test");
    if (count) ippAddIntegers(q, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-default", count, enums);
    if (name) { ipp_t *col = collection(name); ippAddCollection(q, IPP_TAG_PRINTER, "finishings-col-default", col); ippDelete(col); }
    if (no_value) ippAddOutOfBand(q, IPP_TAG_PRINTER, IPP_TAG_NOVALUE, "finishings-col-default");
    ippDelete(submit(q, "test", success));
}
static void setter(const char *value, bool success)
{
    pappl_pr_driver_data_t data, before, after;
    papplPrinterGetDriverData(printer, &before); data = before;
    data.orient_default = IPP_ORIENT_LANDSCAPE;
    cups_option_t *options = NULL;
    int count = value ? cupsAddOption("finishings", value, 0, &options) : 0;
    assert(papplPrinterSetDriverDefaults(printer, &data, count, options) == success);
    cupsFreeOptions(count, options);
    papplPrinterGetDriverData(printer, &after);
    if (!success) assert(after.orient_default == before.orient_default);
}
static void roundtrip(const char *directory, int value, const char *name)
{
    char state[512]; snprintf(state, sizeof state, "%s/state", directory);
    assert(papplSystemSaveState(fixture_system, state));
    pappl_system_t *loaded = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE, "loaded", port+1, NULL, directory, "-", PAPPL_LOGLEVEL_ERROR, NULL, false); assert(loaded);
    pappl_pr_driver_t drivers[] = {{"preset", "Preset", NULL, NULL}, {"legacy", "Legacy", NULL, NULL}};
    papplSystemSetPrinterDrivers(loaded, 2, drivers, NULL, NULL, driver, NULL);
    assert(papplSystemLoadState(loaded, state));
    pappl_printer_t *p = papplSystemFindPrinter(loaded, "/ipp/print/test", 0, NULL); assert(p);
    ipp_t *attrs = papplPrinterGetDriverAttributes(p); assert(attrs);
    assert(ippGetInteger(ippFindAttribute(attrs, "finishings-default", IPP_TAG_ENUM),0) == value);
    ipp_attribute_t *col = ippFindAttribute(attrs, "finishings-col-default", IPP_TAG_ZERO); assert(col);
    if (!name) assert(ippGetValueTag(col) == IPP_TAG_NOVALUE);
    else assert(!strcmp(ippGetString(ippFindAttribute(ippGetCollection(col,0), "finishing-template", IPP_TAG_KEYWORD),0,NULL), name));
    assert(named_count(attrs,"finishings-default") == 1 && named_count(attrs,"finishings-col-default") == 1);
    ippDelete(attrs); papplSystemDelete(loaded); unlink(state);
}
static void copies(const char *format, int number, bool success)
{
    ipp_t *q = request(IPP_OP_VALIDATE_JOB, "test");
    if (format) ippAddString(q, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, format);
    ippAddInteger(q, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", number);
    ippDelete(submit(q, "test", success));
}
int main(void)
{
    char temp[] = "/tmp/pappl-finishings-XXXXXX"; assert(mkdtemp(temp));
    port = 22000 + getpid() % 15000;
    fixture_system = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE, "finishings-test", port, NULL, temp, "-", PAPPL_LOGLEVEL_ERROR, NULL, false); assert(fixture_system);
    pappl_pr_driver_t drivers[] = {{"preset", "Preset", NULL, NULL}, {"legacy", "Legacy", NULL, NULL}};
    papplSystemSetPrinterDrivers(fixture_system, 2, drivers, NULL, NULL, driver, NULL);
    printer = papplPrinterCreate(fixture_system, 0, "test", "preset", "MFG:Test;MDL:Fixture;", "socket://127.0.0.1:9"); assert(printer);
    assert(papplPrinterCreate(fixture_system, 0, "legacy", "legacy", "MFG:Test;MDL:Fixture;", "socket://127.0.0.1:9"));
    assert(papplSystemAddListeners(fixture_system, "localhost")); pthread_t server; assert(!pthread_create(&server, NULL, run, fixture_system));
    http = httpConnect2("localhost", port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_NEVER, 1, 3000, NULL); assert(http);
    defaults(11, "trim");
    const int one[] = {3, 11, 60, 63}, redundant[] = {60,63}, mixed[] = {3,60}, aliases[] = {11,60}, bad[] = {4};
    for (int i=0;i<4;i++) job(one+i, 1, NULL, false, true, "test");
    job(redundant,2,NULL,false,true,"test"); job(mixed,2,NULL,false,true,"test"); job(aliases,2,NULL,false,true,"test");
    const char *templates[] = {"trim", "trim-after-pages", "trim-after-job", "ptouch-chain"};
    for (int i=0;i<4;i++) job(NULL,0,templates[i],false,true,"test");
    job(NULL,0,NULL,true,true,"test"); job(NULL,0,NULL,false,true,"test");
    job(bad,1,NULL,false,false,"test"); job(NULL,0,"unknown",false,false,"test"); job(one+1,1,"trim",false,false,"test");
    update(NULL,0,"trim-after-job",false,true); defaults(63,"trim-after-job");
    job(NULL,0,NULL,false,true,"test"); update(one,1,NULL,false,true); defaults(3,NULL);
    update(NULL,0,NULL,true,true); defaults(3,NULL);
    update(NULL,0,"ptouch-chain",false,false); defaults(3,NULL);
    update(one+2,1,"trim-after-job",false,false); defaults(3,NULL);
    update(bad,1,NULL,false,false); defaults(3,NULL);
    update(one+1,1,"trim-after-pages",false,true); defaults(11,"trim");
    update(redundant,2,NULL,false,true); defaults(60,"trim-after-pages");
    setter("3",true); defaults(3,NULL); roundtrip(temp,3,NULL);
    setter("63",true); defaults(63,"trim-after-job"); roundtrip(temp,63,"trim-after-job");
    setter("60,63",true); defaults(60,"trim-after-pages");
    setter("4",false); defaults(60,"trim-after-pages");
    setter("ptouch-chain",false); defaults(60,"trim-after-pages");
    setter("3,999",false); defaults(60,"trim-after-pages");
    setter("60,",false); defaults(60,"trim-after-pages");
    setter(NULL,true); defaults(60,"trim-after-pages");
    // Attribute substitution never permits unsupported finishing choices.
    job(bad,1,NULL,false,false,"test");
    copies("image/pwg-raster",2,false); copies("image/urf",2,false);
    copies("image/pwg-raster",1,true); copies("image/png",999,true); copies(NULL,999,true);
    // Ordinary legacy drivers retain fixed-mask acceptance and capabilities.
    job(one+1,1,NULL,false,true,"legacy"); job(one+2,1,NULL,false,false,"legacy");
    job(NULL,0,"trim",false,true,"legacy"); job(NULL,0,"ptouch-chain",false,false,"legacy");
    httpClose(http); papplSystemShutdown(fixture_system); pthread_join(server,NULL); papplSystemDelete(fixture_system);
    rmdir(temp); return 0;
}
