/* ge/io.h - platform file access. Names are pack-relative; the platform maps them to disc/HDD/host. */
#ifndef GE_IO_H
#define GE_IO_H
typedef struct ge_file ge_file_t;
void *ge_io_alloc(int size);                         /* 64-byte aligned buffer for DMA-able data */
void  ge_io_free(void *p);
void *ge_io_read_all(const char *name, int *size);   /* NULL on failure; free with ge_io_free */
ge_file_t *ge_io_open(const char *name);             /* NULL on failure */
int   ge_io_pread(ge_file_t *f, unsigned off, void *buf, int len);   /* bytes read or -1 */
void  ge_io_close(ge_file_t *f);
/* Storage selection: ge_io_init(NULL, NULL) = disc; ge_io_init("hdd0:+DEVGAME", "/current") mounts the HDD partition (PFS) and
 * reads/writes files below that folder. Returns 0 on success; on failure the disc stays selected and the result is < 0. */
int   ge_io_init(const char *hdd_partition, const char *hdd_dir);
const char *ge_io_source(void);                      /* "disc" or "hdd" */
int   ge_io_write(const char *name, const void *data, int len);   /* HDD only: creates folders, writes name.part then renames; <0 on error */
int   ge_io_exists(const char *name);                 /* file exists in the selected storage */
int   ge_io_timestamp(char out[24]);                 /* "YYYYMMDD-HHMMSS" from the console clock (or "t<ms>" if unavailable) */
unsigned ge_time_ms(void);                           /* monotonic milliseconds */
int   ge_io_size(ge_file_t *f);                      /* file size in bytes */
void  ge_io_shutdown(void);                          /* unmount the HDD partition (call before exiting) */
void  ge_io_sysinfo(char *out, int cap);             /* one line: BIOS ROMVER and, in HDD mode, disk size */                         
#endif
