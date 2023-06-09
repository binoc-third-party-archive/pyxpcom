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

// PyGBase.cpp - implementation of the PyG_Base class
//
// This code is part of the XPCOM extensions for Python.
//
// Written May 2000 by Mark Hammond.
//
// Based heavily on the Python COM support, which is
// (c) Mark Hammond and Greg Stein.
//
// (c) 2000, ActiveState corp.

/* Allow logging in the release build */
#ifdef MOZ_LOGGING
#define FORCE_PR_LOG
#endif

#include "prlog.h"
#include "prinit.h"
#include "prerror.h"
#include "pratom.h"

#include "PyXPCOM_std.h"

#include <nsIModule.h>
#include <nsIInputStream.h>
#include <nsIWeakReferenceUtils.h>

static PRLogModuleInfo *nsPyxpcomLog = PR_NewLogModule("nsPyxpcomLog");

#define LOG(level, args) PR_LOG(nsPyxpcomLog, level, args)


static PRInt32 cGateways = 0;

PRInt32 _PyXPCOM_GetGatewayCount(void)
{
	return cGateways;
}

extern PyG_Base *MakePyG_nsIModule(PyObject *);
extern PyG_Base *MakePyG_nsIInputStream(PyObject *instance);

static char *PyXPCOM_szDefaultGatewayAttributeName = "_com_instance_default_gateway_";
static PyG_Base *GetDefaultGateway(PyObject *instance);
static bool CheckDefaultGateway(PyObject *real_inst, REFNSIID iid, nsISupports **ret_gateway);

/*static*/ nsresult 
PyG_Base::CreateNew(PyObject *pPyInstance, const nsIID &iid, void **ppResult)
{
	NS_PRECONDITION(ppResult && *ppResult==NULL, "NULL or uninitialized pointer");
	if (ppResult==nullptr)
		return NS_ERROR_NULL_POINTER;

	PyG_Base *ret;
	// Hack for few extra gateways we support.
	if (iid.Equals(NS_GET_IID(nsIModule)))
		ret = MakePyG_nsIModule(pPyInstance);
	else if (iid.Equals(NS_GET_IID(nsIInputStream)))
		ret = MakePyG_nsIInputStream(pPyInstance);
	else
		ret = new PyXPCOM_XPTStub(pPyInstance, iid);
	if (ret==nullptr)
		return NS_ERROR_OUT_OF_MEMORY;
	ret->AddRef(); // The first reference for the caller.
	*ppResult = ret->ThisAsIID(iid);
	NS_ABORT_IF_FALSE(*ppResult != NULL, "ThisAsIID() gave NULL, but we know it supports it!");
	return *ppResult ? NS_OK : NS_ERROR_FAILURE;
}

PyG_Base::PyG_Base(PyObject *instance, const nsIID &iid)
{
	// Note that "instance" is the _policy_ instance!!
	PR_ATOMIC_INCREMENT(&cGateways);
	m_pBaseObject = GetDefaultGateway(instance);
	// m_pWeakRef is an nsCOMPtr and needs no init.

	NS_ABORT_IF_FALSE(!(iid.Equals(NS_GET_IID(nsISupportsWeakReference)) || iid.Equals(NS_GET_IID(nsIWeakReference))),"Should not be creating gateways with weak-ref interfaces");
	m_iid = iid;
	m_pPyObject = instance;
	NS_PRECONDITION(instance, "NULL PyObject for PyXPCOM_XPTStub!");

#ifdef NS_BUILD_REFCNT_LOGGING
	// If XPCOM reference count logging is enabled, then allow us to give the Python class.
	PyObject *realInstance = PyObject_GetAttrString(instance, "_obj_");
	PyObject *r = PyObject_Repr(realInstance);
	const char *szRepr;
	if (r==NULL) {
		PyXPCOM_LogError("Getting the __repr__ of the object failed");
		PyErr_Clear();
		szRepr = "(repr failed!)";
	}
	else
		szRepr = PyString_AsString(r);
	if (szRepr==NULL) szRepr = "";
	int reprOffset = *szRepr=='<' ? 1 : 0;
	static const char *reprPrefix = "component:";
	if (strncmp(reprPrefix, szRepr+reprOffset, strlen(reprPrefix)) == 0)
		reprOffset += strlen(reprPrefix);
	strncpy(refcntLogRepr, szRepr + reprOffset, sizeof(refcntLogRepr)-1);
	refcntLogRepr[sizeof(refcntLogRepr)-1] = '\0';
	// See if we should get rid of the " at 0x12345" portion.
	char *lastPos = strstr(refcntLogRepr, " at ");
	if (lastPos) *lastPos = '\0';
	Py_XDECREF(realInstance);
	Py_XDECREF(r);
#endif // NS_BUILD_REFCNT_LOGGING

#ifdef DEBUG_LIFETIMES
	{
		char *iid_repr;
		nsCOMPtr<nsIInterfaceInfoManager> iim = XPTI_GetInterfaceInfoManager();
		if (iim!=nullptr)
			iim->GetNameForIID(&iid, &iid_repr);
		PyObject *real_instance = PyObject_GetAttrString(instance, "_obj_");
		PyObject *real_repr = PyObject_Repr(real_instance);

		PYXPCOM_LOG_DEBUG("PyG_Base created at %p\n  instance_repr=%s\n  IID=%s\n", this, PyString_AsString(real_repr), iid_repr);
		nsMemory::Free(iid_repr);
		Py_XDECREF(real_instance);
		Py_XDECREF(real_repr);
	}
#endif // DEBUG_LIFETIMES
	Py_XINCREF(instance); // instance should never be NULL - but what's an X between friends!

#ifdef DEBUG_FULL
	LogF("PyGatewayBase: created %s", m_pPyObject ? m_pPyObject->ob_type->tp_name : "<NULL>");
#endif

	#if PYXPCOM_DEBUG_GATEWAY_COUNT
		if (gDebugCountLog) {
			char iidbuf[NSID_LENGTH];
			m_iid.ToProvidedString(iidbuf);
			// Try to get the interface name...
			nsCOMPtr<nsIInterfaceInfoManager> iim(do_GetService(
				NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));
			char *iid_name = nullptr, *iface_buf = nullptr;
			nsresult rv = NS_ERROR_FAILURE;
			if (iim)
				rv = iim->GetNameForIID(&m_iid, &iid_name);
			if (NS_SUCCEEDED(rv)) {
				iface_buf = reinterpret_cast<char*>(moz_xmalloc(NSID_LENGTH + strlen(iid_name) + 2));
				sprintf(iface_buf, "%s/%s", iidbuf, iid_name);
				moz_free(iid_name);
			} else {
				iface_buf = reinterpret_cast<char*>(moz_xmalloc(NSID_LENGTH));
				strcpy(iface_buf, iidbuf);
			}
			PyObject *real_instance = PyObject_GetAttrString(m_pPyObject, "_obj_");
			char *obj_buf = nullptr;
			// on Linux, %p prints 0x........, so we need the +2 here; the +1 is for the null.
			const size_t ptr_size = sizeof(real_instance) * 2 + 2 + 1;
			// Try to get the class name...
			if (real_instance && PyInstance_Check(real_instance)) {
				// old style classes
				PyInstanceObject *inst = reinterpret_cast<PyInstanceObject*>(real_instance);
				PyObject *cls_name = inst->in_class->cl_name;
				if (PyString_Check(cls_name)) {
					obj_buf = reinterpret_cast<char*>(moz_xmalloc(ptr_size + PyString_GET_SIZE(cls_name) + 1));
					sprintf(obj_buf, "%p/%s", real_instance, PyString_AS_STRING(cls_name));
				}
			}
			if (!obj_buf) {
				const char *tp_name = real_instance->ob_type->tp_name;
				size_t name_len = strlen(tp_name);
				obj_buf = reinterpret_cast<char*>(moz_xmalloc(ptr_size + name_len + 1));
				sprintf(obj_buf, "%p/%s", real_instance, tp_name);
			}
			fprintf(gDebugCountLog,
				"G ++ %s Gateway=%p PyObject=%s total=%i\n",
				iface_buf, this, obj_buf, cGateways);
			moz_free(iface_buf);
			moz_free(obj_buf);
			Py_XDECREF(real_instance);
			fflush(gDebugCountLog);
		}
	#endif /* PYXPCOM_DEBUG_GATEWAY_COUNT */
}

PyG_Base::~PyG_Base()
{
	PR_ATOMIC_DECREMENT(&cGateways);
#ifdef DEBUG_LIFETIMES
	PYXPCOM_LOG_DEBUG("PyG_Base: deleted %p", this);
#endif

	#if PYXPCOM_DEBUG_GATEWAY_COUNT
		if (gDebugCountLog) {
			CEnterLeavePython _celp;
			PyObject *real_instance = PyObject_GetAttrString(m_pPyObject, "_obj_");
			char iidbuf[NSID_LENGTH];
			m_iid.ToProvidedString(iidbuf);
			fprintf(gDebugCountLog,
				"G -- %s Gateway=%p PyObject=%p total=%i\n",
				iidbuf, this, real_instance, cGateways);
			Py_XDECREF(real_instance);
			fflush(gDebugCountLog);
		}
	#endif /* PYXPCOM_DEBUG_GATEWAY_COUNT */

	if ( m_pPyObject ) {
		CEnterLeavePython celp;
		Py_DECREF(m_pPyObject);
	}
	if (m_pBaseObject)
		m_pBaseObject->Release();
	if (m_pWeakRef) {
		// Need to ensure some other thread isn't doing a QueryReferent on
		// our weak reference at the same time
		CEnterLeaveXPCOMFramework _celf;
		PyXPCOM_GatewayWeakReference *p = (PyXPCOM_GatewayWeakReference *)(nsISupports *)m_pWeakRef;
		p->m_pBase = nullptr;
		m_pWeakRef = nullptr;
	}
}

NS_IMPL_ADDREF(PyG_Base)

MozExternalRefCountType
PyG_Base::Release(void)
{
	nsrefcnt cnt;
	{ 
		// Temp scope for lock. Ensures some other thread isn't doing a
		// QueryReferent on our weak reference at the same time.
		CEnterLeaveXPCOMFramework _celf;
		cnt = (nsrefcnt) PR_ATOMIC_DECREMENT((PRInt32*)&mRefCnt);
		if ( cnt == 0 && m_pWeakRef ) {
			// We must null out the WeakReference now, otherwise
			// another thread may come along and try to use it, i.e.
			// between the time we release the lock and before we
			// delete the object (which == ka-BOOM!). See Komodo bug
			// http://bugs.activestate.com/show_bug.cgi?id=88165
			PyXPCOM_GatewayWeakReference *p = (PyXPCOM_GatewayWeakReference *)(nsISupports *)m_pWeakRef;
			p->m_pBase = nullptr;
			m_pWeakRef = nullptr;
		}
	}
#ifdef NS_BUILD_REFCNT_LOGGING
	if (m_pBaseObject == NULL)
		NS_LOG_RELEASE(this, cnt, refcntLogRepr);
#endif
	if ( cnt == 0 )
		delete this;
	return cnt;
}


// Get the correct interface pointer for this object given the IID.
void *PyG_Base::ThisAsIID( const nsIID &iid )
{
	if (this==NULL) return NULL;
	if (iid.Equals(NS_GET_IID(nsISupports)))
		return (nsISupports *)(nsIInternalPython *)this;
	if (iid.Equals(NS_GET_IID(nsISupportsWeakReference)))
		return (nsISupportsWeakReference *)this;
	if (iid.Equals(NS_GET_IID(nsIInternalPython))) 
		return (nsISupports *)(nsIInternalPython *)this;
	return NULL;
}

// Call back into Python, passing a Python instance, and get back
// an interface object that wraps the instance.
/*static*/ bool 
PyG_Base::AutoWrapPythonInstance(PyObject *ob, const nsIID &iid, nsISupports **ppret)
{
	NS_PRECONDITION(ppret!=NULL, "null pointer when wrapping a Python instance!");
	NS_PRECONDITION(ob && PyObject_HasAttrString(ob, "__class__"),
			"AutoWrapPythonInstance is expecting a non-NULL instance!");
	bool ok = false;
	if (PR_LOG_TEST(nsPyxpcomLog, PR_LOG_DEBUG)) {
		PyObject *r = PyObject_Repr(ob);
		if (r!=NULL) {
			char idstr[NSID_LENGTH];
			iid.ToProvidedString(idstr);
			LOG(PR_LOG_DEBUG, ("PyG_Base::AutoWrapPythonInstance: ob: '%s' to iid: %s", PyString_AsString(r), idstr));
			Py_DECREF(r);
		}
	}
    // XXX - todo - this static object leaks! (but Python on Windows leaks 2000+ objects as it is ;-)
	static PyObject *func = NULL; // fetch this once and remember!
	PyObject *obIID = NULL;
	PyObject *wrap_ret = NULL;
	PyObject *args = NULL;
	// See if the instance has previously been wrapped.
	if (CheckDefaultGateway(ob, iid, ppret)) {
		ok = true; // life is good!
	} else {
		if (func==NULL) { // not thread-safe, but nothing bad can happen, except an extra reference leak
			PyObject *mod = PyImport_ImportModule("xpcom.server");
			if (mod)
				func = PyObject_GetAttrString(mod, "WrapObject");
			Py_XDECREF(mod);
			if (func==NULL) goto done;
		}

		PyErr_Clear();

		obIID = Py_nsIID::PyObjectFromIID(iid);
		if (obIID==NULL) goto done;
		args = Py_BuildValue("OOzi", ob, obIID, NULL, 0);
		if (args==NULL) goto done;
		wrap_ret = PyEval_CallObject(func, args);
		if (wrap_ret==NULL) goto done;
		ok = Py_nsISupports::InterfaceFromPyObject(wrap_ret, iid, ppret, false, false);
#ifdef DEBUG
		if (ok)
		// Check we _now_ have a default gateway
		{
			nsISupports *temp = NULL;
			NS_ABORT_IF_FALSE(CheckDefaultGateway(ob, iid, &temp), "Auto-wrapped object didn't get a default gateway!");
			if (temp) temp->Release();
		}
#endif
	}
done:
//	Py_XDECREF(func); -- func is static for performance reasons.
	Py_XDECREF(obIID);
	Py_XDECREF(wrap_ret);
	Py_XDECREF(args);
	return ok;
}

// Call back into Python, passing a raw nsIInterface object, getting back
// the object to actually use as the gateway parameter for this interface.
// For example, it is expected that the policy will wrap the interface
// object in one of the xpcom.client.Interface objects, allowing 
// natural usage of the interface from Python clients.
// Note that piid will usually be NULL - this is because the runtime
// reflection interfaces don't provide this information to me.
// In this case, the Python code may choose to lookup the complete
// interface info to obtain the IID.
// It is expected (but should not be assumed) that the method info
// or the IID will be NULL.
// Worst case, the code should provide a wrapper for the nsiSupports interface,
// so at least the user can simply QI the object.
PyObject *
PyG_Base::MakeInterfaceParam(nsISupports *pis, 
			     const nsIID *piid, 
			     int methodIndex /* = -1 */,
			     const XPTParamDescriptor *d /* = NULL */, 
			     int paramIndex /* = -1 */)
{
	if (pis==NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}
	// This condition is true today, but not necessarily so.
	// But if it ever triggers, the poor Python code has no real hope
	// of returning something useful, so we should at least do our
	// best to provide the useful data.
	MOZ_ASSERT(((piid != NULL) ^ (d != NULL)),
	           "No information on the interface available - Python's gunna have a hard time doing much with it!");
	PyObject *obIID = NULL;
	PyObject *obISupports = NULL;
	PyObject *obParamDesc = NULL;
	PyObject *result = NULL;

	// get the basic interface first, as if we fail, we can try and use this.
	// If we don't know the IID, we must explicitly query for nsISupports.
	nsCOMPtr<nsISupports> piswrap;
	nsIID iid_check;
	if (piid) {
		iid_check = *piid;
		piswrap = pis;
	} else {
		iid_check = NS_GET_IID(nsISupports);
		pis->QueryInterface(iid_check, getter_AddRefs(piswrap));
	}

	obISupports = Py_nsISupports::PyObjectFromInterface(piswrap, iid_check, false);
	if (!obISupports)
		goto done;
	if (piid==NULL) {
		obIID = Py_None;
		Py_INCREF(Py_None);
	} else
		obIID = Py_nsIID::PyObjectFromIID(*piid);
	if (obIID==NULL)
		goto done;
	obParamDesc = PyObject_FromXPTParamDescriptor(d);
	if (obParamDesc==NULL)
		goto done;

	result = PyObject_CallMethod(m_pPyObject, 
	                               "_MakeInterfaceParam_",
				       "OOiOi",
				       obISupports,
				       obIID,
				       methodIndex,
				       obParamDesc,
				       paramIndex);
done:
	if (PyErr_Occurred()) {
		NS_ASSERTION(result==NULL, "Have an error, but also a result!");
		PyXPCOM_LogError("Wrapping an interface object for the gateway failed\n");
	}
	Py_XDECREF(obIID);
	Py_XDECREF(obParamDesc);
	if (result==NULL) { // we had an error.
		PyErr_Clear(); // but are not reporting it back to Python itself!
		// return our obISupports.  If NULL, we are really hosed and nothing we can do.
		return obISupports;
	}
	// Don't need to return this - we have a better result.
	Py_XDECREF(obISupports);
	return result;
}

NS_IMETHODIMP
PyG_Base::QueryInterface(REFNSIID iid, void** ppv)
{
	//if (PR_LOG_TEST(nsPyxpcomLog, PR_LOG_DEBUG)) {
	//	char idstr[NSID_LENGTH];
	//	iid.ToProvidedString(idstr);
	//	LOG(PR_LOG_DEBUG, ("PyGatewayBase::QueryInterface: %s", idstr));
	//}

	NS_PRECONDITION(ppv, "NULL pointer");
	if (ppv==nullptr)
		return NS_ERROR_NULL_POINTER;
	*ppv = nullptr;
	// If one of our native interfaces (but NOT nsISupports if we have a base)
	// return this.
	// It is important is that nsISupports come from the base object
	// to ensure that we live by XPCOM identity rules (other interfaces need
	// not abide by this rule - only nsISupports.)
	if ( (m_pBaseObject==NULL || !iid.Equals(NS_GET_IID(nsISupports)))
	   && ((*ppv=ThisAsIID(iid)) != NULL )) {
		(*(nsISupports**)ppv)->AddRef();
		return NS_OK;
	}
	// If we have a "base object", then we need to delegate _every_ remaining 
	// QI to it.
	if (m_pBaseObject != NULL)
		return m_pBaseObject->QueryInterface(iid, ppv);

	// Call the Python policy to see if it (says it) supports the interface
	bool supports = false;
	{ // temp scope for Python lock
		CEnterLeavePython celp;

		PyObject * ob = Py_nsIID::PyObjectFromIID(iid);
		// must say this is an 'internal' call, else we recurse QI into
		// oblivion.
		PyObject * this_interface_ob = Py_nsISupports::PyObjectFromInterface(
		                                       (nsIXPTCProxy *)this,
		                                       iid, false, true);
		if ( !ob || !this_interface_ob) {
			Py_XDECREF(ob);
			Py_XDECREF(this_interface_ob);
			return NS_ERROR_OUT_OF_MEMORY;
		}

		PyObject *result = PyObject_CallMethod(m_pPyObject, "_QueryInterface_",
		                                                    "OO", 
		                                                    this_interface_ob, ob);
		Py_DECREF(ob);
		Py_DECREF(this_interface_ob);

		if ( result ) {
			if (Py_nsISupports::InterfaceFromPyObject(result, iid, (nsISupports **)ppv, true)) {
				// If OK, but NULL, _QI_ returned None, which simply means
				// "no such interface"
				supports = (*ppv!=NULL);
				// result has been QI'd and AddRef'd all ready for return.
			} else {
				// Dump this message and any Python exception before
				// reporting the fact that QI failed - this error
				// may provide clues!
				PyXPCOM_LogError("The _QueryInterface_ method returned an object of type '%s', but an interface was expected\n", result->ob_type->tp_name);
				// supports remains false
			}
			Py_DECREF(result);
		} else {
			NS_ABORT_IF_FALSE(PyErr_Occurred(), "Got NULL result, but no Python error flagged!");
			NS_ASSERTION(!supports, "Have failure with success flag set!");
			PyXPCOM_LogError("The _QueryInterface_ processing failed.\n");
			// supports remains false.
			// We have reported the error, and are returning to COM,
			// so we should clear it.
			PyErr_Clear();
		}
	} // end of temp scope for Python lock - lock released here!
	if ( !supports )
		return NS_ERROR_NO_INTERFACE;
	return NS_OK;
}

NS_IMETHODIMP
PyG_Base::GetWeakReference(nsIWeakReference **ret)
{
	// always delegate back to the "base" gateway for the object, as this tear-off
	// interface may not live as long as the base.  So we recurse back to the base.
	if (m_pBaseObject) {
		NS_PRECONDITION(m_pWeakRef == nullptr, "Not a base object, but do have a weak-ref!");
		return m_pBaseObject->GetWeakReference(ret);
	}
	NS_PRECONDITION(ret, "null pointer");
	if (ret==nullptr) return NS_ERROR_INVALID_POINTER;
	if (!m_pWeakRef) {
		// First query for a weak reference - create it.
		// XXX - this looks like it needs thread safety!?
		m_pWeakRef = new PyXPCOM_GatewayWeakReference(this);
		NS_ABORT_IF_FALSE(m_pWeakRef, "Shouldn't be able to fail creating a weak reference!");
		if (!m_pWeakRef)
			return NS_ERROR_UNEXPECTED;
	}
	*ret = m_pWeakRef;
	(*ret)->AddRef();
	return NS_OK;
}

nsresult PyG_Base::HandleNativeGatewayError(const char *szMethodName)
{
	nsresult rc = NS_OK;
	if (PyErr_Occurred()) {
		// The error handling - fairly involved, but worth it as
		// good error reporting is critical for users to know WTF 
		// is going on - especially with TypeErrors etc in their
		// return values (ie, after the Python code has successfully
		// exited, but we encountered errors unpacking their
		// result values for the COM caller - there is literally no 
		// way to catch these exceptions from Python code, as their
		// is no Python function directly on the call-stack)

		// First line of attack in an error is to call-back on the policy.
		// If the callback of the error handler succeeds and returns an
		// integer (for the nsresult), we take no further action.

		// If this callback fails, we log _2_ exceptions - the error
		// handler error, and the original error.

		bool bProcessMainError = true; // set to false if our exception handler does its thing!
		PyObject *exc_typ, *exc_val, *exc_tb;
		PyErr_Fetch(&exc_typ, &exc_val, &exc_tb);

		PyObject *err_result = PyObject_CallMethod(m_pPyObject, 
	                                       "_GatewayException_",
					       "z(OOO)",
					       szMethodName,
		                               exc_typ ? exc_typ : Py_None, // should never be NULL, but defensive programming...
		                               exc_val ? exc_val : Py_None, // may well be NULL.
					       exc_tb ? exc_tb : Py_None); // may well be NULL.
		if (err_result == NULL) {
			PyXPCOM_LogError("The exception handler _CallMethodException_ failed!\n");
		} else if (err_result == Py_None) {
			// The exception handler has chosen not to do anything with
			// this error, so we still need to print it!
			;
		} else if (PyInt_Check(err_result)) {
			// The exception handler has given us the nresult.
			rc = (nsresult)PyInt_AsLong(err_result);
			bProcessMainError = false;
		} else if (PyLong_Check(err_result)) {
			// The exception handler has given us the nresult.
			rc = static_cast<nsresult>(PyLong_AsUnsignedLong(err_result));
			bProcessMainError = false;
		} else {
			// The exception handler succeeded, but returned other than
			// int or None.
			PyXPCOM_LogError("The _CallMethodException_ handler returned object of type '%s' - None or an integer expected\n", err_result->ob_type->tp_name);
		}
		Py_XDECREF(err_result);
		PyErr_Restore(exc_typ, exc_val, exc_tb);
		if (bProcessMainError) {
			PyXPCOM_LogError("The function '%s' failed\n", szMethodName);
			rc = PyXPCOM_SetCOMErrorFromPyException();
		}
		PyErr_Clear();
	}
	return rc;
}

static nsresult do_dispatch(
	PyObject *pPyObject,
	PyObject **ppResult,
	const char *szMethodName,
	const char *szFormat,
	va_list va
	)
{
	NS_PRECONDITION(ppResult, "Must provide a result buffer");
	*ppResult = nullptr;
	// Build the Invoke arguments...
	PyObject *args = NULL;
	PyObject *method = NULL;
	PyObject *real_ob = NULL;
	nsresult ret = NS_ERROR_FAILURE;
	if ( szFormat )
		args = Py_VaBuildValue((char *)szFormat, va);
	else
		args = PyTuple_New(0);
	if ( !args )
		goto done;

	// make sure a tuple.
	if ( !PyTuple_Check(args) ) {
		PyObject *a = PyTuple_New(1);
		if ( a == NULL )
		{
			Py_DECREF(args);
			goto done;
		}
		PyTuple_SET_ITEM(a, 0, args);
		args = a;
	}
	// Bit to a hack here to maintain the use of a policy.
	// We actually get the policies underlying object
	// to make the call on.
	real_ob = PyObject_GetAttrString(pPyObject, "_obj_");
	if (real_ob == NULL) {
		PyErr_Format(PyExc_AttributeError, "The policy object does not have an '_obj_' attribute.");
		goto done;
	}
	method = PyObject_GetAttrString(real_ob, (char *)szMethodName);
	if ( !method ) {
		PyErr_Clear();
		ret = NS_PYXPCOM_NO_SUCH_METHOD;
		goto done;
	}
	// Make the call
	*ppResult = PyEval_CallObject(method, args);
	ret = *ppResult ? NS_OK : NS_ERROR_FAILURE;
done:
	Py_XDECREF(method);
	Py_XDECREF(real_ob);
	Py_XDECREF(args);
	return ret;
}


nsresult PyG_Base::InvokeNativeViaPolicyInternal(
	const char *szMethodName,
	PyObject **ppResult,
	const char *szFormat,
	va_list va
	)
{
	if ( m_pPyObject == NULL || szMethodName == NULL )
		return NS_ERROR_NULL_POINTER;

	PyObject *temp = nullptr;
	if (ppResult == nullptr)
		ppResult = &temp;
	nsresult nr = do_dispatch(m_pPyObject, ppResult, szMethodName, szFormat, va);

	// If temp is NULL, they provided a buffer, and we don't touch it.
	// If not NULL, *ppResult = temp, and _we_ do own it.
	Py_XDECREF(temp);
	return nr;
}

nsresult PyG_Base::InvokeNativeViaPolicy(
	const char *szMethodName,
	PyObject **ppResult /* = NULL */,
	const char *szFormat /* = NULL */,
	...
	)
{
	va_list va;
	va_start(va, szFormat);
	nsresult nr = InvokeNativeViaPolicyInternal(szMethodName, ppResult, szFormat, va);
	va_end(va);

	if (nr == NS_PYXPCOM_NO_SUCH_METHOD) {
		// Only problem was missing method.
		PyErr_Format(PyExc_AttributeError, "The object does not have a '%s' function.", szMethodName);
	}
	return nr == NS_OK ? NS_OK : HandleNativeGatewayError(szMethodName);
}

// Get at the underlying Python object.
PyObject *PyG_Base::UnwrapPythonObject(void)
{
    Py_INCREF(m_pPyObject);
    return m_pPyObject;
}
/******************************************************

 Some special support to help with object identity.

  In the simplest case, assume a Python XPCOM object is
  supporting a function "nsIWhatever GetWhatever()",
  so implements it as:
    return self
  it is almost certain they intend returning
  the same COM OBJECT to the caller!  Thus, if a user of this COM
  object does:

     p1 = foo.GetWhatever();
     p2 = foo.GetWhatever();

  We almost certainly expect p1==p2==foo.

  We previously _did_ have special support for the "self"
  example above, but this implements a generic scheme that
  works for _all_ objects.

  Whenever we are asked to "AutoWrap" a Python object, the
  first thing we do is see if it has been auto-wrapped before.

  If not, we create a new wrapper, then make a COM weak reference 
  to that wrapper, and store it directly back into the instance
  we are auto-wrapping!  The use of a weak-reference prevents
  cycles.

  The existance of this attribute in an instance indicates if it
  has been previously auto-wrapped.

  If it _has_ previously been auto-wrapped, we de-reference the
  weak reference, and use that gateway.

*********************************************************************/

PyG_Base *GetDefaultGateway(PyObject *policy)
{
	// NOTE: Instance is the policy, not the real instance
	PyObject *instance = PyObject_GetAttrString(policy, "_obj_");
	if (instance == nullptr)
		return nullptr;
	PyObject *ob_existing_weak = PyObject_GetAttrString(instance, PyXPCOM_szDefaultGatewayAttributeName);
	Py_DECREF(instance);
	if (ob_existing_weak != NULL) {
		bool ok = true;
		nsCOMPtr<nsISupports> pSupports;
		ok = Py_nsISupports::InterfaceFromPyObject(ob_existing_weak,
		                                       NS_GET_IID(nsIWeakReference), 
		                                       getter_AddRefs(pSupports),
		                                       false);
		Py_DECREF(ob_existing_weak);
		nsISupports *pip;
		if (ok) {
			nsCOMPtr<nsIWeakReference> pWeakRef = do_QueryInterface(pSupports);
			if (!pWeakRef)
				return nullptr;
			nsresult nr = pWeakRef->QueryReferent( NS_GET_IID(nsIInternalPython), (void **)&pip);
			if (NS_FAILED(nr))
				return nullptr;
			return (PyG_Base *)(nsIInternalPython *)pip;
		}
	} else
		PyErr_Clear();
	return nullptr;
}

bool CheckDefaultGateway(PyObject *real_inst, REFNSIID iid, nsISupports **ret_gateway)
{
	NS_ABORT_IF_FALSE(real_inst, "Did not have an _obj_ attribute");
	if (real_inst==NULL) {
		PyErr_Clear();
		return false;
	}
	PyObject *ob_existing_weak = PyObject_GetAttrString(real_inst, PyXPCOM_szDefaultGatewayAttributeName);
	if (ob_existing_weak != NULL) {
		// We have an existing default, but as it is a weak reference, it
		// may no longer be valid.  Check it.
		bool ok = true;
		nsCOMPtr<nsISupports> pSupports;
		ok = Py_nsISupports::InterfaceFromPyObject(ob_existing_weak,
		                                       NS_GET_IID(nsIWeakReference), 
		                                       getter_AddRefs(pSupports), 
		                                       false);
		Py_DECREF(ob_existing_weak);
		if (ok) {
			nsCOMPtr<nsIWeakReference> pWeakRef = do_QueryInterface(pSupports);
			if (!pWeakRef) {
				ok = false;
			} else {
				Py_BEGIN_ALLOW_THREADS;
				ok = NS_SUCCEEDED(pWeakRef->QueryReferent( iid, (void **)(ret_gateway)));
				Py_END_ALLOW_THREADS;
			}
		}
		if (!ok) {
			// We have the attribute, but not valid - wipe it
			// before restoring it.
			if (0 != PyObject_DelAttrString(real_inst, PyXPCOM_szDefaultGatewayAttributeName))
				PyErr_Clear();
		}
		return ok;
	}
	PyErr_Clear();
	return false;
}

void AddDefaultGateway(PyObject *instance, nsISupports *gateway)
{
	// NOTE: Instance is the _policy_!
	PyObject *real_inst = PyObject_GetAttrString(instance, "_obj_");
	NS_ABORT_IF_FALSE(real_inst, "Could not get the '_obj_' element");
	if (!real_inst) return;
	// the default gateway is a weak reference; we need to make sure it's still alive if it exists
	bool hasValidDefaultGateway = false;
	PyObject *ob_old_weak = PyObject_GetAttrString(real_inst, PyXPCOM_szDefaultGatewayAttributeName);
	if (ob_old_weak) {
		nsCOMPtr<nsISupports> pSupports;
		bool success = Py_nsISupports::InterfaceFromPyObject(ob_old_weak,
								       NS_GET_IID(nsIWeakReference),
								       getter_AddRefs(pSupports),
								       false,
								       false);
		Py_DECREF(ob_old_weak);
		if (success) {
			nsCOMPtr<nsIWeakReference> pWeakRef = do_QueryInterface(pSupports);
			if (pWeakRef) {
				nsCOMPtr<nsISupports> pSupports2 = do_QueryReferent(pWeakRef);
				if (pSupports2) {
					// the old weak reference is fine
					NS_ASSERTION(SameCOMIdentity(pSupports2, gateway),
						"A Python object has a duplicate gateway!");
					hasValidDefaultGateway = true;
				}
			}
		}
	} else
		PyErr_Clear();
	if (!hasValidDefaultGateway) {
		// getting here means either 1) there was no existing default gateway, or
		// 2) the existing gateway is dead
		nsCOMPtr<nsISupportsWeakReference> swr( do_QueryInterface((nsISupportsWeakReference *)(gateway)) );
		NS_ABORT_IF_FALSE(swr, "Our gateway failed with a weak reference query");
		// Create the new default gateway - get a weak reference for our gateway.
		if (swr) {
			nsCOMPtr<nsIWeakReference> pWeakReference;
			swr->GetWeakReference( getter_AddRefs(pWeakReference) );
			if (pWeakReference) {
				PyObject *ob_new_weak = Py_nsISupports::PyObjectFromInterface(pWeakReference, 
										   NS_GET_IID(nsIWeakReference),
										   false ); /* bMakeNicePyObject */
				// pWeakReference reference consumed.
				if (ob_new_weak) {
					PyObject_SetAttrString(real_inst, PyXPCOM_szDefaultGatewayAttributeName, ob_new_weak);
					Py_DECREF(ob_new_weak);
				}
			}
		}
	}
	Py_DECREF(real_inst);
}
