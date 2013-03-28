import distutils.sysconfig
import logging
import os
from sets import Set
import SCons.Action 
from SCons.Script import Delete

import utils

def install_python_module(env, target, source):
    """ Define targets which will install all python file contained in 
        source_dir_path and sub-directories in python_path_prefix.
    """  
    log = logging.getLogger()

    python_path_prefix=target
    source_dir_path=source 
    env['pythonpath'] =distutils.sysconfig.get_python_lib(prefix=python_path_prefix)
    target_lst = []
    clean_target_lst = []

    source_lst = utils.recursive_glob(source_dir_path,'*.py',env)

    for f in source_lst :
        target = utils.replace_base_path(source_dir_path,env['pythonpath'],f,env)
        env.InstallAs(target, f)
        target_lst.append(target)
        # .pyc files will also be removed
        env.Clean(target, "%s%s" % (target ,"c"))
       
        #print "AddPostAction to target %s" % target

    clean_python_path_dir(target_lst,env)
        
    return target_lst

def clean_python_path_dir(target_lst,env):
    """ Delete empty directories in PYTHONPATH
    """
    empty_py_dirs = Set()

    bottom_up_target_list = map(str,target_lst)[::-1]

    print "TARGET LST %s" %  bottom_up_target_list

    for target in  bottom_up_target_list:
        print "TARGET %s" % target
        py_dir = os.path.dirname(str(target))   
        print "PYDIR %s" % py_dir
        if os.path.exists(py_dir):
            other_python_modules = []
            for e in os.listdir(py_dir):
                node = os.path.join(py_dir,e)
                if node not in bottom_up_target_list and node not in empty_py_dirs:
                    print "OTHER PY MOD %s" % node
                    other_python_modules.append(node)
            if other_python_modules == []:
                empty_py_dirs.add(py_dir)
                
    for d in empty_py_dirs:
        env.Clean(target_lst[0], d)
        #env.Execute(Delete(d))
        
    return ""


def generate(env):
    env.AddMethod(install_python_module,'InstallPythonModule')

def exists(env):
    return env.Detect('python')
