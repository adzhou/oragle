#!/bin/sh
# tail -f - would fail with the initial inotify implementation

# Copyright (C) 2009-2014 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

. "${srcdir=.}/tests/init.sh"; path_prepend_ ./src
print_ver_ tail

echo line > exp || framework_failure_
echo line > in || framework_failure_

timeout 1 tail -f < in > out 2> err

# tail from coreutils-7.5 would fail
test $? = 124 || fail=1

# Ensure there was no error output.
test -s err && fail=1

# Ensure there was
compare exp out || fail=1

Exit $fail
