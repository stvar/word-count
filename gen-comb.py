#!/usr/bin/python

# Copyright (C) 2021, 2022  Stefan Vargyas
# 
# This file is part of Word-Count.
# 
# Word-Count is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# Word-Count is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with Word-Count.  If not, see <http://www.gnu.org/licenses/>.

import sys
from itertools import product, repeat

if sys.version_info[0] <= 2:
    from itertools import  \
        ifilter as filter, \
        imap as map

def gen_comb0(n):
    return sorted(
        filter(
            lambda t: t.count(1) > 0,
            product(*repeat([0, 1], n)),
        ),
        key = lambda t: (
            t.count(1),
            [k for k in range(n) if t[k]]
        )
    )

def gen_comb(file):
    L = tuple(
        map(
            str.rstrip,
            file
        )
    )
    n = len(L)
    for t in gen_comb0(n):
        c = " ".join(
            map(
                lambda k: L[k],
                filter(
                    lambda k: t[k],
                    range(n)
                )
            )
        )
        sys.stdout.write(c)
        sys.stdout.write("\n")

def main():
    gen_comb(sys.stdin)

if __name__ == '__main__':
    main()

# $ printf '%s\n' foo bar baz|python gen-comb.py
# foo
# bar
# baz
# foo bar
# foo baz
# bar baz
# foo bar baz
# $


