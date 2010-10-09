/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Initial Developer of the Original Code is Todd Whiteman.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Todd Whiteman <twhitema@gmail.com> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef nsPythonModuleLoader_h__
#define nsPythonModuleLoader_h__

#include <PyXPCOM.h>

#include "nsISupports.h"
#include "nsILocalFile.h"
#include "mozilla/ModuleLoader.h"
#include "mozilla/Module.h"

class nsPythonModuleLoader : public mozilla::ModuleLoader
{
 public:
    NS_DECL_ISUPPORTS

    nsPythonModuleLoader();
    ~nsPythonModuleLoader();

    NS_OVERRIDE virtual const mozilla::Module* LoadModule(nsILocalFile* aFile);
    NS_OVERRIDE virtual const mozilla::Module* LoadModuleFromJAR(nsILocalFile* aJARFile,
                                                                 const nsACString& aPath);

    nsresult Init();
    void UnloadLibraries();

 protected:
    class PythonModule : public mozilla::Module
    {
    public:
        PythonModule(PyObject *aPyModule, PyObject *aPyLocation) : mozilla::Module() {
            mVersion = mozilla::Module::kVersion;
            mCIDs = NULL;
            mContractIDs = NULL;
            mCategoryEntries = NULL;
            getFactoryProc = GetFactory;
            //getFactoryProc = NULL;
            loadProc = NULL;
            unloadProc = NULL;

            mPyObjModule = aPyModule;
            Py_XINCREF(mPyObjModule);

            mPyObjLocation = aPyLocation;
            Py_XINCREF(mPyObjModule);
        }

        ~PythonModule() {
            if (mPyObjModule) {
                Py_XDECREF(mPyObjModule);
                mPyObjModule = NULL;
            }
        }

        PyObject *mPyObjModule;
        PyObject *mPyObjLocation;
        static already_AddRefed<nsIFactory> GetFactory(const mozilla::Module& module,
                                                       const mozilla::Module::CIDEntry& entry);
    };

 friend class PythonModule;

 private:
    PyObject *mPyLoader;
    PyObject *mPyLoadModuleName;
};

#endif /* nsPythonModuleLoader_h__ */