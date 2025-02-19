////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2021 by V+ Publicidad SpA                               //
//  http://www.vmaspublicidad.com                                         //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#ifndef BIG_BUFFER_H
#define BIG_BUFFER_H

#include <zip.h>
#include <unistd.h>

#include <vector>

#include "types.h"

class BigBuffer {
private:
    //TODO: use >> and <<
    static const unsigned int chunkSize = 4*1024; //4 Kilobytes

    class ChunkWrapper;

    typedef std::vector<ChunkWrapper> chunks_t;

    struct CallBackStruct {
        size_t pos;
        const BigBuffer *buf;
        time_t mtime;
    };

    chunks_t chunks;

    /**
     * Callback for zip_source_function.
     * ZIP_SOURCE_CLOSE is not needed to be handled, ZIP_SOURCE_ERROR is
     * never called because read() always successfull.
     * See zip_source_function(3) for details.
     */
    static zip_int64_t zipUserFunctionCallback(void *state, void *data,
            zip_uint64_t len, enum zip_source_cmd cmd);

    /**
     * Return number of chunks needed to keep 'offset' bytes.
     */
    inline static unsigned int chunksCount(zip_uint64_t offset) {
        return (offset + chunkSize - 1) / chunkSize;
    }

    /**
     * Return number of chunk where 'offset'-th byte is located.
     */
    inline static unsigned int chunkNumber(zip_uint64_t offset) {
        return offset / chunkSize;
    }

    /**
     * Return offset inside chunk to 'offset'-th byte.
     */
    inline static int chunkOffset(zip_uint64_t offset) {
        return offset % chunkSize;
    }

public:
    zip_uint64_t len;
    /* store password here, Can be NULL */
    static const char *passwd;

    /**
     * Create new file buffer without mapping to file in a zip archive
     */
    BigBuffer();

    /**
     * Read file data from file inside zip archive
     *
     * @param z         Zip file
     * @param nodeId    Node index inside zip file
     * @param length    File length
     * @throws 
     *      std::exception  On file read error
     *      std::bad_alloc  On memory insufficiency
     */
    BigBuffer(struct zip *z, zip_uint64_t nodeId, zip_uint64_t length);

    ~BigBuffer();

    /**
     * open a file inside zip archive
     */
    static struct zip_file *open(struct zip *z, zip_uint64_t nodeId, int *zep);

    /**
     * Dispatch read requests to chunks of a file and write result to
     * resulting buffer.
     * Reading after end of file is not allowed, so 'size' is decreased to
     * fit file boundaries.
     *
     * @param buf       destination buffer
     * @param size      requested bytes count
     * @param offset    offset to start reading from
     * @return number of bytes read
     */
    int read(char *buf, size_t size, zip_uint64_t offset) const;

    /**
     * Dispatch write request to chunks of a file and grow 'chunks' vector if
     * necessary.
     * If 'offset' is after file end, tail of last chunk cleared before growing.
     *
     * @param buf       Source buffer
     * @param size      Number of bytes to be written
     * @param offset    Offset in file to start writing from
     * @return number of bytes written
     * @throws
     *      std::bad_alloc  If there are no memory for buffer
     */
    int write(const char *buf, size_t size, zip_uint64_t offset);

    /**
     * Create (or replace) file element in zip file. Class instance should
     * not be destroyed until zip_close() is called.
     *
     * @param mtime     File modification time
     * @param z         ZIP archive structure
     * @param fname     File name
     * @param newFile   Is file not yet created?
     * @param index     (INOUT) File index in ZIP archive. Set if new file
     *                  is created
     * @return
     *      0       If successfull
     *      -ENOMEM If there are no memory
     */
    int saveToZip(time_t mtime, struct zip *z, const char *fname,
            bool newFile, zip_int64_t &index);

    /**
     * Truncate buffer at position offset.
     * 1. Free chunks after offset
     * 2. Resize chunks vector to a new size
     * 3. Fill data block that made readable by resize with zeroes
     *
     * @throws
     *      std::bad_alloc  If insufficient memory available
     */
    void truncate(zip_uint64_t offset);
};

#endif

