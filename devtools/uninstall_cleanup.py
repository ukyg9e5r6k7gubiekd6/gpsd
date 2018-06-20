#!/usr/bin/env python

# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import glob
import os
import subprocess
import sys

GPS_LIB_NAME = 'gps'

BINARY_ENCODING = 'latin-1'

if bytes is str:

    polystr = str

else:  # Otherwise we do something real

    def polystr(o):
        "Convert bytes or str to str with proper encoding."
        if isinstance(o, str):
            return o
        if isinstance(o, bytes):
            return str(o, encoding=BINARY_ENCODING)
        raise ValueError


def DoCommand(cmd_list):
    "Perform external command, returning exit code and stdout."
    pipe = subprocess.PIPE
    proc = subprocess.Popen(cmd_list, stdin=pipe, stdout=pipe)
    result, _ = proc.communicate()
    return proc.returncode, polystr(result)


class PythonCommand(object):
    "Object for one system Python command."
    PYTHON_GLOB = 'python*'
    TEXT_PREFIX = b'#!'
    PATH_ENV = 'PATH'
    PATH_ENV_SEP = ':'
    PYTHON_EXE_COMMANDS = [
        'import sys',
        'print(sys.executable)',
        ]

    instances = []

    def __init__(self, command):
        "Set up PythonCommand."
        self.command = command

    @classmethod
    def FindPythons(cls, dir):
        "Create PythonCommand objects by scanning directory."
        pattern = dir + os.path.sep + cls.PYTHON_GLOB
        pythons = glob.glob(pattern)
        for python in pythons:
            with open(python, 'rb') as f:
                if f.read(2) == cls.TEXT_PREFIX:
                    continue
            cls.instances.append(cls(python))
        return cls.instances

    @classmethod
    def FindAllPythons(cls):
        "Create PythonCommand objects by scanning command PATH."
        paths = os.getenv(cls.PATH_ENV)
        for dir in paths.split(cls.PATH_ENV_SEP):
            cls.FindPythons(dir)
        return cls.instances

    def GetExecutable(self):
        "Obtain executable path from this Python."
        command = [self.command, '-c', ';'.join(self.PYTHON_EXE_COMMANDS)]
        status, result = DoCommand(command)
        if status:
            return None
        return result.strip()


class PythonExecutable(object):
    "Object for one Python executable, deduped."
    PYTHON_LIBDIR_COMMANDS = [
        'from distutils import sysconfig',
        'print(sysconfig.get_python_lib())',
        ]

    _by_path = {}

    def __new__(cls, command):
        "Create or update one PythonExecutable from PythonCommand."
        path = command.GetExecutable()
        existing = cls._by_path.get(path)
        if existing:
            existing.commands.append(command)
            return existing
        self = super(PythonExecutable, cls).__new__(cls)
        self.commands = [command]
        self.path = path
        self.libdir = None
        cls._by_path[path] = self
        return self

    def __lt__(self, other):
        "Allow sorting."
        return self.path < other.path

    @classmethod
    def GetAllExecutables(cls, command_list):
        "Build list of executables from list of commands."
        for command in command_list:
            cls(command)
        return sorted(cls._by_path.values())

    def GetLibdir(self):
        "Obtain libdir path from this Python."
        if self.libdir:
            return self.libdir
        command = [self.path, '-c', ';'.join(self.PYTHON_LIBDIR_COMMANDS)]
        status, result = DoCommand(command)
        if status:
            return None
        return result.strip()

    def CleanLib(self, name):
        "Clean up given package from this Python."
        dir = os.path.join(self.GetLibdir(), name)
        if not name or not os.path.exists(dir):
            return
        try:
            os.rmdir(dir)
        except OSError:
            print('Unable to remove %s' % dir)
        else:
            print('Removed empty %s' % dir)

    @classmethod
    def CleanAllLibs(cls, name):
        "Clean up given package from all executables."
        for exe in cls._by_path.values():
            exe.CleanLib(name)


def main(argv):
    "Main function."
    commands = PythonCommand.FindAllPythons()
    PythonExecutable.GetAllExecutables(commands)
    PythonExecutable.CleanAllLibs(GPS_LIB_NAME)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
