About
-----
This version of PyXPCOM is designed to work with Firefox/XULRunner v4 or v5.

PyXPCOM allows for communication between Python and XPCOM, such that a Python
application can access XPCOM objects, and XPCOM can access any Python class
that implements an XPCOM interface. With PyXPCOM, a developer can talk to
XPCOM or embed Gecko from a Python application.

Requirements
------------
* requires Python 2.x (most tested with Python 2.6)
* requires Mozilla XULRunner SDK
* autoconf 2.13

Build Steps
-----------

$ autoconf2.13
$ mkdir obj
$ cd obj
$ ../configure --with-libxul-sdk=/path/to/xulrunner-sdk
$ make

Installation
------------
When successfully built, there will be a "obj/dist/bin" directory that contains
the necessary files.

* libpyxpcom.so  - the core PyXPCOM library
* components/pyxpcom.manifest  - to tell Firefox/XULRunner to load PyXPCOM
* components/libpyloader.dll  - loader library for setting up PyXPCOM
* python  - the pure Python files, this directory must be on the PYTHONPATH

You'll need to ensure that the pyxpcom.manifest is registered/loaded by
XULRunner/Firefox by adding this file to the manifest list.

