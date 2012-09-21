/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Python XPCOM language bindings.
 *
 * The Initial Developer of the Original Code is
 * ActiveState Tool Corp.
 * Portions created by the Initial Developer are Copyright (C) 2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mark Hammond <mhammond@skippinet.com.au> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

//
// This code is part of the XPCOM extensions for Python.
//
// Written May 2000 by Mark Hammond.
//
// Based heavily on the Python COM support, which is
// (c) Mark Hammond and Greg Stein.
//
// (c) 2000, ActiveState corp.

#include "PyXPCOM.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsIFile.h"
#include "nsIComponentRegistrar.h"
#include "nsIConsoleService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsXULAppAPI.h"

#include "nsILocalFile.h"
#include "nsTraceRefcntImpl.h"

#ifdef XP_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "windows.h"
#elif defined(XP_UNIX)
#include "prenv.h"
#endif

#include "nsIEventTarget.h"
#include "PyAppInfo.h"

#define LOADER_LINKS_WITH_PYTHON

#ifndef PYXPCOM_USE_PYGILSTATE
extern PYXPCOM_EXPORT void PyXPCOM_InterpreterState_Ensure();
#endif

// "boot-strap" methods - interfaces we need to get the base
// interface support!

static PyObject *
PyXPCOMMethod_GetComponentManager(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	nsCOMPtr<nsIComponentManager> cm;
	nsresult rv;
	Py_BEGIN_ALLOW_THREADS;
	rv = NS_GetComponentManager(getter_AddRefs(cm));
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(rv) )
		return PyXPCOM_BuildPyException(rv);

	return Py_nsISupports::PyObjectFromInterface(cm, NS_GET_IID(nsIComponentManager), PR_FALSE);
}

// No xpcom callable way to get at the registrar, even though the interface
// is scriptable.
static PyObject *
PyXPCOMMethod_GetComponentRegistrar(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	nsCOMPtr<nsIComponentRegistrar> cm;
	nsresult rv;
	Py_BEGIN_ALLOW_THREADS;
	rv = NS_GetComponentRegistrar(getter_AddRefs(cm));
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(rv) )
		return PyXPCOM_BuildPyException(rv);

	return Py_nsISupports::PyObjectFromInterface(cm, NS_GET_IID(nsISupports), PR_FALSE);
}

static PyObject *
PyXPCOMMethod_GetServiceManager(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	nsCOMPtr<nsIServiceManager> sm;
	nsresult rv;
	Py_BEGIN_ALLOW_THREADS;
	rv = NS_GetServiceManager(getter_AddRefs(sm));
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(rv) )
		return PyXPCOM_BuildPyException(rv);

	// Return a type based on the IID.
	return Py_nsISupports::PyObjectFromInterface(sm, NS_GET_IID(nsIServiceManager));
}

static PyObject *
PyXPCOMMethod_XPTI_GetInterfaceInfoManager(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	nsCOMPtr<nsIInterfaceInfoManager> im(do_GetService(
	                      NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));
	if ( im == nullptr )
		return PyXPCOM_BuildPyException(NS_ERROR_FAILURE);

	/* Return a type based on the IID (with no extra ref) */
	// Can not auto-wrap the interface info manager as it is critical to
	// building the support we need for autowrap.
	return Py_nsISupports::PyObjectFromInterface(im, NS_GET_IID(nsIInterfaceInfoManager), PR_FALSE);
}

static PyObject *
PyXPCOMMethod_NS_InvokeByIndex(PyObject *self, PyObject *args)
{
	PyObject *obIS, *obParams;
	nsCOMPtr<nsISupports> pis;
	int index;

	// We no longer rely on PyErr_Occurred() for our error state,
	// but keeping this assertion can't hurt - it should still always be true!
	NS_ASSERTION(!PyErr_Occurred(), "Should be no pending Python error!");

	if (!PyArg_ParseTuple(args, "OiO", &obIS, &index, &obParams))
		return NULL;

	if (!Py_nsISupports::Check(obIS)) {
		return PyErr_Format(PyExc_TypeError,
		                    "First param must be a native nsISupports wrapper (got %s)",
		                    obIS->ob_type->tp_name);
	}
	// Ack!  We must ask for the "native" interface supported by
	// the object, not specifically nsISupports, else we may not
	// back the same pointer (eg, Python, following identity rules,
	// will return the "original" gateway when QI'd for nsISupports)
	if (!Py_nsISupports::InterfaceFromPyObject(
			obIS, 
			Py_nsIID_NULL, 
			getter_AddRefs(pis), 
			PR_FALSE))
		return NULL;

	PyXPCOM_InterfaceVariantHelper arg_helper((Py_nsISupports *)obIS);
	if (!arg_helper.Init(obParams))
		return NULL;

	if (!arg_helper.PrepareCall())
		return NULL;

	nsresult r;
	Py_BEGIN_ALLOW_THREADS;
	r = NS_InvokeByIndex(pis, index,
	                     arg_helper.mDispatchParams.Length(),
	                     arg_helper.mDispatchParams.Elements());
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);

	return arg_helper.MakePythonResult();
}

/**
 * Wrap the given Python object in a new XPCOM stub (and re-wrap it in python
 * in order to return it)
 * @see xpcom.server.WrapObject
 * @param ob the Python object to wrap
 * @param iid the IID to wrap as
 * @param bWrapClient [default true] whether to allow extra wrapping for Python
 * 	consumers
 */
static PyObject *
PyXPCOMMethod_WrapObject(PyObject *self, PyObject *args)
{
	PyObject *ob, *obIID;
	int bWrapClient = 1;
	if (!PyArg_ParseTuple(args, "OO|i", &ob, &obIID, &bWrapClient))
		return NULL;

	nsIID	iid;
	if (!Py_nsIID::IIDFromPyObject(obIID, &iid))
		return NULL;

	nsCOMPtr<nsISupports> ret;
	nsresult r = PyXPCOM_XPTStub::CreateNew(ob, iid, getter_AddRefs(ret));
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);

	// _ALL_ wrapped objects are associated with a weak-ref
	// to their "main" instance.
	AddDefaultGateway(ob, ret); // inject a weak reference to myself into the instance.

	// Now wrap it in an interface.
	return Py_nsISupports::PyObjectFromInterface(ret, iid, bWrapClient);
}

static PyObject *
PyXPCOMMethod_UnwrapObject(PyObject *self, PyObject *args)
{
	PyObject *ob;
	if (!PyArg_ParseTuple(args, "O", &ob))
		return NULL;

	nsISupports *uob = NULL;
	nsIInternalPython *iob = NULL;
	PyObject *ret = NULL;
	if (!Py_nsISupports::InterfaceFromPyObject(ob, 
				NS_GET_IID(nsISupports), 
				&uob, 
				PR_FALSE))
		goto done;
	if (NS_FAILED(uob->QueryInterface(NS_GET_IID(nsIInternalPython), reinterpret_cast<void **>(&iob)))) {
		PyErr_SetString(PyExc_ValueError, "This XPCOM object is not implemented by Python");
		goto done;
	}
	ret = iob->UnwrapPythonObject();
done:
	Py_BEGIN_ALLOW_THREADS;
	NS_IF_RELEASE(uob);
	NS_IF_RELEASE(iob);
	Py_END_ALLOW_THREADS;
	return ret;
}

// @pymethod int|pythoncom|_GetInterfaceCount|Retrieves the number of interface objects currently in existance
static PyObject *
PyXPCOMMethod_GetInterfaceCount(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ":_GetInterfaceCount"))
		return NULL;
	return PyInt_FromLong(_PyXPCOM_GetInterfaceCount());
	// @comm If is occasionally a good idea to call this function before your Python program
	// terminates.  If this function returns non-zero, then you still have PythonCOM objects
	// alive in your program (possibly in global variables).
}

// @pymethod int|pythoncom|_GetGatewayCount|Retrieves the number of gateway objects currently in existance
static PyObject *
PyXPCOMMethod_GetGatewayCount(PyObject *self, PyObject *args)
{
	// @comm This is the number of Python object that implement COM servers which
	// are still alive (ie, serving a client).  The only way to reduce this count
	// is to have the process which uses these PythonCOM servers release its references.
	if (!PyArg_ParseTuple(args, ":_GetGatewayCount"))
		return NULL;
	return PyInt_FromLong(_PyXPCOM_GetGatewayCount());
}

static PyObject *
PyXPCOMMethod_NS_ShutdownXPCOM(PyObject *self, PyObject *args)
{
	// @comm This is the number of Python object that implement COM servers which
	// are still alive (ie, serving a client).  The only way to reduce this count
	// is to have the process which uses these PythonCOM servers release its references.
	if (!PyArg_ParseTuple(args, ":NS_ShutdownXPCOM"))
		return NULL;
	nsresult nr;
	Py_BEGIN_ALLOW_THREADS;
	nr = NS_ShutdownXPCOM(nullptr);
	Py_END_ALLOW_THREADS;

	// Dont raise an exception - as we are probably shutting down
	// and dont really case - just return the status
	return PyInt_FromLong(nr);
}

static PyObject *
PyXPCOMMethod_MakeVariant(PyObject *self, PyObject *args)
{
	PyObject *ob;
	if (!PyArg_ParseTuple(args, "O:MakeVariant", &ob))
		return NULL;
	nsCOMPtr<nsIVariant> pVar;
	nsresult nr = PyObject_AsVariant(ob, getter_AddRefs(pVar));
	if (NS_FAILED(nr))
		return PyXPCOM_BuildPyException(nr);
	if (pVar == nullptr) {
		NS_ERROR("PyObject_AsVariant worked but returned a NULL ptr!");
		return PyXPCOM_BuildPyException(NS_ERROR_UNEXPECTED);
	}
	return Py_nsISupports::PyObjectFromInterface(pVar, NS_GET_IID(nsIVariant));
}

static PyObject *
PyXPCOMMethod_GetVariantValue(PyObject *self, PyObject *args)
{
	PyObject *ob, *obParent = NULL;
	if (!PyArg_ParseTuple(args, "O|O:GetVariantValue", &ob, &obParent))
		return NULL;

	nsCOMPtr<nsIVariant> var;
	if (!Py_nsISupports::InterfaceFromPyObject(ob, 
				NS_GET_IID(nsISupports), 
				getter_AddRefs(var), 
				PR_FALSE))
		return PyErr_Format(PyExc_ValueError,
				    "Object is not an nsIVariant (got %s)",
				    ob->ob_type->tp_name);

	Py_nsISupports *parent = nullptr;
	if (obParent && obParent != Py_None) {
		if (!Py_nsISupports::Check(obParent)) {
			PyErr_SetString(PyExc_ValueError,
					"Object not an nsISupports wrapper");
			return NULL;
		}
		parent = (Py_nsISupports *)obParent;
	}
	return PyObject_FromVariant(parent, var);
}

PyObject *PyGetSpecialDirectory(PyObject *self, PyObject *args)
{
	char *dirname;
	if (!PyArg_ParseTuple(args, "s:GetSpecialDirectory", &dirname))
		return NULL;
	nsCOMPtr<nsIFile> file;
	nsresult r = NS_GetSpecialDirectory(dirname, getter_AddRefs(file));
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);
	// returned object swallows our reference.
	return Py_nsISupports::PyObjectFromInterface(file, NS_GET_IID(nsIFile));
}

PyObject *AllocateBuffer(PyObject *self, PyObject *args)
{
	int bufSize;
	if (!PyArg_ParseTuple(args, "i", &bufSize))
		return NULL;
	return PyBuffer_New(bufSize);
}

// Writes a message to the console service.  This could be done via pure
// Python code, but is useful when the logging code is actually the
// xpcom .py framework itself (ie, we don't want our logging framework to
// call back into the very code generating the log messages!
PyObject *LogConsoleMessage(PyObject *self, PyObject *args)
{
	char *msg;
	if (!PyArg_ParseTuple(args, "s", &msg))
		return NULL;

	nsCOMPtr<nsIConsoleService> consoleService = do_GetService(NS_CONSOLESERVICE_CONTRACTID);
	if (consoleService)
		consoleService->LogStringMessage(NS_ConvertASCIItoUTF16(msg).get());
	else {
	// This either means no such service, or in shutdown - hardly worth
	// the warning, and not worth reporting an error to Python about - its
	// log handler would just need to catch and ignore it.
	// And as this is only called by this logging setup, any messages should
	// still go to stderr or a logfile.
		NS_WARNING("pyxpcom can't log console message.");
	}

	Py_INCREF(Py_None);
	return Py_None;
}

extern PYXPCOM_EXPORT PyObject *PyXPCOMMethod_IID(PyObject *self, PyObject *args);

static struct PyMethodDef xpcom_methods[]=
{
	{"GetComponentManager", PyXPCOMMethod_GetComponentManager, 1},
	{"GetComponentRegistrar", PyXPCOMMethod_GetComponentRegistrar, 1},
	{"XPTI_GetInterfaceInfoManager", PyXPCOMMethod_XPTI_GetInterfaceInfoManager, 1},
	{"NS_InvokeByIndex", PyXPCOMMethod_NS_InvokeByIndex, 1},
	{"GetServiceManager", PyXPCOMMethod_GetServiceManager, 1},
	{"IID", PyXPCOMMethod_IID, 1}, // IID is wrong - deprecated - not just IID, but CID, etc. 
	{"ID", PyXPCOMMethod_IID, 1}, // This is the official name.
	{"NS_ShutdownXPCOM", PyXPCOMMethod_NS_ShutdownXPCOM, 1},
	{"WrapObject", PyXPCOMMethod_WrapObject, 1},
	{"UnwrapObject", PyXPCOMMethod_UnwrapObject, 1},
	{"_GetInterfaceCount", PyXPCOMMethod_GetInterfaceCount, 1},
	{"_GetGatewayCount", PyXPCOMMethod_GetGatewayCount, 1},
	{"GetSpecialDirectory", PyGetSpecialDirectory, 1},
	{"AllocateBuffer", AllocateBuffer, 1},
	{"LogConsoleMessage", LogConsoleMessage, 1, "Write a message to the xpcom console service"},
	{"MakeVariant", PyXPCOMMethod_MakeVariant, 1},
	{"GetVariantValue", PyXPCOMMethod_GetVariantValue, 1},
	// These should no longer be used - just use the logging.getLogger('pyxpcom')...
	{ NULL }
};

#define REGISTER_IID(t) { \
	PyObject *iid_ob = Py_nsIID::PyObjectFromIID(NS_GET_IID(t)); \
	PyDict_SetItemString(dict, "IID_"#t, iid_ob); \
	Py_DECREF(iid_ob); \
	}

#define REGISTER_INT(val) { \
	PyObject *ob = PyInt_FromLong(val); \
	PyDict_SetItemString(dict, #val, ob); \
	Py_DECREF(ob); \
	}


// local helper to check that xpcom itself has been initialized.
// Theoretically this should only happen when a standard python program
// (ie, hosted by python itself) imports the xpcom module (ie, as part of
// the pyxpcom test suite), hence it lives here...
static PRBool EnsureXPCOM()
{
	static PRBool bHaveInitXPCOM = PR_FALSE;
	if (!bHaveInitXPCOM) {
		// xpcom appears to assert if already initialized, but there
		// is no official way to determine this!  Sadly though,
		// apparently this problem is not real ;) See bug 38671.
		// For now, getting the app directories appears to work.
		nsCOMPtr<nsIFile> file;
		if (NS_FAILED(NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(file)))) {
			// not already initialized.
			nsresult rv;
#ifdef XP_WIN
			// On Windows, we need to locate the Mozilla bin
			// directory.  This by using locating a Moz DLL we depend
			// on, and assume it lives in that bin dir.  Different
			// moz build types (eg, xulrunner, suite) package
			// XPCOM itself differently - but all appear to require
			// nspr4.dll - so this is what we use.
			char landmark[MAX_PATH+1];
			HMODULE hmod = GetModuleHandle("nspr4.dll");
			if (hmod==NULL) {
				PyErr_SetString(PyExc_RuntimeError, "We dont appear to be linked against nspr4.dll.");
				return PR_FALSE;
			}
			GetModuleFileName(hmod, landmark, sizeof(landmark)/sizeof(landmark[0]));
			char *end = landmark + (strlen(landmark)-1);
			while (end > landmark && *end != '\\')
				end--;
			if (end > landmark) *end = '\0';

			nsCOMPtr<nsIFile> ns_bin_dir;
			NS_ConvertASCIItoUTF16 strLandmark(landmark);
			NS_NewLocalFile(strLandmark, PR_FALSE, getter_AddRefs(ns_bin_dir));
			rv = NS_InitXPCOM2(nullptr, ns_bin_dir, nullptr);
#elif defined(XP_UNIX)
			// Elsewhere, try to check MOZILLA_FIVE_HOME
			char* ns_bin_path = PR_GetEnv("MOZILLA_FIVE_HOME");
			nsCOMPtr<nsIFile> ns_bin_dir;
			if (ns_bin_path && *ns_bin_path) {
				rv = NS_NewNativeLocalFile(nsDependentCString(ns_bin_path),
							   PR_FALSE,
							   getter_AddRefs(ns_bin_dir));
				if (NS_FAILED(rv)) {
					ns_bin_dir = nullptr;
				}
			}
			rv = NS_InitXPCOM2(nullptr, ns_bin_dir, nullptr);
#endif
			if (NS_FAILED(rv)) {
				PyErr_SetString(PyExc_RuntimeError, "The XPCOM subsystem could not be initialized");
				return PR_FALSE;
			}

			nsCOMPtr<nsIFile> app_dir;
			char* app_path = PR_GetEnv("PYXPCOM_APPDIR");
			if (app_path && *app_path) {
				rv = NS_NewNativeLocalFile(nsDependentCString(app_path),
				                           PR_FALSE,
				                           getter_AddRefs(app_dir));
				if (NS_FAILED(rv)) {
					app_dir = nullptr;
				}
			}
			PyAppInfo* appinfo = PyAppInfo::GetSingleton(app_dir);
			nsCOMPtr<nsIComponentRegistrar> registrar;
			rv = NS_GetComponentRegistrar(getter_AddRefs(registrar));
			if (appinfo && registrar) {
				const nsCID APPINFO_CID = {
						/* cccd5dab-efc5-4d14-8ee5-7fe30e2a23ba */
						0xcccd5dab, 0xefc5, 0x4d14,
						{0x8e, 0xe5, 0x7f, 0xe3, 0x0e, 0x2a, 0x23, 0xba}
					};
				rv = registrar->RegisterFactory(APPINFO_CID,
				                                "Python XPCOM App Info",
				                                "@mozilla.org/xre/app-info;1",
				                                appinfo);
			}
			if (app_dir) {
				nsCOMPtr<nsIFile> app_manifest;
				rv = app_dir->Clone(getter_AddRefs(app_manifest));
				if (NS_SUCCEEDED(rv)) {
					app_manifest->Append(NS_LITERAL_STRING("pyxpcom.manifest"));
					bool exists = false;
					rv = app_manifest->Exists(&exists);
					if (NS_SUCCEEDED(rv) && exists) {
						(void)XRE_AddManifestLocation(NS_COMPONENT_LOCATION, app_manifest);
					}
				}
			}
		}
		// Even if xpcom was already init, we want to flag it as init!
		bHaveInitXPCOM = PR_TRUE;
	}
	return PR_TRUE;
}

////////////////////////////////////////////////////////////
// The module init code.
//
extern "C" NS_EXPORT
void 
init_xpcom() {
	PyObject *oModule;

	// ensure xpcom already init
	if (!EnsureXPCOM())
		return;

	// ensure the framework has valid state to work with.
	if (!PyXPCOM_Globals_Ensure())
		return;

	// Must force Python to start using thread locks
	PyEval_InitThreads();

	// Create the module and add the functions
	oModule = Py_InitModule("_xpcom", xpcom_methods);

	PyObject *dict = PyModule_GetDict(oModule);
	PyObject *pycom_Error = PyXPCOM_Error;
	if (pycom_Error == NULL || PyDict_SetItemString(dict, "error", pycom_Error) != 0)
	{
		PyErr_SetString(PyExc_MemoryError, "can't define error");
		return;
	}
	PyDict_SetItemString(dict, "IIDType", (PyObject *)&Py_nsIID::type);

	REGISTER_IID(nsISupports);
	REGISTER_IID(nsISupportsCString);
	REGISTER_IID(nsISupportsString);
	REGISTER_IID(nsIModule);
	REGISTER_IID(nsIFactory);
	REGISTER_IID(nsIWeakReference);
	REGISTER_IID(nsISupportsWeakReference);
	REGISTER_IID(nsIClassInfo);
	REGISTER_IID(nsIServiceManager);
	REGISTER_IID(nsIComponentRegistrar);

	// Register our custom interfaces.
	REGISTER_IID(nsIComponentManager);
	REGISTER_IID(nsIInterfaceInfoManager);
	REGISTER_IID(nsIEnumerator);
	REGISTER_IID(nsISimpleEnumerator);
	REGISTER_IID(nsIInterfaceInfo);
	REGISTER_IID(nsIInputStream);
	REGISTER_IID(nsIClassInfo);
	REGISTER_IID(nsIVariant);

	// No good reason not to expose this impl detail, and tests can use it
	REGISTER_IID(nsIInternalPython);
    // Build flags that may be useful.
    PyObject *ob = PyBool_FromLong(
#ifdef NS_DEBUG
                                   1
#else
                                   0
#endif
                                   );
    PyDict_SetItemString(dict, "NS_DEBUG", ob);
    Py_DECREF(ob);
    // Flag we initialized correctly!
    PyXPCOM_ModuleInitialized = PR_TRUE;
}
