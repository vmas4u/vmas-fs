////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2021 by V+ Publicidad SpA                               //
//  http://www.vmaspublicidad.com                                         //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#include <zip.h>
#include <syslog.h>
#include <cerrno>
#include <cassert>
#include <stdexcept>

#include "vmasFSData.h"

VmasFSData::VmasFSData(const char *archiveName, struct zip *z, const char *cwd): m_zip(z), m_archiveName(archiveName), m_cwd(cwd)  {
}

VmasFSData::~VmasFSData() {
    if (chdir(m_cwd.c_str()) != 0) {
        syslog(LOG_ERR, "Unable to chdir() to archive directory %s. Trying to save file into /tmp",
                m_cwd.c_str());
        if (chdir(getenv("TMP")) != 0) {
            chdir("/tmp");
        }
    }
    int res = zip_close(m_zip);
    if (res != 0) {
        syslog(LOG_ERR, "Error while closing archive: %s", zip_strerror(m_zip));
    }
    for (filemap_t::iterator i = files.begin(); i != files.end(); ++i) {
        delete i->second;
    }
}

bool VmasFSData::try_passwd(const char *pass) {
    int zep, try_count;
    struct zip_file *zf;

    try_count = 1;
just_try:
    zep = 0;
    zf = BigBuffer::open(m_zip, 0, &zep);
    if (zf == NULL) {
        if (ZIP_ER_NOPASSWD == zep && try_count--) {
            BigBuffer::passwd = pass;
            goto just_try;
        }
    }

    return zf != NULL;
}

void VmasFSData::build_tree(bool readonly) {
    m_root = FileNode::createRootNode();
    if (m_root == NULL) {
        throw std::bad_alloc();
    }
    m_root->parent = NULL;
    files[m_root->full_name.c_str()] = m_root;
    zip_int64_t n = zip_get_num_entries(m_zip, 0);
    // search for absolute or parent-relative paths
    bool needPrefix = false;
    if (readonly) {
        for (zip_int64_t i = 0; i < n; ++i) {
            const char *name = zip_get_name(m_zip, i, ZIP_FL_ENC_RAW);
            if ((name[0] == '/') || (strncmp(name, "../", 3) == 0)) {
                needPrefix = true;
                break;
            }
        }
    }
    // add zip entries into tree
    for (zip_int64_t i = 0; i < n; ++i) {
        const char *name = zip_get_name(m_zip, i, ZIP_FL_ENC_RAW);
        std::string converted;
        convertFileName(name, readonly, needPrefix, converted);
        const char *cname = converted.c_str();
        if (files.find(cname) != files.end()) {
            syslog(LOG_ERR, "duplicated file name: %s", cname);
            throw std::runtime_error("duplicate file names");
        }
        FileNode *node = FileNode::createNodeForZipEntry(m_zip, cname, i);
        if (node == NULL) {
            throw std::bad_alloc();
        }
        files[node->full_name.c_str()] = node;
    }
    // Connect nodes to tree. Missing intermediate nodes created on demand.
    for (filemap_t::const_iterator i = files.begin(); i != files.end(); ++i)
    {
        FileNode *node = i->second;
        if (node != m_root) {
            connectNodeToTree (node);
        }
    }
}

void VmasFSData::connectNodeToTree (FileNode *node) {
    FileNode *parent = findParent(node);
    if (parent == NULL) {
        parent = FileNode::createIntermediateDir (m_zip,
                node->getParentName().c_str());
        if (parent == NULL) {
            throw std::bad_alloc();
        }
        files[parent->full_name.c_str()] = parent;
        connectNodeToTree (parent);
    } else if (!parent->is_dir) {
        throw std::runtime_error ("bad archive structure");
    }
    // connecting to parent
    node->parent = parent;
    parent->appendChild (node);
}

int VmasFSData::removeNode(FileNode *node) {
    assert(node != NULL);
    assert(node->parent != NULL);
    node->parent->detachChild (node);
    node->parent->setCTime (time(NULL));
    files.erase(node->full_name.c_str());

    zip_int64_t id = node->id;
    delete node;
    if (id >= 0) {
        return (zip_delete (m_zip, id) == 0)? 0 : ENOENT;
    } else {
        return 0;
    }
}

void VmasFSData::validateFileName(const char *fname) {
    if (fname[0] == 0) {
        throw std::runtime_error("empty file name");
    }
    if (strstr(fname, "//") != NULL) {
        throw std::runtime_error(std::string("bad file name (two slashes): ") + fname);
    }
}

void VmasFSData::convertFileName(const char *fname, bool readonly,
        bool needPrefix, std::string &converted) {
    const char *UP_PREFIX = "UP";
    const char *CUR_PREFIX = "CUR";
    const char *ROOT_PREFIX = "ROOT";

    validateFileName(fname);

    assert(fname[0] != 0);
    const char *orig = fname;
    bool parentRelative = false;
    converted.reserve(strlen(fname) + strlen(ROOT_PREFIX) + 1);
    converted = "";
    // add prefix
    if (fname[0] == '/') {
        if (!readonly) {
            throw std::runtime_error("absolute paths are not supported in read-write mode");
        } else {
            assert(needPrefix);
            converted.append(ROOT_PREFIX);
            ++fname;
        }
    } else {
        while (strncmp(fname, "../", 3) == 0) {
            if (!readonly) {
                throw std::runtime_error("paths relative to parent directory are not supported in read-write mode");
            }
            assert(needPrefix);
            converted.append(UP_PREFIX);
            fname += strlen("../");
            parentRelative = true;
        }
        if (needPrefix && !parentRelative) {
            converted.append(CUR_PREFIX);
        }
    }
    if (needPrefix) {
        converted.append("/");
    }
    if (fname[0] == 0) {
        return;
    }
    assert(fname[0] != '/');

    const char *start = fname, *cur;
    while (start[0] != 0 && (cur = strchr(start + 1, '/')) != NULL) {
        if ((cur - start == 1 && start[0] == '.') ||
            (cur - start == 2 && start[0] == '.' && start[1] == '.')) {
            throw std::runtime_error(std::string("bad file name: ") + orig);
        }
        converted.append(start, cur - start + 1);
        start = cur + 1;
    }
    // end of string is reached
    if (strcmp(start, ".") == 0 || strcmp(start, "..") == 0) {
        throw std::runtime_error(std::string("bad file name: ") + orig);
    }
    converted.append(start);
}

FileNode *VmasFSData::findParent (const FileNode *node) const {
    std::string name = node->getParentName();
    return find(name.c_str());
}

void VmasFSData::insertNode (FileNode *node) {
    FileNode *parent = findParent (node);
    assert (parent != NULL);
    parent->appendChild (node);
    node->parent = parent;
    parent->setCTime (node->ctime());
    assert (files.find(node->full_name.c_str()) == files.end());
    files[node->full_name.c_str()] = node;
}

void VmasFSData::renameNode (FileNode *node, const char *newName, bool
        reparent) {
    assert(node != NULL);
    assert(newName != NULL);
    FileNode *parent1 = node->parent, *parent2;
    assert (parent1 != NULL);
    if (reparent) {
        parent1->detachChild (node);
    }

    files.erase(node->full_name.c_str());
    node->rename(newName);
    files[node->full_name.c_str()] = node;

    if (reparent) {
        parent2 = findParent(node);
        assert (parent2 != NULL);
        parent2->appendChild (node);
        node->parent = parent2;
    }

    if (reparent && parent1 != parent2) {
        time_t now = time (NULL);
        parent1->setCTime (now);
        parent2->setCTime (now);
    }
}

FileNode *VmasFSData::find (const char *fname) const {
    filemap_t::const_iterator i = files.find(fname);
    if (i == files.end()) {
        return NULL;
    } else {
        return i->second;
    }
}

void VmasFSData::save () {
    for (filemap_t::const_iterator i = files.begin(); i != files.end(); ++i) {
        FileNode *node = i->second;
        if (node == m_root) {
            continue;
        }
        assert(node != NULL);
        bool saveMetadata = node->isMetadataChanged();
        if (node->isChanged() && !node->is_dir) {
            saveMetadata = true;
            int res = node->save();
            if (res != 0) {
                saveMetadata = false;
                syslog(LOG_ERR, "Error while saving file %s in ZIP archive: %d",
                        node->full_name.c_str(), res);
            }
        }
        if (saveMetadata) {
            if (node->isTemporaryDir()) {
                // persist temporary directory
                zip_int64_t idx = zip_dir_add(m_zip,
                        node->full_name.c_str(), ZIP_FL_ENC_UTF_8);
                if (idx < 0) {
                    syslog(LOG_ERR, "Unable to save directory %s in ZIP archive",
                        node->full_name.c_str());
                    continue;
                }
                node->id = idx;
            }
            int res = node->saveMetadata();
            if (res != 0) {
                syslog(LOG_ERR, "Error while saving metadata for file %s in ZIP archive: %d",
                        node->full_name.c_str(), res);
            }
        }
    }
}

