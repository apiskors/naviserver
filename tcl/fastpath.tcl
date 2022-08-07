#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The Initial Developer of the Original Code and related documentation
# is America Online, Inc. Portions created by AOL are Copyright (C) 1999
# America Online, Inc. All Rights Reserved.
#
#

#
# fastpath.tcl --
#
#   AOLserver 2.x fastpath routines moved from C.  The C code
#   now only handles the simple case of returning a file through
#   an optimized caching routines.
#   In addition, the C code will dispatch to the _ns_dirlist proc
#   below to handle directory listings.
#

set path ns/server/[ns_info server]/fastpath

nsv_set _ns_fastpath type      [ns_config -set $path directorylisting   simple]
nsv_set _ns_fastpath hidedot   [ns_config -bool -set $path hidedotfiles    1]

# the following three lines are apparently not used anymore
#nsv_set _ns_fastpath toppage   [ns_config -bool -set $path returnmwtoppage 0]
#nsv_set _ns_fastpath builddirs [ns_config -bool -set $path builddirs       0]
#nsv_set _ns_fastpath serverlog [ns_config -bool -set $path serverlog       1]

#
# ns_returnok --
#
#   Closes current connection by returning HTTP code 200.
#
# Results:
#   Same as [ns_return]
#
# Side effects:
#   None.
#

proc ns_returnok {} {
    ns_return 200 text/plain {}
}


#
# _ns_dirlist --
#
#   Handle directory listings. This code is invoked from C.
#
# Results:
#   None.
#
# Side effects:
#   Produces output to currently opened connection.
#

proc _ns_dirlist {} {

    if {![ns_conn isconnected]} {
        return
    }

    set url [ns_conn url]
    set dir [ns_url2file $url]
    set loc [ns_conn location]

    #
    # Enforce being called with trailing "/"
    #

    if {[string index $url end] ne "/"} {
        ns_returnredirect "$loc$url/"
        return
    }

    #
    # Handle default case of directory listing.  Simple
    # format is just the files while fancy includes
    # the size and modify time (which is more expensive).
    #

    switch [nsv_get _ns_fastpath type] {
        simple  { set simple 1 }
        fancy   { set simple 0 }
        default { return [ns_returnnotfound] }
    }

    set prefix "${loc}${url}"
    set uptree "<a href=..>..</a>"

    if {$simple} {
        append html "
<pre>
$uptree
"
    } else {
        append html "
<table>
<tr align=left><th>File</th><th>Size</th><th>Date</th></tr>
<tr align=left><td colspan=3>$uptree</td></tr>
"
    }

    if {[nsv_get _ns_fastpath hidedot]} {
        set files [glob -nocomplain -directory $dir *]
    } else {
        set files [glob -nocomplain -directory $dir .* *]
    }

    #
    # Visit every file and format display
    #

    foreach f [lsort $files] {
        set tail [file tail $f]
        if {[file isdirectory $f]} { 
            append tail "/"
        }
        set link "<a href=\"${prefix}${tail}\">${tail}</a>"
        if {$simple} {
            append html $link \n
        } else {
            if {[catch {file stat $f stat}]} {
                append html "
<tr align=left><td>$link</td><td>N/A</td><td>N/A</td></tr>
" \n
            } else {
                set size  [expr {$stat(size)/1000 + 1}]K
                set mtime $stat(mtime)
                set time  [clock format $mtime -format "%d-%h-%Y %H:%M"]
                append html "
<tr align=left><td>$link</td><td>$size</td><td>$time</td></tr>
" \n
            }
        }
    }
    if {$simple} {
        append html "</pre>"
    } else {
        append html "</table>"
    }
    
    ns_returnnotice 200 $url $html
}


#
# ns_zipfile --
#
#   call gzip and optionally a minify command on a file to produce a
#   same-named file in the same directory
#
# Results:
#   None.
#
# Side effects:
#   Create a gzipped file.
#

proc ns_gzipfile {source target} {
    set gzipCmd [ns_config ns/fastpath gzip_cmd]
    if {$gzipCmd eq ""} {error "no ns/fastpath gzip_cmd configured"}
    switch [file extension $source] {
        .css {set minifyCmd [ns_config ns/fastpath minify_css_cmd]}
        .js  {set minifyCmd [ns_config ns/fastpath minify_js_cmd]}
        default {set minifyCmd ""}
    }
    if {$minifyCmd ne ""} {
        if {[catch {
            exec {*}$minifyCmd < $source | {*}$gzipCmd > $target
        } errorMsg]} {
            ns_log warning "minify returned error: $errorMsg"
            exec {*}$gzipCmd < $source > $target
        }
    } else {
        exec {*}$gzipCmd < $source > $target
    }
}

proc ns_brotlifile {source target} {
    set brotliCmd [ns_config ns/fastpath brotli_cmd]
    if {$brotliCmd eq ""} {error "no ns/fastpath brotli_cmd configured"}
    exec {*}$brotliCmd < $source > $target
}

# Local variables:
#    mode: tcl
#    tcl-indent-level: 4
#    indent-tabs-mode: nil
# End:
