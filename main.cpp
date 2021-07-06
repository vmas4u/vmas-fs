////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2021 by V+ Publicidad SpA                               //
//  http://www.vmaspublicidad.com                                         //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

#define KEY_HELP (0)
#define KEY_VERSION (1)
#define KEY_RO (2)
#define KEY_USE_PASSWD (3)

#include "config.h"

#include <fuse.h>
#include <fuse_opt.h>
#include <limits.h>
#include <syslog.h>

#include <cerrno>

#include "vmas-fs.h"
#include "vmasFSData.h"

/**
 * Print usage information
 */
void print_usage() {
    fprintf(stderr, "usage: %s [options] <vfs-file> <mountpoint>\n\n", PROGRAM);
    fprintf(stderr,
            "general options:\n"
            "    -o opt,[opt...]        mount options\n"
            "    -h   --help            print help\n"
            "    -V   --version         print version\n"
            "    -r   -o ro             open archive in read-only mode\n"
            "    -f                     don't detach from terminal\n"
            "    -p                     use password\n"
            "    -d                     turn on debugging, also implies -f\n"
            "\n");
}

/**
 * Print version information (vmas-fs and FUSE library)
 */
void print_version() {
    fprintf(stderr, "%s version: %s\n", PROGRAM, VERSION);
}

/**
 * Parameters for command-line argument processing function
 */
struct vmasfs_param {
    // help shown
    bool help;
    // version information shown
    bool version;
    // number of string arguments
    int strArgCount;
    // zip file name
    const char *fileName;
    // read-only flag
    bool readonly;
    // optional, use passwd
    bool usePasswd;
};

/**
 * Function to process arguments (called from fuse_opt_parse).
 *
 * @param data  Pointer to vmasfs_param structure
 * @param arg is the whole argument or option
 * @param key determines why the processing function was called
 * @param outargs the current output argument list
 * @return -1 on error, 0 if arg is to be discarded, 1 if arg should be kept
 */
static int process_arg(void *data, const char *arg, int key, struct fuse_args *outargs) {
    struct vmasfs_param *param = (vmasfs_param*)data;

    (void)outargs;

    // 'magic' fuse_opt_proc return codes
    const static int KEEP = 1;
    const static int DISCARD = 0;
    const static int ERROR = -1;

    switch (key) {
        case KEY_HELP: {
            print_usage();
            param->help = true;
            return DISCARD;
        }

        case KEY_VERSION: {
            print_version();
            param->version = true;
            return KEEP;
        }

        case KEY_RO: {
            param->readonly = true;
            return KEEP;
        }

        case KEY_USE_PASSWD: {
            param->usePasswd = true;
            return DISCARD;
        }

        case FUSE_OPT_KEY_NONOPT: {
            ++param->strArgCount;
            switch (param->strArgCount) {
                case 1: {
                    // zip file name
                    param->fileName = arg;
                    return DISCARD;
                }
                case 2: {
                    // mountpoint
                    // keep it and then pass to FUSE initializer
                    return KEEP;
                }
                default:
                    fprintf(stderr, "%s: only two arguments allowed: filename and mountpoint\n", PROGRAM);
                    return ERROR;
            }
        }

        default: {
            return KEEP;
        }
    }
}

static const struct fuse_opt vmasfs_opts[] = {
    FUSE_OPT_KEY("-h",          KEY_HELP),
    FUSE_OPT_KEY("--help",      KEY_HELP),
    FUSE_OPT_KEY("-V",          KEY_VERSION),
    FUSE_OPT_KEY("--version",   KEY_VERSION),
    FUSE_OPT_KEY("-r",          KEY_RO),
    FUSE_OPT_KEY("ro",          KEY_RO),
    FUSE_OPT_KEY("-p",          KEY_USE_PASSWD),
    {NULL, 0, 0}
};

int main(int argc, char *argv[]) {
    if (sizeof(void*) > sizeof(uint64_t)) {
        fprintf(stderr,"%s: This program cannot be run on your system because of FUSE design limitation\n", PROGRAM);
        return EXIT_FAILURE;
    }
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    VmasFSData *data = NULL;
    struct vmasfs_param param;
    param.help = false;
    param.version = false;
    param.readonly = false;
    param.strArgCount = 0;
    param.usePasswd = false;
    param.fileName = NULL;

    if (fuse_opt_parse(&args, &param, vmasfs_opts, process_arg)) {
        fuse_opt_free_args(&args);
        return EXIT_FAILURE;
    }

    // if all work is done inside options parsing...
    if (param.help) {
        fuse_opt_free_args(&args);
        return EXIT_SUCCESS;
    }
    
    // pass version switch to HELP library to see it's version
    if (!param.version) {
        // no file name passed
        if (param.fileName == NULL) {
            print_usage();
            fuse_opt_free_args(&args);
            return EXIT_FAILURE;
        }

        openlog(PROGRAM, LOG_PID, LOG_USER);
        if ((data = initVmasFS(PROGRAM, param.fileName, param.readonly))
                == NULL) {
            fuse_opt_free_args(&args);
            return EXIT_FAILURE;
        }
        // try password
        if (param.usePasswd) {
            int try_count = 3;
            while (try_count--) {
                if (data->try_passwd(getpass("Enter password: "))) {
                    break;
                }
                fprintf(stderr,"Incorrect!\n");
            }
            if (try_count < 0) {
                fuse_opt_free_args(&args);
                fprintf(stderr,"%s quit!\n", PROGRAM);
                return EXIT_FAILURE;
            }
        }
    }

    static struct fuse_operations vmasfs_oper;
    /* {{{ */
    vmasfs_oper.init       =   vmasfs_init;
    vmasfs_oper.destroy    =   vmasfs_destroy;
    vmasfs_oper.readdir    =   vmasfs_readdir;
    vmasfs_oper.getattr    =   vmasfs_getattr;
    vmasfs_oper.statfs     =   vmasfs_statfs;
    vmasfs_oper.open       =   vmasfs_open;
    vmasfs_oper.read       =   vmasfs_read;
    vmasfs_oper.write      =   vmasfs_write;
    vmasfs_oper.release    =   vmasfs_release;
    vmasfs_oper.unlink     =   vmasfs_unlink;
    vmasfs_oper.rmdir      =   vmasfs_rmdir;
    vmasfs_oper.mkdir      =   vmasfs_mkdir;
    vmasfs_oper.rename     =   vmasfs_rename;
    vmasfs_oper.create     =   vmasfs_create;
    vmasfs_oper.chmod      =   vmasfs_chmod;
    vmasfs_oper.chown      =   vmasfs_chown;
    vmasfs_oper.flush      =   vmasfs_flush;
    vmasfs_oper.fsync      =   vmasfs_fsync;
    vmasfs_oper.fsyncdir   =   vmasfs_fsyncdir;
    vmasfs_oper.opendir    =   vmasfs_opendir;
    vmasfs_oper.releasedir =   vmasfs_releasedir;
    vmasfs_oper.access     =   vmasfs_access;
    vmasfs_oper.utimens    =   vmasfs_utimens;
    vmasfs_oper.ftruncate  =   vmasfs_ftruncate;
    vmasfs_oper.truncate   =   vmasfs_truncate;
    vmasfs_oper.setxattr   =   vmasfs_setxattr;
    vmasfs_oper.getxattr   =   vmasfs_getxattr;
    vmasfs_oper.listxattr  =   vmasfs_listxattr;
    vmasfs_oper.removexattr=   vmasfs_removexattr;
    vmasfs_oper.readlink   =   vmasfs_readlink;
    vmasfs_oper.symlink    =   vmasfs_symlink;

#if FUSE_VERSION >= 28
    // don't allow NULL path
    vmasfs_oper.flag_nullpath_ok = 0;
#endif
    /* }}} */

    struct fuse *fuse;
    char *mountpoint;
    // this flag ignored because libzip does not supports multithreading
    int multithreaded;
    int res;

    fuse = fuse_setup(args.argc, args.argv, &vmasfs_oper, sizeof(vmasfs_oper), &mountpoint, &multithreaded, data);
    fuse_opt_free_args(&args);
    if (fuse == NULL) {
        delete data;
        return EXIT_FAILURE;
    }
    res = fuse_loop(fuse);
    fuse_teardown(fuse, mountpoint);
    return (res == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
/* vim:set st=4 sw=4 et fdm=marker: */
