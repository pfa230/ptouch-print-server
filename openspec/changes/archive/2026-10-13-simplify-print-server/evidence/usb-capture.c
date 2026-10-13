/* Test-only transparent recording; preserves libusb results and byte counts. */
#define _GNU_SOURCE
#include <libusb-1.0/libusb.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int libusb_bulk_transfer(libusb_device_handle *h, unsigned char endpoint,
                         unsigned char *bytes, int length, int *actual, unsigned int timeout)
{
    int (*real)(libusb_device_handle *,unsigned char,unsigned char *,int,int *,unsigned int);
    real=dlsym(RTLD_NEXT,"libusb_bulk_transfer");
    if (!real) abort();
    int rc=real(h,endpoint,bytes,length,actual,timeout);
    int count=actual?*actual:0;
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    pthread_mutex_lock(&lock);
    FILE *log=fopen("/evidence/usb-io.jsonl","a");
    if(log){
        fprintf(log,"{\"t\":%ld.%09ld,\"ep\":%u,\"rc\":%d,\"requested\":%d,\"actual\":%d,\"hex\":\"",now.tv_sec,now.tv_nsec,endpoint,rc,length,count);
        for(int i=0;i<count;i++) fprintf(log,"%02x",bytes[i]);
        fputs("\"}\n",log);fclose(log);
    }
    if(endpoint==0x02&&count>0){
        FILE *out=fopen("/evidence/usb-out.bin","ab");
        if(out){fwrite(bytes,1,count,out);fclose(out);}
    }
    pthread_mutex_unlock(&lock);
    return rc;
}
