////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2021 by V+ Publicidad SpA                               //
//  http://www.vmaspublicidad.com                                         //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#ifndef VMASFS_DATA
#define VMASFS_DATA

#include <string>

#include "types.h"
#include "fileNode.h"

class VmasFSData {
private:
    /**
     * Check that file name is non-empty and does not contain duplicate
     * slashes
     * @throws std::runtime_exception if name is invalid
     */
    static void validateFileName(const char *fname);

    /**
     * In read-only mode convert file names to replace .. to UP and / to
     * ROOT to make absolute and relative paths accessible via file system.
     * In read-write mode throw an error if path is absolute or relative to
     * parent directory.
     * @param fname file name
     * @param readonly read-only flag
     * @param needPrefix prepend CUR directory prefix to "normal" file
     * names to not mix them with parent-relative or absolute
     * @param converted result string
     * @throws std::runtime_exception if name is invalid
     */
    static void convertFileName(const char *fname, bool readonly,
            bool needPrefix, std::string &converted);

    /**
     * Find node parent by its name
     */
    FileNode *findParent (const FileNode *node) const;

    /**
     * Create node parents (if not yet exist) and connect node to
     * @throws std::bad_alloc
     * @throws std::runtime_error - if parent is not directory
     */
    void connectNodeToTree (FileNode *node);

    FileNode *m_root;
    filemap_t files;
public:
    struct zip *m_zip;
    const char *m_archiveName;
    std::string m_cwd;

    /**
     * Keep archiveName and cwd in class fields and build file tree from z.
     *
     * 'cwd' and 'z' free()-ed in destructor.
     * 'archiveName' should be managed externally.
     */
    VmasFSData(const char *archiveName, struct zip *z, const char *cwd);
    ~VmasFSData();

    /**
     * try password if ZIP DATA is encrypted
     */
    bool try_passwd(const char *pass);

    /**
     * Detach node from tree, and delete associated entry in zip file if
     * present.
     *
     * @param node Node to remove
     * @return Error code or 0 is successful
     */
    int removeNode(FileNode *node);

    /**
     * Build tree of zip file entries from ZIP file
     */
    void build_tree(bool readonly);

    /**
     * Insert new node into tree by adding it to parent's childs list and
     * specifying node parent field.
     */
    void insertNode (FileNode *node);

    /**
     * Detach node from old parent, rename, attach to new parent.
     * @param node
     * @param newName new name
     * @param reparent if false, node detaching is not performed
     */
    void renameNode (FileNode *node, const char *newName, bool reparent);

    /**
     * search for node
     * @return node or NULL
     */
    FileNode *find (const char *fname) const;

    /**
     * Return number of files in tree
     */
    int numFiles () const {
        return files.size() - 1;
    }

    /**
     * Save archive
     */
    void save ();
};

#endif

