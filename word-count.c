// Copyright (C) 2021  Stefan Vargyas
// 
// This file is part of Word-Count.
// 
// Word-Count is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Word-Count is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Word-Count.  If not, see <http://www.gnu.org/licenses/>.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>

#ifndef __GNUC__
#error we need a GCC compiler
#elif __GNUC__ < 4
#error we need a GCC compiler of version >= 4
#endif

#define UNUSED    __attribute__((unused))
#define PRINTF(F) __attribute__((format(printf, F, F + 1)))
#define NORETURN  __attribute__((noreturn))

#define UNLIKELY(x) __builtin_expect((x), 0)
#define LIKELY(x)   __builtin_expect((x), 1)

#define STRINGIFY_(s) #s
#define STRINGIFY(s)  STRINGIFY_(s)

const char stdin_name[] = "<stdin>";

const char program[] = STRINGIFY(PROGRAM);
const char verdate[] = "0.3 -- 2021-12-20 13:04"; // $ date +'%F %R'

const char help[] =
"usage: %s [OPTION...] DICT [TEXT...]\n"
"where the options are:\n"
"  -m|--use-mmap-io=SPEC  use memory-mapped I/O instead of buffered I/O\n"
"                           as specified: either one of 'dict', 'text',\n"
"                           'none' or 'all'; the default is 'none'; '-'\n"
"                           is a shortcut for 'none' and '+' for 'all';\n"
"                           attached env var: $WORD_COUNT_USE_MMAP_IO\n"
"     --version           print version numbers and exit\n"
"  -?|--help              display this help info and exit\n";

// stev: important requirement: VERIFY evaluates E only once!

#define VERIFY(E)             \
    do {                      \
        if (UNLIKELY(!(E)))   \
            UNEXPECT_ERR(#E); \
    }                         \
    while (0)

// stev: important requirement: ENSURE evaluates E only once!

#define ENSURE(E, M, ...)                     \
    do {                                      \
        if (UNLIKELY(!(E)))                   \
            ensure_failed(__FILE__, __LINE__, \
                __func__, M, ## __VA_ARGS__); \
    }                                         \
    while (0)

#define UNEXPECT_ERR(M, ...)               \
    do {                                   \
        unexpect_error(__FILE__, __LINE__, \
            __func__, M, ## __VA_ARGS__);  \
    }                                      \
    while (0)

#define UNEXPECT_VAR(F, N) UNEXPECT_ERR(#N "=" F, N)

#ifdef DEBUG
#define ASSERT(E)                             \
    do {                                      \
        if (UNLIKELY(!(E)))                   \
            assert_failed(__FILE__, __LINE__, \
                __func__, #E);                \
    }                                         \
    while (0)
#else
#define ASSERT(E) \
    do { (void) (E); } while (0)
#endif

#if __STDC_VERSION__ >= 201112L
#define STATIC(E)                                   \
    ({                                              \
        _Static_assert((E), #E);                    \
    })
#else
#define STATIC(E)                                   \
    ({                                              \
        extern int __attribute__                    \
            ((error("assertion failed: '" #E "'"))) \
            static_assert();                        \
        (void) ((E) ? 0 : static_assert());         \
    })
#endif

#define TYPEOF(p) \
    typeof(p)
#define TYPES_COMPATIBLE(t, u) \
    (__builtin_types_compatible_p(t, u))
#define TYPEOF_IS(p, t) \
    TYPES_COMPATIBLE(TYPEOF(p), t)

#define TYPEOF_IS_SIZET(x) \
    TYPEOF_IS(x, size_t)

#define IS_CONSTANT(x) \
    (__builtin_constant_p(x))

#define CONST_CAST(p, t)                \
    (                                   \
        STATIC(TYPEOF_IS(p, t const*)), \
        (t*) (p)                        \
    )

#define TYPE_IS_INTEGER(t) ((t) 1.5 == 1)

#define TYPE_IS_UNSIGNED(t) ((t) 0 < (t) -1)

#define TYPE_IS_SIGNED(t) (!TYPE_IS_UNSIGNED(t))

#define TYPE_IS_UNSIGNED_INT(t) \
    (TYPE_IS_INTEGER(t) && TYPE_IS_UNSIGNED(t))

#define TYPE_IS_SIGNED_INT(t) \
    (TYPE_IS_INTEGER(t) && TYPE_IS_SIGNED(t))

#define TYPE_UNSIGNED_INT_MAX_(t) ((t) -1)

#define TYPE_UNSIGNED_INT_MAX(t)         \
    (                                    \
        STATIC(TYPE_IS_UNSIGNED_INT(t)), \
        TYPE_UNSIGNED_INT_MAX_(t)        \
    )

#define TYPE_SIGNED_INT_MAX_(t) \
    (((((t) 1) << (sizeof(t) * CHAR_BIT - 2)) - 1) * 2 + 1)

#define TYPE_SIGNED_INT_MAX(t)         \
    (                                  \
        STATIC(TYPE_IS_SIGNED_INT(t)), \
        TYPE_SIGNED_INT_MAX_(t)        \
    )

#define UINT_AS_INT_(x, t)                      \
    ({                                          \
        STATIC(                                 \
            TYPE_IS_SIGNED_INT(t));             \
        STATIC(                                 \
            TYPE_IS_UNSIGNED_INT(TYPEOF(x)));   \
        STATIC(                                 \
            TYPE_SIGNED_INT_MAX_(t) <           \
            TYPE_UNSIGNED_INT_MAX_(TYPEOF(x))); \
        ASSERT((x) <= TYPE_SIGNED_INT_MAX_(t)); \
        (t) (x);                                \
    })
#define UINT_AS_INT(x) \
        UINT_AS_INT_(x, int)
#define UINT_AS_OFFT(x) \
        UINT_AS_INT_(x, off_t)

#define UINT_TO_SIZE(x)                          \
    (                                            \
        STATIC(                                  \
            TYPE_IS_UNSIGNED_INT(TYPEOF(x)),     \
        STATIC(                                  \
            TYPE_UNSIGNED_INT_MAX_(TYPEOF(x)) <= \
            SIZE_MAX),                           \
        (size_t) (x)                             \
    )

#define INT_AS_SIZE_(m, x)                     \
    ({                                         \
        STATIC(                                \
            TYPE_IS_SIGNED_INT(TYPEOF(x)));    \
        STATIC(                                \
            TYPE_SIGNED_INT_MAX_(TYPEOF(x)) <= \
            SIZE_MAX);                         \
        m((x) >= 0);                           \
        (size_t) (x);                          \
    })
#define VERIFY_INT_AS_SIZE(x) INT_AS_SIZE_(VERIFY, x)
#define INT_AS_SIZE(x)        INT_AS_SIZE_(ASSERT, x)

#define SIZE_AS_INT(x)               \
    ({                               \
        STATIC(TYPEOF_IS_SIZET(x));  \
        STATIC(INT_MAX <= SIZE_MAX); \
        ASSERT((x) <= INT_MAX);      \
        (int) (x);                   \
    })

#ifdef CONFIG_USE_OVERFLOW_BUILTINS
#define UINT_DEC_NO_OVERFLOW__(t, x) \
    (!__builtin_sub_overflow_p((x), 1, (t) 0))
#define UINT_INC_NO_OVERFLOW__(t, x) \
    (!__builtin_add_overflow_p((x), 1, (t) 0))
#define UINT_SUB_NO_OVERFLOW__(t, x, y) \
    (!__builtin_sub_overflow_p((x), (y), (t) 0))
#define UINT_ADD_NO_OVERFLOW__(t, x, y) \
    (!__builtin_add_overflow_p((x), (y), (t) 0))
#define UINT_MUL_NO_OVERFLOW__(t, x, y) \
    (!__builtin_mul_overflow_p((x), (y), (t) 0))
#else // CONFIG_USE_OVERFLOW_BUILTINS
#define UINT_DEC_NO_OVERFLOW__(t, x) \
    ((x) > 0)
#define UINT_INC_NO_OVERFLOW__(t, x) \
    ((x) < TYPE_UNSIGNED_INT_MAX_(t))
#define UINT_SUB_NO_OVERFLOW__(t, x, y) \
    ((x) >= (y))
#define UINT_ADD_NO_OVERFLOW__(t, x, y) \
    ((x) <= TYPE_UNSIGNED_INT_MAX_(t) - (y))
#define UINT_MUL_NO_OVERFLOW__(t, x, y) \
    ((y) == 0 || (x) <= TYPE_UNSIGNED_INT_MAX_(t) / (y))
#endif // CONFIG_USE_OVERFLOW_BUILTINS

#define UINT_DEC_NO_OVERFLOW_(x) \
        UINT_DEC_NO_OVERFLOW__(TYPEOF(x), x)
#define UINT_INC_NO_OVERFLOW_(x) \
        UINT_INC_NO_OVERFLOW__(TYPEOF(x), x)
#define UINT_SUB_NO_OVERFLOW_(x, y) \
        UINT_SUB_NO_OVERFLOW__(TYPEOF((x) - (y)), x, y)
#define UINT_ADD_NO_OVERFLOW_(x, y) \
        UINT_ADD_NO_OVERFLOW__(TYPEOF((x) + (y)), x, y)
#define UINT_MUL_NO_OVERFLOW_(x, y) \
        UINT_MUL_NO_OVERFLOW__(TYPEOF((x) * (y)), x, y)

#define TYPEOF_IS_UINT(x) \
    TYPE_IS_UNSIGNED_INT(TYPEOF(x))

#define UINT_DEC_NO_OVERFLOW(x)     \
    (                               \
        STATIC(TYPEOF_IS_UINT(x)),  \
        UINT_DEC_NO_OVERFLOW_(x)    \
    )

#define UINT_INC_NO_OVERFLOW(x)     \
    (                               \
        STATIC(TYPEOF_IS_UINT(x)),  \
        UINT_INC_NO_OVERFLOW_(x)    \
    )

#define UINT_SUB_NO_OVERFLOW(x, y)  \
    (                               \
        STATIC(TYPEOF_IS_UINT(x)),  \
        STATIC(TYPEOF_IS_UINT(y)),  \
        UINT_SUB_NO_OVERFLOW_(x, y) \
    )

#define UINT_ADD_NO_OVERFLOW(x, y)  \
    (                               \
        STATIC(TYPEOF_IS_UINT(x)),  \
        STATIC(TYPEOF_IS_UINT(y)),  \
        UINT_ADD_NO_OVERFLOW_(x, y) \
    )

#define UINT_MUL_NO_OVERFLOW(x, y)  \
    (                               \
        STATIC(TYPEOF_IS_UINT(x)),  \
        STATIC(TYPEOF_IS_UINT(y)),  \
        UINT_MUL_NO_OVERFLOW_(x, y) \
    )

#define UINT_NO_OVERFLOW(m, n, ...) \
    m(UINT_ ## n ## _NO_OVERFLOW(__VA_ARGS__))

#define ASSERT_UINT_INC_NO_OVERFLOW(x)    UINT_NO_OVERFLOW(ASSERT, INC, x)
#define ASSERT_UINT_DEC_NO_OVERFLOW(x)    UINT_NO_OVERFLOW(ASSERT, DEC, x)

#define ASSERT_UINT_SUB_NO_OVERFLOW(x, y) UINT_NO_OVERFLOW(ASSERT, SUB, x, y)
#define ASSERT_UINT_ADD_NO_OVERFLOW(x, y) UINT_NO_OVERFLOW(ASSERT, ADD, x, y)
#define ASSERT_UINT_MUL_NO_OVERFLOW(x, y) UINT_NO_OVERFLOW(ASSERT, MUL, x, y)

#define VERIFY_UINT_INC_NO_OVERFLOW(x)    UINT_NO_OVERFLOW(VERIFY, INC, x)
#define VERIFY_UINT_DEC_NO_OVERFLOW(x)    UINT_NO_OVERFLOW(VERIFY, DEC, x)

#define VERIFY_UINT_SUB_NO_OVERFLOW(x, y) UINT_NO_OVERFLOW(VERIFY, SUB, x, y)
#define VERIFY_UINT_ADD_NO_OVERFLOW(x, y) UINT_NO_OVERFLOW(VERIFY, ADD, x, y)
#define VERIFY_UINT_MUL_NO_OVERFLOW(x, y) UINT_NO_OVERFLOW(VERIFY, MUL, x, y)

#define UINT_UN_OP(m, n, op, x)    \
    ({                             \
        UINT_NO_OVERFLOW(m, n, x); \
        (x) op;                    \
    })

#define UINT_INC(x) UINT_UN_OP(ASSERT, INC, +1, x)
#define UINT_DEC(x) UINT_UN_OP(ASSERT, DEC, -1, x)

#define UINT_BIN_OP(m, n, op, x, y)   \
    ({                                \
        UINT_NO_OVERFLOW(m, n, x, y); \
        (x) op (y);                   \
    })

#define UINT_SUB(x, y) UINT_BIN_OP(ASSERT, SUB, -, x, y)
#define UINT_ADD(x, y) UINT_BIN_OP(ASSERT, ADD, +, x, y)
#define UINT_MUL(x, y) UINT_BIN_OP(ASSERT, MUL, *, x, y)

#define UINT_SUB_EQ(x, y) UINT_BIN_OP(ASSERT, SUB, -=, x, y)
#define UINT_ADD_EQ(x, y) UINT_BIN_OP(ASSERT, ADD, +=, x, y)
#define UINT_MUL_EQ(x, y) UINT_BIN_OP(ASSERT, MUL, *=, x, y)

#define UINT_PRE_OP(m, n, op, x)   \
    ({                             \
        UINT_NO_OVERFLOW(m, n, x); \
        op (x);                    \
    })
#define UINT_POST_OP(m, n, op, x)  \
    ({                             \
        UINT_NO_OVERFLOW(m, n, x); \
        (x) op;                    \
    })

#define UINT_PRE_INC(x) UINT_PRE_OP(ASSERT, INC, ++, x)
#define UINT_PRE_DEC(x) UINT_PRE_OP(ASSERT, DEC, --, x)

#define UINT_POST_INC(x) UINT_POST_OP(ASSERT, INC, ++, x)
#define UINT_POST_DEC(x) UINT_POST_OP(ASSERT, DEC, --, x)

#define SIZE_BIT (sizeof(size_t) * CHAR_BIT)

#if SIZE_MAX == UINT_MAX
#define SZ(x) x ## U
#elif SIZE_MAX == ULONG_MAX
#define SZ(x) x ## UL
#else
#error size_t is neither unsigned int nor unsigned long
#endif

// stev: cast 'T**' to 'T const* const*'
#define PTR_PTR_CAST(p, t)         \
    (                              \
        STATIC(TYPEOF_IS(p, t**)), \
        (t const* const*) (p)      \
    )

#define PTR_DIFF(p, b)                  \
    ({                                  \
        ptrdiff_t __d = (p) - (b);      \
        ASSERT(__d >= 0);               \
        STATIC(PTRDIFF_MAX < SIZE_MAX); \
        (size_t) __d;                   \
    })

#define KB(x) (1024 * (x))
#define MB(x) (1024 * KB(x))

typedef unsigned bits_t;

void warning(const char* fmt, ...)
    PRINTF(1);

void error(const char* fmt, ...)
    PRINTF(1)
    NORETURN;

void ensure_failed(
    const char* file, int line, const char* func,
    const char* msg, ...)
    PRINTF(4)
    NORETURN;

void assert_failed(
    const char* file, int line, const char* func,
    const char* expr)
    NORETURN;

void unexpect_error(
    const char* file, int line, const char* func,
    const char* msg, ...)
    PRINTF(4)
    NORETURN;

#define ARRAY_SIZE(a)  \
    (                  \
        sizeof(a) /    \
        sizeof((a)[0]) \
    )
#define ARRAY_INDEX(a, i)   \
    (                       \
        (i) < ARRAY_SIZE(a) \
    )
#define ARRAY_ELEM_REF(a, i)   \
    (                          \
        ARRAY_INDEX(a, i)      \
            ? (a) + (i) : NULL \
    )
#define ARRAY_ELEM(a, i, d)         \
    (                               \
        ARRAY_INDEX(a, i) && (a)[i] \
            ? (a)[i] : (d)          \
    )

typedef char fmt_buf_t[512];

enum error_type_t {
    error_type_warn,
    error_type_err
};

void verror(
    enum error_type_t type,
    const char* fmt, va_list args)
{
#define CASE(e, n) [error_type_ ## e] = #n
    static const char* types[] = {
        CASE(warn, warning),
        CASE(err, error)
    };
    fmt_buf_t b;

    vsnprintf(b, sizeof b - 1, fmt, args);
    b[sizeof b - 1] = 0;

    fprintf(
        stderr, "%s: %s: %s\n", program,
        ARRAY_ELEM(types, type, "???"),
        b);
}

void warning(
    const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    verror(error_type_warn, fmt, args);
    va_end(args);
}

void error(
    const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    verror(error_type_err, fmt, args);
    va_end(args);

    exit(127);
}

void assert_failed(
    const char* file, int line,
    const char* func, const char* expr)
{
    error("assertion failed: %s:%d:%s: %s",
        file, line, func, expr);
}

void ensure_failed(
    const char* file, int line,
    const char* func, const char* msg, ...)
{
    va_list arg;
    fmt_buf_t b;

    va_start(arg, msg);
    vsnprintf(b, sizeof b - 1, msg, arg);
    va_end(arg);

    b[sizeof b - 1] = 0;

    error("%s:%d:%s: %s",
        file, line, func, b);
}

void unexpect_error(
    const char* file, int line,
    const char* func, const char* msg, ...)
{
    va_list arg;
    fmt_buf_t b;

    va_start(arg, msg);
    vsnprintf(b, sizeof b - 1, msg, arg);
    va_end(arg);

    b[sizeof b - 1] = 0;

    error("unexpected error: %s:%d:%s: %s",
        file, line, func, b);
}

void syslib_error_fmt(
    const char* ctxt,
    const char* name,
    const char* msg, ...)
    PRINTF(3)
    NORETURN;

void syslib_error_fmt(
    const char* ctxt,
    const char* name,
    const char* msg, ...)
{
    va_list arg;
    fmt_buf_t b;

    va_start(arg, msg);
    vsnprintf(b, sizeof b - 1, msg, arg);
    va_end(arg);

    b[sizeof b - 1] = 0;

    error("%s: %s: %s", ctxt, name, b);
}

void syslib_error_sys(
    const char* ctxt,
    const char* name,
    int sys_err)
{
    syslib_error_fmt(
        ctxt, name, "%s [errno=%d]",
        strerror(sys_err), sys_err);
}

enum io_error_type_t {
    io_error_type_open,
    io_error_type_close,
    io_error_type_read,
    io_error_type_stat,
    io_error_type_mmap,
};

void io_error_fmt(
    enum io_error_type_t type,
    const char* ctxt,
    const char* file,
    const char* msg, ...)
    PRINTF(4)
    NORETURN;

void io_error_fmt(
    enum io_error_type_t type,
    const char* ctxt,
    const char* file,
    const char* msg, ...)
{
#undef  CASE
#define CASE(e) [io_error_type_ ## e] = #e
    static const char* types[] = {
        CASE(open),
        CASE(close),
        CASE(read),
        CASE(stat),
        CASE(mmap),
    };

    va_list arg;
    fmt_buf_t b;

    va_start(arg, msg);
    vsnprintf(b, sizeof b - 1, msg, arg);
    va_end(arg);

    b[sizeof b - 1] = 0;

    error("failed %sing %s file '%s': %s",
        ARRAY_ELEM(types, type, "???"), ctxt,
        file != NULL ? file : stdin_name,
        b);
}

void io_error_sys(
    enum io_error_type_t type,
    const char* ctxt,
    const char* file,
    int sys_err)
{
    io_error_fmt(
        type, ctxt, file, "%s [errno=%d]",
        strerror(sys_err), sys_err);
}

#define IO_ERROR_FMT(e, c, f, m, ...)          \
    io_error_fmt(io_error_type_ ## e, c, f, m, \
        ## __VA_ARGS__)
#define IO_ERROR_SYS(e, c, f) \
    io_error_sys(io_error_type_ ## e, c, f, errno)

struct mem_buf_node_t
{
    struct mem_buf_node_t* prev;
    size_t size;
    size_t len;
    char ptr[];
};

struct mem_buf_node_t* mem_buf_node_create(
    struct mem_buf_node_t* prev,
    size_t size)
{
    size_t n = sizeof(struct mem_buf_node_t);
    struct mem_buf_node_t* p;

    ASSERT(size > 0);
    ASSERT_UINT_ADD_NO_OVERFLOW(n, size);

    p = malloc(n + size);
    VERIFY(p != NULL);

    p->prev = prev;
    p->size = size;
    p->len  = 0;

    return p;
}

void mem_buf_node_destroy(
    struct mem_buf_node_t* node)
{
    free(node);
}

size_t mem_buf_node_get_free_space(
    struct mem_buf_node_t* node)
{
    ASSERT(node->size > 0);
    ASSERT_UINT_SUB_NO_OVERFLOW(
        node->size, node->len);
    return node->size - node->len;
}

struct mem_buf_t
{
    struct mem_buf_node_t* last;
    size_t min_size;
};

void mem_buf_init(
    struct mem_buf_t* buf,
    size_t min_size)
{
    ASSERT(min_size > 0);

    memset(buf, 0, sizeof *buf);
    buf->min_size = min_size;
}

void mem_buf_done(struct mem_buf_t* buf)
{
    struct mem_buf_node_t *p, *q;

    p = buf->last;
    while (p != NULL) {
        q = p->prev;
        mem_buf_node_destroy(p);
        p = q;
    }
}

const char* mem_buf_node_append(
    struct mem_buf_node_t* node,
    const char* str, size_t sz)
{
    ASSERT(sz > 0);

    size_t n = mem_buf_node_get_free_space(node);
    VERIFY(n >= sz);

    // n => sz <=> size - len >= sz
    //         <=> len + sz <= size
    char* p = node->ptr + node->len;
    memcpy(p, str, sz);
    node->len += sz;
    return p;
}

const char* mem_buf_append(
    struct mem_buf_t* buf,
    const char* str, size_t size)
{
    if (size == 0)
        size = 1;

    size_t n = buf->last != NULL
        ? mem_buf_node_get_free_space(buf->last)
        : 0;
    if (n < size) {
        if (n < buf->min_size)
            n = buf->min_size;
        buf->last = mem_buf_node_create(buf->last, n);
    }

    ASSERT(buf->last != NULL);
    return mem_buf_node_append(buf->last, str, size);
}

enum mem_map_type_t {
    mem_map_type_normal,
    mem_map_type_random,
    mem_map_type_sequential
};

struct mem_map_node_t
{
    struct mem_map_node_t* prev;
    void* ptr;
    off_t size;
};

#define MEM_MAP_ERROR(e) \
    syslib_error_sys("mem-map", #e, errno)

void mem_map_node_set_type(
    struct mem_map_node_t* node,
    enum mem_map_type_t type)
{
#undef  CASE
#define CASE(t, n) \
    [mem_map_type_ ## t] = MADV_ ## n
    static const int types[] = {
        CASE(sequential, SEQUENTIAL),
        CASE(normal, NORMAL),
        CASE(random, RANDOM)
    };

    const int* a =
        ARRAY_ELEM_REF(types, type);
    VERIFY(a != NULL);

    if (madvise(node->ptr, node->size, *a) < 0)
        MEM_MAP_ERROR(madvise);
}

void mem_map_node_init(
    struct mem_map_node_t* node,
    const char* ptr,
    size_t size)
{
    ASSERT(node != NULL);

    node->prev = NULL;
    node->ptr  = CONST_CAST(ptr, char);
    node->size = UINT_AS_OFFT(size);
}

struct mem_map_node_t* mem_map_node_create(
    const char* ptr, size_t size)
{
    struct mem_map_node_t* p;

    p = malloc(sizeof *p);
    VERIFY(p != NULL);

    mem_map_node_init(p, ptr, size);
    return p;
}

void mem_map_node_done(
    struct mem_map_node_t* node)
{
    if (munmap(node->ptr, node->size) < 0)
        MEM_MAP_ERROR(munmap);
}

void mem_map_node_destroy(
    struct mem_map_node_t* node)
{
    mem_map_node_done(node);
    free(node);
}

struct mem_map_t
{
    struct mem_map_node_t* last;
};

void mem_map_init(struct mem_map_t* map)
{
    memset(map, 0, sizeof *map);
}

void mem_map_done(struct mem_map_t* map)
{
    struct mem_map_node_t *p, *q;

    p = map->last;
    while (p != NULL) {
        q = p->prev;
        mem_map_node_destroy(p);
        p = q;
    }
}

struct mem_map_node_t* mem_map_append(
    struct mem_map_t* map,
    const char* ptr,
    size_t size)
{
    struct mem_map_node_t* p;

    p = mem_map_node_create(ptr, size);
    ASSERT(p != NULL);

    p->prev = map->last;
    map->last = p;

    return p;
}

struct mem_range_t
{
    const char* ptr;
    size_t size;
};

enum mem_mgr_type_t {
    mem_mgr_type_buf,
    mem_mgr_type_map
};

struct mem_mgr_t
{
    union {
        struct mem_buf_t buf;
        struct mem_map_t map;
    };
    enum mem_mgr_type_t type;

    void         *impl;
    void        (*done)(void*);
    const char* (*append)(void*,
        struct mem_range_t);
};

const char* mem_mgr_buf_append(
    struct mem_buf_t* buf,
    struct mem_range_t range)
{
    return mem_buf_append(
        buf, range.ptr, range.size);
}

const char* mem_mgr_map_append(
    struct mem_map_t* map,
    struct mem_range_t range)
{
    mem_map_append(
        map, range.ptr, range.size);
    return range.ptr;
}

#define MEM_MGR_INIT(n, ...)          \
    do {                              \
        mem->type =                   \
            mem_mgr_type_ ## n;       \
        mem_ ## n ## _init(           \
            mem->impl = &mem->n,      \
            ## __VA_ARGS__);          \
        mem->done =                   \
            (void (*)(void*))         \
            mem_ ## n ## _done;       \
        mem->append =                 \
            (const char* (*)(void*,   \
                struct mem_range_t))  \
            mem_mgr_ ## n ## _append; \
    } while (0)

void mem_mgr_init(
    struct mem_mgr_t* mem,
    bool mapped)
{
    if (mapped)
        MEM_MGR_INIT(map);
    else
        MEM_MGR_INIT(buf, MB(4));
}

void mem_mgr_done(
    struct mem_mgr_t* mem)
{
    mem->done(mem->impl);
}

const char* mem_mgr_append(
    struct mem_mgr_t* mem,
    struct mem_range_t range)
{
    return mem->append(mem->impl, range);
}

#define MEM_MGR_AS_(t)            \
    ({                            \
        VERIFY(                   \
            mem->type ==          \
            mem_mgr_type_ ## t);  \
        (struct mem_ ## t ## _t*) \
            mem->impl;            \
    })

struct mem_buf_t*
    mem_mgr_as_buf(struct mem_mgr_t* mem)
{ return MEM_MGR_AS_(buf); }

struct mem_map_t*
    mem_mgr_as_map(struct mem_mgr_t* mem)
{ return MEM_MGR_AS_(map); }

// http://www.isthe.com/chongo/tech/comp/fnv/index.html
// FNV Hash

#define LHASH_HASH_KEY(k, ...)     \
    ({                             \
        uint32_t __c;              \
        uint32_t __h = 2166136261; \
        while ((__c = *key ++, ##  \
            __VA_ARGS__)) {        \
            __h *= 16777619;       \
            __h ^= __c;            \
        }                          \
        __h;                       \
    })

uint32_t lhash_hash_key2(const char* key, size_t len)
{ return LHASH_HASH_KEY(key, len --); }

uint32_t lhash_hash_key(const char* key)
{ return LHASH_HASH_KEY(key); }

struct lhash_node_t
{
#ifdef CONFIG_USE_48BIT_PTR
    uintptr_t   key_len;
#else
    const char* key;
    unsigned    len;
#endif
    unsigned    val;
};

struct lhash_t
{
    struct lhash_node_t* table;
    size_t max_load;
    size_t size;
    size_t used;
};

#define LHASH_MUL_FRAC_(v, n, d)    \
    (                               \
        STATIC(TYPEOF_IS_SIZET(v)), \
        STATIC(TYPEOF_IS_SIZET(n)), \
        STATIC(TYPEOF_IS_SIZET(d)), \
        STATIC(IS_CONSTANT(n)),     \
        STATIC(IS_CONSTANT(d)),     \
        STATIC(n > 0),              \
        STATIC(d > 0),              \
        UINT_MUL_NO_OVERFLOW(v, n)  \
        ? v = (v * n) / d,          \
          true                      \
        : false                     \
    )
#define LHASH_MUL_FRAC(v, ...) \
        LHASH_MUL_FRAC_(v, __VA_ARGS__)

#define LHASH_FRAC(n, d) SZ(n), SZ(d)

// stev: Knuth, TAOCP, vol 3, 2nd edition,
// 6.4 Hashing, p. 528
#define LHASH_REHASH_LOAD \
        LHASH_FRAC(3, 4) // 0.75

// stev: double the size of the table
// each time decided to enlarge it; a
// variant would be to double its size
// every second time enlarging it --
// which amounts to the factor below
// be sqrt(2) ~= 1.4142 (~= 71 / 50)
#define LHASH_REHASH_SIZE \
        LHASH_FRAC(2, 1) // 2.0

#define LHASH_MAX_LOAD()               \
    ({                                 \
        size_t __r = hash->size;       \
        VERIFY(LHASH_MUL_FRAC(         \
            __r, LHASH_REHASH_LOAD));  \
        VERIFY(__r < hash->size);      \
        /* => __r <= hash->size - 1 */ \
        VERIFY(__r > 0);               \
        __r;                           \
    })

bool lhash_is_prime(size_t n)
{
    size_t d = 3, s = 9, i;

    while (s < n && (n % d)) {
        // (a + 2) ^ 2 = a^2 + 4*(a + 1)
        // invariant: s == d^2
        ASSERT_UINT_INC_NO_OVERFLOW(d);
        d ++;
        ASSERT_UINT_MUL_NO_OVERFLOW(d, SZ(4));
        i = d * 4;
        ASSERT_UINT_ADD_NO_OVERFLOW(s, i);
        s += i;
        ASSERT_UINT_INC_NO_OVERFLOW(d);
        d ++;
    }
    return s >= n;
}

size_t lhash_next_prime(size_t n)
{
    const size_t N = 4294967291;

    STATIC(SIZE_BIT >= 32);
    ASSERT(n <= N);

    n |= 1;
    while (n < N && !lhash_is_prime(n))
        n += 2;

    return n;
}

void lhash_init(
    struct lhash_t* hash,
    size_t init_size)
{
    if (init_size == 0)
        init_size = 512;

    memset(hash, 0, sizeof *hash);

    hash->size = lhash_next_prime(init_size);
    hash->max_load = LHASH_MAX_LOAD();

    hash->table = calloc(
        hash->size, sizeof *hash->table);
    VERIFY(hash->table != NULL);
}

void lhash_done(struct lhash_t* hash)
{
    free(hash->table);
}

#define LHASH_ASSERT_INVARIANTS(hash)        \
    do {                                     \
        ASSERT((hash)->size > 0);            \
        ASSERT((hash)->used < (hash)->size); \
    } while (0)

#ifndef CONFIG_USE_48BIT_PTR
#define LHASH_NODE_KEY(n) \
    (                     \
        (n)->key          \
    )
#define LHASH_NODE_LEN(n) \
    (                     \
        (n)->len          \
    )
#define LHASH_NODE_INIT(n, k, l) \
    do {                         \
        (n)->key = (k);          \
        (n)->len = (l);          \
        (n)->val = 0;            \
    } while (0)
#else // CONFIG_USE_48BIT_PTR

// https://www.intel.com/content/dam/www/public/us/en/documents/manuals/
// 64-ia-32-architectures-software-developer-vol-1-manual.pdf
//
// Intel® 64 and IA-32 Architectures
// Software Developer’s Manual
// Volume 1: Basic Architecture
// 3.3.7.1 Canonical Addressing
//
// In 64-bit mode, an address is considered to be
// in canonical form if address bits 63 through
// to the most-significant implemented bit by the
// microarchitecture are set to either all ones
// or all zeros.
// Intel 64 architecture defines a 64-bit linear
// address. Implementations can support less. The
// first implementation of IA-32 processors with
// Intel 64 architecture supports a 48-bit linear
// address. This means a canonical address must
// have bits 63 through 48 set to zeros or ones
// (depending on whether bit 47 is a zero or one).

#define BIT(n) (1UL << (n))
#define SET(n) (BIT(n) - 1)

#define LHASH_NODE_KEY(n)                  \
    (                                      \
        STATIC(ULONG_MAX == UINT64_MAX),   \
        STATIC(UINTPTR_MAX == UINT64_MAX), \
        (const char*) (                    \
            /* take as such bits 0..46 */  \
             ((n)->key_len & SET(47)) |    \
            /* sign-extend bit 47 */       \
            -((n)->key_len & BIT(47))      \
        )                                  \
    )
#define LHASH_NODE_LEN(n)                  \
    (                                      \
        STATIC(ULONG_MAX == UINT64_MAX),   \
        STATIC(UINTPTR_MAX == UINT64_MAX), \
        STATIC(UINT16_MAX <= UINT_MAX),    \
        (unsigned) ((n)->key_len >> 48)    \
    )
#define LHASH_NODE_INIT(n, k, l)           \
    do {                                   \
        STATIC(ULONG_MAX == UINT64_MAX);   \
        STATIC(UINTPTR_MAX == UINT64_MAX); \
        uintptr_t __k = (uintptr_t) (k);   \
        ASSERT(__k < BIT(48));             \
        ASSERT((l) < BIT(16));             \
        (n)->key_len = ((l) << 48) | __k;  \
        (n)->val = 0;                      \
    } while (0)

#endif // CONFIG_USE_48BIT_PTR

void lhash_rehash(struct lhash_t* hash)
{
    struct lhash_node_t *t, *p, *e, *q;
    size_t s;

    LHASH_ASSERT_INVARIANTS(hash);

    s = hash->size;
    VERIFY(LHASH_MUL_FRAC(
        s, LHASH_REHASH_SIZE));
    VERIFY(s > hash->size);

    s = lhash_next_prime(s);
    // => hash->size < s

    t = calloc(s, sizeof *hash->table);
    VERIFY(t != NULL);

    for (p = hash->table,
         e = p + hash->size;
         p < e;
         p ++) {
        const char* k = LHASH_NODE_KEY(p);
        if (k == NULL)
            continue;

        unsigned l = LHASH_NODE_LEN(p);
        q = t + lhash_hash_key2(k, l) % s;

        while (LHASH_NODE_KEY(q) != NULL) {
            if (q == t)
                q += s - 1;
            else
                q --;
        }

        *q = *p;
    }

    free(hash->table);

    hash->table = t;
    hash->size = s;
    hash->max_load = LHASH_MAX_LOAD();
    // the new size > the old size =>
    // the invariants are preserved
}

#define LHASH_NODE_KEY_EQ(p, k, l) \
    ({                             \
        const char* __k =          \
            LHASH_NODE_KEY(p);     \
        unsigned __l =             \
            LHASH_NODE_LEN(p);     \
        __l == l &&                \
        !memcmp(__k, k, l);        \
    })

bool lhash_insert(
    struct lhash_t* hash,
    const char* key, size_t len,
    struct lhash_node_t** result)
{
    struct lhash_node_t* p;
    uint32_t h;

    ASSERT(key != NULL);
    LHASH_ASSERT_INVARIANTS(hash);

    h = lhash_hash_key2(key, len);
    p = hash->table + h % hash->size;

    while (LHASH_NODE_KEY(p) != NULL) {
        if (LHASH_NODE_KEY_EQ(p, key, len)) {
            *result = p;
            return false;
        }
        if (p == hash->table)
            p += hash->size - 1;
        else
            p --;
    }

    ASSERT(hash->max_load <= hash->size - 1);
    if (hash->used < hash->max_load) {
        // => hash->used < hash->size - 1
        goto new_node;
    }

    // stev: let 'S' be the size of the hash
    // table before 'lhash_rehash' increases
    // it strictly; therefore: hash->used < S

    lhash_rehash(hash);

    // stev: we have that:
    //   hash->used < hash->size - 1
    // indeed:
    //   hash->size increased strictly =>
    //   S < hash->size <=>
    //   S <= hash->size - 1 =>
    //   hash->used < S <= hash->size - 1

    p = hash->table + h % hash->size;

    while (LHASH_NODE_KEY(p) != NULL) {
        if (p == hash->table)
            p += hash->size - 1;
        else
            p --;
    }

new_node:
    // stev: hash->used < hash->size - 1
    hash->used ++;

    *result = p;
    return true;
}

bool lhash_lookup(
    const struct lhash_t* hash,
    const char* key, size_t len,
    struct lhash_node_t** result)
{
    struct lhash_node_t* p;

    ASSERT(key != NULL);
    LHASH_ASSERT_INVARIANTS(hash);

    p = hash->table + lhash_hash_key2(key, len) %
        hash->size;

    while (LHASH_NODE_KEY(p) != NULL) {
        if (LHASH_NODE_KEY_EQ(p, key, len)) {
            *result = p;
            return true;
        }
        if (p == hash->table)
            p += hash->size - 1;
        else
            p --;
    }

    *result = NULL;
    return false;
}

void lhash_print(
    const struct lhash_t* hash, FILE* file)
{
    struct lhash_node_t *p, *e;
    const char* k;
    unsigned l;

    for (p = hash->table,
         e = p + hash->size;
         p < e;
         p ++) {
        k = LHASH_NODE_KEY(p);
        l = LHASH_NODE_LEN(p);
        if (k != NULL && p->val > 0)
            fprintf(file, "%u\t%.*s\n",
                p->val, UINT_AS_INT(l), k);
    }
}

struct file_buf_t
{
    const char* name;
    const char* ctxt;
    FILE* stream;
    char* ptr;
    size_t size;
    size_t len;
};

#define FILE_BUF_IO_ERROR(e) \
    IO_ERROR_SYS(e, file->ctxt, file->name)

void file_buf_init(
    struct file_buf_t* file,
    const char* name,
    const char* ctxt)
{
    memset(file, 0, sizeof *file);
    file->name = name;
    file->ctxt = ctxt;

    if (name == NULL)
        file->stream = stdin;
    else {
        file->stream = fopen(name, "r");
        if (file->stream == NULL)
            FILE_BUF_IO_ERROR(open);
    }
}

void file_buf_done(
    struct file_buf_t* file)
{
    if (file->ptr != NULL) {
        ASSERT(file->size > 0);
        free(file->ptr);
    }
    if (file->name != NULL)
        fclose(file->stream);
}

bool file_buf_get_line(
    struct file_buf_t* file,
    char const** ptr,
    size_t* len)
{
    errno = 0;
    ssize_t r = getline(
        &file->ptr, &file->size,
        file->stream);

    if (r < 0) {
        if (errno)
            FILE_BUF_IO_ERROR(read);
        return false;
    }
    // => r >= 0
    file->len = r;

    ASSERT(file->ptr != NULL);
    ASSERT(file->size > 0);

    ASSERT(file->len < file->size);
    ASSERT(file->len > 0);

    char* p = &file->ptr[file->len - 1];
    if (*p == '\n') {
        file->len --;
        *p = 0;
    }
    // => ptr + len is valid &&
    //    ptr[len] == 0

    *ptr = file->ptr;
    *len = file->len;
    return true;
}

struct file_map_t
{
    const char* name;
    const char* ctxt;
    struct mem_map_node_t map;
    bits_t released: 1;
    off_t line;
};

#define FILE_MAP_IO_ERROR(e) \
    IO_ERROR_SYS(e, file->ctxt, file->name)
#define FILE_MAP_IO_ERROR_FMT(e, m, ...)    \
    IO_ERROR_FMT(e, file->ctxt, file->name, \
        m, ## __VA_ARGS__)

void file_map_init(
    struct file_map_t* file,
    enum mem_map_type_t type,
    const char* name,
    const char* ctxt)
{
    memset(file, 0, sizeof *file);
    file->name = name;
    file->ctxt = ctxt;

    int fd;
    if (name == NULL)
        fd = 0;
    else {
        fd = open(name, O_RDONLY);
        if (fd < 0)
            FILE_MAP_IO_ERROR(open);
    }

    struct stat fs;
    if (fstat(fd, &fs) < 0)
        FILE_MAP_IO_ERROR(stat);

    if (!S_ISREG(fs.st_mode))
        FILE_MAP_IO_ERROR_FMT(stat,
            "not a regular file");

    off_t size = fs.st_size;
    char* ptr = mmap(NULL, size,
        PROT_READ, MAP_PRIVATE,
        fd, 0);
    ASSERT(ptr != NULL);

    if (ptr == MAP_FAILED)
        FILE_MAP_IO_ERROR(mmap);

    if (close(fd) < 0)
        FILE_MAP_IO_ERROR(close);

    mem_map_node_init(
        &file->map, ptr,
        INT_AS_SIZE(size));
    mem_map_node_set_type(
        &file->map,
        type);
}

void file_map_done(
    struct file_map_t* file)
{
    if (!file->released)
        mem_map_node_done(&file->map);
}

bool file_map_get_line(
    struct file_map_t* file,
    char const** ptr,
    size_t* len)
{
    size_t sz =
        INT_AS_SIZE(file->map.size);
    size_t ln =
        INT_AS_SIZE(file->line);

    // stev: main invariant:
    ASSERT(ln <= sz);
    size_t n = sz - ln;

    if (n == 0)
        return false;
    // => n > 0

    char* b = file->map.ptr + ln;
    char* p = memchr(b, '\n', n);

    size_t d = p != NULL
        ? PTR_DIFF(p + 1, b)
        : n;
    ASSERT(d <= n);

    *ptr = b;
    *len = d - (p != NULL);

    // stev: main invariant is preserved:
    // d <= n <=> ln + d <= ln + n
    //                   == ln + sz - ln
    //                   == sz
    //        <=> ln + d <= sz
    file->line += d;

    return true;
}

struct mem_range_t
    file_map_release(struct file_map_t* file)
{
    ASSERT(!file->released);
    file->released = true;

    return (struct mem_range_t) {
        .ptr  = file->map.ptr,
        .size = file->map.size
    };
}

enum file_io_type_t {
    file_io_type_buf,
    file_io_type_map
};

struct file_io_t
{
    union {
        struct file_buf_t buf;
        struct file_map_t map;
    };
    enum file_io_type_t type;

    void  *impl;
    void (*done)(void*);
    bool (*get_line)(void*,
        char const**, size_t*);
};

#define FILE_IO_INIT(n, ...)         \
    do {                             \
        file->type =                 \
            file_io_type_ ## n;      \
        file_ ## n ## _init(         \
            file->impl = &file->n,   \
            ## __VA_ARGS__);         \
        file->done =                 \
            (void (*)(void*))        \
            file_ ## n ## _done;     \
        file->get_line =             \
            (bool (*)(void*,         \
                char const**,        \
                size_t*))            \
            file_ ## n ## _get_line; \
    } while (0)

void file_io_init(
    struct file_io_t* file,
    const char* name,
    const char* ctxt,
    bool mapped)
{
    if (mapped)
        FILE_IO_INIT(
            map, mem_map_type_sequential,
            name, ctxt);
    else
        FILE_IO_INIT(
            buf, name, ctxt);
}

void file_io_done(
    struct file_io_t* file)
{
    file->done(file->impl);
}

bool file_io_get_line(
    struct file_io_t* file,
    char const** ptr,
    size_t* len)
{
    return file->get_line(
        file->impl, ptr, len);
}

#define FILE_IO_AS_(t)             \
    ({                             \
        VERIFY(                    \
            file->type ==          \
            file_io_type_ ## t);   \
        (struct file_ ## t ## _t*) \
            file->impl;            \
    })

struct file_buf_t*
    file_io_as_buf(struct file_io_t* file)
{ return FILE_IO_AS_(buf); }

struct file_map_t*
    file_io_as_map(struct file_io_t* file)
{ return FILE_IO_AS_(map); }

struct dict_t
{
    bits_t mapped_dict: 1;
    bits_t mapped_text: 1;
    struct mem_mgr_t mem;
    struct lhash_t hash;
    size_t n_words;
};

void dict_init(
    struct dict_t* dict,
    bool mapped_dict,
    bool mapped_text)
{
    dict->mapped_dict = mapped_dict;
    dict->mapped_text = mapped_text;

    mem_mgr_init(&dict->mem, mapped_dict);
    lhash_init(&dict->hash, 1024);

    dict->n_words = 0;
}

void dict_done(struct dict_t* dict)
{
    lhash_done(&dict->hash);
    mem_mgr_done(&dict->mem);
}

void dict_load(
    struct dict_t* dict,
    const char* file_name)
{
    struct file_io_t f;
    size_t l = 0, k;
    const char* b;

    file_io_init(
        &f, file_name, "dictionary",
        dict->mapped_dict);

    while (file_io_get_line(&f, &b, &k)) {
        const char *p;

        l ++;

        if ((p = memchr(b, 0, k)) != NULL) {
            size_t d = PTR_DIFF(p, b);

            warning("NUL char in line #%zu: truncating "
                    "it from length %zu to %zu", l, k, d);
            k = d;
        }

        if (k == 0 || b[0] == '#')
            continue;

#ifdef CONFIG_USE_48BIT_PTR
        if (k > UINT16_MAX) {
            warning("ignoring word on line #%zu: its length "
                    "%zu exceeds the maximum allowed %" PRIu16,
                    l, k, UINT16_MAX);
            continue;
        }
#endif

        struct lhash_node_t* e = NULL;
        if (!lhash_insert(&dict->hash, b, k, &e))
            warning("duplicated word in line #%zu: '%.*s'",
                l, SIZE_AS_INT(k), b);
        else {
            ASSERT(e != NULL);
            if (!dict->mapped_dict) {
                struct mem_buf_t* m =
                    mem_mgr_as_buf(&dict->mem);
                ASSERT(m != NULL);
                b = mem_buf_append(m, b, k);
            }
            LHASH_NODE_INIT(e, b, k);
        }
    }

    if (dict->mapped_dict) {
        struct file_map_t* a = file_io_as_map(&f);
        ASSERT(a != NULL);

        struct mem_range_t r = file_map_release(a);
        ASSERT(r.ptr != NULL);
        ASSERT(r.size > 0);

        struct mem_map_t* m =
            mem_mgr_as_map(&dict->mem);
        ASSERT(m != NULL);

        struct mem_map_node_t* n =
            mem_map_append(m, r.ptr, r.size);
        ASSERT(n != NULL);

        mem_map_node_set_type(
            n, mem_map_type_random);
    }

    file_io_done(&f);
}

#define UCHAR(c)                    \
    (                               \
        STATIC(TYPEOF_IS(c, char)), \
        (unsigned char) (c)         \
    )

typedef char ascii_table_t[256];

size_t memspn(
    const char* p, size_t n,
    const ascii_table_t t)
{
    const char* q = p;
    while (n --) {
        if (!t[UCHAR(*q)])
            break;
        q ++;
    }
    return PTR_DIFF(q, p);
}

size_t memcspn(
    const char* p, size_t n,
    const ascii_table_t t)
{
    const char* q = p;
    while (n --) {
        if (t[UCHAR(*q)])
            break;
        q ++;
    }
    return PTR_DIFF(q, p);
}

void dict_count(
    struct dict_t* dict,
    const char* file_name)
{
    static const ascii_table_t wsp = {
        [' ']  = 1, ['\t'] = 1, ['\f'] = 1,
        ['\n'] = 1, ['\r'] = 1, ['\v'] = 1,
        ['\0'] = 1
    };

    struct file_io_t f;
    size_t w = 0, k;
    const char* p;

    file_io_init(
        &f, file_name, "input",
        dict->mapped_text);

    while (file_io_get_line(&f, &p, &k)) {
        while (k > 0) {
            // stev: skip over whitespaces
            size_t s = memspn(p, k, wsp);
            ASSERT(s <= k);
            p += s;
            k -= s;

            // stev: compute word length
            size_t n = memcspn(p, k, wsp);
            if (n == 0) break;
            w ++;

            struct lhash_node_t* e = NULL;
            if (lhash_lookup(&dict->hash, p, n, &e)) {
                ASSERT(e != NULL);
                ASSERT(e->val < UINT_MAX);
                e->val ++;
            }

            ASSERT(n <= k);
            p += n;
            k -= n;
        }
    }

    file_io_done(&f);

    ASSERT_UINT_ADD_NO_OVERFLOW(
        dict->n_words, w);
    dict->n_words += w;
}

void dict_print(
    const struct dict_t* dict, FILE* file)
{
    lhash_print(&dict->hash, file);
    fprintf(file, "%zu\ttotal\n",
        dict->n_words);
}

struct options_t
{
    char const         *dict;
    char const* const  *inputs;
    size_t            n_inputs;
    bits_t dict_use_mmap_io: 1;
    bits_t text_use_mmap_io: 1;
};

void options_invalid_opt_arg(
    const char* opt_name, const char* opt_arg)
{
    error("invalid argument for '%s' option: '%s'",
        opt_name, opt_arg);
}

void options_parse_use_mmap_io_optarg(
    struct options_t* opts,
    const char* opt_name,
    const char* opt_arg)
{
    struct spec_t
    { const char* name; bits_t value; };
    enum {
        dict = 1 << 0,
        text = 1 << 1,
        all  = dict|text,
        none = 0
    };
    static const struct spec_t specs[] = {
#undef  CASE
#define CASE(n) \
    { .name = #n, .value = n }
        CASE(dict),
        CASE(text),
        CASE(none),
        CASE(all),
#undef  CASE
#define CASE(n, v) \
    { .name = #n, .value = v }
        CASE(+, all),
        CASE(-, none)
    };
    const struct spec_t *p, *e;

    if (opt_name != NULL)
        ASSERT(opt_arg != NULL);
    else
    if (opt_arg == NULL)
        return;

    for (p = specs,
         e = p + ARRAY_SIZE(specs);
         p < e;
         p ++) {
        if (!strcmp(p->name, opt_arg))
            break;
    }

    if (p >= e) {
        if (opt_name == NULL)
            return;
        options_invalid_opt_arg(
            opt_name,
            opt_arg);
    }

    opts->dict_use_mmap_io =
        (p->value & dict) != 0;
    opts->text_use_mmap_io =
        (p->value & text) != 0;
}

const struct options_t*
    options(int argc, char** argv)
{
    static struct options_t opts;

#define GET_ENV(n) getenv("WORD_COUNT_" #n)

    // stev: partially initialize 'opts' from
    // the program's environment variable list
    options_parse_use_mmap_io_optarg(
        &opts, NULL, GET_ENV(USE_MMAP_IO));

    enum {
        // stev: instance options:
        use_mmap_io_opt = 'm',

        // stev: info options:
        help_opt        = '?',
        version_opt     = 128
    };

    static const struct option longs[] = {
        { "use-mmap-io", 1,       0, use_mmap_io_opt },
        { "version",     0,       0, version_opt },
        { "help",        0, &optopt, help_opt },
        { 0,             0,       0, 0 }
    };
    static const char shorts[] = ":m:";

    struct bits_opts_t
    {
        bits_t usage: 1;
        bits_t version: 1;
    };
    struct bits_opts_t bits = {
        .usage = false,
        .version = false
    };
    int opt;

#define missing_opt_arg_str(n) \
    error("argument for option '%s' not found", n)
#define missing_opt_arg_ch(n) \
    error("argument for option '-%c' not found", n);
#define not_allowed_opt_arg(n) \
    error("option '%s' does not allow an argument", n)
#define invalid_opt_str(n) \
    error("invalid command line option '%s'", n)
#define invalid_opt_ch(n) \
    error("invalid command line option '-%c'", n)

#define argv_optind()               \
    ({                              \
        ASSERT(optind > 0);         \
        ASSERT(optind -  1 < argc); \
        argv[optind - 1];           \
    })
#define optopt_char()                      \
    ({                                     \
        ASSERT((unsigned) optopt < 0x80u); \
        (char) optopt;                     \
    })

    opterr = 0;
    optind = 1;
    while ((opt = getopt_long(
        argc, argv, shorts, longs, 0)) != EOF) {
        switch (opt) {
        case use_mmap_io_opt:
            options_parse_use_mmap_io_optarg(
                &opts, "use-mmap-io",
                optarg);
            break;
        case version_opt:
            bits.version = true;
            break;
        case 0:
            bits.usage = true;
            break;
        case ':': {
            const char* opt = argv_optind();
            if (opt[0] == '-' && opt[1] == '-')
                missing_opt_arg_str(opt);
            else
                missing_opt_arg_ch(optopt_char());
            break;
        }
        case '?':
        default:
            if (optopt == 0)
                invalid_opt_str(argv_optind());
            else {
                char* opt = argv_optind();
                if (opt[0] != '-' || opt[1] != '-') {
                    if (optopt != '?')
                        invalid_opt_ch(optopt_char());
                    else
                        bits.usage = true;
                }
                else {
                    char* end = strchr(opt, '=');
                    if (end) *end = '\0';
                    if (end || optopt != '?')
                        not_allowed_opt_arg(opt);
                    else
                        bits.usage = true;
                }
            }
            break;
        }
    }

    ASSERT(optind > 0);
    ASSERT(optind <= argc);

    argc -= optind;
    argv += optind;

    if (bits.version)
        printf("%s: version %s\n",
            program, verdate);
    if (bits.usage)
        printf(help, program);

    if (bits.version ||
        bits.usage)
        exit(0);

    if (argc <= 0)
        error("dictionary file name not given");

    char const* const* args =
        PTR_PTR_CAST(argv, char);

    opts.dict = *args;
    opts.inputs = ++ args;
    opts.n_inputs = -- argc;

    return &opts;
}

int main(int argc, char* argv[])
{
    const struct options_t* opt =
        options(argc, argv);

    struct dict_t dict;
    dict_init(&dict,
        opt->dict_use_mmap_io,
        opt->text_use_mmap_io);
    dict_load(&dict, opt->dict);

    if (!opt->n_inputs)
        dict_count(&dict, NULL);
    else {
        char const* const* p;
        char const* const* e;

        for (p = opt->inputs,
             e = p + opt->n_inputs;
             p < e;
             p ++) {
            ASSERT(*p != NULL);
            dict_count(&dict, *p);
        }
    }

    dict_print(&dict, stdout);
    dict_done(&dict);

    return 0;
}


