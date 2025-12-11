/*
 *  Copyright (C) 2025 CS416 Rutgers CS
 *	Rutgers Tiny File System
 *	File:	rufs.c
 *
 *  List all group member's name: Kelvin Ihezue, Bryan Shangguan
 *  username of iLab: ki120, bys8
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
	char block_buf[BLOCK_SIZE];
	if (bio_read(sb.i_bitmap_blk, block_buf) < 0) {
		return -1;
	}
	bitmap_t ibitmap = (bitmap_t)block_buf;

	// Step 2: Traverse inode bitmap to find an available slot
	for (int i = 0; i < sb.max_inum; i++) {
		if (get_bitmap(ibitmap, i) == 0) {
			// Step 3: Update inode bitmap and write to disk 
			set_bitmap(ibitmap, i);
			if (bio_write(sb.i_bitmap_blk, block_buf) < 0) {
				return -1;
			}
			return i;
		}
	}

	// no inodes available
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
    // Step 1: Read data block bitmap from disk
	char block_buf[BLOCK_SIZE];
	if (bio_read(sb.d_bitmap_blk, block_buf) < 0) {
		return -1;
	}
	bitmap_t dbitmap = (bitmap_t)block_buf;

	// Step 2: Traverse data block bitmap to find an available slot
	for (int i = 0; i < sb.max_dnum; i++) {
		if (get_bitmap(dbitmap, i) == 0) {
			// Step 3: Update data block bitmap and write to disk 
			set_bitmap(dbitmap, i);
			if (bio_write(sb.d_bitmap_blk, block_buf) < 0) {
				return -1;
			}
			// return the absolute block number
			return sb.d_start_blk + i;
		}
	}

	return -1;
}

/* 
 * inode operations
 */

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
                
                if (dirent) {
                    *dirent = entries[j];
                }
                return 0; // found
            }
        }
    }

    return -ENOENT;
}

int dir_add(struct inode *dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	struct dirent *dir_entry;
	char block_buf[BLOCK_SIZE];
	int found_spot = 0;
	int entries_per_block = BLOCK_SIZE / sizeof(struct dirent);

	struct dirent existing;
	if (dir_find(dir_inode->ino, fname, name_len, &existing) == 0) {
		return -EEXIST;
	}

	for (int i = 0; i < 16; i++) {
		if (dir_inode->direct_ptr[i] == 0) {
			int new_blk = get_avail_blkno();
			if (new_blk < 0) return -ENOSPC;
			
			dir_inode->direct_ptr[i] = new_blk;
			dir_inode->size += BLOCK_SIZE;
			
			memset(block_buf, 0, BLOCK_SIZE);
			bio_write(new_blk, block_buf);
		}

		// read block
		if (bio_read(dir_inode->direct_ptr[i], block_buf) < 0) return -EIO;
		
		dir_entry = (struct dirent *)block_buf;

		for (int j = 0; j < entries_per_block; j++) {
			if (!dir_entry[j].valid) {
				// Step 3: Add directory entry in dir_inode's data block and write to disk
				dir_entry[j].valid = 1;
				dir_entry[j].ino = f_ino;
				dir_entry[j].len = name_len;
				strncpy(dir_entry[j].name, fname, name_len);
				dir_entry[j].name[name_len] = '\0';
				
				// write block back to disk
				bio_write(dir_inode->direct_ptr[i], block_buf);
				
				found_spot = 1;
				break;
			}
		}
		if (found_spot) break;
	}

	if (!found_spot) return -ENOSPC; // directory full

	// update directory inode
	time_t now = time(NULL);
	dir_inode->vstat.st_mtime = now;
	dir_inode->vstat.st_atime = now;
	
	// wite directory inode to disk
	writei(dir_inode->ino, dir_inode); 
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

    // 8. Save superblock in memory
    sb = newsb;
    writei(0, &root);

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
	dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
	// Step 1: call get_node_by_path() to get inode from path
	struct inode node;
	int ret = get_node_by_path(path, 0, &node);
	
	if (ret < 0) {
		return -ENOENT;
	}

	// Step 2: fill attribute of file into stbuf from inode
	stbuf->st_mode = node.vstat.st_mode;
	stbuf->st_nlink = node.vstat.st_nlink;
	stbuf->st_uid = node.vstat.st_uid;
	stbuf->st_gid = node.vstat.st_gid;
	stbuf->st_size = node.size;
	stbuf->st_mtime = node.vstat.st_mtime;
	stbuf->st_atime = node.vstat.st_atime;
	stbuf->st_ctime = node.vstat.st_ctime;
	stbuf->st_blksize = BLOCK_SIZE;
	stbuf->st_blocks = (node.size + BLOCK_SIZE - 1) / BLOCK_SIZE * (BLOCK_SIZE / 512);

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
    // 1: Check if directory already exists
    struct inode tmp;
    if (get_node_by_path(path, 0, &tmp) == 0) {
        return -EEXIST;
    }

    // 2: Split path
    char path_copy[PATH_MAX];
    strncpy(path_copy, path, PATH_MAX);
    path_copy[PATH_MAX - 1] = '\0';

    char *dirc  = strdup(path_copy);
    char *basec = strdup(path_copy);
    char *parent_path = dirname(dirc);
    char *name        = basename(basec);

    if (parent_path == NULL || name == NULL || strlen(name) == 0) {
        free(dirc); free(basec);
        return -EINVAL;
    }

    // 3: Find parent directory inode
    struct inode parent_inode;
    int ret = get_node_by_path(parent_path, 0, &parent_inode);
    if (ret < 0) {
        free(dirc); free(basec);
        return ret;
    }

    if (!parent_inode.valid || parent_inode.type != TYPE_DIR) {
        free(dirc); free(basec);
        return -ENOTDIR;
    }

    // 4: Get an available inode number
    int new_ino = get_avail_ino();
    if (new_ino < 0) {
        free(dirc); free(basec);
        return -ENOSPC;
    }

    // 5: Add a directory entry to the parent
    ret = dir_add(&parent_inode, (uint16_t)new_ino, name, strlen(name));
    if (ret < 0) {
        free(dirc); free(basec);
        return ret;
    }

    // FIX: Update parent link count (parent has new subdirectory '..')
    parent_inode.link++;
    parent_inode.vstat.st_nlink++;
    parent_inode.vstat.st_mtime = time(NULL);
    writei(parent_inode.ino, &parent_inode);

    // 6: Initialize new directory inode
    struct inode new_dir;
    memset(&new_dir, 0, sizeof(new_dir));

    new_dir.ino   = (uint16_t)new_ino;
    new_dir.valid = 1;
    new_dir.type  = TYPE_DIR;
    new_dir.size  = 0;
    new_dir.link  = 2;  // "." and ".."

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
    char path_copy[PATH_MAX];
    strncpy(path_copy, path, PATH_MAX);
    path_copy[PATH_MAX - 1] = '\0';

    char *dirc = strdup(path_copy);
    char *basec = strdup(path_copy);
    
    char *dname = dirname(dirc);
    char *bname = basename(basec);

    // Step 2: Call get_node_by_path() to get inode of parent directory
    struct inode parent_inode;
    int ret = get_node_by_path(dname, 0, &parent_inode);
    if (ret < 0) {
        free(dirc);
        free(basec);
        return ret; // parent not found
    }

    // Step 3: Call get_avail_ino() to get an available inode number
    int new_ino = get_avail_ino();
    if (new_ino < 0) {
        free(dirc);
        free(basec);
        return -ENOSPC;
    }

    // Step 4: Call dir_add() to add directory entry of target file to parent directory
    ret = dir_add(&parent_inode, new_ino, bname, strlen(bname));
    if (ret < 0) {
        free(dirc);
        free(basec);
        return ret;
    }

    // Step 5: Update inode for target file
    struct inode new_inode;
    memset(&new_inode, 0, sizeof(struct inode));
    new_inode.ino = new_ino;
    new_inode.valid = 1;
    new_inode.size = 0;
    new_inode.type = TYPE_FILE;
    new_inode.link = 1;
    
    // Set vstat info
    new_inode.vstat.st_mode = S_IFREG | mode;
    new_inode.vstat.st_nlink = 1;
    new_inode.vstat.st_uid = getuid();
    new_inode.vstat.st_gid = getgid();
    new_inode.vstat.st_size = 0;
    new_inode.vstat.st_blksize = BLOCK_SIZE;
    time(&new_inode.vstat.st_mtime);
    new_inode.vstat.st_atime = new_inode.vstat.st_mtime;
    new_inode.vstat.st_ctime = new_inode.vstat.st_mtime;

    // Step 6: Call writei() to write inode to disk
    writei(new_ino, &new_inode);

    free(dirc);
    free(basec);

    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode node;
	int ret = get_node_by_path(path, 0, &node);

	// Step 2: If not find, return -1 (or appropriate error)
	if (ret < 0) {
		return -ENOENT;
	}

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode node;
	int ret = get_node_by_path(path, 0, &node);
	if (ret < 0) return ret;

	// bound check
	if (offset >= node.size) return 0;
	if (offset + size > node.size) {
		size = node.size - offset;
	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int bytes_read = 0;
	char block_buf[BLOCK_SIZE];

	while (bytes_read < size) {
		int curr_offset = offset + bytes_read;
		int blk_idx = curr_offset / BLOCK_SIZE;
		int blk_offset = curr_offset % BLOCK_SIZE;
		int bytes_to_copy = BLOCK_SIZE - blk_offset;
		if (bytes_to_copy > (size - bytes_read)) {
			bytes_to_copy = size - bytes_read;
		}

		if (blk_idx >= 16) break; // max file size limit for direct pointers

		int disk_blk = node.direct_ptr[blk_idx];
		
		if (disk_blk != 0) {
			bio_read(disk_blk, block_buf);
		} else {
			memset(block_buf, 0, BLOCK_SIZE);
		}

		// Step 3: copy the correct amount of data from offset to buffer
		memcpy(buffer + bytes_read, block_buf + blk_offset, bytes_to_copy);
		bytes_read += bytes_to_copy;
	}

	// Note: this function should return the amount of bytes you copied to buffer
	return bytes_read;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode node;
	int ret = get_node_by_path(path, 0, &node);
	if (ret < 0) return ret;

	// Step 2: Based on size and offset, read its data blocks from disk
	int bytes_written = 0;
	char block_buf[BLOCK_SIZE];

	while (bytes_written < size) {
		int curr_offset = offset + bytes_written;
		int blk_idx = curr_offset / BLOCK_SIZE;
		int blk_offset = curr_offset % BLOCK_SIZE;
		int bytes_to_copy = BLOCK_SIZE - blk_offset;
		if (bytes_to_copy > (size - bytes_written)) {
			bytes_to_copy = size - bytes_written;
		}

		if (blk_idx >= 16) return -EFBIG; // file too big

		// allocate block if it doesnt exist
		if (node.direct_ptr[blk_idx] == 0) {
			int new_blk = get_avail_blkno();
			if (new_blk < 0) return -ENOSPC;
			node.direct_ptr[blk_idx] = new_blk;
			// Zero out the new block initially
			memset(block_buf, 0, BLOCK_SIZE);
			bio_write(new_blk, block_buf);
		}

		// Step 3: Write the correct amount of data from offset to disk
		if (bytes_to_copy < BLOCK_SIZE) {
			bio_read(node.direct_ptr[blk_idx], block_buf);
		}
		
		memcpy(block_buf + blk_offset, buffer + bytes_written, bytes_to_copy);
		bio_write(node.direct_ptr[blk_idx], block_buf);

		bytes_written += bytes_to_copy;
	}

	// Step 4: Update the inode info and write it to disk
	if (offset + bytes_written > node.size) {
		node.size = offset + bytes_written;
		node.vstat.st_size = node.size;
	}
	time(&node.vstat.st_mtime);
	
	writei(node.ino, &node);

	// Note: this function should return the amount of bytes you write to disk
	return bytes_written;
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

