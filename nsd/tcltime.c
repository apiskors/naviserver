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


/*
 * tcltime.c --
 *
 *      A Tcl interface to the Ns_Time microsecond resolution time
 *      routines and some time formatting commands.
 */

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static void SetTimeInternalRep(Tcl_Obj *objPtr, const Ns_Time *timePtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2);

static int SetTimeFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2);

static void UpdateStringOfTime(Tcl_Obj *objPtr)
    NS_GNUC_NONNULL(1);

static int TmObjCmd(ClientData isGmt, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
    NS_GNUC_NONNULL(2);

static int GetTimeFromString(Tcl_Interp *interp, const char *str, char separator, Ns_Time *tPtr)
    NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(4);

/*
 * Local variables defined in this file.
 */

static Tcl_ObjType timeType = {
    "ns:time",
    NULL,
    NULL,
    UpdateStringOfTime,
    SetTimeFromAny
};

static const Tcl_ObjType *intTypePtr;



/*
 *----------------------------------------------------------------------
 *
 * NsTclInitTimeType --
 *
 *      Initialize Ns_Time Tcl_Obj type.
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
NsTclInitTimeType()
{
#ifndef _WIN32
    Tcl_Obj obj;
    if (sizeof(obj.internalRep) < sizeof(Ns_Time)) {
        Tcl_Panic("NsTclInitObjs: sizeof(obj.internalRep) < sizeof(Ns_Time)");
    }
#endif
    intTypePtr = Tcl_GetObjType("int");
    if (intTypePtr == NULL) {
        Tcl_Panic("NsTclInitObjs: no int type");
    }
    Tcl_RegisterObjType(&timeType);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclNewTimeObj --
 *
 *      Creates new time object.
 *
 * Results:
 *      Pointer to new time object.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *
Ns_TclNewTimeObj(const Ns_Time *timePtr)
{
    Tcl_Obj *objPtr = Tcl_NewObj();

    NS_NONNULL_ASSERT(timePtr != NULL);
    
    Tcl_InvalidateStringRep(objPtr);
    SetTimeInternalRep(objPtr, timePtr);

    return objPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclSetTimeObj --
 *
 *      Set a Tcl_Obj to an Ns_Time object.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      String rep is invalidated and internal rep is set.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclSetTimeObj(Tcl_Obj *objPtr, const Ns_Time *timePtr)
{

    NS_NONNULL_ASSERT(timePtr != NULL);
    NS_NONNULL_ASSERT(objPtr != NULL);

    if (Tcl_IsShared(objPtr)) {
        Tcl_Panic("Ns_TclSetTimeObj called with shared object");
    }
    Tcl_InvalidateStringRep(objPtr);
    SetTimeInternalRep(objPtr, timePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetTimeFromObj --
 *
 *      Return the internal value of an Ns_Time Tcl_Obj.  If the value is
 *      specified as integer, the value is interpreted as seconds.
 *
 * Results:
 *      TCL_OK or TCL_ERROR if not a valid Ns_Time.
 *
 * Side effects:
 *      Object is converted to Ns_Time type if necessary.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclGetTimeFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Ns_Time *timePtr)
{
    long sec;

    NS_NONNULL_ASSERT(interp != NULL);
    NS_NONNULL_ASSERT(objPtr != NULL);
    NS_NONNULL_ASSERT(timePtr != NULL);

    if (objPtr->typePtr == intTypePtr) {
        if (Tcl_GetLongFromObj(interp, objPtr, &sec) != TCL_OK) {
            return TCL_ERROR;
        }
        timePtr->sec = sec;
        timePtr->usec = 0;
    } else {
        if (Tcl_ConvertToType(interp, objPtr, &timeType) != TCL_OK) {
            return TCL_ERROR;
        }
        timePtr->sec =  (long) objPtr->internalRep.twoPtrValue.ptr1;
        timePtr->usec = (long) objPtr->internalRep.twoPtrValue.ptr2;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetTimePtrFromObj --
 *
 *      Convert the Tcl_Obj to an Ns_Time type and return a pointer to
 *      it's internal rep.
 *
 * Results:
 *      TCL_OK or TCL_ERROR if not a valid Ns_Time.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclGetTimePtrFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Ns_Time **timePtrPtr)
{

    NS_NONNULL_ASSERT(interp != NULL);
    NS_NONNULL_ASSERT(objPtr != NULL);
    NS_NONNULL_ASSERT(timePtrPtr != NULL);
    
    if (objPtr->typePtr != &timeType) {
        if (Tcl_ConvertToType(interp, objPtr, &timeType) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    *timePtrPtr = ((Ns_Time *) (void *) &objPtr->internalRep);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTimeObjCmd --
 *
 *      Implements ns_time.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclTimeObjCmd(ClientData UNUSED(clientData), Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    Ns_Time result = {0, 0}, t1, t2;
    long sec;
    int opt;

    static const char *const opts[] = {
	"adjust", "diff", "format", "get", "incr", "make",
	"seconds", "microseconds", NULL
    };
    enum {
        TAdjustIdx, TDiffIdx, TFormatIdx, TGetIdx, TIncrIdx, TMakeIdx,
        TSecondsIdx, TMicroSecondsIdx
    };

    if (objc < 2) {
      Tcl_SetObjResult(interp, Tcl_NewLongObj((long)time(NULL)));
        return TCL_OK;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                            &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (opt) {
    case TGetIdx:
        Ns_GetTime(&result);
        break;

    case TMakeIdx:
        if (objc != 3 && objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "sec ?usec?");
            return TCL_ERROR;
        }
        if (Tcl_GetLongFromObj(interp, objv[2], &sec) != TCL_OK) {
            return TCL_ERROR;
        }
        result.sec = sec;
        if (objc == 3) {
            result.usec = 0;
        } else if (Tcl_GetLongFromObj(interp, objv[3], &result.usec) != TCL_OK) {
            return TCL_ERROR;
        }
        break;

    case TIncrIdx:
        if (objc != 4 && objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "time sec ?usec?");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK
                || Tcl_GetLongFromObj(interp, objv[3], &sec) != TCL_OK) {
            return TCL_ERROR;
        }
        t2.sec = sec;
        if (objc == 4) {
            t2.usec = 0;
        } else if (Tcl_GetLongFromObj(interp, objv[4], &t2.usec) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_IncrTime(&result, t2.sec, t2.usec);
        break;

    case TDiffIdx:
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "time1 time2");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &t1) != TCL_OK ||
            Ns_TclGetTimeFromObj(interp, objv[3], &t2) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_DiffTime(&t1, &t2, &result);
        break;

    case TAdjustIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "time");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_AdjTime(&result);
        break;

    case TSecondsIdx:
    case TMicroSecondsIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "time");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewLongObj((long)(opt == TSecondsIdx ?
						       result.sec : result.usec)));
        return TCL_OK;

    case TFormatIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "time");
            return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK) {
            return TCL_ERROR;
        }

	{ 
	  Tcl_DString ds, *dsPtr = &ds;

	  Tcl_DStringInit(dsPtr);
	  Ns_DStringPrintf(dsPtr, " %" PRIu64 ".%06ld", 
			   (int64_t)result.sec, result.usec);
	  Tcl_DStringResult(interp, dsPtr);
	}
	return TCL_OK;

    default:
        /* unexpected value */
        assert(opt && 0);
        break;
    }

    Tcl_SetObjResult(interp, Ns_TclNewTimeObj(&result));

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLocalTimeObjCmd, NsTclGmTimeObjCmd --
 *
 *      Implements ns_gmtime and ns_localtime.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      ns_localtime depends on the time zone of the server process.
 *
 *----------------------------------------------------------------------
 */

static int
TmObjCmd(ClientData isGmt, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    time_t     now;
    struct tm *ptm;
    Tcl_Obj   *objPtr[9];

    NS_NONNULL_ASSERT(interp != NULL);
    
    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
    now = time(NULL);
    if (PTR2INT(isGmt) != 0) {
        ptm = ns_gmtime(&now);
    } else {
        ptm = ns_localtime(&now);
    }
    objPtr[0] = Tcl_NewIntObj(ptm->tm_sec);
    objPtr[1] = Tcl_NewIntObj(ptm->tm_min);
    objPtr[2] = Tcl_NewIntObj(ptm->tm_hour);
    objPtr[3] = Tcl_NewIntObj(ptm->tm_mday);
    objPtr[4] = Tcl_NewIntObj(ptm->tm_mon);
    objPtr[5] = Tcl_NewIntObj(ptm->tm_year);
    objPtr[6] = Tcl_NewIntObj(ptm->tm_wday);
    objPtr[7] = Tcl_NewIntObj(ptm->tm_yday);
    objPtr[8] = Tcl_NewIntObj(ptm->tm_isdst);
    Tcl_SetListObj(Tcl_GetObjResult(interp), 9, objPtr);

    return TCL_OK;
}

int
NsTclGmTimeObjCmd(ClientData UNUSED(clientData), Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    return TmObjCmd(INT2PTR(1), interp, objc, objv);
}

int
NsTclLocalTimeObjCmd(ClientData UNUSED(clientData), Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    return TmObjCmd(NULL, interp, objc, objv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSleepObjCmd --
 *
 *      Sleep with milisecond resolution.
 *
 * Results:
 *      Tcl Result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclSleepObjCmd(ClientData UNUSED(clientData), Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    int     ms;
    Ns_Time *tPtr = NULL;
    Ns_ObjvSpec args[] = {
        {"timespec", Ns_ObjvTime, &tPtr, NULL},
        {NULL, NULL, NULL, NULL}
    };
    
    if (Ns_ParseObjv(NULL, args, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;    
    }

    if (tPtr->sec < 0 || (tPtr->sec == 0 && tPtr->usec < 0)) {
        Tcl_AppendResult(interp, "invalid timespec: ",
                         Tcl_GetString(objv[1]), NULL);
        return TCL_ERROR;
    }
    ms = (int)(tPtr->sec * 1000 + tPtr->usec / 1000);
    Tcl_Sleep(ms);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStrftimeObjCmd --
 *
 *      Implements ns_fmttime.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      Depends on the time zone of the server process.
 *
 *----------------------------------------------------------------------
 */

int
NsTclStrftimeObjCmd(ClientData UNUSED(clientData), Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    const char *fmt;
    char    buf[200];
    time_t  t;
    long    sec;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "time ?fmt?");
        return TCL_ERROR;
    }
    if (Tcl_GetLongFromObj(interp, objv[1], &sec) != TCL_OK) {
        return TCL_ERROR;
    }
    t = sec;

    if (objc > 2) {
        fmt = Tcl_GetString(objv[2]);
    } else {
        fmt = "%c";
    }
    if (strftime(buf, sizeof(buf), fmt, ns_localtime(&t)) == 0u) {
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "invalid time: ",
                               Tcl_GetString(objv[1]), NULL);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * UpdateStringOfTime --
 *
 *      Update the string representation for an Ns_Time object.
 *
 *      Note: This procedure does not free an existing old string rep
 *      so storage will be lost if this has not already been done.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The object's string is set to a valid string that results from
 *      the Ns_Time-to-string conversion.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateStringOfTime(Tcl_Obj *objPtr)
{
    Ns_Time *timePtr;
    int      len;
    char     buf[(TCL_INTEGER_SPACE * 2) + 1];

    NS_NONNULL_ASSERT(objPtr != NULL);

    timePtr = (Ns_Time *) (void *) &objPtr->internalRep;
    Ns_AdjTime(timePtr);
    if (timePtr->usec == 0) {
        len = snprintf(buf, sizeof(buf), "%ld", timePtr->sec);
    } else {
        len = snprintf(buf, sizeof(buf), "%ld:%ld",
                       timePtr->sec, timePtr->usec);
    }
    Ns_TclSetStringRep(objPtr, buf, len);
}


/*
 *----------------------------------------------------------------------
 *
 * GetTimeFromString --
 *
 *      Try to fill ns_Time struct from a string based on a specified
 *      separator (':' or '.'). The colon separater is for the classical
 *      NaviServer time format "sec:usec", whereas the dot is used for the
 *      floating point format.
 *
 * Results:
 *      TCL_OK, TCL_ERROR or TCL_CONTINUE
 *
 * Side effects:
 *      Fill in sec und usec in the specified Ns_Time on success.
 *
 *----------------------------------------------------------------------
 */

static int
GetTimeFromString(Tcl_Interp *interp, const char *str, char separator, Ns_Time *tPtr)
{
    /*
     * Look for the separator
     */
    char *sep;

    NS_NONNULL_ASSERT(str != NULL);
    NS_NONNULL_ASSERT(tPtr != NULL);

    sep = strchr(str, (int)UCHAR(separator));
    if (sep != NULL) {
        int intValue;
        
        /*
         * First get sec from the string.
         */
        if (unlikely(sep == str)) {
            /*
             * The first character was the separator, treat sec as 0.
             */
            tPtr->sec = 0L;
        } else {
            int result;
            
            /*
             * Overwrite the separator with a null-byte to make the
             * first part null-terminated.
             */
            *sep = '\0';
            result = Tcl_GetInt(interp, str, &intValue);
            *sep = separator;
            if (result != TCL_OK) {
                return TCL_ERROR;
            }
            tPtr->sec = (long)intValue;
        }

        /*
         * Get usec
         */
        if (separator == '.') {
            double dblValue;
            
            if (Tcl_GetDouble(interp, sep, &dblValue) != TCL_OK) {
                return TCL_ERROR;
            }
            tPtr->usec = (long)(dblValue * 1000000.0);
            
        } else {
            if (Tcl_GetInt(interp, sep+1, &intValue) != TCL_OK) {
                return TCL_ERROR;
            }
            tPtr->usec = (long)intValue;
        }
        return TCL_OK;
    }
    return TCL_CONTINUE;
}


/*
 *----------------------------------------------------------------------
 *
 * SetTimeFromAny --
 *
 *      Attempt to generate an Ns_Time internal respresentation for the Tcl
 *      object. It interprets integer as seconds, but allows as well the form
 *      sec:usec or sec.fraction.
 *
 * Results:
 *      The return value is a standard object Tcl result. If an error occurs
 *      during conversion, an error message is left in the interpreter's
 *      result unless "interp" is NULL.
 *
 * Side effects:
 *      If no error occurs, an int is stored as "objPtr"s internal
 *      representation.
 *
 *----------------------------------------------------------------------
 */
static int
SetTimeFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    Ns_Time  t;
    long     sec;
    int      result = TCL_OK;

    NS_NONNULL_ASSERT(interp != NULL);
    NS_NONNULL_ASSERT(objPtr != NULL);
    
    if (objPtr->typePtr == intTypePtr) {
        /*
         * When the type is "int", usec is 0.
         */
        if (Tcl_GetLongFromObj(interp, objPtr, &sec) != TCL_OK) {
            return TCL_ERROR;
        }
        t.sec = sec;
        t.usec = 0;
    } else {
        result = Ns_GetTimeFromString(interp, Tcl_GetString(objPtr), &t);
    }
    
    if (result == TCL_OK) {
        Ns_AdjTime(&t);
        SetTimeInternalRep(objPtr, &t);
    }

    return result;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_GetTimeFromString --
 *
 *      Convert string to time structure.  Check, if the string contains the
 *      classical NaviServer separator for sec:usec and interprete the string
 *      in this format.  If not, check if this has a "." as separator, and use
 *      a floating point notation. 
 *
 * Results: 
 *      Tcl result code.
 *
 * Side effects:
 *      If an error occurs and interp is given, leave error message in the
 *      interp.
 *
 *----------------------------------------------------------------------
 */
int
Ns_GetTimeFromString(Tcl_Interp *interp, const char *str, Ns_Time *tPtr)
{
    int result;

    NS_NONNULL_ASSERT(str != NULL);
    NS_NONNULL_ASSERT(tPtr != NULL);

    result = GetTimeFromString(interp, str, ':', tPtr);
    
    if (result == TCL_CONTINUE) {
        result = GetTimeFromString(interp, str, '.', tPtr);
    }
    if (result == TCL_CONTINUE) {
        char *ptr = NULL;
        long sec;
        
        /*
         * No separator found, so try to interprete the string as integer
         */
        sec = strtol(str, &ptr, 10);
        
        if (likely(str != ptr)) {
            /*
             * We could parse at least a part of the string as integer.
             */
            tPtr->sec = sec;
            tPtr->usec = 0;
            result = TCL_OK;

        } else {
            /*
             * Still no success. If we have an interp, leave error message.
             */ 
            if (interp != NULL) {
                Ns_TclPrintfResult(interp, "Invalid time value '%s'", str);
            }
            result = TCL_ERROR;
        }
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * SetTimeInternalRep --
 *
 *      Set the internal Ns_Time, freeing a previous internal rep if
 *      necessary.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Object will be an Ns_Time type.
 *
 *----------------------------------------------------------------------
 */

static void
SetTimeInternalRep(Tcl_Obj *objPtr, const Ns_Time *timePtr)
{
    NS_NONNULL_ASSERT(objPtr != NULL);
    NS_NONNULL_ASSERT(timePtr != NULL);
    
    Ns_TclSetTwoPtrValue(objPtr, &timeType,
                         INT2PTR(timePtr->sec), INT2PTR(timePtr->usec));
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */
