#/usr/bin/bash

# Copyright (C) 2021  Stefan Vargyas
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

program="$0"

usage="\
usage: $program [ACTION|OPTION]... [ARG]...
where the actions are:
  -A|--all-build-run     build 'word-count' and run all tests on it
                           for each valid combination of the script's
                           command line options \`-g|--valgrind' and
                           \`-m|--use-mmap-io={-,+,dict|text}', along
                           with the following 'Makefile' parameters:
                             * no 'CONFIG' or 'CONFIG+=USE_48BIT_PTR';
                             * no 'SANITIZE', 'SANITIZE=address',
                               or 'SANITIZE=undefined';
                             * no 'OPT' or 'OPT=3'
  -B|--build-run         build 'word-count' and run all tests on it
  -C|--commands          don't run the generated test commands, but
                           only print them out
  -R|--run               run all generated test commands on existing
                           'word-count' binary (default)
and the options are:
  -c|--no-color          do not colorize output
  -e|--echo              echo the command line invoking the script
  -g|--valgrind          execute each 'word-count' instance under
                           'valgrind'
  -m|--use-mmap-io=SPEC  execute each 'word-count' instance with an
                           environment variable \$WORD_COUNT_USE_MMAP_IO
                           set to SPEC; it can be either 'dict', 'text',
                           'none' or 'all'; '-' is a shortcut for 'none'
                           and '+' for 'all'
  -v|--verbose           be verbose
  -?|--help              display this help info and exit"

quote()
{
    local __n__
    local __v__

    [ -z "$1" -o \
      "$1" == "__n__" -o \
      "$1" == "__v__" ] &&
    return 1

    printf -v __n__ '%q' "$1"
    eval __v__="\"\$$__n__\""
    #!!! echo "!!! 0 __v__='$__v__'"
    test -z "$__v__" && return 0
    printf -v __v__ '%q' "$__v__"
    #!!! echo "!!! 1 __v__='$__v__'"
    printf -v __v__ '%q' "$__v__"  # double quote
    #!!! echo "!!! 2 __v__='$__v__'"
    test -z "$SHELL_BASH_QUOTE_TILDE" &&
    __v__="${__v__//\~/\\~}"
    eval "$__n__=$__v__"
}

quote2()
{
    local __n__
    local __v__

    local __q__='q'
    [ "$1" == '-i' ] && {
        __q__=''
        shift
    }

    [ -z "$1" -o \
      "$1" == "__n__" -o \
      "$1" == "__v__" -o \
      "$1" == "__q__" ] &&
    return 1

    printf -v __n__ '%q' "$1"
    eval __v__="\"\$$__n__\""
    __v__="$(sed -nr '
        H
        $!b
        g
        s/^\n//
        s/\x27/\0\\\0\0/g'${__q__:+'
        s/^/\x27/
        s/$/\x27/'}'
        p
    ' <<< "$__v__")"
    test -n "$__v__" &&
    printf -v __v__ '%q' "$__v__"
    eval "$__n__=$__v__"
}

set -o pipefail
shopt -s extglob

action='R'
echo=''
no_color=''
valgrind=''
use_mmap_io=''
verbose=''
args=''

error()
{ echo >&2 "${program##*/}: error: $@"; }

parse-options()
{
    local o
    local a
    while [ "$#" -gt 0 ]; do
        o="$1"
        case "$o" in
            -[ABCR])
                action="${o:1}"
                ;;
            --@(all-build-run|build-run|commands|run))
                action="$(tr '[:lower:]' '[:upper:]' \
                    <<< "${o:2:1}")"
                ;;
            -c|--no-color)
                no_color='e'
                ;;
            -e|--echo)
                echo='e'
                ;;
            -g|--valgrind)
                valgrind='v'
                ;;
            -m*|--use-mmap-io*)
                if [ "${o:0:2}" == '-m' ]; then
                    if [ "${#o}" -gt 2 ]; then
                        a="${o:2}"
                    else
                        a="$2"
                        shift
                    fi
                else
                    if [ "${#o}" -eq 13 ]; then
                        error "argument for option '$o' not given"
                        return 1
                    elif [ "${o:13:1}" != '=' ]; then
                        error "unknown option '$o'"
                        return 1
                    else
                        a="${o:14}"
                    fi
                fi
                [[ "$a" != @([-+]|dict|text|all|none) ]] && {
                    error "invalid argument '$a' for option '$o'"
                    return 1
                }
                use_mmap_io="$a"
                ;;
            -v|--verbose)
                verbose='v'
                ;;
            -\?|--help)
                action='?'
                ;;
            -*)	error "unknown option '$1'"
                return 1
                ;;
            *)	quote o
                args+="${args:+ }${o:-''}"
                ;;
        esac
        shift
    done
    return 0
}

parse-options "$@" ||
exit 1

[ "$action" == '?' ] && {
    echo "$usage"
    exit 0
}

open="\x1b\x5b\x30\x31"
close="\x1b\x5b\x30\x6d"
yellow="$open\x3b\x33\x33\x6d"
color="$yellow"

[ -n "$no_color" ] && {
    close=''
    color=''
}

[ -n "$echo" ] && {
    echo="$ $program $@"
    echo -ne "$color"
    echo -n  "${echo//?( )-e/}"
    echo -e  "$close"
}

build-run-tests()
{
    local gcc="${GCC:-gcc}"
    quote gcc

    local o
    local G=0
    local a=''
    while [ "$#" -gt 0 ]; do
        o="$1"
        if [ "$action" == 'A' ]; then
            # stev: skip over 'SANITIZE=' and 'OPT=' args
            [[ "$o" == @(SANITIZE|OPT)=* ]] &&
            continue
        else
            # stev: count the number or 'SANITIZE=' args
            [[ "${o:0:9}" == 'SANITIZE=' ]] &&
            ((G ++))
        fi
        quote o
        a+=" ${o:-''}"
        shift
    done

    local c
    local p
    local s
    local g
    local m

    if [ "$action" == 'A' ]; then
        c="\
$program -B${verbose:+ -v}${no_color:+ -c} -e$a"
        local r=0
        for p in '' USE_48BIT_PTR; do
            for s in '' address undefined; do
                for o in '' 3; do
                    a=''
                    [ -n "$p" ] && a+=" CONFIG+=$p"
                    [ -n "$s" ] && a+=" SANITIZE=$s"
                    [ -n "$o" ] && a+=" OPT=$o"
                    eval "$c$a" ||
                    ((r ++))
                done
            done
        done
        return $((r != 0))
    elif [ "$action" == 'B' ]; then
        c="\
make"
        [ -z "$verbose" ] && c+=" \
-s"
        c+=" \
GCC=$gcc -B$a && {"
        for g in '' g; do
            # stev: this script's options
            # `-g|--valgrind' cannot be
            # combined with the Makefile
            # parameter `SANITIZE='
            [ -n "$g" -a "$G" -ne 0 ] &&
            continue

            for m in - + ' dict' ' text'; do
                c+=" \
$program -R${verbose:+ -v}${no_color:+ -c} -e${g:+ -g} -m$m;"
            done
        done
        c+="\
 }"
        eval "$c"
    else
        error \
            "build-run-tests:" \
            "internal: unexpected action='$action'"
        return 1
    fi
}

[ -n "$valgrind" ] &&
trap 'rm -f valgrind.log' EXIT

if [ -n "$valgrind" ]; then
word-count()
{
    local l='valgrind.log'

    valgrind --log-file="$l" ./word-count "$@" ||
    { error "valgrind exited with error code: $?"; return 1; }

    grep -qP '^==\d+==\s+in use at exit: 0 bytes' "$l" ||
    { error "valgrind found memory leaks"; return 1; }

    return 0
}
else
word-count()
{ ./word-count "$@"; }
fi

run-test()
{
    local u=0
    [[ "$1" == -+([0-9]) ]] && {
        u="${1:1}"
        shift
    }

    local n="$1" # name
    local e="$2" # expected
    local o="$3" # obtained

    local n2="$n"
    quote2 -i n2

    local c="\
diff -u$u \\
-Lexpected <($e) \\
-Lobtained <(($o) 2>&1 ||
echo 'test-case failed: $n2')"

    [ "$action" == 'C' ] && {
        echo -ne "$color"
        echo -n  "# test $n"
        echo -e  "$close"
        echo "$ $c"
        return 0
    }

    # stev: [ "$action" == 'R' ]

    [ -n "$verbose" ] &&
    echo -n "test $n: "

    local s
    s="$(eval "$c")"
    local r=$?

    if [ "$r" -ne 0 ]; then
        [ -z "$verbose" ] &&
        echo -n "test $n: "
        echo failed
        [ -n "$verbose" -a -n "$s" ] &&
        printf '%s\n' "$s"
    elif [ -n "$verbose" ]; then
        echo OK
    fi
}

run-tests()
{
    local tmpd='/tmp/word-count-dict.XXX'
    local tmpt='/tmp/word-count-text.XXX'

    local dict_temp_file=''
    local text_temp_file=''

    trap "\
test -n \"\$dict_temp_file\" &&
rm -f   \"\$dict_temp_file\"; \
test -n \"\$text_temp_file\" &&
rm -f   \"\$text_temp_file\"" \
    RETURN

    [ -n "$use_mmap_io" ] &&
    export WORD_COUNT_USE_MMAP_IO="$use_mmap_io"

    [[ "$WORD_COUNT_USE_MMAP_IO" == @(+|dict|all) ]] && {
        if [ "$action" == 'C' ]; then
            dict_temp_file="$tmpd"
        else
            dict_temp_file="$(mktemp "$tmpd")" &&
            [ -n "$dict_temp_file" ] || {
                error "run-tests: failed creating dict temp file"
                exit 1
            }
        fi
    }
    [[ "$WORD_COUNT_USE_MMAP_IO" == @(+|text|all) ]] && {
        if [ "$action" == 'C' ]; then
            text_temp_file="$tmpt"
        else
            text_temp_file="$(mktemp "$tmpt")" &&
            [ -n "$text_temp_file" ] || {
                error "run-tests: failed creating text temp file"
                exit 1
            }
        fi
    }

    local k
    local n
    local d
    local i
    local o

    local T="${program%.sh}.def"
    source "$T"

    for ((k=0;k<${#tests[@]};k+=4)); do
        n="${tests[k+0]}" # +0 name
        d="${tests[k+1]}" # +1 dict
        i="${tests[k+2]}" # +2 input
        o="${tests[k+3]}" # +3 ouput

        quote2 -i d
        quote2 -i i
        quote2 -i o

        # stev: do not quote any occurrence of
        # $dict_temp_file and $text_temp_file

        local c=''
        [ -n "$text_temp_file" ] && c+=${c:+$'\n'}"\
echo -ne '$i' > $text_temp_file &&"
        [ -n "$dict_temp_file" ] && c+=${c:+$'\n'}"\
echo -ne '$d' > $dict_temp_file &&"
        [ -z "$text_temp_file" ] && c+=${c:+$'\n'}"\
echo -ne '$i'|"
        c+="
word-count"
        [ -z "$dict_temp_file" ] && c+=" \
<(echo -ne '$d')"
        [ -n "$dict_temp_file" ] && c+=" \
$dict_temp_file"
        [ -n "$text_temp_file" ] && c+=" \
$text_temp_file"
        c+="|
sort -k 1n,1 -k 2,2"

        run-test -100 "$n" "echo -e '$o'" "$c"
    done

    sed "$T" -rn -e '
        /^(test-[a-z0-9-]+)\(\)\s*$/!b
        s//\1/
        h
        n
        /^\s*\{\s*$/!b
        g
        p'|
    while read n; do
        eval "$n"
    done
}

if [[ "$action" == [AB] ]]; then
    # stev: do not quote $args below
    build-run-tests $args
elif [[ "$action" == [CR] ]]; then
    run-tests
fi


