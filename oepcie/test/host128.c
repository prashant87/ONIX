#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../liboepcie/oedevice.h"
#include "../liboepcie/oepcie.h"

int main()
{
    // Error string buffer
    const size_t elen = 100;
    char ebuf[elen];

    // Frames per block read
    const size_t frames_per_read = 100;

    // Generate context
    oe_ctx ctx = NULL;
    ctx = oe_create_ctx();
    if (!ctx) exit(EXIT_FAILURE);

    // Set stream paths
    const char *config_path = "/tmp/rat128_config";
    const char *sig_path = "/tmp/rat128_signal";
    const char *data_path = "/tmp/rat128_data";

    // Async streams are similar to pipes
    // NB: Only needed if host starts before firmware
    mkfifo(sig_path, 0666);
    mkfifo(data_path, 0666);

    oe_set_ctx_opt(ctx, OE_CONFIGSTREAMPATH, config_path, strlen(config_path) + 1);
    oe_set_ctx_opt(ctx, OE_SIGNALSTREAMPATH, sig_path, strlen(sig_path) + 1);
    oe_set_ctx_opt(ctx, OE_DATASTREAMPATH, data_path, strlen(data_path) + 1);

    // Initialize context and discover hardware
    assert(oe_init_ctx(ctx) == 0);

    // Examine device map
    oe_size_t num_devs = 0;
    size_t num_devs_sz = sizeof(num_devs);
    oe_get_ctx_opt(ctx, OE_NUMDEVICES, &num_devs, &num_devs_sz); 

    oe_device_t devices[num_devs];
    size_t devices_sz = sizeof(devices);
    oe_get_ctx_opt(ctx, OE_DEVICEMAP, devices, &devices_sz);

    printf("Found the following devices:\n");
    int dev_idx;
    for (dev_idx = 0; dev_idx < num_devs; dev_idx++) {

        char dev_str[80];
        oe_device(devices[dev_idx].id, dev_str, 80);

        printf("\t%d) ID: %d (%s), Offset: %u, Read size:%u\n",
               dev_idx,
               devices[dev_idx].id,
               dev_str,
               devices[dev_idx].read_offset,
               devices[dev_idx].read_size);
    }

    oe_size_t frame_size = 0;
    size_t frame_size_sz = sizeof(frame_size);
    oe_get_ctx_opt(ctx, OE_READFRAMESIZE, &frame_size, &frame_size_sz); 
    printf("Frame size: %u bytes\n", frame_size);

    // Start acquisition
    // Try to write to base clock freq, which is write only
    int rc = oe_write_reg(ctx, OE_PCIEMASTERDEVIDX, OE_PCIEMASTERDEV_BASECLKHZ, 10e6);
    assert(rc == OE_EREADONLY && "Succesful write to read-only register.");

    uint32_t base_hz;
    rc = oe_read_reg(ctx, OE_PCIEMASTERDEVIDX, OE_PCIEMASTERDEV_BASECLKHZ, &base_hz);
    if (rc) {
        oe_error(rc, ebuf, elen);
        printf("%s\n", ebuf);
    }
    assert(!rc && "Register read failure.");
    
    //  Calculate m and d to get 10kHz
    uint32_t fs_desired = 100;
    uint32_t m = 1;
    uint32_t d = (m * base_hz) / fs_desired;

    // Set clock divider to 10/100 to get 10kHz sample clock
    rc = oe_write_reg(ctx, OE_PCIEMASTERDEVIDX, OE_PCIEMASTERDEV_CLKM, m);
    if (rc) {
        oe_error(rc, ebuf, elen);
        printf("Error: %s\n", ebuf);
    }
    assert(!rc && "Register write failure.");

    rc = oe_write_reg(ctx, OE_PCIEMASTERDEVIDX, OE_PCIEMASTERDEV_CLKD, d);
    if (rc) {
        oe_error(rc, ebuf, elen);
        printf("Error: %s\n", ebuf);
    }
    assert(!rc && "Register write failure.");

    uint32_t fs_hz;
    rc = oe_read_reg(ctx, OE_PCIEMASTERDEVIDX, OE_PCIEMASTERDEV_FSCLKHZ, &fs_hz);
    if (rc) {
        oe_error(rc, ebuf, elen);
        printf("%s\n", ebuf);
    }
    assert(!rc && "Register read failure.");

    assert(fs_hz == fs_desired && "Sample rate set failed.");

    // Start acquisition
    rc = oe_write_reg(ctx, OE_PCIEMASTERDEVIDX, OE_PCIEMASTERDEV_RUNNING, 1);
    if (rc) {
        oe_error(rc, ebuf, elen);
        printf("%s\n", ebuf);
    }

    const size_t read_size = frame_size * frames_per_read;
    uint8_t buffer[frame_size * frames_per_read];

    while (oe_read(ctx, &buffer, read_size)  >= 0)  {

        printf("-- start read block --\n");

        int f;
        for (f = 0; f < frames_per_read; f++) {

            size_t frame_offset = f * frame_size;
            uint64_t sample = *(uint64_t *)(buffer + frame_offset);

            printf("Sample: %" PRIu64 "\n", sample);

            for (dev_idx = 0; dev_idx < num_devs; dev_idx++) {
                
                oe_device_t this_dev = devices[dev_idx];
                
                char dev_name[20] = {0};
                oe_device(this_dev.id, dev_name, 20);

                printf("\tDev: %d (%s)" PRIu64 "\n", dev_idx, dev_name);
                int16_t *lfp = (int16_t *)((uint8_t *)buffer + this_dev.read_offset);
                int i;
                printf("\tData: [");
                for (i = 0; i < 10; i++)
                    printf("%" PRId16 " ", *(lfp + i));
                printf("...]\n");
            }
        }
    }

    oe_destroy_ctx(ctx);

    return 0;
}