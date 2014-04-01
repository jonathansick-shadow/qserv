# -*- python -*-
#
# LSST Data Management System
# Copyright 2013 LSST Corporation.
#
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the LSST License Statement and
# the GNU General Public License along with this program.  If not,
# see <http://www.lsstcorp.org/LegalNotices/>.
#
# runner.py : construct a runner script that wraps up LD_LIBRARY_PATH and PYTHONPATH
import os, stat

scriptTemplate = """#!/bin/bash
# Autogenerated by site_scons/runner.py during a build
# Wraps up the build environment so you can run arbitrary commands with the
# right libs and python path used for building.
# Usage: %(sname)s mycmd arg1 arg2 arg3

LD_LIBRARY_PATH=%(ldpath)s
PYTHONPATH=%(pythonpath)s
export LD_LIBRARY_PATH
export PYTHONPATH
$*
"""

def makeRunner(target=None, source=None, env=None):
    libpath = filter(None, env["LIBPATH"])
    ppEnv = env.get("python_prefix","") + ":" + env.get("PYTHONPATH","") 
    fname = str(target[0])
    s = scriptTemplate % {
        "ldpath" : ":".join(map(str, libpath)), #commaReplace(env["LIBPATH"]),
        "pythonpath" : ppEnv,
        "sname" : os.path.basename(fname)
        }
    open(fname, "w").write(s)
    # Set permissions: ug+rwx, o+rx
    os.chmod(fname,
             stat.S_IRUSR |  stat.S_IWUSR |  stat.S_IXUSR
             |  stat.S_IRGRP |  stat.S_IWGRP | stat.S_IXGRP
             |  stat.S_IROTH | stat.S_IXOTH)
    return None # Successful
