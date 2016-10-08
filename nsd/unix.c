/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://mozilla.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 *
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */


#ifndef _WIN32

/*
 * unix.c --
 *
 *  Unix specific routines.
 */

#include "nsd.h"
#include <pwd.h>

typedef enum {
    PwUID, PwNAME, PwDIR, PwGID
} PwElement;


/*
 * Static functions defined in this file.
 */

static int Pipe(int *fds, int sockpair)
    NS_GNUC_NONNULL(1);

static void Abort(int signal);

static bool GetPwNam(const char *user, PwElement elem, long *longResult, Ns_DString *dsPtr, char **freePtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(5);

/*
 * Static variables defined in this file.
 */

#if !defined(HAVE_GETPWNAM_R) || !defined(HAVE_GETPWUID_R) || !defined(HAVE_GETGRGID_R) || !defined(HAVE_GETGRNAM_R)
static Ns_Mutex lock = NULL;
#endif
static int debugMode = 0;


/*
 *----------------------------------------------------------------------
 *
 * NsBlockSignal --
 *
 *      Mask one specific signal.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Signal will be pending until NsRestoreSignal.
 *
 *----------------------------------------------------------------------
 */

void
NsBlockSignal(int signal)
{
    sigset_t set;

    sigemptyset(&set);
    (void)sigaddset(&set, signal);
    ns_sigmask(SIG_BLOCK, &set, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsUnblockSignal --
 *
 *      Restores one specific signal.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Signal will be unblock.
 *
 *----------------------------------------------------------------------
 */

void
NsUnblockSignal(int signal)
{
    sigset_t set;

    sigemptyset(&set);
    (void)sigaddset(&set, signal);
    ns_sigmask(SIG_UNBLOCK, &set, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsBlockSignals --
 *
 *      Block signals at startup.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Signals will be pending until NsHandleSignals.
 *
 *----------------------------------------------------------------------
 */

void
NsBlockSignals(int debug)
{
    sigset_t set;

    /*
     * Block SIGHUP, SIGPIPE, SIGTERM, SIGQUIT and SIGINT. This mask is
     * inherited by all subsequent threads so that only this
     * thread will catch the signals in the sigwait() loop below.
     * Unfortunately this makes it impossible to kill the
     * server with a signal other than SIGKILL until startup
     * is complete.
     */

    debugMode = debug;
    sigemptyset(&set);
    (void)sigaddset(&set, SIGPIPE);
    (void)sigaddset(&set, SIGTERM);
    (void)sigaddset(&set, SIGHUP);
    (void)sigaddset(&set, SIGQUIT);
    if (debugMode == 0) {
        /* NB: Don't block SIGINT in debug mode for Solaris dbx. */
        (void)sigaddset(&set, SIGINT);
    }
    ns_sigmask(SIG_BLOCK, &set, NULL);

    /*
     * Make sure "synchronous" signals (those generated by execution
     * errors like SIGSEGV or SIGILL which get delivered to the thread
     * that caused them) have an appropriate handler installed.
     */

    ns_signal(SIGILL, Abort);
    ns_signal(SIGTRAP, Abort);
    ns_signal(SIGBUS, Abort);
    ns_signal(SIGSEGV, Abort);
    ns_signal(SIGFPE, Abort);
}


/*
 *----------------------------------------------------------------------
 * NsRestoreSignals --
 *
 *      Restore all signals to their default value.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
NsRestoreSignals(void)
{
    sigset_t set;
    int sig;

    for (sig = 1; sig < NSIG; ++sig) {
        ns_signal(sig, (void (*)(int)) SIG_DFL);
    }
    sigfillset(&set);
    ns_sigmask(SIG_UNBLOCK, &set, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsHandleSignals --
 *
 *      Loop forever processing signals until a term signal
 *      is received.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      HUP callbacks may be called.
 *
 *----------------------------------------------------------------------
 */

int
NsHandleSignals(void)
{
    sigset_t set;
    int err, sig;

    /*
     * Wait endlessly for trigger wakeups.
     */

    sigemptyset(&set);
    (void)sigaddset(&set, SIGTERM);
    (void)sigaddset(&set, SIGHUP);
    (void)sigaddset(&set, SIGQUIT);
    if (debugMode == 0) {
        (void)sigaddset(&set, SIGINT);
    }
    do {
        do {
            err = ns_sigwait(&set, &sig);
        } while (err == EINTR);
        if (err != 0) {
            Ns_Fatal("signal: ns_sigwait failed: %s", strerror(errno));
        }
        if (sig == SIGHUP) {
            NsRunSignalProcs();
        }
    } while (sig == SIGHUP);

    /*
     * Unblock the signals and exit.
     */

    ns_sigmask(SIG_UNBLOCK, &set, NULL);

    return sig;
}


/*
 *----------------------------------------------------------------------
 *
 * NsSendSignal --
 *
 *      Send a signal to the main thread.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Main thread in NsHandleSignals will wakeup.
 *
 *----------------------------------------------------------------------
 */

void
NsSendSignal(int sig)
{
    if (sig == NS_SIGTERM) {
        NS_finalshutdown = 1;
    }
    if (kill(Ns_InfoPid(), sig) != 0) {
        Ns_Fatal("unix: kill() failed: '%s'", strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsMemMap --
 *
 *      Maps a file to memory. The file will be mapped as shared
 *      and read or write, depeding on the passed mode.
 *
 * Results:
 *      NS_OK - file was mapped OK; details of the mapped address
 *              and the mapped size are left in the FileMap struct ptr.
 *
 *      NS_ERROR - operation failed.
 *
 * Side effects:
 *      If the operation failed, server log event will be sent.
 *
 *----------------------------------------------------------------------
 */

Ns_ReturnCode
NsMemMap(const char *path, size_t size, int mode, FileMap *mapPtr)
{
    NS_NONNULL_ASSERT(path != NULL);
    NS_NONNULL_ASSERT(mapPtr != NULL);
    
    /*
     * Open the file according to map mode
     */
    switch (mode) {
    case NS_MMAP_WRITE:
        mapPtr->handle = ns_open(path, O_BINARY | O_RDWR, 0);
        break;
    case NS_MMAP_READ:
        mapPtr->handle = ns_open(path, O_BINARY | O_RDONLY, 0);
        break;
    default:
        return NS_ERROR;
    }

    if (mapPtr->handle == -1) {
        Ns_Log(Warning, "mmap: ns_open(%s) failed: %s", path, strerror(errno));
        return NS_ERROR;
    }

    /*
     * Map the file as shared and to a system-assigned address
     * per default.
     */

    mapPtr->addr = mmap(0, size, mode, MAP_SHARED, mapPtr->handle, 0);
    if (mapPtr->addr == MAP_FAILED) {
        Ns_Log(Warning, "mmap: mmap(%s) failed: %s", path, strerror(errno));
        ns_close(mapPtr->handle);
        return NS_ERROR;
    }

    ns_close(mapPtr->handle);
    mapPtr->size = size;

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsMemUmap --
 *
 *      Unmaps a file.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
NsMemUmap(const FileMap *mapPtr)
{
    NS_NONNULL_ASSERT(mapPtr != NULL);
    munmap(mapPtr->addr, mapPtr->size);
}


/*
 *----------------------------------------------------------------------
 * ns_sockpair, ns_pipe --
 *
 *      Create a pipe/socketpair with fd's set close on exec.
 *
 * Results:
 *      0 if ok, -1 otherwise.
 *
 * Side effects:
 *      Updates given fd array.
 *
 *----------------------------------------------------------------------
 */

int
ns_sockpair(int *socks)
{
    NS_NONNULL_ASSERT(socks != NULL);
    
    return Pipe(socks, 1);
}

int
ns_pipe(int *fds)
{
    NS_NONNULL_ASSERT(fds != NULL);
    return Pipe(fds, 0);
}

static int
Pipe(int *fds, int sockpair)
{
    int err;

    NS_NONNULL_ASSERT(fds != NULL);
    
    if (sockpair != 0) {
        err = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    } else {
        err = pipe(fds);
    }
    if (err == 0) {
        fcntl(fds[0], F_SETFD, 1);
        fcntl(fds[1], F_SETFD, 1);
    }
    return err;
}


/*
 *----------------------------------------------------------------------
 * ns_sock_set_blocking --
 *
 *      Set a channel blocking or non-blocking
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Change blocking state of a channel
 *
 *----------------------------------------------------------------------
 */

int
ns_sock_set_blocking(NS_SOCKET fd, bool blocking) 
{
    int result;
#if defined USE_FIONBIO
    int state = (blocking == 0);

    result = ioctl(fd, FIONBIO, &state);
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if (blocking != 0) {
	result = fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    } else {
	result = fcntl(fd, F_SETFL, flags|O_NONBLOCK);
    }
#endif
    return result;
}


/*
 *----------------------------------------------------------------------
 * GetPwNam --
 *
 *      generalized version of getpwnam() and getpwnam_r
 *
 * Results:
 *      NS_TRUE if user is found; NS_FALSE otherwise.
 *
 * Side effects:
 *      returning results in arguments 3 or 4 and freePtr in arg 5.
 *
 *----------------------------------------------------------------------
 */

static bool
GetPwNam(const char *user, PwElement elem, long *longResult, Ns_DString *dsPtr, char **freePtr) {
    struct passwd *pwPtr;
    bool success;
#if defined(HAVE_GETPWNAM_R)
    struct passwd pw;
    char *buffer;
    size_t bufSize = 4096u;

    NS_NONNULL_ASSERT(user != NULL);
    NS_NONNULL_ASSERT(freePtr != NULL);
    
    pwPtr = NULL;
    buffer = ns_malloc(bufSize);
    do {
        int errorCode = getpwnam_r(user, &pw, buffer, bufSize, &pwPtr);
        if (errorCode != ERANGE) {
            break;
        }
        bufSize *= 2u;
        buffer = ns_realloc(buffer, bufSize);
    } while (1);
    *freePtr = buffer;
#else
    NS_NONNULL_ASSERT(user != NULL);
    NS_NONNULL_ASSERT(freePtr != NULL);

    Ns_MutexLock(&lock);
    pwPtr = getpwnam(user);
#endif    

    if (pwPtr != NULL) {
        success = NS_TRUE;

        switch (elem) {
        case PwUID: 
            *longResult = (long) pwPtr->pw_uid;
            break;
        case PwGID: 
            *longResult = (long) pwPtr->pw_gid;
            break;
        case PwNAME:
            if (dsPtr != NULL) {
                Ns_DStringAppend(dsPtr, pwPtr->pw_name);
            }
            break;
        case PwDIR:
            if (dsPtr != NULL) {
                Ns_DStringAppend(dsPtr, pwPtr->pw_dir);
            }
            break;
        }
    } else {
        success = NS_FALSE;
    }

#if !defined(HAVE_GETPWNAM_R)
    Ns_MutexUnlock(&lock);
#endif

    return success;
}


/*
 *----------------------------------------------------------------------
 * GetPwUID --
 *
 *      generalized version of getpwuid() and getpwuid_r
 *
 * Results:
 *      NS_TRUE if user is found; NS_FALSE otherwise.
 *
 * Side effects:
 *      returning results in arguments 3 or 4 and freePtr in arg 5.
 *
 *----------------------------------------------------------------------
 */

bool
GetPwUID(uid_t uid, PwElement elem, int *intResult, Ns_DString *dsPtr, char **freePtr) {
    struct passwd *pwPtr;
    bool           success;
#if defined(HAVE_GETPWUID_R)
    struct passwd  pw;
    char          *buffer;
    size_t         bufSize = 4096u;

    pwPtr = NULL;
    buffer = ns_malloc(bufSize);
    do {
        int errorCode = getpwuid_r(uid, &pw, buffer, bufSize, &pwPtr);
        if (errorCode != ERANGE) {
            break;
        }
        bufSize *= 2u;
        buffer = ns_realloc(buffer, bufSize);
    } while (1);
    *freePtr = buffer;
    
#else
    Ns_MutexLock(&lock);
    pwPtr = getpwuid(uid);
#endif

    if (pwPtr != NULL) {
        success = NS_TRUE;

        switch (elem) {
        case PwUID: 
            *intResult = (int) pwPtr->pw_uid;
            break;
        case PwGID: 
            *intResult = (int) pwPtr->pw_gid;
            break;
        case PwNAME:
            if (dsPtr != NULL) {
                Ns_DStringAppend(dsPtr, pwPtr->pw_name);
            }
            break;
        case PwDIR:
            if (dsPtr != NULL) {
                Ns_DStringAppend(dsPtr, pwPtr->pw_dir);
            }
            break;
        }
    } else {
        success = NS_FALSE;
    }

#if !defined(HAVE_GETPWUID_R)
    Ns_MutexUnlock(&lock);
#endif

    return success;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetNameForUid --
 *
 *      Get the user name given the id
 *
 * Results:
 *      NS_TRUE if id is found; NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_GetNameForUid(Ns_DString *dsPtr, uid_t uid)
{
    char *ptr = NULL;
    bool success;

    NS_NONNULL_ASSERT(dsPtr != NULL);
    
    success = GetPwUID(uid, PwNAME, NULL, dsPtr, &ptr);
    if (ptr != NULL) {
        ns_free(ptr);
    }
    return success;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetNameForGid --
 *
 *      Get the group name given the id
 *
 * Results:
 *      NS_TRUE if id is found; NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_GetNameForGid(Ns_DString *dsPtr, gid_t gid)
{
    struct group *grPtr;
#if defined(HAVE_GETGRGID_R)
    struct group gr;
    size_t bufSize = 4096u;
    char *buffer;
    int errorCode = 0;

    grPtr = NULL;
    buffer = ns_malloc(bufSize);
    do {
        errorCode = getgrgid_r(gid, &gr, buffer, bufSize, &grPtr);
        if (errorCode == ERANGE) {
            bufSize *= 2u;
            buffer = ns_realloc(buffer, bufSize);
        }
    } while (errorCode == ERANGE);
    if (grPtr != NULL && dsPtr != NULL) {
        Ns_DStringAppend(dsPtr, grPtr->gr_name);
    }
    ns_free(buffer);
#else
    Ns_MutexLock(&lock);
    grPtr = getgrgid((gid_t)gid);
    if (grPtr != NULL && dsPtr != NULL) {
        Ns_DStringAppend(dsPtr, grPtr->gr_name);
    }
    Ns_MutexUnlock(&lock);
#endif
    return (grPtr != NULL) ? NS_TRUE : NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetUserHome --
 *
 *      Get the home directory name for a user name
 *
 * Results:
 *      Return NS_TRUE if user name is found in /etc/passwd file and
 *      NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_GetUserHome(Ns_DString *dsPtr, const char *user)
{
    char *ptr = NULL;
    bool success;

    NS_NONNULL_ASSERT(dsPtr != NULL);
    
    success = GetPwNam(user, PwDIR, NULL, dsPtr, &ptr);
    if (ptr != NULL) {
        ns_free(ptr);
    }
    return success;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetUserGid --
 *
 *      Get the group id for a user name
 *
 * Results:
 *      Group id or -1 if not found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

long
Ns_GetUserGid(const char *user)
{
    char *ptr = NULL;
    long retcode = -1;

    NS_NONNULL_ASSERT(user != NULL);
    
    (void) GetPwNam(user, PwGID, &retcode, NULL, &ptr);
    if (ptr != NULL) {
        ns_free(ptr);
    }
    return retcode;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetUid --
 *
 *      Get user id for a user name.
 *
 * Results:
 *      User id or -1 if not found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

long
Ns_GetUid(const char *user)
{
    char *ptr = NULL;
    long retcode = -1;

    NS_NONNULL_ASSERT(user != NULL);

    (void) GetPwNam(user, PwUID, &retcode, NULL, &ptr);
    if (ptr != NULL) {
        ns_free(ptr);
    }
    return (long)retcode;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetGid --
 *
 *      Get the group id from a group name.
 *
 * Results:
 *      Group id or -1 if not found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

long
Ns_GetGid(const char *group)
{
    struct group *grPtr;
    long          result;
#if defined(HAVE_GETGRNAM_R)
    struct group  gr;
    size_t        bufSize = 4096u;
    char         *buffer;
    int           errorCode = 0;

    NS_NONNULL_ASSERT(group != NULL);

    grPtr = NULL;
    buffer = ns_malloc(bufSize);
    do {
        errorCode = getgrnam_r(group, &gr, buffer, bufSize, &grPtr);
        if (errorCode == ERANGE) {
            bufSize *= 2u;
            buffer = ns_realloc(buffer, bufSize);
        }
    } while (errorCode == ERANGE);
    result = (grPtr == NULL) ? -1 : (long) grPtr->gr_gid;
    ns_free(buffer);
#else
    NS_NONNULL_ASSERT(group != NULL);

    Ns_MutexLock(&lock);
    grPtr = getgrnam(group);
    if (grPtr == NULL) {
        result = -1;
    } else {
        result = (long)grPtr->gr_gid;
    }
    Ns_MutexUnlock(&lock);
#endif

    return result;
}

/*
 *----------------------------------------------------------------------
 * Ns_SetGroup --
 *
 *      Set the effective group ID of the current process
 *
 * Results:
 *      NS_ERROR on error or NS_OK
 *
 * Side effects:
 *      All suplementry groups will be reset
 *
 *----------------------------------------------------------------------
 */

int
Ns_SetGroup(const char *group)
{
    int nc;

    if (group != NULL) {
        long gidResult = Ns_GetGid(group);

        if (gidResult == -1) {
            if (sscanf(group, "%24d%n", (int*)&gidResult, &nc) != 1
                || nc != (int)strlen(group)
                || Ns_GetNameForGid(NULL, (gid_t)gidResult) == NS_FALSE) {
                Ns_Log(Error, "Ns_GetGroup: unknown group '%s'", group);
                return NS_ERROR;
            }
        }

        if (setgroups(0, NULL) != 0) {
            Ns_Log(Error, "Ns_SetGroup: setgroups(0, NULL) failed: %s",
                     strerror(errno));
            return NS_ERROR;
        }

        if (gidResult != getgid() && setgid((gid_t)gidResult) != 0) {
            Ns_Log(Error, "Ns_SetGroup: setgid(%ld) failed: %s", (long)gidResult, strerror(errno));
            return NS_ERROR;
        }
        Ns_Log(Debug, "Ns_SetGroup: set group id to %ld", (long)gidResult);
    }
    return NS_OK;
}

/*
 *----------------------------------------------------------------------
 * Ns_SetUser --
 *
 *      Set the effective user ID of the current process
 *
 * Results:
 *      NS_ERROR on error or NS_OK
 *
 * Side effects:
 *      All suplementry groups will be assigned as well
 *
 *----------------------------------------------------------------------
 */

int
Ns_SetUser(const char *user)
{
    int nc;
    long uid;
    Ns_DString ds;

    if (user != NULL) {
        long gid = -1;

        Ns_DStringInit(&ds);
        uid = Ns_GetUid(user);
        if (uid == -1) {

            /*
             * Hm, try see if given as numeric uid...
             */
            if (sscanf(user, "%24ld%n", &uid, &nc) != 1
                || nc != (int)strlen(user)
                || Ns_GetNameForUid(&ds, (uid_t)uid) == NS_FALSE) {
                Ns_Log(Error, "Ns_SetUser: unknown user '%s'", user);
                Ns_DStringFree(&ds);
                return NS_ERROR;
            }
            user = Ns_DStringValue(&ds);
        }

        gid = Ns_GetUserGid(user);

        if (initgroups(user, (NS_INITGROUPS_GID_T)gid) != 0) {
            Ns_Log(Error, "Ns_SetUser: initgroups(%s, %ld) failed: %s", user,
                   gid, strerror(errno));
            Ns_DStringFree(&ds);
            return NS_ERROR;
        }
        Ns_DStringFree(&ds);

        if (gid > -1 && gid != (int)getgid() && setgid((gid_t)gid) != 0) {
            Ns_Log(Error, "Ns_SetUser: setgid(%ld) failed: %s", gid, strerror(errno));
            return NS_ERROR;
        }
        if (uid != (int)getuid() && setuid((uid_t)uid) != 0) {
            Ns_Log(Error, "Ns_SetUser: setuid(%ld) failed: %s", uid, strerror(errno));
            return NS_ERROR;
        }
        Ns_Log(Debug, "Ns_SetUser: set user id to %ld", uid);
    }
    return NS_OK;
}

#ifdef HAVE_POLL
int
ns_poll(struct pollfd *fds, NS_POLL_NFDS_TYPE nfds, long timo)
{
    NS_NONNULL_ASSERT(fds != NULL);
    return poll(fds, nfds, (int)timo);
}
#else

/*
 * -----------------------------------------------------------------
 *  Copyright 1994 University of Washington
 *
 *  Permission is hereby granted to copy this software, and to
 *  use and redistribute it, except that this notice may not be
 *  removed.  The University of Washington does not guarantee
 *  that this software is suitable for any purpose and will not
 *  be held liable for any damage it may cause.
 * -----------------------------------------------------------------
 *
 *  Modified to work properly on Darwin 10.2 or less.
 *  Also, heavily reformatted to be more readable.
 */

int
ns_poll(struct pollfd *fds, NS_POLL_NFDS_TYPE nfds, long timo)
{
    struct timeval timeout, *toptr;
    fd_set ifds, ofds, efds;
    int i, rc, n = -1;

    NS_NONNULL_ASSERT(fds != NULL);

    FD_ZERO(&ifds);
    FD_ZERO(&ofds);
    FD_ZERO(&efds);

    for (i = 0; i < nfds; ++i) {
        if (fds[i].fd == NS_INVALID_FD) {
            continue;
        }
        if (fds[i].fd > n) {
            n = fds[i].fd;
        }
        if ((fds[i].events & POLLIN)) {
            FD_SET(fds[i].fd, &ifds);
        }
        if ((fds[i].events & POLLOUT)) {
            FD_SET(fds[i].fd, &ofds);
        }
        if ((fds[i].events & POLLPRI)) {
            FD_SET(fds[i].fd, &efds);
        }
    }
    if (timo < 0) {
        toptr = NULL;
    } else {
        toptr = &timeout;
        timeout.tv_sec = timo / 1000;
        timeout.tv_usec = (timo - timeout.tv_sec * 1000) * 1000;
    }
    rc = select(++n, &ifds, &ofds, &efds, toptr);
    if (rc <= 0) {
        return rc;
    }
    for (i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        if (fds[i].fd == NS_INVALID_FD) {
            continue;
        }
        if (FD_ISSET(fds[i].fd, &ifds)) {
            fds[i].revents |= POLLIN;
        }
        if (FD_ISSET(fds[i].fd, &ofds)) {
            fds[i].revents |= POLLOUT;
        }
        if (FD_ISSET(fds[i].fd, &efds)) {
            fds[i].revents |= POLLPRI;
        }
    }

    return rc;
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * inet_ntop --
 *
 *      In case, we have no inet_ntop(), define it in terms of ns_inet_ntoa
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A core file will be left wherever the server was running.
 *
 *----------------------------------------------------------------------
 */

#if !defined(HAVE_INET_NTOP) && !defined(_WIN32)
const char *
inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    char *ipString;

    ((struct sockaddr *)src)->sa_family = af;
    ipString = ns_inet_ntoa((struct sockaddr *)src);
    memcpy(dst, ipString, (size_t)size);
    return dst;
}
#endif



/*
 *----------------------------------------------------------------------
 *
 * Abort --
 *
 *      Ensure that we drop core on fatal signals like SIGBUS and
 *      SIGSEGV.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A core file will be left wherever the server was running.
 *
 *----------------------------------------------------------------------
 */

static void
Abort(int signal)
{
    Tcl_Panic("received fatal signal %d", signal);
}

#endif

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */
