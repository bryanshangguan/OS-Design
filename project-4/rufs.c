/*
 *  Copyright (C) 2025 CS416 Rutgers CS
 *	Rutgers Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <time.h>   
#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

/* Global in-memory superblock (loaded in rufs_init) */
struct superblock sb;

// Declare your in-memory data structures here

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	
	// Step 2: Traverse inode bitmap to find an available slot

	// Step 3: Update inode bitmap and write to disk 

	return 0;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	
	// Step 2: Traverse data block bitmap to find an available slot

	// Step 3: Update data block bitmap and write to disk 

	return 0;
}

/* 
 * inode operations
 */

// Mock Version: **FOR PERSON B**
int readi(uint16_t ino, struct inode *inode) {

	// Step 1: Get the inode's on-disk block number
	// Step 2: Get offset of the inode in the inode on-disk block
	// Step 3: Read the block from disk and then copy into inode structure

	if (ino >= sb.max_inum) {
        return -1;
    }

    int inodes_per_block = BLOCK_SIZE / (int)sizeof(struct inode);
    int rel_block        = ino / inodes_per_block;              // which inode block
    int index_in_block   = ino % inodes_per_block;              // which inode within that block
    int blkno            = sb.i_start_blk + rel_block;

    char block_buf[BLOCK_SIZE];

    if (bio_read(blkno, block_buf) <= 0) {
        return -1;
    }

    struct inode *block_inodes = (struct inode *)block_buf;
    *inode = block_inodes[index_in_block];


	return 0;
}

// Mock Version: **FOR PERSON B**
int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	// Step 2: Get the offset in the block where this inode resides on disk
	// Step 3: Write inode to disk 

	if (ino >= sb.max_inum) {
        return -1;
    }

    int inodes_per_block = BLOCK_SIZE / (int)sizeof(struct inode);
    int rel_block        = ino / inodes_per_block;
    int index_in_block   = ino % inodes_per_block;
    int blkno            = sb.i_start_blk + rel_block;

    char block_buf[BLOCK_SIZE];

    // Read the existing block first (so we don't clobber other inodes)
    if (bio_read(blkno, block_buf) <= 0) {
        // if read fails, zero it out so we can still write just this inode
        memset(block_buf, 0, BLOCK_SIZE);
    }

    struct inode *block_inodes = (struct inode *)block_buf;
    block_inodes[index_in_block] = *inode;

    if (bio_write(blkno, block_buf) < 0) {
        return -1;
    }

	return 0;
}


/* 
 * directory operations
 */

// Kelvin's Part:
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	// Step 2: Get data block of current directory from inode
	// Step 3: Read directory's data block and check each directory entry.
	// If the name matches, then copy directory entry to dirent structure

	struct inode dir_inode;

    // 1: Read the directory inode
    if (readi(ino, &dir_inode) < 0) {
        return -ENOENT;
    }

    if (!dir_inode.valid || dir_inode.type != TYPE_DIR) {
        return -ENOTDIR;
    }

    char block_buf[BLOCK_SIZE];

    // 2: Iterate over the directory's direct data blocks
    for (int i = 0; i < 16; i++) {
        int blkno = dir_inode.direct_ptr[i];
        if (blkno == 0) {
            continue; // no block assigned here
        }

        if (bio_read(blkno, block_buf) <= 0) {
            continue;
        }

        int entries_per_block = BLOCK_SIZE / (int)sizeof(struct dirent);
        struct dirent *entries = (struct dirent *)block_buf;

        // 3: Scan entries in this block
        for (int j = 0; j < entries_per_block; j++) {
            if (!entries[j].valid)
                continue;

            if (entries[j].len == (uint16_t)name_len &&
                strncmp(entries[j].name, fname, name_len) == 0) {
                if (out) {
                    *out = entries[j];
                }
                return 0; // found
            }
        }
    }

    return -ENOENT;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}


/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	if (strcmp(path, "/") == 0) {
        return readi(root_ino, inode);
    }

    // Make a writable copy of the path for strtok_r
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX);
    tmp[PATH_MAX - 1] = '\0';

    uint16_t curr_ino = root_ino;
    struct dirent de;
    struct inode curr_inode;

    char *saveptr = NULL;
    char *token = strtok_r(tmp, "/", &saveptr);

    if (!token) {
        // path was "" or "/"
        return readi(root_ino, inode);
    }

    while (token != NULL) {
        size_t len = strlen(token);

        // Lookup token in the current directory
        int ret = dir_find(curr_ino, token, len, &de);
        if (ret < 0) {
            return ret; // ENOENT or ENOTDIR
        }

        curr_ino = de.ino;
        token = strtok_r(NULL, "/", &saveptr);
    }

    // Read final inode
    return readi(curr_ino, inode);
}

/* 
 * Make file system
 */

// Kelvin's Part:
int rufs_mkfs() {

	dev_init(diskfile_path);


    struct superblock newsb;
    memset(&newsb, 0, sizeof(newsb));

    newsb.magic_num = MAGIC_NUM;
    newsb.max_inum  = MAX_INUM;
    newsb.max_dnum  = MAX_DNUM;

    newsb.i_bitmap_blk = 1;
    newsb.d_bitmap_blk = 2;

    int inode_region_blocks =
        (MAX_INUM * (int)sizeof(struct inode) + BLOCK_SIZE - 1) / BLOCK_SIZE;

    newsb.i_start_blk = 3;
    newsb.d_start_blk = newsb.i_start_blk + inode_region_blocks;

    // 3. Write superblock to block 0
    char block_buf[BLOCK_SIZE];
    memset(block_buf, 0, BLOCK_SIZE);
    memcpy(block_buf, &newsb, sizeof(newsb));
    bio_write(0, block_buf);

    // 4. Initialize inode + data bitmaps on disk
    unsigned char ibitmap[BLOCK_SIZE];
    unsigned char dbitmap[BLOCK_SIZE];
    memset(ibitmap, 0, BLOCK_SIZE);
    memset(dbitmap, 0, BLOCK_SIZE);

    // Mark inode 0 (root) as used
    set_bitmap(ibitmap, 0);

    bio_write(newsb.i_bitmap_blk, ibitmap);
    bio_write(newsb.d_bitmap_blk, dbitmap);

    // 5. Zero out inode region
    memset(block_buf, 0, BLOCK_SIZE);
    for (int b = 0; b < inode_region_blocks; b++) {
        bio_write(newsb.i_start_blk + b, block_buf);
    }

    // 6. Zero out data region
    for (int b = 0; b < newsb.max_dnum; b++) {
        bio_write(newsb.d_start_blk + b, block_buf);
    }

    // 7. Initialize root directory inode (ino = 0)
    struct inode root;
    memset(&root, 0, sizeof(root));

    root.ino   = 0;
    root.valid = 1;
    root.type  = TYPE_DIR;
    root.size  = 0;
    root.link  = 2;   // "." and ".."

    // Fill POSIX stat info for root
    root.vstat.st_mode   = S_IFDIR | 0755;
    root.vstat.st_nlink  = 2;
    root.vstat.st_uid    = getuid();
    root.vstat.st_gid    = getgid();
    root.vstat.st_size   = 0;
    root.vstat.st_blksize= BLOCK_SIZE;

    time_t now = time(NULL);
    root.vstat.st_atime  = now;
    root.vstat.st_mtime  = now;
    root.vstat.st_ctime  = now;

    // Ask Person B's helper to actually write the inode structure
    writei(0, &root);

    // 8. Save superblock in memory
    sb = newsb;

	return 0;
}


/* 
 * FUSE file operations
 */

// Kelvin's Part: 
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk

	if (access(diskfile_path, F_OK) != 0) {
        // Not found → mkfs will create + open + format
        rufs_mkfs();
    } else {
        // Exists → just open
        if (dev_open(diskfile_path) < 0) {
            perror("dev_open in rufs_init");
            exit(EXIT_FAILURE);
        }

        // Load superblock from disk
        char block_buf[BLOCK_SIZE];
        if (bio_read(0, block_buf) <= 0) {
            fprintf(stderr, "rufs_init: failed to read superblock\n");
            exit(EXIT_FAILURE);
        }
        memcpy(&sb, block_buf, sizeof(sb));

        if (sb.magic_num != MAGIC_NUM) {
            fprintf(stderr, "rufs_init: bad magic number (disk not RUFS?)\n");
            // You could call rufs_mkfs() here to reformat, but we just warn.
        }
    }

	
	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	// Step 2: Close diskfile

	dev_close();

}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

	stbuf->st_mode   = S_IFDIR | 0755;
	stbuf->st_nlink  = 2;
	time(&stbuf->st_mtime);

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1

	struct inode node;
    int ret = get_node_by_path(path, 0, &node);
    if (ret < 0) {
        return ret;
    }

    if (!node.valid || node.type != TYPE_DIR) {
        return -ENOTDIR;
    }

	return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: Read directory entries from its data blocks, and copy them to filler

	struct inode dir_inode;
    int ret = get_node_by_path(path, 0, &dir_inode);
    if (ret < 0) {
        return ret;
    }

    if (!dir_inode.valid || dir_inode.type != TYPE_DIR) {
        return -ENOTDIR;
    }

    // "." and ".." are required by FUSE even if we don't store them on disk
    filler(buffer, ".",  NULL, 0);
    filler(buffer, "..", NULL, 0);

    char block_buf[BLOCK_SIZE];

    // Iterate over directory data blocks
    for (int i = 0; i < 16; i++) {
        int blkno = dir_inode.direct_ptr[i];
        if (blkno == 0)
            continue;

        if (bio_read(blkno, block_buf) <= 0)
            continue;

        int entries_per_block = BLOCK_SIZE / (int)sizeof(struct dirent);
        struct dirent *entries = (struct dirent *)block_buf;

        for (int j = 0; j < entries_per_block; j++) {
            if (!entries[j].valid)
                continue;

            // Add the entry name to the directory listing
            filler(buffer, entries[j].name, NULL, 0);
        }
    }

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	// Step 2: Call get_node_by_path() to get inode of parent directory
	// Step 3: Call get_avail_ino() to get an available inode number
	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	// Step 5: Update inode for target directory
	// Step 6: Call writei() to write inode to disk


	// 1: Check if directory already exists
    struct inode tmp;
    if (get_node_by_path(path, 0, &tmp) == 0) {
        // Already exists
        return -EEXIST;
    }

    // 2: Split path into parent directory and new dir name
    char path_copy[PATH_MAX];
    strncpy(path_copy, path, PATH_MAX);
    path_copy[PATH_MAX - 1] = '\0';

    char *dirc  = strdup(path_copy);
    char *basec = strdup(path_copy);

    char *parent_path = dirname(dirc);
    char *name        = basename(basec);

    if (parent_path == NULL || name == NULL || strlen(name) == 0) {
        free(dirc);
        free(basec);
        return -EINVAL;
    }

    // 3: Find parent directory inode
    struct inode parent_inode;
    int ret = get_node_by_path(parent_path, 0, &parent_inode);
    if (ret < 0) {
        free(dirc);
        free(basec);
        return ret;
    }

    if (!parent_inode.valid || parent_inode.type != TYPE_DIR) {
        free(dirc);
        free(basec);
        return -ENOTDIR;
    }

    // 4: Get an available inode number for the new directory (Person B's helper)
    int new_ino = get_avail_ino();
    if (new_ino < 0) {
        free(dirc);
        free(basec);
        return -ENOSPC;
    }

    // 5: Add a directory entry to the parent (Person B's dir_add)
    ret = dir_add(parent_inode, (uint16_t)new_ino, name, strlen(name));
    if (ret < 0) {
        free(dirc);
        free(basec);
        return ret;
    }

    // 6: Initialize new directory inode
    struct inode new_dir;
    memset(&new_dir, 0, sizeof(new_dir));

    new_dir.ino   = (uint16_t)new_ino;
    new_dir.valid = 1;
    new_dir.type  = TYPE_DIR;
    new_dir.size  = 0;
    new_dir.link  = 2;  // "." and ".." (conceptually)

    new_dir.vstat.st_mode   = S_IFDIR | (mode & 0777 ? mode & 0777 : 0755);
    new_dir.vstat.st_nlink  = 2;
    new_dir.vstat.st_uid    = getuid();
    new_dir.vstat.st_gid    = getgid();
    new_dir.vstat.st_size   = 0;
    new_dir.vstat.st_blksize= BLOCK_SIZE;

    time_t now = time(NULL);
    new_dir.vstat.st_atime  = now;
    new_dir.vstat.st_mtime  = now;
    new_dir.vstat.st_ctime  = now;

    // 7: Persist new directory inode
    writei(new_dir.ino, &new_dir);

    free(dirc);
    free(basec);

	return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}


/* 
 * Functions you DO NOT need to implement for this project
 * (stubs provided for completeness)
 */

static int rufs_rmdir(const char *path) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_unlink(const char *path) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.mkdir		= rufs_mkdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,

	//Operations that you don't have to implement.
	.rmdir		= rufs_rmdir,
	.releasedir	= rufs_releasedir,
	.unlink		= rufs_unlink,
	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

