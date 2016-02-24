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

#include "nsd.h"

/*
 * sockaddr.c --
 *
 *      Generic Interface for IPv4 and IPv6
 */


#if defined(__APPLE__) || defined(__darwin__)
/* OSX seems not to define these. */
# ifndef s6_addr16
#  define s6_addr16 __u6_addr.__u6_addr16
# endif
# ifndef s6_addr32
#  define s6_addr32 __u6_addr.__u6_addr32
# endif
#endif


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockaddrMask --
 *
 *	Compute from and address and a mask a masked address in a generic way
 *	(for IPv4 and IPv6 addresses).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The last argument (maskedAddr) is updated.
 *
 *----------------------------------------------------------------------
 */
void
Ns_SockaddrMask(struct sockaddr *addr, struct sockaddr *mask, struct sockaddr *maskedAddr)
{
    NS_NONNULL_ASSERT(addr != NULL);
    NS_NONNULL_ASSERT(mask != NULL);
    NS_NONNULL_ASSERT(maskedAddr != NULL);

    /*
     * Copy the full content to maskedAddr in case it is not identical.
     */
    if (addr != maskedAddr) {
        memcpy(maskedAddr, addr, sizeof(struct NS_SOCKADDR_STORAGE));
    }

    if (addr->sa_family == AF_INET6 && mask->sa_family == AF_INET6) {
        struct in6_addr *addrBits   = &(((struct sockaddr_in6 *)addr)->sin6_addr);
        struct in6_addr *maskBits   = &(((struct sockaddr_in6 *)mask)->sin6_addr);
        struct in6_addr *maskedBits = &(((struct sockaddr_in6 *)maskedAddr)->sin6_addr);
        int i;

        /*
         * Perform bitwise masking over the full array. Maybe we need
         * something special for IN6_IS_ADDR_V4MAPPED.
         */
        for (i = 0; i < 4; i++) {
            maskedBits->s6_addr32[i] = addrBits->s6_addr32[i] & maskBits->s6_addr32[i];
        }
        /*
          fprintf(stderr, "#### addr   %s\n",ns_inet_ntoa(addr));
          fprintf(stderr, "#### mask   %s\n",ns_inet_ntoa(mask));
          fprintf(stderr, "#### masked %s\n",ns_inet_ntoa(maskedAddr));
        */
    } else if (addr->sa_family == AF_INET && mask->sa_family == AF_INET) {
        ((struct sockaddr_in *)maskedAddr)->sin_addr.s_addr =
            ((struct sockaddr_in *)addr)->sin_addr.s_addr &
            ((struct sockaddr_in *)mask)->sin_addr.s_addr;
    } else if (addr->sa_family != AF_INET && addr->sa_family != AF_INET6) {
        Ns_Log(Error, "nsperm: invalid address family %d detected (Ns_SockaddrMask addr)", addr->sa_family);
    } else if (mask->sa_family != AF_INET && mask->sa_family != AF_INET6) {
        Ns_Log(Error, "nsperm: invalid address family %d detected (Ns_SockaddrMask mask)", mask->sa_family);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockaddrMaskBits --
 *
 *	Build a mask with the given bits in a IPv4 or IPv6 sockaddr
 *
 * Results:
 *	Mask computed in 1 arg.
 *
 * Side effects:
 *	The first argument is updated.
 *
 *----------------------------------------------------------------------
 */
void
Ns_SockaddrMaskBits(struct sockaddr *mask, unsigned int nrBits)
{
    NS_NONNULL_ASSERT(mask != NULL);

    if (mask->sa_family == AF_INET6) {
        struct in6_addr *addr = &(((struct sockaddr_in6 *)mask)->sin6_addr);
        int i;

        if (nrBits > 128) {
            Ns_Log(Warning, "Invalid bitmask /%d: can be most 128 bits", nrBits);
            nrBits = 128;
        }
        /*
         * Set the mask bits in the leading 32 bit ints to 1.
         */
        for (i = 0; i < 4 && nrBits >= 32; i++, nrBits -= 32) {
            addr->s6_addr32[i] = (~0u);
        }
        /*
         * Set the partial mask.
         */
        if (i < 4 && nrBits > 0) {
            addr->s6_addr32[i] = htonl((~0u) << (32 - nrBits));
            i++;
        }
        /*
         * Clear trailing 32 bit ints.
         */
        for (; i < 4; i++) {
            addr->s6_addr32[i] = 0u;
        }
        /*fprintf(stderr, "#### FINAL mask %s\n",ns_inet_ntoa(mask));*/
    } else if (mask->sa_family == AF_INET) {
        if (nrBits > 32) {
            Ns_Log(Warning, "Invalid bitmask /%d: can be most 32 bits", nrBits);
            nrBits = 32;
        }
        ((struct sockaddr_in *)mask)->sin_addr.s_addr = htonl((~0u) << (32 - nrBits));
    } else {
        Ns_Log(Error, "nsperm: invalid address family %d detected (Ns_SockaddrMaskBits)", mask->sa_family);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ns_inet_ntop --
 *
 *    This function is a version of inet_ntop() which is agnostic to IPv4 and IPv6.
 *
 * Results:
 *    String pointing to printable ip address.
 *
 * Side effects:
 *    Update provided buffer with resulting character string.
 *
 *----------------------------------------------------------------------
 */
const char *
ns_inet_ntop(const struct sockaddr *saPtr, char *buffer, size_t size) {
    const char *result;

    NS_NONNULL_ASSERT(saPtr != NULL);
    NS_NONNULL_ASSERT(buffer != NULL);

    if (saPtr->sa_family == AF_INET6) {
        result = inet_ntop(AF_INET6, &((struct sockaddr_in6 *)saPtr)->sin6_addr, buffer, size);
    } else {
        result = inet_ntop(AF_INET, &((struct sockaddr_in *)saPtr)->sin_addr, buffer, size);
    }
    
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * ns_inet_pton --
 *
 *  Convert an IPv4/IPv6 address in textual form to a binary IPv6
 *  form. IPV4 addresses are converted to "mapped IPv4 addresses".
 *
 * Results:
 *  >0  = Success.
 *  <=0 = Error:
 *   <0 = Invalid address family. As this routine hardcodes the AF,
 *        this result should not occur.
 *    0 = Parse error.
 *
 * Side effects:
 *  None.
 *
 *----------------------------------------------------------------------
 */
int
ns_inet_pton(struct sockaddr *saPtr, const char *addr) {
    int r;

    NS_NONNULL_ASSERT(saPtr != NULL);
    NS_NONNULL_ASSERT(addr != NULL);

    /*
     * First try whether the address parses as an IPv4 address
     */
    r = inet_pton(AF_INET, addr, &((struct sockaddr_in *)saPtr)->sin_addr);
    if (r > 0) {
        saPtr->sa_family = AF_INET;
        /*Ns_LogSockaddr(Notice, "ns_inet_pton returns IPv4 address", saPtr);*/
    } else {
#ifdef HAVE_IPV6
        /*
         * No IPv4 address, try to parse as IPv6 address
         */
        r = inet_pton(AF_INET6, addr, &((struct sockaddr_in6 *)saPtr)->sin6_addr);
        saPtr->sa_family = AF_INET6;

        /*Ns_LogSockaddr(Notice, "ns_inet_pton returns IPv6 address", saPtr);*/
#endif
    }
    return r;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetSockAddr --
 *
 *      Take a host/port and fill in a NS_SOCKADDR_STORAGE structure
 *      appropriately. Host may be an IP address or a DNS name.
 *
 * Results:
 *      NS_OK/NS_ERROR
 *
 * Side effects:
 *      May perform DNS query.
 *
 *----------------------------------------------------------------------
 */
int
Ns_GetSockAddr(struct sockaddr *saPtr, const char *host, int port)
{
    NS_NONNULL_ASSERT(saPtr != NULL);

    //fprintf(stderr, "# GetSockAddr host '%s' port %d\n", host, port);
    
    memset(saPtr, 0, sizeof(struct NS_SOCKADDR_STORAGE));
    
#ifdef HAVE_IPV6
    if (host == NULL) {
        saPtr->sa_family = AF_INET6;
        ((struct sockaddr_in6 *)saPtr)->sin6_addr = in6addr_any;
    } else {
        int r;

        r = ns_inet_pton((struct sockaddr *)saPtr, host);
        if (r <= 0) {
            Ns_DString ds;

            Ns_DStringInit(&ds);
            //fprintf(stderr, "# GetSockAddr ... calls Ns_GetAddrByHost\n");
            if (Ns_GetAddrByHost(&ds, host) == NS_TRUE) {
                //fprintf(stderr, "# GetAddrByHost returns <%s>\n", ds.string);
                r = ns_inet_pton((struct sockaddr *)saPtr, ds.string);
            }
            Ns_DStringFree(&ds);
            if (r <= 0) {
                //fprintf(stderr, "# GetSockAddr ... fails\n");
                return NS_ERROR;
            }
        }
    }

#else
    saPtr->sa_family = AF_INET;

    if (host == NULL) {
        ((struct sockaddr_in *)saPtr)->sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        ((struct sockaddr_in *)saPtr)->sin_addr.s_addr = inet_addr(host);
        if (((struct sockaddr_in *)saPtr)->sin_addr.s_addr == INADDR_NONE) {
            Ns_DString ds;

            Ns_DStringInit(&ds);
            if (Ns_GetAddrByHost(&ds, host) == NS_TRUE) {
                ((struct sockaddr_in *)saPtr)->sin_addr.s_addr = inet_addr(ds.string);
            }
            Ns_DStringFree(&ds);
            if (((struct sockaddr_in *)saPtr)->sin_addr.s_addr == INADDR_NONE) {
                return NS_ERROR;
            }
        }
    }
#endif

    Ns_SockaddrSetPort((struct sockaddr *)saPtr, port);
    /*Ns_LogSockaddr(Notice, "Ns_GetSockAddr returns", (const struct sockaddr *)saPtr);*/

    return NS_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_SockaddrGetPort --
 *
 *      Generic function to obtain port from an IPv4 or IPv6 sock addr.
 *
 * Results:
 *      Port number
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
unsigned short
Ns_SockaddrGetPort(const struct sockaddr *saPtr)
{
    unsigned int port;
    
    NS_NONNULL_ASSERT(saPtr != NULL);
    
#ifdef HAVE_IPV6
    if (saPtr->sa_family == AF_INET6) {
        port = ((struct sockaddr_in6 *)saPtr)->sin6_port;
    } else {
        port = ((struct sockaddr_in *)saPtr)->sin_port;
    }
#else
    port = ((struct sockaddr_in *)saPtr)->sin_port;
#endif
    
    return htons(port);
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_SockaddrSetPort --
 *
 *      Generic function to set port in an IPv4 or IPv6 sock addr.
 *
 * Results:
 *      Port number
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
void
Ns_SockaddrSetPort(struct sockaddr *saPtr, unsigned short port)
{
    NS_NONNULL_ASSERT(saPtr != NULL);
    
#ifdef HAVE_IPV6
    if (saPtr->sa_family == AF_INET6) {
        ((struct sockaddr_in6 *)saPtr)->sin6_port = ntohs(port);
    } else {
        ((struct sockaddr_in *)saPtr)->sin_port = ntohs(port);
    }
#else
    ((struct sockaddr_in *)saPtr)->sin_port = ntohs(port);
#endif

}

/*
 *----------------------------------------------------------------------
 *
 * Ns_SockaddrGetSockLen --
 *
 *      Generic function to obtain socklen from an IPv4 or IPv6 sockaddr.
 *
 * Results:
 *      socklen.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
socklen_t
Ns_SockaddrGetSockLen(const struct sockaddr *saPtr)
{
    size_t socklen;
    
    NS_NONNULL_ASSERT(saPtr != NULL);
    
#ifdef HAVE_IPV6
    socklen = (saPtr->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
#else
    socklen = sizeof(struct sockaddr_in);
#endif
    
    return (socklen_t)socklen;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_LogSockSockaddr --
 *
 *      Function to log generic SockAddr.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Entry in log file.
 *
 *----------------------------------------------------------------------
 */
void
Ns_LogSockaddr(Ns_LogSeverity severity, const char *prefix, const struct sockaddr *saPtr)
{
    const char *family;
    char        ipString[NS_IPADDR_SIZE], *ipStrPtr = ipString;
    
    NS_NONNULL_ASSERT(prefix != NULL);
    NS_NONNULL_ASSERT(saPtr != NULL);

    family = (saPtr->sa_family == AF_INET6) ? "AF_INET6" :
        (saPtr->sa_family == AF_INET) ? "AF_INET" : "UNKOWN";

    ns_inet_ntop(saPtr, ipString, NS_IPADDR_SIZE);
    
    Ns_Log(severity, "%s: SockAddr %p, family %s, ip %s, port %d",
           prefix, (void*)saPtr, family, ipStrPtr, Ns_SockaddrGetPort(saPtr));
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */
