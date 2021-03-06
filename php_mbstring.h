/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2009 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Moriyoshi Koizumi <moriyoshi@php.net>                        |
   +----------------------------------------------------------------------+
 */

/* $Id$ */

#ifndef _MBSTRING_H
#define _MBSTRING_H

#ifdef COMPILE_DL_MBSTRING_NG
#undef HAVE_MBSTRING_NG
#define HAVE_MBSTRING_NG 1
#endif

#if HAVE_MBSTRING_NG

#include <unicode/utypes.h>
#include <unicode/pwin32.h>

#ifdef PHP_WIN32
#	undef MBSTRING_NG_API
#	ifdef MBSTRING_NG_EXPORTS
#		define MBSTRING_NG_API __declspec(dllexport)
#	elif defined(COMPILE_DL_MBSTRING_NG)
#		define MBSTRING_NG_API __declspec(dllimport)
#	else
#		define MBSTRING_NG_API /* nothing special */
#	endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#	undef MBSTRING_NG_API
#	define MBSTRING_NG_API __attribute__ ((visibility("default")))
#else
#	undef MBSTRING_NG_API
#	define MBSTRING_NG_API /* nothing special */
#endif

extern zend_module_entry mbstring_ng_module_entry;
#define mbstring_ng_module_ptr &mbstring_ng_module_entry

typedef struct {
    UChar *p;
    int32_t len;
    int32_t nalloc;
    int persistent;
} php_mb2_ustring;

typedef struct {
    const char **items;
    size_t nitems;
    char *alloc;
    size_t alloc_size;
    int persistent:1;
} php_mb2_char_ptr_list;

typedef struct php_mb2_uconverter_callback_ctx {
	char *dbuf;
	char *pdl;
	int persistent;
	const UChar *unassigned_subst_str_u;
	int32_t unassigned_subst_str_u_len;
	const UChar *illegal_subst_str_u;
	int32_t illegal_subst_str_u_len;
#ifdef ZTS
	TSRMLS_D;
#endif
} php_mb2_uconverter_callback_ctx;

typedef struct php_mb2_output_handler_ctx {
    int32_t pvbuf_basic_len;
    UConverter *from_conv;
    UConverter *to_conv;
    php_mb2_uconverter_callback_ctx ctx;
    UChar *pvbuf;
    UChar *ppvs, *ppvd;
} php_mb2_output_handler_ctx;

ZEND_BEGIN_MODULE_GLOBALS(mbstring_ng)
    struct {
        char *locale;
        php_mb2_char_ptr_list detect_order;
        php_mb2_char_ptr_list http_input;
        char *http_output;
        char *internal_encoding;
        php_mb2_ustring subst_string_unassigned;
        php_mb2_ustring subst_string_illegal;
        zend_bool encoding_translation;
        char *http_output_conv_mimetypes;
    } ini;
    struct {
        int in_ucnv_error_handler;
        php_mb2_output_handler_ctx output_handler;
        HashTable regex_cache;
        int regex_flags;
    } runtime;
ZEND_END_MODULE_GLOBALS(mbstring_ng)

#ifdef ZTS
#define MBSTR_NG(v) TSRMG(mbstring_ng_globals_id, zend_mbstring_ng_globals *, v)
#else
#define MBSTR_NG(v) (mbstring_ng_globals.v)
#endif

#else	/* HAVE_MBSTRING */

#define mbstring_ng_module_ptr NULL

#endif	/* HAVE_MBSTRING */

#define phpext_mbstring_ng_ptr mbstring_ng_module_ptr

#endif		/* _MBSTRING_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
