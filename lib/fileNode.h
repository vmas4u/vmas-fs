////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2021 by V+ Publicidad SpA                               //
//  http://www.vmaspublicidad.com                                         //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#ifndef FILE_NODE_H
#define FILE_NODE_H

#include <string>
#include <unistd.h>
#include <sys/stat.h>

#include "types.h"
#include "bigBuffer.h"

class FileNode {
friend class VmasFSData;
private:
    // must not be defined
    FileNode (const FileNode &);
    FileNode &operator= (const FileNode &);

    enum nodeState {
        CLOSED,
        OPENED,
        CHANGED,
        NEW,
        NEW_DIR
    };

    BigBuffer *buffer;
    struct zip *zip;
    int open_count;
    nodeState state;

    zip_uint64_t m_size;
    bool has_cretime, metadataChanged;
    mode_t m_mode;
    time_t m_mtime, m_atime, m_ctime, cretime;
    uid_t m_uid;
    gid_t m_gid;

    void parse_name();
    void processExtraFields();
    void processExternalAttributes();
    int updateExtraFields() const;
    int updateExternalAttributes() const;

    static const zip_int64_t ROOT_NODE_INDEX, NEW_NODE_INDEX;
    FileNode(struct zip *zip, const char *fname, zip_int64_t id);

protected:
    static FileNode *createIntermediateDir(struct zip *zip, const char *fname);

public:
    /**
     * Create new regular file
     */
    static FileNode *createFile(struct zip *zip, const char *fname,
            uid_t owner, gid_t group, mode_t mode);
    /**
     * Create new symbolic link
     */
    static FileNode *createSymlink(struct zip *zip, const char *fname);
    /**
     * Create new directory for ZIP file entry
     */
    static FileNode *createDir(struct zip *zip, const char *fname,
            zip_int64_t id, uid_t owner, gid_t group, mode_t mode);
    /**
     * Create root pseudo-node for file system
     */
    static FileNode *createRootNode();
    /**
     * Create node for existing ZIP file entry
     */
    static FileNode *createNodeForZipEntry(struct zip *zip,
            const char *fname, zip_int64_t id);
    ~FileNode();
    
    /**
     * add child node to list
     */
    void appendChild (FileNode *child);

    /**
     * remove child node from list
     */
    void detachChild (FileNode *child);

    /**
     * Rename file without reparenting
     */
    void rename (const char *new_name);

    int open();
    int read(char *buf, size_t size, zip_uint64_t offset);
    int write(const char *buf, size_t size, zip_uint64_t offset);
    int close();

    /**
     * Invoke zip_add() or zip_replace() for file to save it.
     * Should be called only if item is needed to ba saved into zip file.
     *
     * @return 0 if success, != 0 on error
     */
    int save();

    /**
     * Save file metadata to ZIP
     * @return libzip error code or 0 on success
     */
    int saveMetadata () const;

    /**
     * Truncate file.
     *
     * @return
     *      0       If successful
     *      EBADF   If file is currently closed
     *      EIO     If insufficient memory available (because ENOMEM not
     *              listed in truncate() error codes)
     */
    int truncate(zip_uint64_t offset);

    inline bool isChanged() const {
        return state == CHANGED || state == NEW;
    }

    inline bool isMetadataChanged() const {
        return metadataChanged;
    }

    inline bool isTemporaryDir() const {
        return (state == NEW_DIR) && (id == NEW_NODE_INDEX);
    }

    /**
     * Change file mode
     */
    void chmod (mode_t mode);
    inline mode_t mode() const {
        return m_mode;
    }

    /**
     * set atime and mtime
     */
    void setTimes (time_t atime, time_t mtime);

    void setCTime (time_t ctime);

    inline time_t atime() const {
        return m_atime;
    }
    inline time_t ctime() const {
        return m_ctime;
    }
    inline time_t mtime() const {
        return m_mtime;
    }

    /**
     * Get parent name
     */
    //TODO: rewrite without memory allocation
    inline std::string getParentName () const {
        if (name > full_name.c_str()) {
            return std::string (full_name, 0, name - full_name.c_str() - 1);
        } else {
            return "";
        }
    }

    /**
     * owner and group
     */
    void setUid (uid_t uid);
    void setGid (gid_t gid);
    inline uid_t uid () const {
        return m_uid;
    }
    inline gid_t gid () const {
        return m_gid;
    }

    zip_uint64_t size() const;

    const char *name;
    std::string full_name;
    bool is_dir;
    zip_int64_t id;
    nodelist_t childs;
    FileNode *parent;
};
#endif

