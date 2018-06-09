
/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

/** @file
 *
 * This file system mirrors the existing file system hierarchy of the
 * system, starting at the root file system. This is implemented by
 * just "passing through" all requests to the corresponding user-space
 * libc functions. Its performance is terrible.
 *
 * Compile with
 *
 *     gcc -Wall passthrough.c `pkg-config fuse3 --cflags --libs` -o passthrough
 *
 * ## Source code ##
 * \include passthrough.c
 */


#define FUSE_USE_VERSION 31

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "file_control.h"
#include <vector>
#include <plog/Log.h>
#include <plog/Appenders/ConsoleAppender.h>

FileControl* control = NULL;

static void *xmp_init(struct fuse_conn_info *conn,
		      struct fuse_config *cfg)
{
	LOG_INFO << "xmp_init";
	(void) conn;
	cfg->use_ino = 1;

	/* Pick up changes from lower filesystem right away. This is
	   also necessary for better hardlink support. When the kernel
	   calls the unlink() handler, it does not know the inode of
	   the to-be-removed entry and can therefore not invalidate
	   the cache of the associated inode - resulting in an
	   incorrect st_nlink value being reported for any remaining
	   hardlinks to this inode. */
	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;

	control->Init();

	return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf,
		       struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_getattr: " << path;
	(void) fi;
	int res;

	control->Sync(path);
	res = lstat(control->Resolve(path).c_str(), stbuf);
	LOG_INFO << "xmp_getattr end: " << path;
	if (res == -1)
		return -errno;
	if (control->FindFile(path)) stbuf->st_mtime = control->FindFile(path)->timestamp;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	LOG_INFO << "xmp_access";
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;

	control->Sync(path);
	res = access(control->Resolve(path).c_str(), mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	LOG_INFO << "xmp_readlink";
	return -EACCES;
/*
	int res;

	char *tmp;
	int len_path = strlen(path);
	int len_pd = strlen(pd);
	tmp = (char *)malloc(len_pd + len_path + 1);
	getnewpath(tmp, path, pd);

	res = readlink(tmp, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
*/
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi,
		       enum fuse_readdir_flags flags)
{
	LOG_INFO << "xmp_readdir";
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;
	(void) flags;

	if (strcmp(path, "/") == 0) {
		filler(buf, ".", NULL, 0, (fuse_fill_dir_flags)0);
		filler(buf, "..", NULL, 0, (fuse_fill_dir_flags)0);
		for(string name: control->KeyNames()) {
			filler(buf, name.c_str(), NULL, 0, (fuse_fill_dir_flags)0);
		}
		return 0;
	}

	control->SyncDir(path);
	dp = opendir(control->Resolve(path).c_str());
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0, (fuse_fill_dir_flags)0))
			break;
	}

	closedir(dp);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	LOG_INFO << "xmp_mknod";
	int res;
	
	if (!control->IsAccessible(path))
		return -EACCES;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(control->Resolve(path).c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(control->Resolve(path).c_str(), mode);
	else
		res = mknod(control->Resolve(path).c_str(), mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	LOG_INFO << "xmp_mkdir";
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;

	res = mkdir(control->Resolve(path).c_str(), mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	LOG_INFO << "xmp_unlink";
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;
	if (control->IsTopLevel(path))
		return -EACCES;

	control->Sync(path);
	control->ClearCache(path);
	res = control->DeleteFile(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	LOG_INFO << "xmp_rmdir";
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;
	if (control->IsTopLevel(path))
		return -EACCES;

	control->SyncDir(path);
	res = rmdir(control->Resolve(path).c_str());
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	LOG_INFO << "xmp_symlink";
	return -EACCES;
/*
	int res;

	if (!control->IsAccessible(from) || !control->IsAccessible(to))
		return -EACCES;
	if (!control->IsTopLevel(from))
		return -EACCES;
	char *tmp;
	int len_from = strlen(from);
	int len_pd = strlen(pd);
	tmp = (char *)malloc(len_pd + len_from + 1);
	getnewpath(tmp, from, pd);

	char *tmp1;
	int len_to = strlen(to);
	tmp1 = (char *)malloc(len_pd + len_to + 1);
	getnewpath(tmp1, to, pd);

	res = symlink(tmp, tmp1);
	if (res == -1)
		return -errno;

	return 0;
*/
}

static int xmp_rename(const char *from, const char *to, unsigned int flags)
{
	LOG_INFO << "xmp_rename";
	int res;

	if (!control->IsAccessible(from) || !control->IsAccessible(to))
		return -EACCES;
	if (control->IsTopLevel(from) || control->IsTopLevel(to))
		return -EACCES;

	if (flags)
		return -EINVAL;

	control->Sync(from);
	control->ClearCache(from);
	control->Sync(to);
	control->ClearCache(to);
	res = control->RenameFile(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	LOG_INFO << "xmp_link";
	return -EACCES;
/*
	int res;

	if (!control->IsAccessible(from) || !control->IsAccessible(to))
		return -EACCES;
	if (!control->IsTopLevel(from))
		return -EACCES;
	char *tmp;
	int len_from = strlen(from);
	int len_pd = strlen(pd);
	tmp = (char *)malloc(len_pd + len_from + 1);
	getnewpath(tmp, from, pd);

	char *tmp1;
	int len_to = strlen(to);
	tmp1 = (char *)malloc(len_pd + len_to + 1);
	getnewpath(tmp1, to, pd);

	res = link(tmp, tmp1);
	if (res == -1)
		return -errno;

	return 0;
*/
}

static int xmp_chmod(const char *path, mode_t mode,
		     struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_chmod";
	(void) fi;
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;

	control->Sync(path);
	res = chmod(control->Resolve(path).c_str(), mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid,
		     struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_chown";
	(void) fi;
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;
	if (control->IsTopLevel(path))
		return -EACCES;

	control->Sync(path);
	res = lchown(control->Resolve(path).c_str(), uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size,
			struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_truncate";
	return -EACCES;
/*
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;
	if (control->IsTopLevel(path))
		return -EACCES;
	char *tmp;
	int len_path = strlen(path);
	int len_pd = strlen(pd);
	tmp = (char *)malloc(len_pd + len_path + 1);
	getnewpath(tmp, path, pd);

	control->TouchFile(path);
	if (fi != NULL)
		res = ftruncate(fi->fh, size);
	else
		res = truncate(tmp, size);
	if (res == -1)
		return -errno;

	return 0;
*/
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2],
		       struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_utimens";
	(void) fi;
	int res;

	control->Sync(path);
	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, control->Resolve(path).c_str(), ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode,
		      struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_create";
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;
	if (control->IsTopLevel(path))
		return -EACCES;

	control->Sync(path);
	res = control->NewFile(path, fi->flags, mode);
	if (res == -1)
		return -errno;

	fi->fh = res;
	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_open";
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;
	if (control->IsTopLevel(path))
		return -EACCES;

	control->Sync(path);
	res = open(control->Resolve(path).c_str(), fi->flags);
	if (res == -1)
		return -errno;

	fi->fh = res;
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_read";
	int fd;
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;
	if (control->IsTopLevel(path))
		return -EACCES;

	if(fi == NULL)
		fd = open(control->Resolve(path).c_str(), O_RDONLY);
	else
		fd = fi->fh;
	
	if (fd == -1)
		return -errno;

	res = control->ReadFile(path, fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_write";
	int fd;
	int res;

	if (!control->IsAccessible(path))
		return -EACCES;
	if (control->IsTopLevel(path))
		return -EACCES;

	(void) fi;
	if(fi == NULL) {
		fd = open(control->Resolve(path).c_str(), O_WRONLY);
		LOG_INFO << "xmp_write fd 1";
	} else {
		fd = fi->fh;
		LOG_INFO << "xmp_write fd 2";
	}
	LOG_INFO << "xmp_write fd get = " << fd;
	
	if (fd == -1)
		return -errno;

	res = control->WriteFile(path, fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	LOG_INFO << "xmp_statfs";
	int res;

	control->Sync(path);
	res = statvfs(control->Resolve(path).c_str(), stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_release";
	control->Sync(path);
	close(fi->fh);
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_fsync:" << path;
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) isdatasync;
	(void) fi;
	control->Sync(path);
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	LOG_INFO << "xmp_fallocate";
	return -EACCES;
	// int fd;
	// int res;

	// (void) fi;

	// char *tmp;
	// int len_path = strlen(path);
	// int len_pd = strlen(pd);
	// tmp = (char *)malloc(len_pd + len_path + 1);
	// getnewpath(tmp, path, pd);

	// if (mode)
	// 	return -EOPNOTSUPP;

	// if(fi == NULL)
	// 	fd = open(tmp, O_WRONLY);
	// else
	// 	fd = fi->fh;
	
	// if (fd == -1)
	// 	return -errno;

	// res = -posix_fallocate(fd, offset, length);

	// if(fi == NULL)
	// 	close(fd);
	// return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	LOG_INFO << "xmp_setxattr";
	control->Sync(path);
	int res = lsetxattr(control->Resolve(path).c_str(), name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	LOG_INFO << "xmp_getxattr";
	control->Sync(path);
	int res = lgetxattr(control->Resolve(path).c_str(), name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	LOG_INFO << "xmp_listxattr";
	control->Sync(path);
	int res = llistxattr(control->Resolve(path).c_str(), list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	LOG_INFO << "xmp_removexattr";
	control->Sync(path);
	int res = lremovexattr(control->Resolve(path).c_str(), name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper;

void bind()
{
	xmp_oper.init       = xmp_init;
	xmp_oper.getattr	= xmp_getattr;
	xmp_oper.access		= xmp_access;
	xmp_oper.readlink	= xmp_readlink;
	xmp_oper.readdir	= xmp_readdir;
	xmp_oper.mknod		= xmp_mknod;
	xmp_oper.mkdir		= xmp_mkdir;
	xmp_oper.symlink	= xmp_symlink;
	xmp_oper.unlink		= xmp_unlink;
	xmp_oper.rmdir		= xmp_rmdir;
	xmp_oper.rename		= xmp_rename;
	xmp_oper.link		= xmp_link;
	xmp_oper.chmod		= xmp_chmod;
	xmp_oper.chown		= xmp_chown;
	xmp_oper.truncate	= xmp_truncate;
#ifdef HAVE_UTIMENSAT
	xmp_oper.utimens	= xmp_utimens;
#endif
	xmp_oper.open		= xmp_open;
	xmp_oper.create 	= xmp_create;
	xmp_oper.read		= xmp_read;
	xmp_oper.write		= xmp_write;
	xmp_oper.statfs		= xmp_statfs;
	xmp_oper.release	= xmp_release;
	xmp_oper.fsync		= xmp_fsync;
#ifdef HAVE_POSIX_FALLOCATE
	xmp_oper.fallocate	= xmp_fallocate;
#endif
#ifdef HAVE_SETXATTR
	xmp_oper.setxattr	= xmp_setxattr;
	xmp_oper.getxattr	= xmp_getxattr;
	xmp_oper.listxattr	= xmp_listxattr;
	xmp_oper.removexattr	= xmp_removexattr;
#endif
}

namespace plog
{
template<class Formatter> // Typically a formatter is passed as a template parameter.
class MyAppender : public IAppender // All appenders MUST inherit IAppender interface.
{
public:
	FILE* fd;
	MyAppender()
	{
		char buf[1024];
		sprintf(buf, "log%d.txt", getpid());
		fd = fopen(buf, "w");
	}

	virtual void write(const Record& record) // This is a method from IAppender that MUST be implemented.
	{
		util::nstring str = Formatter::format(record); // Use the formatter to get a string from a record.
		fwrite(str.c_str(), 1, str.length(), fd);
		fflush(fd);
	}
};
}

int main(int argc, char *argv[])
{
	/*
		argc = 2 + 1 + keycount
		argv:
			./passthrought.c
			mountpoint
			/theplace_real
			name1:key1
			name1:key2
			......
	*/
	static plog::MyAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::debug, &consoleAppender);

	vector<string> keys;

	for (int i = 3; i < argc; i++)
		keys.push_back(argv[i]);

	control = new FileControl(argv[2], keys);

	// umask(0);
	bind();
	return fuse_main(2, argv, &xmp_oper, NULL);
}
