/*-------------------------------------------------------------------------
 *
 * windows.c
 *	  Windows platform implementation
 *
 * This file contains Windows implementations of functions that are
 * normally provided by POSIX on Unix systems.
 *
 *-------------------------------------------------------------------------
 */

#include "platform.h"
#include <io.h>
#include <winsock2.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <time.h>
#include <errno.h>	/* ENOSYS */

/*
 * Windows implementation for strndup (not available in standard library)
 */
char *
platform_strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *result = malloc(len + 1);
	
	if (result)
	{
		memcpy(result, s, len);
		result[len] = '\0';
	}
	
	return result;
}

/*
 * Windows implementation for getline (not available in standard library)
 */
ssize_t
getline(char **lineptr, size_t *n, FILE *stream)
{
	size_t pos = 0;
	int c;
	
	if (lineptr == NULL || n == NULL || stream == NULL)
		return -1;
	
	if (*lineptr == NULL)
	{
		*n = 128;
		*lineptr = malloc(*n);
		if (*lineptr == NULL)
			return -1;
	}
	
	while ((c = fgetc(stream)) != EOF)
	{
		if (pos + 1 >= *n)
		{
			size_t new_size = *n * 2;
			char *new_ptr = realloc(*lineptr, new_size);
			if (new_ptr == NULL)
				return -1;
			*lineptr = new_ptr;
			*n = new_size;
		}
		
		(*lineptr)[pos++] = c;
		if (c == '\n')
			break;
	}
	
	if (pos == 0)
		return -1;
	
	(*lineptr)[pos] = '\0';
	return pos;
}

/*
 * Windows implementation for basename/dirname from libgen.h
 */
char *
platform_basename(char *path)
{
	char *p;
	
	if (!path || !*path)
		return ".";

	p = path + strlen(path) - 1;
	while (p > path && (*p == '\\' || *p == '/'))
		*p-- = '\0';

	p = strrchr(path, '\\');
	if (!p)
		p = strrchr(path, '/');

	return p ? p + 1 : path;
}

char *
platform_dirname(char *path)
{
	static char dot[] = ".";
	char *p;
	
	if (!path || !*path)
		return dot;

	p = path + strlen(path) - 1;

	while (p > path && (*p == '\\' || *p == '/'))
		*p-- = '\0';

	while (p > path && *p != '\\' && *p != '/')
		p--;

	if (p == path)
	{
		if (*p == '\\' || *p == '/')
			return path;
		return dot;
	}

	while (p > path && (*p == '\\' || *p == '/'))
		p--;

	p[1] = '\0';
	return path;
}

int
platform_usleep(unsigned int usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * (LONGLONG)usec);

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	if (timer == NULL)
		return -1;

	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);

	return 0;
}

/*
 * Windows implementation of poll() using select()
 * Limited implementation - only handles what pspg needs
 */
#ifndef WSAPoll

#define pollfd pollfd_custom

struct pollfd_custom {
	int fd;
	short events;
	short revents;
};

#undef POLLIN
#undef POLLOUT
#undef POLLERR

#define POLLIN  0x0001
#define POLLOUT 0x0004
#define POLLERR 0x0008

int
poll(struct pollfd_custom *fds, unsigned int nfds, int timeout)
{
	fd_set readfds, writefds, exceptfds;
	struct timeval tv, *tvp;
	int maxfd = -1;
	unsigned int i;
	int rc;
	
#ifdef _WIN32
	/*
	 * Special handling for console input on Windows with PDCurses.
	 * PDCurses uses getch() which handles console reads directly,
	 * so we just indicate input is available.
	 */
	if (nfds > 0 && fds[0].fd >= 0)
	{
		HANDLE hInput = (HANDLE)_get_osfhandle(fds[0].fd);
		DWORD fdwMode;

		if (hInput != INVALID_HANDLE_VALUE && GetConsoleMode(hInput, &fdwMode))
		{
			if (timeout > 0)
				Sleep(timeout);
			else if (timeout < 0)
				Sleep(100);

			fds[0].revents = POLLIN;
			return 1;
		}
	}
#endif

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);	for (i = 0; i < nfds; i++)
	{
		if (fds[i].fd < 0)
			continue;
			
		if (fds[i].events & POLLIN)
			FD_SET(fds[i].fd, &readfds);
		if (fds[i].events & POLLOUT)
			FD_SET(fds[i].fd, &writefds);
		FD_SET(fds[i].fd, &exceptfds);
		
		if (fds[i].fd > maxfd)
			maxfd = fds[i].fd;
		
		fds[i].revents = 0;
	}
	
	if (timeout < 0)
		tvp = NULL;
	else
	{
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		tvp = &tv;
	}
	
	rc = select(maxfd + 1, &readfds, &writefds, &exceptfds, tvp);
	
	if (rc > 0)
	{
		for (i = 0; i < nfds; i++)
		{
			if (fds[i].fd < 0)
				continue;
				
			if (FD_ISSET(fds[i].fd, &readfds))
				fds[i].revents |= POLLIN;
			if (FD_ISSET(fds[i].fd, &writefds))
				fds[i].revents |= POLLOUT;
			if (FD_ISSET(fds[i].fd, &exceptfds))
				fds[i].revents |= POLLERR;
		}
	}
	
	return rc;
}

#endif /* WSAPoll */

/*
 * Simplified popen for Windows
 * The fork/pipe/exec implementation in infra.c won't work on Windows
 */
int
run_command_with_pipes_win32(const char *command, int *fin, int *fout, int *ferr)
{
	/* TODO: Implement using CreateProcess + CreatePipe */
	*fin = -1;
	*fout = -1;
	*ferr = -1;
	return -1;
}

/*
 * getpass for Windows - read password without echoing.
 * Password input with echo disabled using Windows console API.
 */
char *
getpass(const char *prompt)
{
	static char password[128];
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode, count;
	
	fputs(prompt, stderr);
	fflush(stderr);
	
	if (hStdin == INVALID_HANDLE_VALUE || !GetConsoleMode(hStdin, &mode))
		return NULL;
	
	SetConsoleMode(hStdin, mode & (~ENABLE_ECHO_INPUT));
	
	if (!ReadConsoleA(hStdin, password, sizeof(password) - 1, &count, NULL))
	{
		SetConsoleMode(hStdin, mode);
		return NULL;
	}
	
	SetConsoleMode(hStdin, mode);
	
	password[count] = '\0';
	while (count > 0 && (password[count - 1] == '\r' || password[count - 1] == '\n'))
		password[--count] = '\0';
	
	fputc('\n', stderr);
	
	return password;
}

/*
 * Stub implementations for Unix-specific functions that are not used on Windows
 * but are referenced through compat headers
 */

int
pipe(int pipefd[2])
{
	(void)pipefd;
	errno = ENOSYS;
	return -1;
}

int
fork(void)
{
	errno = ENOSYS;
	return -1;
}

#ifdef _MSC_VER /* implemented in (win)pthread MinGW library */
int clock_gettime(int clk_id, struct timespec *tp)
{
	static LARGE_INTEGER frequency = { 0 };
	LARGE_INTEGER counter;

	if (frequency.QuadPart == 0) {
		QueryPerformanceFrequency(&frequency);
	}

	QueryPerformanceCounter(&counter);

	tp->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
	tp->tv_nsec = (long)((counter.QuadPart % frequency.QuadPart) * 1000000000 / frequency.QuadPart);

	return 0;
}
#endif /* _MSC_VER */
