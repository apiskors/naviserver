/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The Initial Developer of the Original Code and related documentation
 * is America Online, Inc. Portions created by AOL are Copyright (C) 1999
 * America Online, Inc. All Rights Reserved.
 *
 */

/*
 * nsproxy.c --
 *
 *      Simple main for ns_proxy slave.
 */

#include "nsproxy.h"

static int
MyInit(Tcl_Interp *UNUSED(interp))
{
    return TCL_OK;
}

int
main(int argc, char **argv)
{
    return Ns_ProxyMain(argc, argv, MyInit);
}
