
#include "pipe2.h"

/* Specification.  */
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>

#ifndef O_CLOEXEC
#	define O_CLOEXEC	0
#endif

int
pipe2(int fd[2], int flags)
{
/* Check the supported flags.  */
	if ((flags & ~(O_CLOEXEC | O_NONBLOCK)) != 0)
	{
		errno = EINVAL;
		return -1;
	}

	if (pipe (fd) < 0)
		return -1;

/* POSIX <http://www.opengroup.org/onlinepubs/9699919799/functions/pipe.html>
   says that initially, the O_NONBLOCK and FD_CLOEXEC flags are cleared on
   both fd[0] amd fd[1].  */
	if (flags & O_NONBLOCK)
	{
		int fcntl_flags;

		if ((fcntl_flags = fcntl (fd[1], F_GETFL, 0)) < 0
			|| fcntl (fd[1], F_SETFL, fcntl_flags | O_NONBLOCK) == -1
			|| (fcntl_flags = fcntl (fd[0], F_GETFL, 0)) < 0
			|| fcntl (fd[0], F_SETFL, fcntl_flags | O_NONBLOCK) == -1)
			goto fail;
	}

	if (flags & O_CLOEXEC)
	{
		int fcntl_flags;

		if ((fcntl_flags = fcntl (fd[1], F_GETFD, 0)) < 0
			|| fcntl (fd[1], F_SETFD, fcntl_flags | FD_CLOEXEC) == -1
			|| (fcntl_flags = fcntl (fd[0], F_GETFD, 0)) < 0
			|| fcntl (fd[0], F_SETFD, fcntl_flags | FD_CLOEXEC) == -1)
			goto fail;
	}

#ifdef O_BINARY
	if (flags & O_BINARY)
	{
		setmode (fd[1], O_BINARY);
		setmode (fd[0], O_BINARY);
	}
	else if (flags & O_TEXT)
	{
		setmode (fd[1], O_TEXT);
		setmode (fd[0], O_TEXT);
	}
#endif

	return 0;

fail:
	{
		int saved_errno = errno;
		close (fd[0]);
		close (fd[1]);
		errno = saved_errno;
		return -1;
	}
}

/* eof */
