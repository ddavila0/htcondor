/* 
** Copyright 1994 by Miron Livny, and Mike Litzkow
** 
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted,
** provided that the above copyright notice appear in all copies and that
** both that copyright notice and this permission notice appear in
** supporting documentation, and that the names of the University of
** Wisconsin and the copyright holders not be used in advertising or
** publicity pertaining to distribution of the software without specific,
** written prior permission.  The University of Wisconsin and the 
** copyright holders make no representations about the suitability of this
** software for any purpose.  It is provided "as is" without express
** or implied warranty.
** 
** THE UNIVERSITY OF WISCONSIN AND THE COPYRIGHT HOLDERS DISCLAIM ALL
** WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE UNIVERSITY OF
** WISCONSIN OR THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT
** OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
** OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
** OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
** OR PERFORMANCE OF THIS SOFTWARE.
** 
** Author:  Mike Litzkow
**
*/ 

/*******************************************************************
  System call stubs which need special treatment and cannot be generated
  automatically go here...
*******************************************************************/
#define _POSIX_SOURCE

#if defined(Solaris)
#include "../condor_includes/_condor_fix_types.h"
#include </usr/ucbinclude/sys/rusage.h>
#endif 


	/* Temporary - need to get real PSEUDO definitions brought in... */
#define PSEUDO_getwd	1

#include "syscall_numbers.h"
#include "condor_syscall_mode.h"
#include "condor_constants.h"
#include "file_table_interf.h"
#include <stdio.h>
#include <limits.h>
#if defined(HPUX9)
#	ifdef _PROTOTYPES   /* to remove compilation errors unistd.h */
#	undef _PROTOTYPES
#	endif
#endif

#include <unistd.h>

#include "_condor_fix_types.h"
#include "condor_fix_timeval.h"
#include <sys/resource.h>
#include <sys/uio.h>
#include <errno.h>
#include "debug.h"

static int fake_readv( int fd, const struct iovec *iov, int iovcnt );
static int fake_writev( int fd, const struct iovec *iov, int iovcnt );

/*Local fake readv, writev's for linux.  Linux does not have a SYS_readv	*/
/*or SYS_writev									*/
static int linux_fake_readv( int fd, const struct iovec *iov, int iovcnt );
static int linux_fake_writev( int fd, const struct iovec *iov, int iovcnt );

char	*getwd( char * );

/*
  The process should exit making the status value available to its parent
  (the starter) - can only be a local operation.
*/
void
_exit( status )
int status;
{
	(void) syscall( SYS_exit, status );
}

/*
  The Open Files Table module needs to keep the current working
  directory in memory so that it can remember pathnames of files that
  are opened, even if they are opened by simple file names.  We first
  try to do the indicated change of directory, and then if that works,
  we record the new current working directory.
*/
int
chdir( const char *path )
{
	int rval, status;
	char	tbuf[ _POSIX_PATH_MAX ];

		/* Try the system call */
	if( LocalSysCalls() ) {
		rval = syscall( SYS_chdir, path );
	} else {
		rval = REMOTE_syscall( CONDOR_chdir, path );
	}

		/* If it fails we can stop here */
	if( rval < 0 ) {
		return rval;
	}

	if( MappingFileDescriptors ) {
		store_working_directory();
	}

	return rval;
}

#if defined(SYS_fchdir)
int
fchdir( int fd )
{
	int rval, real_fd;

	if( MappingFileDescriptors() ) {
		if( (real_fd = MapFd(fd)) < 0 ) {
			return -1;
		}
	}

		/* Try the system call */
	if( LocalSysCalls() ) {
		rval = syscall( SYS_fchdir, real_fd );
	} else {
		rval = REMOTE_syscall( CONDOR_fchdir, real_fd );
	}

		/* If it fails we can stop here */
	if( rval < 0 ) {
		return rval;
	}

	if( MappingFileDescriptors ) {
		store_working_directory();
	}

	return rval;
}
#endif

/*
Keep Condor's version of the CWD up to date
*/
store_working_directory()
{
	char	tbuf[ _POSIX_PATH_MAX ];
	char	*status;

#if defined(HPUX9)
	/* avoid infinite recursion in HPUX9, where getwd calls chdir, which
	   calls store_working_directory, which calls getwd...  - Jim B. */
	static int inside_call = 0;

	if (inside_call)
		return;
	inside_call = 1;
#endif

		/* Get the information */
#if defined(Solaris)
	status = getcwd( tbuf, _POSIX_PATH_MAX);
#else
	status = getwd( tbuf );
#endif

		/* This routine returns 0 on error! */
	if( !status ) {
		fprintf( stderr, tbuf );
		abort();
	}

		/* Ok - everything worked */
	Set_CWD( tbuf );

#if defined(HPUX9)
	inside_call = 0;
#endif
}

/*
  Condor doesn't support the fork() system call, so by definition the
  resource usage of all our child processes is zero.  We must support
  this, since those users in the POSIX world will call utimes() which
  probably checks resource ruage for children - even though there are
  none.

  In the remote case things are a bit more complicated.  The rusage
  should he the sum of what the user process has accumulated on the
  current machine, and the usages it accumulated on all the machines
  where it has run in the past.
*/

/*
#if defined(IRIX53)

int
getrusage( int who, struct rusage *rusage )
{ 
	dprintf(D_ALWAYS, "Aborting on getrusage\n"); 
	abort(); 
	return(0);
}
*/

#if !defined(Solaris) && !defined(IRIX53)
int
getrusage( int who, struct rusage *rusage )
{
	int rval;
	struct rusage accum_rusage;

	if( LocalSysCalls() ) {
		rval = syscall( SYS_getrusage, who, rusage );
	} else {

			/* Condor user processes don't have children - yet */
		if( who != RUSAGE_SELF ) {
			memset( (char *)rusage, '\0', sizeof(struct rusage) );
			return 0;
		}

			/* Get current rusage for the running job. */
		syscall( SYS_getrusage, who, rusage);

			/* Get accumulated rusage from previous runs */
		rval = REMOTE_syscall( CONDOR_getrusage, who, &accum_rusage );

			/* Sum the two. */
		update_rusage(rusage, &accum_rusage);
	}

	return( rval );
}
#endif

/*
  This routine which is normally provided in the C library determines
  whether a given file descriptor refers to a tty.  The underlying
  mechanism is an ioctl, but Condor does not support ioctl.  We
  therefore provide a "quick and dirty" substitute here.  This may
  not always be correct, but it will get you by in most cases.
*/
int
isatty( int filedes )
{
		/* Stdin, stdout, and stderr are redirected to normal
		** files for all condor jobs.
		*/
	if( RemoteSysCalls() ) {
		return FALSE;
	}

		/* Assume stdin, stdout, and stderr are the only ttys */
	switch( filedes ) {
		case 0:
		case 1:
		case 2:
				return TRUE;
		default:
			return FALSE;
	}
}

/*
  We don't handle readv directly in remote system calls.  Instead we
  break the readv up into a series of individual reads.
*/
#if defined(HPUX9) || defined(LINUX)
ssize_t
readv( int fd, const struct iovec *iov, size_t iovcnt )
#else
int
readv( int fd, struct iovec *iov, int iovcnt )
#endif
{
	int rval;
	int user_fd;

	if( (user_fd=MapFd(fd)) < 0 ) {
		return -1;
	}

	if( LocalSysCalls() ) {
		#if defined(LINUX)
		rval = linux_fake_readv( user_fd, iov, iovcnt );
		#else
		rval = syscall( SYS_readv, user_fd, iov, iovcnt );
		#endif
	} else {
		rval = fake_readv( user_fd, iov, iovcnt );
	}

	return rval;
}

/*
  Handle a call to readv which must be done remotely, (not NFS).  Note
  the user file descriptor has already been mapped by the calling routine,
  and we already know this is a remote system call.  We handle the call
  by breaking it up into a series of remote reads.
*/
static int
fake_readv( int fd, const struct iovec *iov, int iovcnt )
{
	register int i, rval = 0, cc;

	for( i = 0; i < iovcnt; i++ ) {
		cc = REMOTE_syscall( CONDOR_read, fd, iov->iov_base, iov->iov_len );
		if( cc < 0 ) {
			return cc;
		}

		rval += cc;
		if( cc != iov->iov_len ) {
			return rval;
		}

		iov++;
	}

	return rval;
}

/*Fake local readv for Linux						*/
static int
linux_fake_readv( int fd, const struct iovec *iov, int iovcnt )
{
	register int i, rval = 0, cc;

	for( i = 0; i < iovcnt; i++ ) {
		cc = syscall( SYS_read, fd, iov->iov_base, iov->iov_len );
		if( cc < 0 ) {
			return cc;
		}

		rval += cc;
		if( cc != iov->iov_len ) {
			return rval;
		}

		iov++;
	}

	return rval;
}


/*
  We don't handle writev directly in remote system calls.  Instead we
  break the writev up into a series of individual writes.
*/
#if defined(HPUX9) || defined(LINUX) 
ssize_t
writev( int fd, const struct iovec *iov, size_t iovcnt )
#elif defined(Solaris)
ssize_t
writev( int fd, const struct iovec *iov, int iovcnt )
#else
int
writev( int fd, struct iovec *iov, int iovcnt )
#endif
{
	int rval;
	int user_fd;

	if( (user_fd=MapFd(fd)) < 0 ) {
		return -1;
	}

	if( LocalSysCalls() ) {
		#if defined(LINUX)
		rval = linux_fake_writev( user_fd, iov, iovcnt );
		#else
		rval = syscall( SYS_writev, user_fd, iov, iovcnt );
		#endif
	} else {
		rval = fake_writev( user_fd, iov, iovcnt );
	}

	return rval;
}

/*
  Handle a call to writev which must be done remotely, (not NFS).  Note
  the user file descriptor has already been mapped by the calling routine,
  and we already know this is a remote system call.  We handle the call
  by breaking it up into a series of remote writes.
*/
static int
fake_writev( int fd, const struct iovec *iov, int iovcnt )
{
	register int i, rval = 0, cc;

	for( i = 0; i < iovcnt; i++ ) {
		cc = REMOTE_syscall( CONDOR_write, fd, iov->iov_base, iov->iov_len );
		if( cc < 0 ) {
			return cc;
		}

		rval += cc;
		if( cc != iov->iov_len ) {
			return rval;
		}

		iov++;
	}

	return rval;
}

/*Fake local writev for Linux						*/
static int
linux_fake_writev( int fd, const struct iovec *iov, int iovcnt )
{
	register int i, rval = 0, cc;

	for( i = 0; i < iovcnt; i++ ) {
		cc = syscall( SYS_write, fd, iov->iov_base, iov->iov_len );
		if( cc < 0 ) {
			return cc;
		}

		rval += cc;
		if( cc != iov->iov_len ) {
			return rval;
		}

		iov++;
	}

	return rval;
}

#if defined(SUNOS41)
#	include <sys/termio.h>
#	include <sys/mtio.h>
#endif

#if defined(HPUX9)
#	include <sys/mib.h>
#	include <sys/ioctl.h>
#endif

#if defined(Solaris)
/* int
ioctl( int fd, int request, ...) */
#else
int
ioctl( int fd, int request, caddr_t arg )
{
#if defined(HPUX9)
	static int first_time = 1;
	static int MaxOpenFiles;
	static struct nmparms *parmset;
	if (first_time) {
		first_time = 0;
		MaxOpenFiles = sysconf(_SC_OPEN_MAX);
		parmset = (struct nmparms *)calloc(sizeof(struct nmparms),
										   MaxOpenFiles);
	}
	if (request == 0x40206d02) {
		struct nmparms *ptr = (struct nmparms *)arg;
		dprintf( D_ALWAYS, "got NMIOSET request 0x%x on fd %d\n",
				request, fd );
		dprintf( D_ALWAYS, "objid = %d, buffer = 0x%x, len = 0x%x\n",
				ptr->objid, ptr->buffer, ptr->len );
		parmset[fd].objid = ptr->objid;
		parmset[fd].buffer = ptr->buffer;
		parmset[fd].len = ptr->len;
		return 0;
	}
	else if (request == 0x80086d01) {
		struct nmparms *ptr = (struct nmparms *)arg;
		dprintf( D_ALWAYS, "got NMIOGET request 0x%x on fd %d\n",
				request, fd);
		ptr->objid = parmset[fd].objid;
		ptr->buffer = parmset[fd].buffer;
		ptr->len = parmset[fd].len;
		dprintf( D_ALWAYS, "objid = %d, buffer = 0x%x, len = 0x%x\n",
				ptr->objid, ptr->buffer, ptr->len );
		return 0;
	}		
#endif
	switch( request ) {
#if defined(SUNOS41)
	  case MTIOCGET:
		fprintf( stderr, "Got mag tape request - reply -1\n" );
		return -1;
	  case TCGETA:
		fprintf( stderr, "Got terminal IO request - reply -1\n" );
		return -1;
#endif
	  default:
		fprintf( stderr, "Got (unknown) ioctl 0x%x on fd %d - reply -1\n",
															request, fd );
		return -1;
	}
}
#endif

#if defined(AIX32)
	char *
	getwd( char *path )
	{
		if( LocalSysCalls() ) {
			return getcwd( path, _POSIX_PATH_MAX );
		} else {
			return (char *)REMOTE_syscall( CONDOR_getwd, path );
		}
	}
#endif

#if defined(AIX32)
	int
	kwritev( int fd, struct iovec *iov, int iovcnt, int ext )
	{
		int rval;
		int user_fd;

		if( ext != 0 ) {
			errno = ENOSYS;
			return -1;
		}

		if( (user_fd=MapFd(fd)) < 0 ) {
			return -1;
		}

		if( LocalSysCalls() ) {
			rval = syscall( SYS_kwritev, user_fd, iov, iovcnt );
		} else {
			rval = fake_writev( user_fd, iov, iovcnt );
		}

		return rval;
	}
#endif

#if defined(AIX32)
	int
	kreadv( int fd, struct iovec *iov, int iovcnt, int ext )
	{
		int rval;
		int user_fd;

		if( ext != 0 ) {
			errno = ENOSYS;
			return -1;
		}

		if( (user_fd=MapFd(fd)) < 0 ) {
			return -1;
		}

		if( LocalSysCalls() ) {
			rval = syscall( SYS_kreadv, user_fd, iov, iovcnt );
		} else {
			rval = fake_readv( user_fd, iov, iovcnt );
		}

		return rval;
	}
#endif

#if defined OSF1
/*
  This is some kind of cleanup routine for dynamically linked programs which
  is called by exit.  For some reason it occasionally cuases a SEGV
  when mixed with the condor checkpointing code.  Since condor programs
  are always statically linked, we just make a dummy here to avoid
  the problem.
*/

void ldr_atexit() {}
#endif

/* This has been added to solve the problem with the printf and fprintf 
statements on alphas but have not enclosed it in ifdefs since it will not
harm to other platforms */

__write(int fd, char *buf, int size)
{
return write(fd,buf,size);
}

/* These are similar additions as above.  This problem cropped up for 
   FORTRAN programs on Solaris 2.4 and OSF1.  -Jim B. */

#if !defined(HPUX9)
#if defined( SYS_write )
_write( int fd, const void *buf, size_t len )
{
	return write(fd,buf,len);
}
#endif

#if defined( SYS_read )
_read( int fd, void *buf, size_t len )
{
	return read(fd,buf,len);
}

__read( int fd, void *buf, size_t len )
{
	return read(fd,buf,len);
}
#endif

#if defined( SYS_lseek )
off_t
_lseek( int fd, off_t offset, int whence )
{
	return lseek(fd,offset,whence);
}

__lseek( int fd, off_t offset, int whence )
{
	return lseek(fd,offset,whence);
}
#endif

#if defined(OSF1)
char *
__getwd( char *path_name )
{
	return getwd(path_name);
}

char *getcwd ( char *buffer, size_t size )
{
	if (buffer == NULL) {
		buffer = (char *)malloc(size);
	}
	return getwd( buffer );
}	

#if defined( SYS_ftruncate )
int
ftruncate( int fd, off_t length )
{
	int	rval;
	int	user_fd;
	int use_local_access = FALSE;

	/* The below length check is a hack to patch an f77 problem on
	   OSF1.  - Jim B. */

	if (length < 0)
		return 0;

	if( (user_fd=MapFd(fd)) < 0 ) {
		return (int)-1;
	}
	if( LocalAccess(fd) ) {
		use_local_access = TRUE;
	}

	if( LocalSysCalls() || use_local_access ) {
		rval = syscall( SYS_ftruncate, user_fd, length );
	} else {
		rval = REMOTE_syscall( CONDOR_ftruncate, user_fd, length );
	}

	return rval;
}
#endif /* defined( SYS_ftruncate ) */

#endif /* defined(OSF1) */
#endif /* !defined(HPUX9) */
