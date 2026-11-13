#ifndef KERNEL_MONITOR_FS_H
#define KERNEL_MONITOR_FS_H

/* The file commands of the bm> prompt (SYS-19): they work on the SD card through fat.h, from a
 * current folder. Each returns 0, or 1 after it said what went wrong (a line stops there). */
int mfs_ls(int argc, char **argv);
int mfs_cd(int argc, char **argv);
int mfs_pwd(int argc, char **argv);
int mfs_cat(int argc, char **argv);
int mfs_cp(int argc, char **argv);
int mfs_rm(int argc, char **argv);
int mfs_mkdir(int argc, char **argv);

/* The path a word stands for from a folder: "." and ".." resolved, no trailing '/'
 * (the root is "/"). 0, or -1 if it does not fit. */
int mfs_resolve(const char *base, const char *arg, char *out, unsigned cap);

/* The path of a word from the current folder, as the commands read it. 0, or -1 if it does not fit. */
int mfs_path(const char *word, char *out, unsigned cap);

#endif
