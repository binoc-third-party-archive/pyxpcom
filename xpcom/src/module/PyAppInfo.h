/**
 * PyAppInfo.h
 * nsIXULRuntime / nsIAppInfo implementation for Python-initiated XPCOM
 * enviroments
 * Note that this is only used if Python is initializing XPCOM; see
 * EnsureXPCOM in _xpcom.cpp.  In an application that embeds Python, this is
 * not used.  (This makes it useful for PyXPCOM unit tests.)
 */

#include "nsIXULAppInfo.h"
#include "nsIXULRuntime.h"
#include "nsIFactory.h"

#include "nsXREAppData.h"

class PyAppInfo: public nsIXULAppInfo,
                 public nsIXULRuntime,
                 public nsIFactory
{
public:
    static PyAppInfo* GetSingleton(nsIFile* aAppDir);

    NS_DECL_ISUPPORTS
    NS_DECL_NSIXULAPPINFO
    NS_DECL_NSIXULRUNTIME
    NS_DECL_NSIFACTORY

private:
    PyAppInfo(nsIFile* aAppDir);
    ~PyAppInfo();

protected:
    nsXREAppData mAppData;
    bool mLogConsoleErrors;
};