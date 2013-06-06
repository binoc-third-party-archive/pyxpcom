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

#include "PyXPCOM_std.h"
#include "nsCOMArray.h"
#include <nsIInterfaceInfoManager.h>
#include "mozilla/XPTInterfaceInfoManager.h"

using namespace mozilla;

static nsIInterfaceInfoManager *GetI(PyObject *self) {
	nsIID iid = NS_GET_IID(nsIInterfaceInfoManager);

	if (!Py_nsISupports::Check(self, iid)) {
		PyErr_SetString(PyExc_TypeError, "This object is not the correct interface");
		return NULL;
	}
	return (nsIInterfaceInfoManager *)Py_nsISupports::GetI(self);
}

static PyObject *PyGetInfoForIID(PyObject *self, PyObject *args)
{
	PyObject *obIID = NULL;
	if (!PyArg_ParseTuple(args, "O", &obIID))
		return NULL;

	nsIInterfaceInfoManager *pI = GetI(self);
	if (pI==NULL)
		return NULL;

	nsIID iid;
	if (!Py_nsIID::IIDFromPyObject(obIID, &iid))
		return NULL;

	nsCOMPtr<nsIInterfaceInfo> pi;
	nsresult r;
	Py_BEGIN_ALLOW_THREADS;
	r = pI->GetInfoForIID(&iid, getter_AddRefs(pi));
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);

	/* Return a type based on the IID (with no extra ref) */
	nsIID new_iid = NS_GET_IID(nsIInterfaceInfo);
	// Can not auto-wrap the interface info manager as it is critical to
	// building the support we need for autowrap.
	return Py_nsISupports::PyObjectFromInterface(pi, new_iid, false);
}

static PyObject *PyGetInfoForName(PyObject *self, PyObject *args)
{
	char *name;
	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	nsIInterfaceInfoManager *pI = GetI(self);
	if (pI==NULL)
		return NULL;

	nsCOMPtr<nsIInterfaceInfo> pi;
	nsresult r;
	Py_BEGIN_ALLOW_THREADS;
	r = pI->GetInfoForName(name, getter_AddRefs(pi));
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);

	/* Return a type based on the IID (with no extra ref) */
	// Can not auto-wrap the interface info manager as it is critical to
	// building the support we need for autowrap.
	return Py_nsISupports::PyObjectFromInterface(pi, NS_GET_IID(nsIInterfaceInfo), false);
}

static PyObject *PyGetNameForIID(PyObject *self, PyObject *args)
{
	PyObject *obIID = NULL;
	if (!PyArg_ParseTuple(args, "O", &obIID))
		return NULL;

	nsIInterfaceInfoManager *pI = GetI(self);
	if (pI==NULL)
		return NULL;

	nsIID iid;
	if (!Py_nsIID::IIDFromPyObject(obIID, &iid))
		return NULL;

	char *ret_name = NULL;
	nsresult r;
	Py_BEGIN_ALLOW_THREADS;
	r = pI->GetNameForIID(&iid, &ret_name);
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);

	PyObject *ret = PyString_FromString(ret_name);
	nsMemory::Free(ret_name);
	return ret;
}

static PyObject *PyGetIIDForName(PyObject *self, PyObject *args)
{
	char *name;
	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	nsIInterfaceInfoManager *pI = GetI(self);
	if (pI==NULL)
		return NULL;

	nsIID *iid_ret;
	nsresult r;
	Py_BEGIN_ALLOW_THREADS;
	r = pI->GetIIDForName(name, &iid_ret);
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);

	PyObject *ret = Py_nsIID::PyObjectFromIID(*iid_ret);
	nsMemory::Free(iid_ret);
	return ret;
}

static PyObject *GetScriptableInterfaces(PyObject *self, PyObject *args, bool fn_scripables_only)
{
	nsIInterfaceInfoManager *pI = GetI(self);
	if (pI==NULL)
		return NULL;

	nsCOMPtr<nsIEnumerator> interfaces_enum;
	nsCOMPtr<nsIInterfaceInfo> iinfo;
	nsCOMPtr<nsISupports> entry;

	/**
	 * nsIInterfaceInfoManager does not provide us a clean way to get at all
	 * the interfaces (it used to, but has since been deleted), so we have
	 * to hackily ask for all interfaces that begin with a certain prefix
	 * (minimum length of 1 character), and then merge these results back
	 * together.
	 */

	nsresult r;
	int i;
	bool isFunction;
	char *alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	int len = strlen(alphabet);
	char prefix[2];
	const char *if_name = nullptr;
	const nsIID *iid = nullptr;

	PyObject *pyiid;
	PyObject* pydict = PyDict_New();
	if (!pydict) {
		return NULL;
	}

	for (i=0; i < len; i++) {

		// Request for the interfaces beginning with character prefix.
		sprintf(prefix, "%c", *(alphabet+i));
		r = pI->EnumerateInterfacesWhoseNamesStartWith(prefix, getter_AddRefs(interfaces_enum));
		if (NS_FAILED(r)) {
			return PyXPCOM_BuildPyException(r);
		}

		r = interfaces_enum->First();
		if (NS_FAILED(r)) {
			// Empty interface list, that's okay then.
			continue;
		}

		// Add all returned interfaces to the python dict.
		for ( ; interfaces_enum->IsDone() == static_cast<nsresult>(NS_ENUMERATOR_FALSE)
		      ; interfaces_enum->Next())
		{
			// Get the interface values.
			r = interfaces_enum->CurrentItem(getter_AddRefs(entry));
			if (NS_FAILED(r)) {
				return PyXPCOM_BuildPyException(r);
			}
			nsCOMPtr<nsIInterfaceInfo> iinfo(do_QueryInterface(entry));
			if (fn_scripables_only) {
				if (NS_FAILED(iinfo->IsFunction(&isFunction))) {
					continue;
				}
				if (!isFunction) {
					continue;
				}
			}
			iinfo->GetNameShared(&if_name);
			iinfo->GetIIDShared(&iid);

			// Convert values to Python.
			pyiid = Py_nsIID::PyObjectFromIID(*iid);
			if (!pyiid) {
				Py_XDECREF(pyiid);
				Py_DECREF(pydict);
				return NULL;
			}

			// Add results to the dictionary.
			if (PyDict_SetItemString(pydict, if_name, pyiid) != 0) {
				Py_DECREF(pyiid);
				Py_DECREF(pydict);
				return NULL;
			}
			Py_DECREF(pyiid);
		}

	}

	return pydict;
}

static PyObject *PyGetScriptableInterfaces(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;

	return GetScriptableInterfaces(self, args, false);
}

static PyObject *PyGetFunctionInterfaces(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;

	return GetScriptableInterfaces(self, args, true);
}

// TODO:
// void autoRegisterInterfaces();

PyMethodDef 
PyMethods_IInterfaceInfoManager[] =
{
	{ "GetInfoForIID", PyGetInfoForIID, 1},
	{ "getInfoForIID", PyGetInfoForIID, 1},
	{ "GetInfoForName", PyGetInfoForName, 1},
	{ "getInfoForName", PyGetInfoForName, 1},
	{ "GetIIDForName", PyGetIIDForName, 1},
	{ "getIIDForName", PyGetIIDForName, 1},
	{ "GetNameForIID", PyGetNameForIID, 1},
	{ "getNameForIID", PyGetNameForIID, 1},
	{ "GetScriptableInterfaces", PyGetScriptableInterfaces, 1, "Return dict (name, iid) of all scriptable interfaces"},
	{ "GetFunctionInterfaces", PyGetFunctionInterfaces, 1, "Return dict (name, iid) of all scriptable function interfaces"},
	{NULL}
};
