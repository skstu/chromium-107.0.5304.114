#!/bin/bash
# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This alias allows invocations of `python3` to work as expected under msys
# bash. In particular, it detects if stdout+stdin are both attached to a
# pseudo-tty, and if so, invokes python3 in interactive mode. If this is not
# the case, or the user passes any arguments, python3 will be invoked
# unmodified.
python3() {
  if [[ $# > 0 ]]; then
    python3.bat "$@"
  else
    readlink /proc/$$/fd/0 | grep pty > /dev/null
    TTY0=$?
    readlink /proc/$$/fd/1 | grep pty > /dev/null
    TTY1=$?
    if [ $TTY0 == 0 ] && [ $TTY1 == 0 ]; then
      PYTHON_DIRECT=1 PYTHONUNBUFFERED=1 python3.bat -i
    else
      python3.bat
    fi
  fi
}
