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

// Unfortunately, we can not use an XPConnect object for
// the nsiModule and nsiComponentLoader interfaces.
//  As XPCOM shuts down, it shuts down the interface manager before 
// it releases all the modules.  This is a bit of a problem for 
// us, as it means we can't get runtime info on the interface at shutdown time.

#include "PyXPCOM_std.h"
#include <nsIModule.h>
#include <nsIFile.h>

class PyG_nsIModule : public PyG_Base, public nsIModule
{
public:
	PyG_nsIModule(PyObject *instance) : PyG_Base(instance, NS_GET_IID(nsIModule)) {;}
	PYGATEWAY_BASE_SUPPORT(nsIModule, PyG_Base);

	NS_DECL_NSIMODULE
};

PyG_Base *MakePyG_nsIModule(PyObject *instance)
{
	return new PyG_nsIModule(instance);
}


// Create a factory object for creating instances of aClass.
NS_IMETHODIMP
PyG_nsIModule::GetClassObject(nsIComponentManager *aCompMgr,
                                const nsCID& aClass,
                                const nsIID& aIID,
                                void** r_classObj)
{
	NS_PRECONDITION(r_classObj, "null pointer");
	*r_classObj = nsnull;
	CEnterLeavePython _celp;
	PyObject *cm = PyObject_FromNSInterface(aCompMgr, NS_GET_IID(nsIComponentManager));
	PyObject *iid = Py_nsIID::PyObjectFromIID(aIID);
	PyObject *clsid = Py_nsIID::PyObjectFromIID(aClass);
	const char *methodName = "getClassObject";
	PyObject *ret = NULL;
	nsresult nr = InvokeNativeViaPolicy(methodName, &ret, "OOO", cm, clsid, iid);
	Py_XDECREF(cm);
	Py_XDECREF(iid);
	Py_XDECREF(clsid);
	if (NS_SUCCEEDED(nr)) {
		nr = Py_nsISupports::InterfaceFromPyObject(ret, aIID, (nsISupports **)r_classObj, PR_FALSE);
		if (PyErr_Occurred())
			nr = HandleNativeGatewayError(methodName);
	}
	if (NS_FAILED(nr)) {
		NS_ABORT_IF_FALSE(*r_classObj==NULL, "returning error result with an interface - probable leak!");
	}
	Py_XDECREF(ret);
	return nr;
}

NS_IMETHODIMP
PyG_nsIModule::RegisterSelf(nsIComponentManager *aCompMgr,
                              nsIFile* aPath,
                              const char* registryLocation,
                              const char* componentType)
{
	NS_PRECONDITION(aCompMgr, "null pointer");
	NS_PRECONDITION(aPath, "null pointer");
	CEnterLeavePython _celp;
	PyObject *cm = PyObject_FromNSInterface(aCompMgr, NS_GET_IID(nsIComponentManager));
	PyObject *path = PyObject_FromNSInterface(aPath, NS_GET_IID(nsIFile));
	const char *methodName = "registerSelf";
	nsresult nr = InvokeNativeViaPolicy(methodName, NULL, "OOzz", cm, path, registryLocation, componentType);
	Py_XDECREF(cm);
	Py_XDECREF(path);
	return nr;
}

NS_IMETHODIMP
PyG_nsIModule::UnregisterSelf(nsIComponentManager* aCompMgr,
                            nsIFile* aPath,
                            const char* registryLocation)
{
	NS_PRECONDITION(aCompMgr, "null pointer");
	NS_PRECONDITION(aPath, "null pointer");
	CEnterLeavePython _celp;
	PyObject *cm = PyObject_FromNSInterface(aCompMgr, NS_GET_IID(nsIComponentManager));
	PyObject *path = PyObject_FromNSInterface(aPath, NS_GET_IID(nsIFile));
	const char *methodName = "unregisterSelf";
	nsresult nr = InvokeNativeViaPolicy(methodName, NULL, "OOz", cm, path, registryLocation);
	Py_XDECREF(cm);
	Py_XDECREF(path);
	return nr;
}

NS_IMETHODIMP
PyG_nsIModule::CanUnload(nsIComponentManager *aCompMgr, bool *okToUnload)
{
	NS_PRECONDITION(aCompMgr, "null pointer");
	NS_PRECONDITION(okToUnload, "null pointer");
	CEnterLeavePython _celp;
	// we are shutting down - don't ask for a nice wrapped object.
	PyObject *cm = PyObject_FromNSInterface(aCompMgr, NS_GET_IID(nsIComponentManager), PR_FALSE);
	const char *methodName = "canUnload";
	PyObject *ret = NULL;
	nsresult nr = InvokeNativeViaPolicy(methodName, &ret, "O", cm);
	Py_XDECREF(cm);
	if (NS_SUCCEEDED(nr)) {
		*okToUnload = PyInt_AsLong(ret);
		if (PyErr_Occurred())
			nr = HandleNativeGatewayError(methodName);
	}
	Py_XDECREF(ret);
	return nr;
}
