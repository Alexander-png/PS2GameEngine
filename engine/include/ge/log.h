/* ge/log.h - in-RAM log. Lines are collected without touching any device (so timing runs are not disturbed) and written out
 * with ge_log_save() (HDD: logs/<run>.txt; PS2Deployer uploads the folder to the SMB share). */
#ifndef GE_LOG_H
#define GE_LOG_H
void ge_log_init(const char *app, int capacity);        /* run name = <app>-<timestamp>; capacity in bytes */
void ge_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* appends "[   12.345] text\n" (ms since start) */
int  ge_log_save(void);                                 /* write/overwrite the log file; 0 ok, <0 no writable storage / error */
const char *ge_log_name(void);                          /* file name relative to the pack root, e.g. logs/roomtest-20261017-184600.txt */
int  ge_log_truncated(void);
int  ge_log_size(void);                                 /* bytes collected in the current segment */
void ge_log_segment(const char *tag);                   /* save the current file and continue in logs/<run>-<tag>.txt (empty buffer) */
#endif
