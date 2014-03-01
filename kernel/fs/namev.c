#include "kernel.h"
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"

/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
 * Most of the work should be done by the vnode's implementation
 * specific lookup() function, but you may want to special case
 * "." and/or ".." here depnding on your implementation.
 *
 * If dir has no lookup(), return -ENOTDIR.
 *
 * Note: returns with the vnode refcount on *result incremented.
 */
int
lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result)
{
    if (dir->vn_ops->lookup == NULL){
        return -ENOTDIR;
    }

    KASSERT(name != NULL);

    if (len == 1 && name[0] == '.'){
        *result = dir;
        vref(*result);
        dbg(DBG_VNREF, "incremented ref count on %d\n", (*result)->vn_vno);
        return 0;
    }
    
    if (len == 2 && name[0] == '.' && name[1] == '.'){
        dbg(DBG_VFS, "enountered the dir '..'\n");
    }

    int lookup_result = dir->vn_ops->lookup(dir, name, len, result);
    
    dbg(DBG_VFS, "result of lookup: %d\n", lookup_result);

    KASSERT(lookup_result == 0);
    KASSERT(result != NULL);

    vref(*result);
    dbg(DBG_VNREF, "incremented ref count on %d\n", (*result)->vn_vno);

    return 0;
}


/* When successful this function returns data in the following "out"-arguments:
 *  o res_vnode: the vnode of the parent directory of "name"
 *  o name: the `basename' (the element of the pathname)
 *  o namelen: the length of the basename
 *
 * For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
 * &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
 * vnode corresponding to "/s5fs/bin" in res_vnode.
 *
 * The "base" argument defines where we start resolving the path from:
 * A base value of NULL means to use the process's current working directory,
 * curproc->p_cwd.  If pathname[0] == '/', ignore base and start with
 * vfs_root_vn.  dir_namev() should call lookup() to take care of resolving each
 * piece of the pathname.
 *
 * Note: A successful call to this causes vnode refcount on *res_vnode to
 * be incremented.
 */
int
dir_namev(const char *pathname, size_t *namelen, const char **name,
          vnode_t *base, vnode_t **res_vnode)
{
    vnode_t *parent = NULL;
    vnode_t *curr;

    if (*pathname == '/'){
        curr = vfs_root_vn;
        vref(curr);
        dbg(DBG_VNREF, "incremented ref count on %d\n", curr->vn_vno);

        while (*pathname == '/'){    
            pathname++;
        }

    } else if (base == NULL){
        curr = curproc->p_cwd;
        dbg(DBG_VNREF, "incremented ref count on %d\n", curr->vn_vno);
    }

    int should_continue = 1;
    int dir_name_start = 0;
    int next_name = 0;

    int cur_name_len;

    while (pathname[next_name] != '\0'){
        if (parent != NULL){
            vput(parent);
            dbg(DBG_VNREF, "decremented ref count on %d\n", parent->vn_vno);
        }

        parent = curr;

        dir_name_start = next_name;

        /* first, find the end of the current dir name */
        while (pathname[next_name] != '/' && pathname[next_name] != '\0'){
            next_name++;
        }

        /* save the length of the current dir name in case we need it
         * outside the loop (if the current dir is actually the base) */
        cur_name_len = next_name - dir_name_start;

        /* then, look up the node */
        int lookup_result = lookup(parent, (pathname + dir_name_start),
                next_name - dir_name_start, &curr);

        /* TODO LOTS of error checking */
        
        /* read away any trailing slashes */
        while (pathname[next_name] == '/'){
            next_name++;
        }
    }

    *namelen = cur_name_len;
    *name = (pathname + dir_name_start);

    if (parent != NULL){
        *res_vnode = parent;
    } else {
        *res_vnode = parent;
        vref(*res_vnode);
        dbg(DBG_VNREF, "incremented ref count on %d\n", parent->vn_vno);
    }

    vput(curr);
    dbg(DBG_VNREF, "decremented ref count on %d\n", curr->vn_vno);

    return 0;
}

/* This returns in res_vnode the vnode requested by the other parameters.
 * It makes use of dir_namev and lookup to find the specified vnode (if it
 * exists).  flag is right out of the parameters to open(2); see
 * <weenix/fnctl.h>.  If the O_CREAT flag is specified, and the file does
 * not exist call create() in the parent directory vnode.
 *
 * Note: Increments vnode refcount on *res_vnode.
 */
int
open_namev(const char *pathname, int flag, vnode_t **res_vnode, vnode_t *base)
{
    size_t namelen;
    const char *name;

    int namev_result = dir_namev(pathname, &namelen, &name, base, res_vnode);

    if (namev_result == 0){
        dbg(DBG_VFS, "found the file %s\n", name);
        return 0;
    }


        NOT_YET_IMPLEMENTED("VFS: open_namev");
        return 0;
}

#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
 * to the given buffer. On success 0 is returned. If 'dir' does not
 * contain 'entry' then -ENOENT is returned. If the given buffer cannot
 * hold the result then it is filled with as many characters as possible
 * and a null terminator, -ERANGE is returned.
 *
 * Files can be uniquely identified within a file system by their
 * inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
 * directories cannot have more than one link there is always
 * a unique solution. The path is writen to the given buffer.
 * On success 0 is returned. On error this function returns a
 * negative error code. See the man page for getcwd(3) for
 * possible errors. Even if an error code is returned the buffer
 * will be filled with a valid string which has some partial
 * information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");

        return -ENOENT;
}
#endif /* __GETCWD__ */
