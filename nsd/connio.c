/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
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


/*
 * connio.c --
 *
 *      Handle connection I/O.
 */

#include "nsd.h"

NS_RCSID("@(#) $Header$");

#define IOBUFSZ 2048

/*
 * Local functions defined in this file
 */

static int ConnSend(Ns_Conn *conn, int nsend, Tcl_Channel chan,
                    FILE *fp, int fd);
static int ConnCopy(Ns_Conn *conn, size_t ncopy, Tcl_Channel chan,
                    FILE *fp, int fd);
 

/*
 *-----------------------------------------------------------------
 *
 * Ns_ConnInit --
 *
 *      Initialize a connection (no longer used).
 *
 * Results:
 *      Always NS_OK.
 * 
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------
 */

int
Ns_ConnInit(Ns_Conn *conn)
{
    return NS_OK;
}


/*
 *-----------------------------------------------------------------
 *
 * Ns_ConnClose --
 *
 *      Close a connection.
 *
 * Results:
 *      Always NS_OK.
 * 
 * Side effects:
 *      The underlying socket in the connection is closed or moved
 *      to the waiting keep-alive list.
 *
 *-----------------------------------------------------------------
 */

int
Ns_ConnClose(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;
    int   keep;
    
    if (connPtr->sockPtr != NULL) {

        keep = (conn->flags & NS_CONN_KEEPALIVE) ? 1 : 0;

        /*
         * In chunked mode we must send the last chunk with zero size
         */

        if ((conn->flags & NS_CONN_WRITE_CHUNKED)
            && !(conn->flags & NS_CONN_SENT_LAST_CHUNK)) {
            if (Ns_WriteConn(conn, 0, 0) != NS_OK) {
                keep = 0;
            }
        }

        NsSockClose(connPtr->sockPtr, keep);
        connPtr->sockPtr = NULL;
        connPtr->flags |= NS_CONN_CLOSED;
    }

    if (connPtr->itPtr != NULL) {
        NsTclRunAtClose(connPtr->itPtr);
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSend --
 *
 *      Sends buffers to clients, including any queued
 *      write-behind data if necessary.  Unlike in
 *      previous versions, this routine attempts to send
 *      all data if possible.
 *
 * Results:
 *      Number of bytes written, -1 for error on first send.
 *
 * Side effects:
 *      Will truncate queued data after send.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnSend(Ns_Conn *conn, struct iovec *bufs, int nbufs)
{
    Conn         *connPtr = (Conn *) conn;
    int           nwrote, towrite, i, n;
    struct iovec  sbufs[NS_CONN_MAXBUFS];

    if (connPtr->sockPtr == NULL) {
        return -1;
    }

    /*
     * Send up to NS_CONN_MAXBUFS(16) buffers, including the queued output
     * buffer if necessary.
     */

    towrite = 0;
    n = 0;
    if (connPtr->queued.length > 0) {
        sbufs[n].iov_base = connPtr->queued.string;
        sbufs[n].iov_len = connPtr->queued.length;
        towrite += sbufs[n].iov_len;
        ++n;
    }
    for (i = 0; i < nbufs && n < NS_CONN_MAXBUFS; ++i) {
        if (bufs[i].iov_len > 0 && bufs[i].iov_base != NULL) {
            sbufs[n].iov_base = bufs[i].iov_base;
            sbufs[n].iov_len = bufs[i].iov_len;
            towrite += bufs[i].iov_len;
            ++n;
        }
    }
    nbufs = n;
    bufs = sbufs;
    n = nwrote = 0;
    while (towrite > 0) {
        n = NsSockSend(connPtr->sockPtr, bufs, nbufs);
        if (n < 0) {
            break;
        }
        towrite -= n;
        nwrote  += n;
        if (towrite > 0) {
            for (i = 0; i < nbufs && n > 0; ++i) {
                if (n > (int) bufs[i].iov_len) {
                    n -= bufs[i].iov_len;
                    bufs[i].iov_base = NULL;
                    bufs[i].iov_len = 0;
                } else {
                    bufs[i].iov_base = (char *) bufs[i].iov_base + n;
                    bufs[i].iov_len -= n;
                    n = 0;
                }
            }
        }
    }
    if (nwrote > 0) {
        connPtr->nContentSent += nwrote;
        if (connPtr->queued.length > 0) {
            n = connPtr->queued.length - nwrote;
            if (n <= 0) {
                nwrote -= connPtr->queued.length;
                Tcl_DStringTrunc(&connPtr->queued, 0);
            } else {
                memmove(connPtr->queued.string,
                        connPtr->queued.string + nwrote, (size_t)n);
                Tcl_DStringTrunc(&connPtr->queued, n);
                nwrote = 0;
            }
        }
    } else {

        /*
         * Return error on first send, if any, from NsSockSend above.
         */

        nwrote = n;
    }

    return nwrote;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnWrite --
 *
 *      Send a single buffer to the client.
 *
 * Results:
 *      # of bytes written from buffer or -1 on error.
 *
 * Side effects:
 *      Stuff may be written. In chunked mode writing 0 bytes means
 *      terminating chunked stream with zero chunk and ending CRLF
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnWrite(Ns_Conn *conn, CONST void *vbuf, int towrite)
{
    int          nsend;
    char         hdr[32];
    struct iovec buf[3];

    if (!(conn->flags & NS_CONN_WRITE_CHUNKED)) {
        buf[0].iov_base = (void *) vbuf;
        buf[0].iov_len = towrite;
        return Ns_ConnSend(conn, buf, 1);
    }

    /*
     * Send data as chunked: size CRLF data CRLF
     */

    buf[0].iov_base = hdr;
    buf[0].iov_len = sprintf(hdr, "%x\r\n", towrite);
    buf[1].iov_base = (void *) vbuf;
    buf[1].iov_len = towrite;
    buf[2].iov_base = "\r\n";
    buf[2].iov_len = 2;

    nsend = Ns_ConnSend(conn, buf, 3);

    /*
     *  In chunked mode we actually sent more data
     *  but Ns_WriteConn does not know about that
     */

    if (nsend == buf[0].iov_len + buf[1].iov_len + buf[2].iov_len) {
        nsend = towrite;
    }

    /*
     *  We should mark when zero length buffer was sent because it will be
     *  considered as last chunk
     */

    if (towrite == 0 && (conn->flags & NS_CONN_WRITE_CHUNKED)) {
        conn->flags |= NS_CONN_SENT_LAST_CHUNK;
    }

    return nsend;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_WriteConn --
 *
 *      This will write a buffer to the conn. It promises to write 
 *      all of it. 
 *
 * Results:
 *      NS_OK/NS_ERROR
 *
 * Side effects:
 *      Stuff may be written 
 *
 *----------------------------------------------------------------------
 */

int
Ns_WriteConn(Ns_Conn *conn, CONST char *buf, int len)
{
    if (Ns_ConnWrite(conn, buf, len) != len) {
        return NS_ERROR;
    }
    return NS_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_WriteCharConn --
 *
 *      This will write a string buffer to the conn.  The distinction
 *      being that the given data is explicitly a UTF8 character string,
 *      and will be put out in an 'encoding-aware' manner.
 *      It promises to write all of it.
 *
 *      If we think we are writing the headers (which is the default),
 *      then we send the data exactly as it is given to us.  If we are
 *      truly in the headers, then they are supposed to be US-ASCII,
 *      which is a subset of UTF-8, so no translation should be needed
 *      if the user has been good and not put any 8-bit characters
 *      into it.
 *
 *      If we have been told that we are sending the content, and we
 *      have been given an encoding to translate the content to, then
 *      we assume that the caller is handing us UTF-8 bytes and we
 *      translate them to the preset encoding.
 *
 * Results:
 *      NS_OK/NS_ERROR
 *
 * Side effects:
 *      Stuff may be written 
 *
 *----------------------------------------------------------------------
 */

int
Ns_WriteCharConn(Ns_Conn *conn, CONST char *buf, int len)
{
    int             status;
    CONST char     *utfBytes;
    int             utfCount; /* # of bytes in utfBytes */
    int             utfConvertedCount;  /* # of bytes of utfBytes converted */
    char            encodedBytes[IOBUFSZ];
    int             encodedCount; /* # of bytes converted in encodedBytes */
    Tcl_Interp     *interp;
    Conn           *connPtr = (Conn *)conn;

    status = NS_OK;

    if (connPtr->encoding == NULL) {
        status = Ns_WriteConn(conn, buf, len);
    } else {

        utfBytes = buf;
        utfCount = len;
        interp = Ns_GetConnInterp(conn);

        while (utfCount > 0 && status == NS_OK) {

            /*
             * Convert a chunk to the desired encoding.
             */

            status = Tcl_UtfToExternal(interp,
                         connPtr->encoding,
                         utfBytes, utfCount,
                         0, NULL,              /* flags, encoding state */
                         encodedBytes, sizeof(encodedBytes),
                         &utfConvertedCount,
                         &encodedCount,
                         NULL);                /* # of chars encoded */

            if (status != TCL_OK && status != TCL_CONVERT_NOSPACE) {
                status = NS_ERROR;
                break;
            }

            /*
             * Send the converted chunk.
             */

            status = NS_OK;
            buf = encodedBytes;
            len = encodedCount;

            status = Ns_WriteConn(conn, buf, len);

            utfCount -= utfConvertedCount;
            utfBytes += utfConvertedCount;
        }
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPuts --
 *
 *      Write a null-terminated string to the conn; no trailing 
 *      newline will be appended despite the name. 
 *
 * Results:
 *      See Ns_WriteConn 
 *
 * Side effects:
 *      See Ns_WriteConn 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnPuts(Ns_Conn *conn, CONST char *string)
{
    return Ns_WriteConn(conn, string, (int) strlen(string));
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSendDString --
 *
 *      Write contents of a DString
 *
 * Results:
 *      See Ns_WriteConn 
 *
 * Side effects:
 *      See Ns_WriteConn 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnSendDString(Ns_Conn *conn, Ns_DString *dsPtr)
{
    return Ns_WriteConn(conn, dsPtr->string, dsPtr->length);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSendChannel, Fp, Fd, Buf --
 *
 *      Send an open channel, FILE, fd or memory buffer.
 *
 * Results:
 *      NS_OK/NS_ERROR
 *
 * Side effects:
 *      See ConnSend().
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnSendChannel(Ns_Conn *conn, Tcl_Channel chan, int nsend)
{
    return ConnSend(conn, nsend, chan, NULL, -1);
}

int
Ns_ConnSendFp(Ns_Conn *conn, FILE *fp, int nsend)
{
    return ConnSend(conn, nsend, NULL, fp, -1);
}

int
Ns_ConnSendFd(Ns_Conn *conn, int fd, int nsend)
{
    return ConnSend(conn, nsend, NULL, NULL, fd);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnFlushContent --
 *
 *      Finish reading waiting content.
 *
 * Results:
 *      NS_OK.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnFlushContent(Ns_Conn *conn)
{
    Conn    *connPtr = (Conn *) conn;
    Request *reqPtr = connPtr->reqPtr;

    if (connPtr->sockPtr == NULL) {
        return -1;
    }
    reqPtr->next  += reqPtr->avail;
    reqPtr->avail  = 0;

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGets --
 *
 *      Read in a string from a connection, stopping when either 
 *      we've run out of data, hit a newline, or had an error 
 *
 * Results:
 *      Pointer to given buffer or NULL on error.
 *
 * Side effects:
 *  
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnGets(char *buf, size_t bufsize, Ns_Conn *conn)
{
    char *p;

    p = buf;
    while (bufsize > 1) {
        if (Ns_ConnRead(conn, p, 1) != 1) {
            return NULL;
        }
        if (*p++ == '\n') {
            break;
        }
        --bufsize;
    }
    *p = '\0';

    return buf;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnRead --
 *
 *      Copy data from read-ahead buffers.
 *
 * Results:
 *      Number of bytes copied.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnRead(Ns_Conn *conn, void *vbuf, int toread)
{
    Conn    *connPtr = (Conn *) conn;
    Request *reqPtr = connPtr->reqPtr;

    if (connPtr->sockPtr == NULL) {
        return -1;
    }
    if (toread > reqPtr->avail) {
        toread = reqPtr->avail;
    }
    memcpy(vbuf, reqPtr->next, (size_t)toread);
    reqPtr->next  += toread;
    reqPtr->avail -= toread;

    return toread;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReadLine --
 *
 *      Read a line (\r\n or \n terminated) from the conn.
 *
 * Results:
 *      NS_OK if a line was read.  NS_ERROR if no line ending
 *      was found or the line would be too long.
 *
 * Side effects:
 *      Stuff may be read 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReadLine(Ns_Conn *conn, Ns_DString *dsPtr, int *nreadPtr)
{
    Conn       *connPtr = (Conn *) conn;
    Request    *reqPtr = connPtr->reqPtr;
    Driver     *drvPtr = connPtr->drvPtr;
    char       *eol;
    int         nread, ncopy;

    if (connPtr->sockPtr == NULL
        || (eol = strchr(reqPtr->next, '\n')) == NULL
        || (nread = (eol - reqPtr->next)) > drvPtr->maxline) {
        return NS_ERROR;
    }
    ncopy = nread;
    ++nread;
    if (nreadPtr != NULL) {
        *nreadPtr = nread;
    }
    if (ncopy > 0 && eol[-1] == '\r') {
        --ncopy;
    }
    Ns_DStringNAppend(dsPtr, reqPtr->next, ncopy);
    reqPtr->next  += nread;
    reqPtr->avail -= nread;

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReadHeaders --
 *
 *      Read the headers and insert them into the passed-in set 
 *
 * Results:
 *      NS_OK/NS_ERROR 
 *
 * Side effects:
 *      Stuff will be read from the conn 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReadHeaders(Ns_Conn *conn, Ns_Set *set, int *nreadPtr)
{
    Ns_DString      ds;
    Conn           *connPtr = (Conn *) conn;
    NsServer       *servPtr = connPtr->servPtr;
    int             status, nread, nline, maxhdr;

    Ns_DStringInit(&ds);
    nread = 0;
    maxhdr = connPtr->drvPtr->maxheaders;
    status = NS_OK;
    while (nread < maxhdr && status == NS_OK) {
        Ns_DStringTrunc(&ds, 0);
        status = Ns_ConnReadLine(conn, &ds, &nline);
        if (status == NS_OK) {
            nread += nline;
            if (nread > maxhdr) {
                status = NS_ERROR;
            } else {
                if (ds.string[0] == '\0') {
                    break;
                }
                status = Ns_ParseHeader(set, ds.string, servPtr->opts.hdrcase);
            }
        }
    }
    if (nreadPtr != NULL) {
        *nreadPtr = nread;
    }
    Ns_DStringFree(&ds);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCopyToDString --
 *
 *      Copy data from a connection to a dstring.
 *
 * Results:
 *      NS_OK or NS_ERROR 
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnCopyToDString(Ns_Conn *conn, size_t tocopy, Ns_DString *dsPtr)
{
    Conn    *connPtr = (Conn *) conn;
    Request *reqPtr = connPtr->reqPtr;
    int      ncopy = (int) tocopy;

    if (connPtr->sockPtr == NULL || reqPtr->avail < ncopy) {
        return NS_ERROR;
    }
    Ns_DStringNAppend(dsPtr, reqPtr->next, ncopy);
    reqPtr->next  += ncopy;
    reqPtr->avail -= ncopy;

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCopyToFile, Fd, Channel --
 *
 *      Copy data from a connection to a channel, FILE, or fd. 
 *
 * Results:
 *      NS_OK or NS_ERROR 
 *
 * Side effects:
 *      See ConnCopy().
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnCopyToChannel(Ns_Conn *conn, size_t ncopy, Tcl_Channel chan)
{
    return ConnCopy(conn, ncopy, chan, NULL, -1);
}

int
Ns_ConnCopyToFile(Ns_Conn *conn, size_t ncopy, FILE *fp)
{
    return ConnCopy(conn, ncopy, NULL, fp, -1);
}

int
Ns_ConnCopyToFd(Ns_Conn *conn, size_t ncopy, int fd)
{
    return ConnCopy(conn, ncopy, NULL, NULL, fd);
}


/*
 *----------------------------------------------------------------------
 *
 * ConnCopy --
 *
 *      Copy connection content to a channel, FILE, or fd.
 *
 * Results:
 *      NS_OK or NS_ERROR if not all content could be read.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
ConnCopy(Ns_Conn *conn, size_t tocopy, Tcl_Channel chan, FILE *fp, int fd)
{
    Conn    *connPtr = (Conn *) conn;
    Request *reqPtr = connPtr->reqPtr;
    int      nwrote;
    int      ncopy = (int) tocopy;

    if (connPtr->sockPtr == NULL || reqPtr->avail < ncopy) {
        return NS_ERROR;
    }
    while (ncopy > 0) {
        if (chan != NULL) {
            nwrote = Tcl_Write(chan, reqPtr->next, ncopy);
        } else if (fp != NULL) {
            nwrote = fwrite(reqPtr->next, 1, (size_t)ncopy, fp);
            if (ferror(fp)) {
                nwrote = -1;
            }
        } else {
            nwrote = write(fd, reqPtr->next, (size_t)ncopy);
        }
        if (nwrote < 0) {
            return NS_ERROR;
        }
        ncopy -= nwrote;
        reqPtr->next  += nwrote;
        reqPtr->avail -= nwrote;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ConnSend --
 *
 *      Send content from a channel, FILE, or fd.
 *
 * Results:
 *      NS_OK or NS_ERROR if a write failed.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
ConnSend(Ns_Conn *conn, int nsend, Tcl_Channel chan, FILE *fp, int fd)
{
    int   toread, nread, status;
    char  buf[IOBUFSZ];
 
    /*
     * Even if nsend is 0, ensure all queued data (like HTTP response
     * headers) get flushed.
     */

    if (nsend == 0) {
        Ns_WriteConn(conn, NULL, 0);
    }

    /*
     * Check for submision into writer queue
     */

    if (NsQueueWriter(conn, nsend, chan, fp, fd) == NS_OK) {
        return NS_OK;
    }

    status = NS_OK;
    while (status == NS_OK && nsend > 0) {
        toread = nsend;
        if (toread > sizeof(buf)) {
            toread = sizeof(buf);
        }
        if (chan != NULL) {
            nread = Tcl_Read(chan, buf, toread);
        } else if (fp != NULL) {
            nread = fread(buf, 1, (size_t)toread, fp);
            if (ferror(fp)) {
                nread = -1;
            }
        } else {
            nread = read(fd, buf, (size_t)toread);
        }
        if (nread == -1) {
            status = NS_ERROR;
        } else if (nread == 0) {
            nsend = 0;  /* NB: Silently ignore a truncated file. */
        } else if ((status = Ns_WriteConn(conn, buf, nread)) == NS_OK) {
            nsend -= nread;
        }
    }

    return status;
}
