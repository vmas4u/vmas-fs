////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2021 by V+ Publicidad SpA                               //
//  http://www.vmaspublicidad.com                                         //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#ifndef VMAS_FS_H
#define VMAS_FS_H

/**
 * Main functions of vmas-fs file system (to be called by FUSE library)
 */

extern "C" {

/**
 * Initialize libzip and vmas-fs structures.
 *
 * @param program   Program name
 * @param fileName  ZIP file name
 * @return NULL if an error occured, otherwise pointer to VmasFSData structure.
 */
class VmasFSData *initVmasFS(const char *program, const char *fileName,
        bool readonly);

/**
 * Initialize filesystem
 *
 * Report current working dir and archive file name to syslog.
 *
 * @return filesystem-private data
 */
void *vmasfs_init(struct fuse_conn_info *conn);

/**
 * Destroy filesystem
 *
 * Save all modified data back to ZIP archive and report to syslog about completion.
 * Note that filesystem unmounted before this method finishes
 */
void vmasfs_destroy(void *data);

int vmasfs_getattr(const char *path, struct stat *stbuf);

int vmasfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);

int vmasfs_statfs(const char *path, struct statvfs *buf);

int vmasfs_open(const char *path, struct fuse_file_info *fi);

int vmasfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);

int vmasfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int vmasfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int vmasfs_release (const char *path, struct fuse_file_info *fi);

int vmasfs_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi);

int vmasfs_truncate(const char *path, off_t offset);

int vmasfs_unlink(const char *path);

int vmasfs_rmdir(const char *path);

int vmasfs_mkdir(const char *path, mode_t mode);

int vmasfs_rename(const char *path, const char *new_path);

int vmasfs_utimens(const char *path, const struct timespec tv[2]);

#if ( __APPLE__ )
int vmasfs_setxattr(const char *, const char *, const char *, size_t, int, uint32_t);
int vmasfs_getxattr(const char *, const char *, char *, size_t, uint32_t);
#else
int vmasfs_setxattr(const char *, const char *, const char *, size_t, int);
int vmasfs_getxattr(const char *, const char *, char *, size_t);
#endif

int vmasfs_listxattr(const char *, char *, size_t);

int vmasfs_removexattr(const char *, const char *);

int vmasfs_chmod(const char *, mode_t);

int vmasfs_chown(const char *, uid_t, gid_t);

int vmasfs_flush(const char *, struct fuse_file_info *);

int vmasfs_fsync(const char *, int, struct fuse_file_info *);

int vmasfs_fsyncdir(const char *, int, struct fuse_file_info *);

int vmasfs_opendir(const char *, struct fuse_file_info *);

int vmasfs_releasedir(const char *, struct fuse_file_info *);

int vmasfs_access(const char *, int);

int vmasfs_readlink(const char *, char *, size_t);

int vmasfs_symlink(const char *, const char *);

}

#endif

