#!/usr/bin/env python

import re
import os
import sys
import shutil
import subprocess
from functools import partial

sys_re = re.compile("^/System")
usr_re = re.compile("^/usr/lib/")
exe_re = re.compile("@executable_path")

def is_user_lib(objfile, libname):
    return not sys_re.match(libname) and \
           not usr_re.match(libname) and \
           not exe_re.match(libname) and \
           not "libobjc" in libname and \
           not "libSystem" in libname and \
           not "libgcc" in libname and \
           not os.path.basename(objfile) in libname

def otool(objfile):
    if '.dylib' in objfile:
        objfile = find(os.path.basename(objfile), ['/opt/staging/lib', '/opt/osxcross/target/macports/pkgs/opt/local'])
    command = "x86_64-apple-darwin16-otool -L %s | grep -e '\t' | awk '{ print $1 }'" % objfile
    output  = subprocess.check_output(command, shell = True)
    return filter(partial(is_user_lib, objfile), output.split())

def install_name_tool_change(old, new, objfile):
    subprocess.call(["install_name_tool", "-change", old, new, objfile])

def install_name_tool_id(name, objfile):
    subprocess.call(["install_name_tool", "-id", name, objfile])

def libraries(objfile, result = dict()):
    libs_list       = otool(objfile)
    result[objfile] = set(libs_list)

    for lib in libs_list:
        if lib not in result:
            libraries(lib, result)

    return result

def leafs(libs_dict, processed = []):
    result    = []
    processed = set(processed)

    for objfile, libs in libs_dict.items():
        if libs <= processed:
            result.append(objfile)

    return result

def lib_path(binary):
    return os.path.join(os.path.dirname(binary), 'lib')

def lib_name(lib):
    return os.path.join("@executable_path", "lib", os.path.basename(lib))

def find(name, paths):
    paths = ' '.join(paths)
    return subprocess.check_output("find %s -iname '%s'" % (paths, name), shell=True).splitlines()[0]

def process_libraries(libs_dict, binary, processed = []):
    ls   = leafs(libs_dict, processed)
    diff = set(ls) - set(processed)
    if diff == set():
        return
    print diff
    for src in diff:
        name = lib_name(src)
        dst  = os.path.join(lib_path(binary), os.path.basename(src))
        print 'Searching for %s...' % src
        lib_src = find(os.path.basename(src), ['/opt/staging/lib', '/opt/osxcross/target/macports/pkgs/opt/local', '.'])
        if os.path.basename(src) != os.path.basename(binary):
            shutil.copy(lib_src, dst)
            os.chmod(dst, 0o755)
            install_name_tool_id(name, lib_src)
        else:
            dst = lib_src

        if src in libs_dict[binary]:
            install_name_tool_change(src, name, binary)

        for p in processed:
            if p in libs_dict[src]:
                install_name_tool_change(p, lib_name(p), dst)

    process_libraries(libs_dict, binary, ls)

def main():
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(lib_path(binary)):
        os.makedirs(lib_path(binary))
    libs = libraries(binary)
    print(libs)
    process_libraries(libs, binary)

if __name__ == "__main__":
    main()
