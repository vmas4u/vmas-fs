////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2021 by V+ Publicidad SpA                               //
//  http://www.vmaspublicidad.com                                         //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#define STANDARD_BLOCK_SIZE (512)
#define ERROR_STR_BUF_LEN 0x100

#include "../config.h"

#include <fuse.h>
#include <zip.h>
#include <unistd.h>
#include <limits.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/statvfs.h>

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <queue>

#include "vmas-fs.h"
#include "types.h"
#include "fileNode.h"
#include "vmasFSData.h"

using namespace std;

//TODO: Move printf-s out this function
VmasFSData *initVmasFS(const char *program, const char *fileName,
        bool readonly) {
    VmasFSData *data = NULL;
    int err;
    struct zip *zip_file;

    if ((zip_file = zip_open(fileName, ZIP_CREATE, &err)) == NULL) {
        char err_str[ERROR_STR_BUF_LEN];
        zip_error_to_str(err_str, ERROR_STR_BUF_LEN, err, errno);
        fprintf(stderr, "%s: cannot open zip archive %s: %s\n", program, fileName, err_str);
        return data;
    }

    try {
        // current working directory
        char *cwd = (char*)malloc(PATH_MAX + 1);
        if (cwd == NULL) {
            throw std::bad_alloc();
        }
        if (getcwd(cwd, PATH_MAX) == NULL) {
            perror(NULL);
            return data;
        }

        data = new VmasFSData(fileName, zip_file, cwd);
        free(cwd);
        if (data == NULL) {
            throw std::bad_alloc();
        }
        try {
            data->build_tree(readonly);
        }
        catch (...) {
            delete data;
            throw;
        }
    }
    catch (std::bad_alloc) {
        syslog(LOG_ERR, "no enough memory");
        fprintf(stderr, "%s: no enough memory\n", program);
        return NULL;
    }
    catch (const std::exception &e) {
        syslog(LOG_ERR, "error opening ZIP file: %s", e.what());
        fprintf(stderr, "%s: unable to open ZIP file: %s\n", program, e.what());
        return NULL;
    }
    return data;
}

void *vmasfs_init(struct fuse_conn_info *conn) {
    (void) conn;
    VmasFSData *data = (VmasFSData*)fuse_get_context()->private_data;
    syslog(LOG_INFO, "Mounting file system on %s (cwd=%s)", data->m_archiveName, data->m_cwd.c_str());
    return data;
}

inline VmasFSData *get_data() {
    return (VmasFSData*)fuse_get_context()->private_data;
}

inline struct zip *get_zip() {
    return get_data()->m_zip;
}

void vmasfs_destroy(void *data) {
    VmasFSData *d = (VmasFSData*)data;
    d->save ();
    delete d;
    syslog(LOG_INFO, "File system unmounted");
}

FileNode *get_file_node(const char *fname) {
    return get_data()->find (fname);
}

int vmasfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    if (node->is_dir) {
        stbuf->st_nlink = 2 + node->childs.size();
    } else {
        stbuf->st_nlink = 1;
    }
    stbuf->st_mode = node->mode();
    stbuf->st_blksize = STANDARD_BLOCK_SIZE;
    stbuf->st_ino = node->id;
    stbuf->st_blocks = (node->size() + STANDARD_BLOCK_SIZE - 1) / STANDARD_BLOCK_SIZE;
    stbuf->st_size = node->size();
    stbuf->st_atime = node->atime();
    stbuf->st_mtime = node->mtime();
    stbuf->st_ctime = node->ctime();
    stbuf->st_uid = node->uid();
    stbuf->st_gid = node->gid();

    return 0;
}

int vmasfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    for (nodelist_t::const_iterator i = node->childs.begin(); i != node->childs.end(); ++i) {
        filler(buf, (*i)->name, NULL, 0);
    }

    return 0;
}

int vmasfs_statfs(const char *path, struct statvfs *buf) {
    (void) path;

    // Getting amount of free space in directory with archive
    struct statvfs st;
    int err;
    if ((err = statvfs(get_data()->m_cwd.c_str(), &st)) != 0) {
        return -err;
    }
    buf->f_bavail = buf->f_bfree = st.f_frsize * st.f_bavail;

    buf->f_bsize = 1;
    //TODO: may be append archive size?
    buf->f_blocks = buf->f_bavail + 0;

    buf->f_ffree = 0;
    buf->f_favail = 0;

    buf->f_files = get_data()->numFiles();
    buf->f_namemax = 255;

    return 0;
}

int vmasfs_open(const char *path, struct fuse_file_info *fi) {
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    if (node->is_dir) {
        return -EISDIR;
    }
    fi->fh = (uint64_t)node;

    try {
        return node->open();
    }
    catch (std::bad_alloc) {
        return -ENOMEM;
    }
    catch (std::exception) {
        return -EIO;
    }
}

int vmasfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    if (*path == '\0') {
        return -EACCES;
    }
    FileNode *node = get_file_node(path + 1);
    if (node != NULL) {
        return -EEXIST;
    }
    node = FileNode::createFile (get_zip(), path + 1,
            fuse_get_context()->uid, fuse_get_context()->gid, mode);
    if (node == NULL) {
        return -ENOMEM;
    }
    get_data()->insertNode (node);
    fi->fh = (uint64_t)node;

    return node->open();
}

int vmasfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) path;

    return ((FileNode*)fi->fh)->read(buf, size, offset);
}

int vmasfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) path;

    return ((FileNode*)fi->fh)->write(buf, size, offset);
}

int vmasfs_release (const char *path, struct fuse_file_info *fi) {
    (void) path;

    return ((FileNode*)fi->fh)->close();
}

int vmasfs_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi) {
    (void) path;

    return -((FileNode*)fi->fh)->truncate(offset);
}

int vmasfs_truncate(const char *path, off_t offset) {
    if (*path == '\0') {
        return -EACCES;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    if (node->is_dir) {
        return -EISDIR;
    }
    int res;
    if ((res = node->open()) != 0) {
        return res;
    }
    if ((res = node->truncate(offset)) != 0) {
        node->close();
        return -res;
    }
    return node->close();
}

int vmasfs_unlink(const char *path) {
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    if (node->is_dir) {
        return -EISDIR;
    }
    return -get_data()->removeNode(node);
}

int vmasfs_rmdir(const char *path) {
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    if (!node->is_dir) {
        return -ENOTDIR;
    }
    if (!node->childs.empty()) {
        return -ENOTEMPTY;
    }
    return -get_data()->removeNode(node);
}

int vmasfs_mkdir(const char *path, mode_t mode) {
    if (*path == '\0') {
        return -ENOENT;
    }
    zip_int64_t idx = zip_dir_add(get_zip(), path + 1, ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        return -ENOMEM;
    }
    FileNode *node = FileNode::createDir(get_zip(), path + 1, idx,
            fuse_get_context()->uid, fuse_get_context()->gid, mode);
    if (node == NULL) {
        return -ENOMEM;
    }
    get_data()->insertNode (node);
    return 0;
}

int vmasfs_rename(const char *path, const char *new_path) {
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    if (*new_path == '\0') {
        return -EINVAL;
    }
    FileNode *new_node = get_file_node(new_path + 1);
    if (new_node != NULL) {
        int res = get_data()->removeNode(new_node);
        if (res !=0) {
            return -res;
        }
    }

    int len = strlen(new_path);
    int oldLen = strlen(path + 1) + 1;
    std::string new_name;
    if (!node->is_dir) {
        --len;
        --oldLen;
    }
    new_name.reserve(len + ((node->is_dir) ? 1 : 0));
    new_name.append(new_path + 1);
    if (node->is_dir) {
        new_name.push_back('/');
    }

    try {
        struct zip *z = get_zip();
        // Renaming directory and its content recursively
        if (node->is_dir) {
            queue<FileNode*> q;
            q.push(node);
            while (!q.empty()) {
                FileNode *n = q.front();
                q.pop();
                for (nodelist_t::const_iterator i = n->childs.begin(); i != n->childs.end(); ++i) {
                    FileNode *nn = *i;
                    q.push(nn);
                    char *name = (char*)malloc(len + nn->full_name.size() - oldLen + (nn->is_dir ? 2 : 1));
                    if (name == NULL) {
                        //TODO: check that we are have enough memory before entering this loop
                        return -ENOMEM;
                    }
                    strcpy(name, new_name.c_str());
                    strcpy(name + len, nn->full_name.c_str() + oldLen);
                    if (nn->is_dir) {
                        strcat(name, "/");
                    }
                    if (nn->id >= 0) {
                        zip_file_rename(z, nn->id, name, ZIP_FL_ENC_UTF_8);
                    }
                    // changing child list may cause loop iterator corruption
                    get_data()->renameNode (nn, name, false);
                    
                    free(name);
                }
            }
        }
        if (node->id >= 0) {
            zip_file_rename(z, node->id, new_name.c_str(), ZIP_FL_ENC_UTF_8);
        }
        get_data()->renameNode (node, new_name.c_str(), true);

        return 0;
    }
    catch (...) {
        return -EIO;
    }
}

int vmasfs_utimens(const char *path, const struct timespec tv[2]) {
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    node->setTimes (tv[0].tv_sec, tv[1].tv_sec);
    return 0;
}

#if ( __APPLE__ )
int vmasfs_setxattr(const char *, const char *, const char *, size_t, int, uint32_t) {
#else
int vmasfs_setxattr(const char *, const char *, const char *, size_t, int) {
#endif
    return -ENOTSUP;
}

#if ( __APPLE__ )
int vmasfs_getxattr(const char *, const char *, char *, size_t, uint32_t) {
#else
int vmasfs_getxattr(const char *, const char *, char *, size_t) {
#endif
    return -ENOTSUP;
}

int vmasfs_listxattr(const char *, char *, size_t) {
    return -ENOTSUP;
}

int vmasfs_removexattr(const char *, const char *) {
    return -ENOTSUP;
}

int vmasfs_chmod(const char *path, mode_t mode) {
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    node->chmod(mode);
    return 0;
}

int vmasfs_chown(const char *path, uid_t uid, gid_t gid) {
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    if (uid != (uid_t) -1) {
        node->setUid (uid);
    }
    if (gid != (gid_t) -1) {
        node->setGid (gid);
    }
    return 0;
}

int vmasfs_flush(const char *, struct fuse_file_info *) {
    return 0;
}

int vmasfs_fsync(const char *, int, struct fuse_file_info *) {
    return 0;
}

int vmasfs_fsyncdir(const char *, int, struct fuse_file_info *) {
    return 0;
}

int vmasfs_opendir(const char *, struct fuse_file_info *) {
  return 0;
}

int vmasfs_releasedir(const char *, struct fuse_file_info *) {
    return 0;
}

int vmasfs_access(const char *, int) {
    return 0;
}

int vmasfs_readlink(const char *path, char *buf, size_t size) {
    if (*path == '\0') {
        return -ENOENT;
    }
    FileNode *node = get_file_node(path + 1);
    if (node == NULL) {
        return -ENOENT;
    }
    if (!S_ISLNK(node->mode())) {
        return -EINVAL;
    }
    int res;
    if ((res = node->open()) != 0) {
        if (res == -EMFILE) {
            res = -ENOMEM;
        }
        return res;
    }
    int count = node->read(buf, size - 1, 0);
    buf[count] = '\0';
    node->close();
    return 0;
}

int vmasfs_symlink(const char *dest, const char *path) {
    if (*path == '\0') {
        return -EACCES;
    }
    FileNode *node = get_file_node(path + 1);
    if (node != NULL) {
        return -EEXIST;
    }
    node = FileNode::createSymlink (get_zip(), path + 1);
    if (node == NULL) {
        return -ENOMEM;
    }
    get_data()->insertNode (node);

    int res;
    if ((res = node->open()) != 0) {
        if (res == -EMFILE) {
            res = -ENOMEM;
        }
        return res;
    }
    res = node->write(dest, strlen(dest), 0);
    node->close();
    return (res < 0) ? -ENOMEM : 0;
}

