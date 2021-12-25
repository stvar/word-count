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

.PHONY: default clean allclean all

default: all

PROGRAM := word-count

SRCS := word-count.c

BIN := ${PROGRAM}

# GCC parameters

GCC := gcc
GCC_STD := gnu11

CFLAGS := -Wall -Wextra -std=${GCC_STD} \
          -DPROGRAM=${PROGRAM}

# build-time configuration parameters

param-arg = $(shell \
    bash -c 'tr -s " \t" "\n" <<< "$3"|sed -nr "/^($2)$$/!{s/^/invalid $1 parameter: \x22/;s/$$/\x22/;p;q}"' 2>&1)

param-dup = $(shell \
    bash -c 'tr -s " \t" "\n" <<< "$2"|sed -r "s/=.+$$//"|sort|uniq -d|sed -nr "1{s/^/duplicated $1 parameter: \x22/;s/$$/\x22/;p;q}"' 2>&1)

CFGS := USE_48BIT_PTR|USE_OVERFLOW_BUILTINS|USE_IO_BUF_LINEAR_GROWTH

ifdef CONFIG
CONFIG_CHECK = $(call param-arg,config,${CFGS},${CONFIG})
ifneq (${CONFIG_CHECK},)
$(error ${CONFIG_CHECK})
endif
endif

ifdef CONFIG
CONFIG_CHECK = $(call param-dup,config,${CONFIG})
ifneq (${CONFIG_CHECK},)
$(error ${CONFIG_CHECK})
endif
endif

ifdef CONFIG
CFLAGS += $(addprefix -DCONFIG_, ${CONFIG})
endif

# other build-time parameters

ifdef OPT
ifneq ($(words ${OPT}),1)
$(error invalid OPT='${OPT}')
endif
ifneq ($(filter-out 0 1 2 3,${OPT}),)
$(error invalid OPT='${OPT}')
endif
CFLAGS += -O$(strip ${OPT})
else
CFLAGS += -DDEBUG -g
endif

ifdef SANITIZE
ifneq ($(words ${SANITIZE}),1)
$(error invalid SANITIZE='${SANITIZE}')
endif
ifneq ($(filter-out undefined address,${SANITIZE}),)
$(error invalid SANITIZE='${SANITIZE}')
endif
CFLAGS += -fsanitize=$(strip ${SANITIZE})
LDFLAGS += -fsanitize=$(strip ${SANITIZE})
endif

# building rules

${BIN}: ${SRCS}
	${GCC} ${CFLAGS} ${SRCS} -o $@

# main targets

all: ${BIN}

clean:
	rm -f *~

allclean: clean
	rm -f ${BIN}


