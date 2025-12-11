/*
 *  Copyright (C) 2025 CS416 Rutgers CS
 *  Rutgers Tiny File System
 *
 *  File:  rufs.h
 *
 */

#ifndef _TFS_H
#define _TFS_H

#include <linux/limits.h>     // portable instead of <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

/*
 * Global FS constants
 */
#define MAGIC_NUM 0x5C3A
#define MAX_INUM  1024
#define MAX_DNUM  16384

// Simple file type tags for inode->type
#define TYPE_FILE 1
#define TYPE_DIR  2

#define root_ino 0

/*
 * Superblock
 */
struct superblock {
    uint32_t    magic_num;      /* magic number */
    uint16_t    max_inum;       /* maximum inode number */
    uint16_t    max_dnum;       /* maximum data block number */
    uint32_t    i_bitmap_blk;   /* start block of inode bitmap */
    uint32_t    d_bitmap_blk;   /* start block of data block bitmap */
    uint32_t    i_start_blk;    /* start block of inode region */
    uint32_t    d_start_blk;    /* start block of data block region */
};

/*
 * Inode
 */
struct inode {
    uint16_t    ino;            /* inode number */
    uint16_t    valid;          /* validity of the inode */
    uint32_t    size;           /* size of the file (bytes) */
    uint32_t    type;           /* TYPE_FILE or TYPE_DIR */
    uint32_t    link;           /* link count */
    int         direct_ptr[16]; /* direct pointer to data block */
    int         indirect_ptr[8];/* indirect pointer to data block (unused for this proj) */
    struct stat vstat;          /* inode stat (for getattr) */
};

/*
 * Directory entry
 */
struct dirent {
    uint16_t ino;               /* inode number of the directory entry */
    uint16_t valid;             /* validity of the directory entry */
    char     name[208];         /* name of the directory entry */
    uint16_t len;               /* length of name */
};

/*
 * bitmap operations
 */
typedef unsigned char* bitmap_t;

static inline void set_bitmap(bitmap_t b, int i) {
    b[i / 8] |= (1 << (i & 7));
}

static inline void unset_bitmap(bitmap_t b, int i) {
    b[i / 8] &= ~(1 << (i & 7));
}

static inline uint8_t get_bitmap(bitmap_t b, int i) {
    return (b[i / 8] & (1 << (i & 7))) ? 1 : 0;
}

#endif
