////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2021 by V+ Publicidad SpA                               //
//  http://www.vmaspublicidad.com                                         //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#ifndef VMASFS_TYPES_H
#define VMASFS_TYPES_H

#include <unistd.h>

#include <cstring>
#include <cstdlib>
#include <list>
#include <map>

class FileNode;
class VmasFSData;

struct ltstr {
    bool operator() (const char* s1, const char* s2) const {
        return strcmp(s1, s2) < 0;
    }
};

typedef std::list <FileNode*> nodelist_t;
typedef std::map <const char*, FileNode*, ltstr> filemap_t;

#endif

