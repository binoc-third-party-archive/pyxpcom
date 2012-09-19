"""
Generate nsError.py via clang
"""

from pprint import pprint
import re, subprocess, sys

if len(sys.argv) < 3:
    print "Usage: %s nsError.h nsError.py" % (sys.argv[0])
    sys.exit(1)

for compiler in "clang", "gcc":
    try:
        subprocess.check_call("which %s >/dev/null 2>/dev/null" % (compiler,),
                              shell=True)
    except subprocess.CalledProcessError:
        pass
    else:
        break # found a good compiler

undefs = ("nscore_h___", "nsError_h__", "__STDC_HOSTED__", "__STDC_VERSION__", "__STDC__")
cmd = [compiler, "-undef", "-dM", "-E", "-Dnscore_h___", sys.argv[1]]
output = subprocess.check_output(cmd)

functions = []
simples = {} # simple things, e.g. constant numbers
expressions = {} # involving operators
calls = {} # involving function calls
modules = {}

out = open(sys.argv[2], "w")

for line in output.splitlines():
    prefix, name, value = line.split(" ", 2)
    if prefix != "#define":
        continue # huh?
    if name in undefs:
        continue # ignore these
    if not "(" in name:
        if name.startswith("NS_ERROR_MODULE_"):
            modules[name] = value
        elif "(" in value:
            calls[name] = value
        elif set("+-*/").intersection(value) or not set("0123456789").intersection(value[0]):
            expressions[name] = value
        else:
            simples[name] = value
    else:
        functions.append((name, value.replace("!", " not ")))

env = {}

# print out simple values first
for literals in simples, modules, expressions:
    for name, value in sorted(literals.items()):
        stmt = "%s = %s\n" % (name, value)
        try:
            exec stmt in env
        except Exception, ex:
            pass # we might depend on things not there yet
        else:
            # nsresult is _signed_
            if isinstance(env[name], int) and env[name] > 0x80000000:
                stmt = "%s = (%s) - 0x100000000\n" % (name, value)
                exec stmt in env
            out.write(stmt)
            del literals[name]

# print out some unforunately hard-coded functions
hardcoded = ["def NS_LIKELY(x): return x\n", "def NS_UNLIKELY(x): return x\n"]
for stmt in hardcoded:
    exec stmt in env
    out.write(stmt)

# next, print out functions
cast_re = re.compile("\(([^( )]+)\)")
for name, value in functions:
    args = name.split("(", 2)[-1].strip(")").split(",")
    for match in reversed(list(cast_re.finditer(value))):
        if match.group(1) in args:
            continue
        value = value[:match.start()] + value[match.end():]
    stmt = "def %s: return %s\n" % (name, value)
    try:
        exec stmt in env
    except Exception, ex:
        sys.stderr.write("Failed: %s: %s\n" % (stmt, ex))
    else:
        out.write(stmt)

for literals in calls, modules, simples, expressions:
    for name, value in sorted(literals.items()):
        stmt = "%s = %s\n" % (name, value)
        try:
            exec stmt in env
        except Exception, ex:
            sys.stderr.write("Failed: %s: %s\n" % (stmt, ex))
        else:
            # nsresult is _signed_
            if isinstance(env[name], int) and env[name] > 0x80000000:
                stmt = "%s = (%s) - 0x100000000\n" % (name, value)
                exec stmt in env
            out.write(stmt)
            del literals[name]
