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
   | Based on the work by:                                                |
   |         Tsukada Takuya <tsukada@fminn.nagano.nagano.jp>              |
   |         Rui Hirokawa <hirokawa@php.net>                              |
   +----------------------------------------------------------------------+
 */

/* $Id: mbstring.c,v 1.224.2.22.2.25.2.55 2009/05/27 13:42:17 tony2001 Exp $ */

/* {{{ includes */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unicode/uchar.h>
#include <unicode/ucnv.h>
#include <unicode/uloc.h>
#include <unicode/ucsdet.h>
#include <unicode/ustring.h>
#include <unicode/uregex.h>

#include "php.h"
#include "php_variables.h"
#include "php_ini.h"
#include "SAPI.h"
#include "ext/standard/php_string.h"
#include "ext/standard/php_mail.h"
#include "ext/standard/exec.h"
#include "ext/standard/php_smart_str.h"
#include "ext/standard/url.h"
#include "ext/standard/info.h"

#include "php_mbstring.h"
/* }}} */

#if HAVE_MBSTRING_NG

#define PHP_MBREGEX_MAXCACHE 50

#define PHP_MB_FUNCTION(name) PHP_FUNCTION(mb2_ ## name)
#define PHP_MB_FE(name, arginfo) PHP_FE(mb2_ ## name, arginfo)
#define PHP_MB_INI_ENTRY_NAME(name) "mbstring2." name
#define STD_PHP_MB_INI_ENTRY(name, default_value, flags, handler, field, type, global) \
	STD_PHP_INI_ENTRY(PHP_MB_INI_ENTRY_NAME(name), default_value, flags, handler, field, type, global)
#define PHP_MB_INI_ENTRY_EX(name, default_value, flags, handler, arg) \
	PHP_INI_ENTRY_EX(PHP_MB_INI_ENTRY_NAME(name), default_value, flags, handler, arg)

#define STD_PHP_MB_INI_BOOLEAN(name, default_value, flags, handler, field, type, global) \
	STD_PHP_INI_BOOLEAN(PHP_MB_INI_ENTRY_NAME(name), default_value, flags, handler, field, type, global)

typedef struct php_mb2_mime_type_buf {
	char *charset;
	int32_t charset_len;
	char *type;
	int32_t type_len;
} php_mb2_mime_type_buf;

ZEND_DECLARE_MODULE_GLOBALS(mbstring_ng);
static PHP_GINIT_FUNCTION(mbstring_ng);
static PHP_GSHUTDOWN_FUNCTION(mbstring_ng);
static PHP_MINIT_FUNCTION(mbstring_ng);
static PHP_MSHUTDOWN_FUNCTION(mbstring_ng);
static PHP_RINIT_FUNCTION(mbstring_ng);
static PHP_RSHUTDOWN_FUNCTION(mbstring_ng);
static PHP_MINFO_FUNCTION(mbstring_ng);

static PHP_INI_MH(php_mb2_OnUpdateEncodingList);
static PHP_INI_MH(php_mb2_OnUpdateUnicodeString);
static PHP_INI_MH(php_mb2_OnUpdate_substitute_character);

static PHP_MB_FUNCTION(strtoupper);
static PHP_MB_FUNCTION(strtolower);
static PHP_MB_FUNCTION(strtotitle);
static PHP_MB_FUNCTION(internal_encoding);
static PHP_MB_FUNCTION(preferred_mime_name);
static PHP_MB_FUNCTION(parse_str);
static PHP_MB_FUNCTION(output_handler);
static PHP_MB_FUNCTION(strlen);
static PHP_MB_FUNCTION(strpos);
static PHP_MB_FUNCTION(strrpos);
static PHP_MB_FUNCTION(stripos);
static PHP_MB_FUNCTION(strripos);
static PHP_MB_FUNCTION(strstr);
static PHP_MB_FUNCTION(stristr);
static PHP_MB_FUNCTION(substr_count);
static PHP_MB_FUNCTION(substr);
static PHP_MB_FUNCTION(strcut);
static PHP_MB_FUNCTION(strwidth);
static PHP_MB_FUNCTION(strimwidth);
static PHP_MB_FUNCTION(convert_encoding);
static PHP_MB_FUNCTION(detect_encoding);
static PHP_MB_FUNCTION(list_encodings);

static PHP_MB_FUNCTION(ereg);
static PHP_MB_FUNCTION(eregi);
static PHP_MB_FUNCTION(ereg_replace);
static PHP_MB_FUNCTION(eregi_replace);
static PHP_MB_FUNCTION(split);
static PHP_MB_FUNCTION(ereg_match);

static void php_mb2_char_ptr_list_ctor(php_mb2_char_ptr_list *list, int persistent);
static void php_mb2_char_ptr_list_dtor(php_mb2_char_ptr_list *list);
static int php_mb2_char_ptr_list_reserve(php_mb2_char_ptr_list *list, size_t nitems_grow, size_t alloc_size_grow);

static int php_mb2_parse_encoding_list(const char *value, int32_t value_length, php_mb2_char_ptr_list *pretval, int persistent);
static int php_mb2_zval_to_encoding_list(zval *repr, php_mb2_char_ptr_list *pretval TSRMLS_DC);
static int php_mb2_convert_encoding(const char *input, int32_t length, const char *to_encoding, const char * const *from_encodings, size_t num_from_encodings, char **output, int32_t *output_len, int persistent TSRMLS_DC);
static int php_mb2_encode(const UChar *input, int32_t length, const char *to_encoding, char **output, int32_t *output_len, int persistent TSRMLS_DC);
static char *php_mb2_detect_encoding(const char *input, int32_t length, const char * const *from_encodings, size_t num_from_encodings TSRMLS_DC);
static int php_mb2_ustring_ctor(php_mb2_ustring *str, int persistent);
static int php_mb2_ustring_ctor_from_n(php_mb2_ustring *, const char *str, int32_t len, const char *encoding, int persistent);
static int php_mb2_ustring_appendu(php_mb2_ustring *, const UChar *ustr, int32_t len);
static int php_mb2_ustring_appendn(php_mb2_ustring *, const char *str, int32_t len, UConverter *from_conv);
static int php_mb2_ustring_reserve(php_mb2_ustring *, int32_t len);
static const UChar *php_mb2_ustring_offset(const php_mb2_ustring *, int32_t offset);
static const UChar *php_mb2_ustring_roffset(const php_mb2_ustring *ustr, int32_t offset);
static void php_mb2_ustring_dtor(php_mb2_ustring *str);
static int php_mb2_get_eaw(const UChar *str, int32_t len, zend_bool ambiguous_as_half, int *retval);
static unsigned int php_mb2_sapi_filter(int arg, char *var, char **val, unsigned int val_len, unsigned int *new_val_len TSRMLS_DC);
static unsigned int php_mb2_sapi_filter_init(TSRMLS_D);
static php_mb2_register_sapi_filter(TSRMLS_C);
static php_mb2_unregister_sapi_filter(TSRMLS_C);
static void php_mb2_uconverter_from_unicode_callback(const void *_ctx, UConverterFromUnicodeArgs *args, UChar *units, int32_t length, UChar32 codePoint, UConverterCallbackReason reason, UErrorCode *err);
static void php_mb2_uconverter_to_unicode_callback(const void *_ctx, UConverterToUnicodeArgs *args, const char *units, int32_t length, UConverterCallbackReason reason, UErrorCode *err);
static int php_mb2_parse_mime_type(php_mb2_mime_type_buf *retval, const char *header, size_t header_len);
static void php_mb2_mime_type_buf_dtor(php_mb2_mime_type_buf *buf);
static URegularExpression *php_mb2_regex_open(const char *pattern, int32_t pattern_len, const char *encoding, uint32_t flags TSRMLS_DC);

/* {{{ arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_internal_encoding, 0, 0, 0)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_preferred_mime_name, 0, 0, 1)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_parse_str, 0, 0, 1)
	ZEND_ARG_INFO(0, encoded_string)
	ZEND_ARG_INFO(1, result)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_output_handler, 0, 0, 2)
	ZEND_ARG_INFO(0, contents)
	ZEND_ARG_INFO(0, status)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strlen, 0, 0, 1)
	ZEND_ARG_INFO(0, str)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strpos, 0, 0, 2)
	ZEND_ARG_INFO(0, haystack)
	ZEND_ARG_INFO(0, needle)
	ZEND_ARG_INFO(0, offset)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strrpos, 0, 0, 2)
	ZEND_ARG_INFO(0, haystack)
	ZEND_ARG_INFO(0, needle)
	ZEND_ARG_INFO(0, offset)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_stripos, 0, 0, 2)
	ZEND_ARG_INFO(0, haystack)
	ZEND_ARG_INFO(0, needle)
	ZEND_ARG_INFO(0, offset)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strripos, 0, 0, 2)
	ZEND_ARG_INFO(0, haystack)
	ZEND_ARG_INFO(0, needle)
	ZEND_ARG_INFO(0, offset)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strstr, 0, 0, 2)
	ZEND_ARG_INFO(0, haystack)
	ZEND_ARG_INFO(0, needle)
	ZEND_ARG_INFO(0, part)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_stristr, 0, 0, 2)
	ZEND_ARG_INFO(0, haystack)
	ZEND_ARG_INFO(0, needle)
	ZEND_ARG_INFO(0, part)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_substr_count, 0, 0, 2)
	ZEND_ARG_INFO(0, haystack)
	ZEND_ARG_INFO(0, needle)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_substr, 0, 0, 2)
	ZEND_ARG_INFO(0, str)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, length)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strcut, 0, 0, 2)
	ZEND_ARG_INFO(0, str)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, length)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strwidth, 0, 0, 1)
	ZEND_ARG_INFO(0, str)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strimwidth, 0, 0, 3)
	ZEND_ARG_INFO(0, str)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, width)
	ZEND_ARG_INFO(0, trimmarker)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_convert_encoding, 0, 0, 2)
	ZEND_ARG_INFO(0, str)
	ZEND_ARG_INFO(0, to)
	ZEND_ARG_INFO(0, from)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strtoupper, 0, 0, 1)
	ZEND_ARG_INFO(0, sourcestring)
	ZEND_ARG_INFO(0, encoding)
	ZEND_ARG_INFO(0, locale)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strtolower, 0, 0, 1)
	ZEND_ARG_INFO(0, sourcestring)
	ZEND_ARG_INFO(0, encoding)
	ZEND_ARG_INFO(0, locale)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strtotitle, 0, 0, 1)
	ZEND_ARG_INFO(0, sourcestring)
	ZEND_ARG_INFO(0, encoding)
	ZEND_ARG_INFO(0, locale)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_detect_encoding, 0, 0, 1)
	ZEND_ARG_INFO(0, str)
	ZEND_ARG_INFO(0, encoding_list)
	ZEND_ARG_INFO(0, strict)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_mb_list_encodings, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_ereg, 0, 0, 2)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, string)
	ZEND_ARG_INFO(1, registers)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_eregi, 0, 0, 2)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, string)
	ZEND_ARG_INFO(1, registers)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_ereg_replace, 0, 0, 3)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, replacement)
	ZEND_ARG_INFO(0, string)
	ZEND_ARG_INFO(0, option)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_eregi_replace, 0, 0, 3)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, replacement)
	ZEND_ARG_INFO(0, string)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_split, 0, 0, 2)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, string)
	ZEND_ARG_INFO(0, limit)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_ereg_match, 0, 0, 2)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, string)
	ZEND_ARG_INFO(0, option)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_ereg_search, 0, 0, 0)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, option)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_ereg_search_pos, 0, 0, 0)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, option)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_ereg_search_regs, 0, 0, 0)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, option)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_ereg_search_init, 0, 0, 1)
	ZEND_ARG_INFO(0, string)
	ZEND_ARG_INFO(0, pattern)
	ZEND_ARG_INFO(0, option)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_mb_ereg_search_getregs, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_mb_ereg_search_getpos, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_ereg_search_setpos, 0, 0, 1)
	ZEND_ARG_INFO(0, position)
ZEND_END_ARG_INFO()
/* }}} */

/* {{{ zend_function_entry mbstring_ng_functions[] */
const zend_function_entry mbstring_ng_functions[] = {
	PHP_MB_FE(strtoupper,			arginfo_mb_strtoupper)
	PHP_MB_FE(strtolower,			arginfo_mb_strtolower)
	PHP_MB_FE(strtotitle,			arginfo_mb_strtotitle)
	PHP_MB_FE(internal_encoding,	arginfo_mb_internal_encoding)
	PHP_MB_FE(preferred_mime_name,	arginfo_mb_preferred_mime_name)
	PHP_MB_FE(parse_str,			arginfo_mb_parse_str)
	PHP_MB_FE(output_handler,		arginfo_mb_output_handler)
	PHP_MB_FE(strlen,				arginfo_mb_strlen)
	PHP_MB_FE(strpos,				arginfo_mb_strpos)
	PHP_MB_FE(strrpos,				arginfo_mb_strrpos)
	PHP_MB_FE(stripos,				arginfo_mb_stripos)
	PHP_MB_FE(strripos,				arginfo_mb_strripos)
	PHP_MB_FE(strstr,				arginfo_mb_strstr)
	PHP_MB_FE(stristr,				arginfo_mb_stristr)
	PHP_MB_FE(substr_count,			arginfo_mb_substr_count)
	PHP_MB_FE(substr,				arginfo_mb_substr)
	PHP_MB_FE(strcut,				arginfo_mb_strcut)
	PHP_MB_FE(strwidth,				arginfo_mb_strwidth)
	PHP_MB_FE(strimwidth,			arginfo_mb_strimwidth)
	PHP_MB_FE(convert_encoding,		arginfo_mb_convert_encoding)
	PHP_MB_FE(detect_encoding,		arginfo_mb_detect_encoding)
	PHP_MB_FE(list_encodings,		arginfo_mb_list_encodings)
	PHP_MB_FE(ereg,					arginfo_mb_ereg)
	PHP_MB_FE(eregi,				arginfo_mb_eregi)
	PHP_MB_FE(ereg_replace,			arginfo_mb_ereg_replace)
	PHP_MB_FE(eregi_replace,		arginfo_mb_eregi_replace)
	PHP_MB_FE(split,				arginfo_mb_split)
	PHP_MB_FE(ereg_match,			arginfo_mb_ereg_match)
	{ NULL, NULL, NULL }
};
/* }}} */

/* {{{ zend_module_entry mbstring_module_entry */
static zend_module_dep mbstring_ng_module_dep[] = {
	ZEND_MOD_REQUIRED("filter")
	{ NULL }
};

zend_module_entry mbstring_ng_module_entry = {
	STANDARD_MODULE_HEADER_EX,
	NULL, mbstring_ng_module_dep,
	"mbstring_ng",
	mbstring_ng_functions,
	PHP_MINIT(mbstring_ng),
	PHP_MSHUTDOWN(mbstring_ng),
	PHP_RINIT(mbstring_ng),
	PHP_RSHUTDOWN(mbstring_ng),
	PHP_MINFO(mbstring_ng),
	NO_VERSION_YET,
	PHP_MODULE_GLOBALS(mbstring_ng),
	PHP_GINIT(mbstring_ng),
	PHP_GSHUTDOWN(mbstring_ng),
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};
/* }}} */

#ifdef COMPILE_DL_MBSTRING_NG
ZEND_GET_MODULE(mbstring_ng)
#endif

/* {{{ php.ini directive registration */
PHP_INI_BEGIN()
	STD_PHP_MB_INI_ENTRY("locale", "default", PHP_INI_ALL,
						OnUpdateString, ini.locale,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("detect_order", "ASCII", PHP_INI_ALL,
						php_mb2_OnUpdateEncodingList, ini.detect_order,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("http_input", "pass", PHP_INI_ALL,
						php_mb2_OnUpdateEncodingList, ini.http_input,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("http_output", "pass", PHP_INI_ALL,
						OnUpdateString, ini.http_output,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("internal_encoding", "UTF-8", PHP_INI_ALL,
						OnUpdateString, ini.internal_encoding,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	PHP_MB_INI_ENTRY_EX("substitute_character", "?", PHP_INI_ALL,
						php_mb2_OnUpdate_substitute_character, NULL)
	STD_PHP_MB_INI_ENTRY("subst_string_unassigned", "?", PHP_INI_ALL,
						php_mb2_OnUpdateUnicodeString,
						ini.subst_string_unassigned,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("subst_string_illegal", "?", PHP_INI_ALL,
						php_mb2_OnUpdateUnicodeString,
						ini.subst_string_illegal,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_BOOLEAN("encoding_translation", "0",
						PHP_INI_SYSTEM | PHP_INI_PERDIR,
						OnUpdateBool, ini.encoding_translation,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("http_output_conv_mimetypes",
						"^(text/|application/xhtml\\+xml)",
						PHP_INI_ALL, OnUpdateString,
						ini.http_output_conv_mimetypes,
						zend_mbstring_ng_globals, mbstring_ng_globals)
PHP_INI_END()
/* }}} */

/* {{{ module global initialize handler */
static PHP_GINIT_FUNCTION(mbstring_ng)
{
	php_mb2_parse_encoding_list("UTF-8", sizeof("UTF-8") - 1, &mbstring_ng_globals->ini.http_input, 1 TSRMLS_CC);
	php_mb2_char_ptr_list_ctor(&mbstring_ng_globals->ini.detect_order, 1);
	php_mb2_char_ptr_list_ctor(&mbstring_ng_globals->ini.http_input, 1);
	php_mb2_ustring_ctor(&mbstring_ng_globals->ini.subst_string_illegal, 1);
	php_mb2_ustring_ctor(&mbstring_ng_globals->ini.subst_string_unassigned, 1);

	mbstring_ng_globals->ini.locale = NULL;
	mbstring_ng_globals->ini.internal_encoding = NULL;
	mbstring_ng_globals->ini.http_output = NULL;

	{
		php_mb2_output_handler_ctx *ctx = &mbstring_ng_globals->runtime.output_handler;

		ctx->pvbuf = NULL;
		ctx->from_conv = NULL;
		ctx->to_conv = NULL;
	}
}
/* }}} */

static unsigned int (*php_mb2_next_input_filter)(int arg, char *var, char **val, unsigned int val_len, unsigned int *new_val_len TSRMLS_DC);
static unsigned int (*php_mb2_next_input_filter_init)(TSRMLS_D);

/* {{{ PHP_GSHUTDOWN_FUNCTION */
static PHP_GSHUTDOWN_FUNCTION(mbstring_ng)
{
	{
		php_mb2_output_handler_ctx *ctx = &mbstring_ng_globals->runtime.output_handler;

		if (ctx->pvbuf) {
			efree(ctx->pvbuf);
		}

		if (ctx->from_conv) {
			ucnv_close(ctx->from_conv);
		}

		if (ctx->to_conv) {
			ucnv_close(ctx->to_conv);
		}
	}

	php_mb2_ustring_dtor(&mbstring_ng_globals->ini.subst_string_unassigned);
	php_mb2_ustring_dtor(&mbstring_ng_globals->ini.subst_string_illegal);
	php_mb2_char_ptr_list_dtor(&mbstring_ng_globals->ini.http_input);
	php_mb2_char_ptr_list_dtor(&mbstring_ng_globals->ini.detect_order);
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION(mbstring_ng) */
static PHP_MINIT_FUNCTION(mbstring_ng)
{
	REGISTER_INI_ENTRIES();

	php_mb2_register_sapi_filter();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION(mbstring_ng) */
static PHP_MSHUTDOWN_FUNCTION(mbstring_ng)
{
	UNREGISTER_INI_ENTRIES();

	php_mb2_unregister_sapi_filter();
	return SUCCESS;
}
/* }}} */

static void php_mb2_regex_cache_dtor_cb(URegularExpression *rex)
{
	uregex_close(rex);
}

/* {{{ PHP_RINIT_FUNCTION(mbstring_ng) */
static PHP_RINIT_FUNCTION(mbstring_ng)
{
	zend_hash_init(&MBSTR_NG(runtime).regex_cache, 0, NULL, (dtor_func_t)php_mb2_regex_cache_dtor_cb, 0);
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION(mbstring_ng) */
static PHP_RSHUTDOWN_FUNCTION(mbstring_ng)
{
	zend_hash_destroy(&MBSTR_NG(runtime).regex_cache);
	{
		php_mb2_output_handler_ctx *ctx = &MBSTR_NG(runtime).output_handler;

		if (ctx->pvbuf) {
			efree(ctx->pvbuf);
			ctx->pvbuf = NULL;
		}

		if (ctx->from_conv) {
			ucnv_close(ctx->from_conv);
			ctx->from_conv = NULL;
		}

		if (ctx->to_conv) {
			ucnv_close(ctx->to_conv);
			ctx->to_conv = NULL;
		}
	}
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION(mbstring_ng) */
static PHP_MINFO_FUNCTION(mbstring_ng)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "Multibyte Support", "enabled");
	php_info_print_table_row(2, "HTTP input encoding translation", MBSTR_NG(ini).encoding_translation ? "enabled": "disabled");	
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

/* {{{ static PHP_INI_MH(php_mb2_OnUpdateEncodingList) */
static PHP_INI_MH(php_mb2_OnUpdateEncodingList)
{
#ifndef ZTS
	char *base = (char *) mh_arg2;
#else
	char *base = (char *) ts_resource(*((int *) mh_arg2));
#endif
	php_mb2_char_ptr_list *p = (php_mb2_char_ptr_list *)(base + (size_t) mh_arg1);
	php_mb2_char_ptr_list new_list;
	int res = php_mb2_parse_encoding_list(new_value, (size_t)new_value_length, &new_list, 1 TSRMLS_CC);

	php_mb2_char_ptr_list_dtor(p);

	if (SUCCESS == res) {
		*p = new_list;
	} else {
		php_mb2_char_ptr_list_ctor(p, 1);
	}

	return SUCCESS;
}
/* }}} */

/* {{{ static PHP_INI_MH(php_mb2_OnUpdateUnicodeString) */
static PHP_INI_MH(php_mb2_OnUpdateUnicodeString)
{
#ifndef ZTS
	char *base = (char *) mh_arg2;
#else
	char *base = (char *) ts_resource(*((int *) mh_arg2));
#endif
	php_mb2_ustring *p = (php_mb2_ustring *)(base + (size_t) mh_arg1);
	php_mb2_ustring new_value_ustr;

	if (FAILURE == php_mb2_ustring_ctor_from_n(&new_value_ustr, new_value, new_value_length, MBSTR_NG(ini).internal_encoding ? MBSTR_NG(ini).internal_encoding: "ASCII", 1)) {
		return FAILURE;
	}
	php_mb2_ustring_dtor(p);
	*p = new_value_ustr;

	return SUCCESS;
}
/* }}} */

/* {{{ static PHP_INI_MH(php_mb2_OnUpdate_substitute_character) */
static PHP_INI_MH(php_mb2_OnUpdate_substitute_character)
{
	if (new_value != NULL && stage != PHP_INI_STAGE_DEACTIVATE) {
		zend_alter_ini_entry(PHP_MB_INI_ENTRY_NAME("subst_string_illegal"), sizeof(PHP_MB_INI_ENTRY_NAME("subst_string_illegal")), new_value, new_value_length, PHP_INI_SYSTEM, stage TSRMLS_CC);
		zend_alter_ini_entry(PHP_MB_INI_ENTRY_NAME("subst_string_unassigned"), sizeof(PHP_MB_INI_ENTRY_NAME("subst_string_unassigned")), new_value, new_value_length, PHP_INI_SYSTEM, stage TSRMLS_CC);
	}
	return SUCCESS;
}
/* }}} */

/* {{{ _php_mb2_get_eaw */
static int _php_mb2_code_to_eaw_table[2][U_EA_COUNT] = {
	{ 1, 2, 1, 2, 1, 2 },
	{ 1, 1, 1, 2, 1, 2 }
};	

#define _php_mb2_get_uchar_eaw(c, ambiguous_as_half) \
	_php_mb2_code_to_eaw_table[ambiguous_as_half][u_getIntPropertyValue((c), UCHAR_EAST_ASIAN_WIDTH)]
/* }}} */

/* {{{ proto string mb_internal_encoding([string encoding])
   Sets the current internal encoding or Returns the current internal encoding as a string */
PHP_MB_FUNCTION(internal_encoding)
{
	char *name = NULL;
	int name_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s", &name, &name_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto string mb_preferred_mime_name(string encoding)
   Return the preferred MIME name (charset) as a string */
PHP_MB_FUNCTION(preferred_mime_name)
{
	char *name = NULL;
	const char *retval;
	int name_len;
	UErrorCode err = U_ZERO_ERROR;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &name, &name_len) == FAILURE) {
		return;
	}

	retval = ucnv_getStandardName(name, "IANA", &err);
	if (!retval) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No corresponding standard name for %s", name);
		RETURN_FALSE;
	}

	RETURN_STRING(retval, 1);
}
/* }}} */

/* {{{ proto bool mb_parse_str(string encoded_string [, array result])
   Parses GET/POST/COOKIE data and sets global variables */
PHP_MB_FUNCTION(parse_str)
{
	zval **track_vars_array = NULL;
	char *encstr = NULL;
	int encstr_len;
	char *res;
	zend_bool old_ini_val;

	track_vars_array = NULL;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|Z", &encstr, &encstr_len, &track_vars_array) == FAILURE) {
		return;
	}

	old_ini_val = MBSTR_NG(ini).encoding_translation;
	MBSTR_NG(ini).encoding_translation = TRUE;

	res = estrndup(encstr, encstr_len);

	if (track_vars_array == NULL) {
		zval tmp;

		if (!EG(active_symbol_table)) {
			zend_rebuild_symbol_table(TSRMLS_C);
		}
		Z_ARRVAL(tmp) = EG(active_symbol_table);
		sapi_module.treat_data(PARSE_STRING, res, &tmp TSRMLS_CC);
	} else 	{
		/* Clear out the array that was passed in. */
		zval_dtor(*track_vars_array);
		array_init(*track_vars_array);
		
		sapi_module.treat_data(PARSE_STRING, res, *track_vars_array TSRMLS_CC);
	}

	MBSTR_NG(ini).encoding_translation = old_ini_val;
}
/* }}} */

/* {{{ proto string mb_output_handler(string contents, int status)
   Returns string in output buffer converted to the http_output encoding */
PHP_MB_FUNCTION(output_handler)
{
	size_t chunk_len = 4096;
	const char *string;
	int string_len;
	long status;
	UErrorCode err;
	char *pd;
	const char *ps, *psl;
	php_mb2_output_handler_ctx *ctx = &MBSTR_NG(runtime).output_handler;
	php_mb2_uconverter_callback_ctx *cctx = &ctx->ctx;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl", &string, &string_len, &status) == FAILURE) {
		return;
	}

	if (OG(ob_nesting_level)) {
	   	if (OG(active_ob_buffer).chunk_size > 1) {
			chunk_len = OG(active_ob_buffer).chunk_size;
		} 
	}

	ps = string, psl = string + string_len;

	if ((status & PHP_OUTPUT_HANDLER_START)) {
		php_mb2_mime_type_buf mimetype_buf;
		const char *in_enc = MBSTR_NG(ini).internal_encoding;
		const char *out_enc = MBSTR_NG(ini).http_output;
		UConverter *from_conv, *to_conv;
		if (out_enc && strcasecmp(out_enc, "pass") == 0) {
			RETURN_FALSE;
		}

		{
			const char *mimetype = SG(sapi_headers).mimetype ? SG(sapi_headers).mimetype: (SG(default_mimetype) ? SG(default_mimetype): SAPI_DEFAULT_MIMETYPE);
			if (SUCCESS == php_mb2_parse_mime_type(&mimetype_buf, mimetype, strlen(mimetype))) {
				php_mb2_ustring mimetype_u;
				err = U_ZERO_ERROR;
				if (SUCCESS == php_mb2_ustring_ctor_from_n(&mimetype_u, mimetype_buf.type, mimetype_buf.type_len, "ASCII", 0)) {
					URegularExpression *rex = uregex_openC(MBSTR_NG(ini).http_output_conv_mimetypes, UREGEX_CASE_INSENSITIVE, NULL, &err);
					if (U_FAILURE(err)) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to compile the regular expression: %s", MBSTR_NG(ini).http_output_conv_mimetypes);
					} else {
						UBool result = FALSE;
						uregex_setText(rex, mimetype_u.p, mimetype_u.len, &err);
						if (U_SUCCESS(err)) {
							result = uregex_find(rex, -1, &err);
						}
						if (U_FAILURE(err)) {
							php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to match the regular expression");
						} else {
							if (!result) {
								uregex_close(rex);
								php_mb2_ustring_dtor(&mimetype_u);
								php_mb2_mime_type_buf_dtor(&mimetype_buf);
								RETURN_FALSE;
							}
						}
					}
					uregex_close(rex);
					php_mb2_ustring_dtor(&mimetype_u);
				}

				if (!out_enc || !*out_enc) {
					out_enc = mimetype_buf.charset;
				}
			}
		}

		if (!out_enc) {
			php_mb2_mime_type_buf_dtor(&mimetype_buf);
			RETURN_FALSE;
		}

		ctx->to_conv = ctx->from_conv = NULL;
		ctx->pvbuf = NULL;
		cctx->dbuf = NULL;
		cctx->persistent = 0;
		cctx->unassigned_subst_str_u = MBSTR_NG(ini).subst_string_unassigned.p;
		cctx->unassigned_subst_str_u_len = MBSTR_NG(ini).subst_string_unassigned.len;
		cctx->illegal_subst_str_u = MBSTR_NG(ini).subst_string_illegal.p;
		cctx->illegal_subst_str_u_len = MBSTR_NG(ini).subst_string_illegal.len;
#ifdef ZTS
		cctx->TSRMLS_C = TSRMLS_C;
#endif
		ctx->pvbuf_basic_len = chunk_len;

		if (ctx->pvbuf_basic_len + cctx->unassigned_subst_str_u_len < ctx->pvbuf_basic_len || sizeof(UChar) * (ctx->pvbuf_basic_len + cctx->unassigned_subst_str_u_len) / sizeof(UChar) != ctx->pvbuf_basic_len + cctx->unassigned_subst_str_u_len) {
			php_mb2_mime_type_buf_dtor(&mimetype_buf);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate temporary buffer");
			goto fail;
		}
		ctx->ppvs = ctx->ppvd = ctx->pvbuf = safe_emalloc(ctx->pvbuf_basic_len + cctx->unassigned_subst_str_u_len, sizeof(UChar), 0);

		err = U_ZERO_ERROR;
		to_conv = ucnv_open(out_enc, &err);
		php_mb2_mime_type_buf_dtor(&mimetype_buf); /* free the mimetype_buf as it is no longer necessary at this point */
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the encoder for %s (error: %s)", out_enc, u_errorName(err));
			goto fail;
		}

		ucnv_setFromUCallBack(to_conv, (UConverterFromUCallback)php_mb2_uconverter_from_unicode_callback, cctx, NULL, NULL, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to set the callback for the encoder (error: %s)", u_errorName(err));
			goto fail;
		}

		err = U_ZERO_ERROR;
		from_conv = ucnv_open(in_enc, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", in_enc, u_errorName(err));
			goto fail;
		}

		ucnv_setToUCallBack(from_conv, (UConverterToUCallback)php_mb2_uconverter_to_unicode_callback, cctx, NULL, NULL, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to set the callback to the decoder (error: %s)", u_errorName(err));
			goto fail;
		}

		ctx->from_conv = from_conv;
		ctx->to_conv = to_conv;

		cctx->dbuf = emalloc(chunk_len + 1);
		cctx->pdl = cctx->dbuf + chunk_len;
		pd = cctx->dbuf;
		ucnv_convertEx(ctx->to_conv, ctx->from_conv, &pd, cctx->pdl, &ps, psl, ctx->pvbuf, &ctx->ppvs, &ctx->ppvd, ctx->pvbuf + ctx->pvbuf_basic_len, TRUE, FALSE, &err);
		while (err == U_BUFFER_OVERFLOW_ERROR) {
			size_t new_dbuf_size;
			char *new_dbuf;
			new_dbuf_size = (cctx->pdl - cctx->dbuf) << 1;
			if (new_dbuf_size + 1 < cctx->pdl - cctx->dbuf || !(new_dbuf = erealloc(cctx->dbuf, new_dbuf_size + 1))) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the buffer for conversion results");
				goto fail;
			}
			pd = new_dbuf + (pd - cctx->dbuf);
			cctx->dbuf = new_dbuf;
			cctx->pdl = new_dbuf + new_dbuf_size;

			err = U_ZERO_ERROR;
			ucnv_convertEx(ctx->to_conv, ctx->from_conv, &pd, cctx->pdl, &ps, psl, ctx->pvbuf, &ctx->ppvs, &ctx->ppvd, ctx->pvbuf + ctx->pvbuf_basic_len, FALSE, FALSE, &err);
		}
		if (U_FAILURE(err)) {
			UErrorCode dummy = U_ZERO_ERROR;
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert from %s to %s (error: %s)", ucnv_getName(ctx->from_conv, &dummy), ucnv_getName(ctx->to_conv, &dummy), u_errorName(err));
			goto fail;
		}
	} else {
		if (!ctx->to_conv || !ctx->from_conv) {
			RETURN_FALSE;
		}

		cctx->dbuf = emalloc(chunk_len + 1);
		cctx->pdl = cctx->dbuf + chunk_len;
		pd = cctx->dbuf;
		for (;;) {
			size_t new_dbuf_size;
			char *new_dbuf;
			err = U_ZERO_ERROR;
			ucnv_convertEx(ctx->to_conv, ctx->from_conv, &pd, cctx->pdl, &ps, psl, ctx->pvbuf, &ctx->ppvs, &ctx->ppvd, ctx->pvbuf + ctx->pvbuf_basic_len, FALSE, TRUE, &err);
			if (U_SUCCESS(err)) {
				break;
			}
			if (err != U_BUFFER_OVERFLOW_ERROR) {
				UErrorCode dummy = U_ZERO_ERROR;
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert from %s to %s (error: %s)", ucnv_getName(ctx->from_conv, &dummy), ucnv_getName(ctx->to_conv, &dummy), u_errorName(err));
				goto fail;
			}
			new_dbuf_size = (cctx->pdl - cctx->dbuf) << 1;
			if (new_dbuf_size + 1 < cctx->pdl - cctx->dbuf || !(new_dbuf = erealloc(cctx->dbuf, new_dbuf_size + 1))) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the buffer for conversion results");
				goto fail;
			}
			pd = new_dbuf + (pd - cctx->dbuf);
			cctx->dbuf = new_dbuf;
			cctx->pdl = new_dbuf + new_dbuf_size;
		}
	}

	if ((status & PHP_OUTPUT_HANDLER_END)) {
		if (ctx->pvbuf) {
			efree(ctx->pvbuf);
			ctx->pvbuf = NULL;
		}

		if (ctx->from_conv) {
			ucnv_close(ctx->from_conv);
			ctx->from_conv = NULL;
		}

		if (ctx->to_conv) {
			ucnv_close(ctx->to_conv);
			ctx->to_conv = NULL;
		}
	}

	*pd = '\0';
	RETURN_STRINGL(cctx->dbuf, pd - cctx->dbuf, 0);
fail:
	if (cctx->dbuf) {
		efree(cctx->dbuf);
		cctx->dbuf = NULL;
	}

	if (ctx->pvbuf) {
		efree(ctx->pvbuf);
		ctx->pvbuf = NULL;
	}

	if (ctx->from_conv) {
		ucnv_close(ctx->from_conv);
		ctx->from_conv = NULL;
	}

	if (ctx->to_conv) {
		ucnv_close(ctx->to_conv);
		ctx->to_conv = NULL;
	}
}
/* }}} */

/* {{{ proto int mb_strlen(string str [, string encoding])
   Get character numbers of a string */
PHP_MB_FUNCTION(strlen)
{
	char *string_val = NULL;
	int string_len;
	char *enc_name = NULL;
	int enc_name_len;
	php_mb2_ustring ustr;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &string_val, &string_len, &enc_name, &enc_name_len) == FAILURE) {
		return;
	}

	if (FAILURE == php_mb2_ustring_ctor_from_n(&ustr, string_val, string_len, enc_name, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert the input to Unicode");
		RETURN_FALSE;
	}

	RETVAL_LONG(u_countChar32(ustr.p, ustr.len));

	php_mb2_ustring_dtor(&ustr);
}
/* }}} */

/* {{{ proto int mb_strpos(string haystack, string needle [, int offset [, string encoding]])
   Find position of first occurrence of a string within another */
PHP_MB_FUNCTION(strpos)
{
	char *haystack;
	int haystack_len;
	char *needle;
	int needle_len;
	char *encoding = NULL;
	int encoding_len;
	long offset = 0;
	UConverter *conv;
	UErrorCode err;
	php_mb2_ustring haystack_ustr;
	php_mb2_ustring needle_ustr;
	const UChar *start;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|ls", &haystack, &haystack_len, &needle, &needle_len, &offset, &encoding, &encoding_len) == FAILURE) {
		return;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	RETVAL_FALSE;

	if (php_mb2_ustring_ctor(&needle_ustr, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	if (php_mb2_ustring_ctor(&haystack_ustr, 0)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);
	if (U_FAILURE(err)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_mb2_ustring_dtor(&haystack_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, haystack, haystack_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (offset < 0 || !(start = php_mb2_ustring_offset(&haystack_ustr, offset))) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offset (%d) not contained in string", offset);
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, needle, needle_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	{
		const UChar *p = u_strFindFirst(start, (haystack_ustr.p + haystack_ustr.len) - start, needle_ustr.p, needle_ustr.len);
		if (p) {
			RETVAL_LONG(u_countChar32(haystack_ustr.p, p - haystack_ustr.p));
		}
	}
out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&needle_ustr);
	php_mb2_ustring_dtor(&haystack_ustr);
}
/* }}} */

/* {{{ proto int mb_strrpos(string haystack, string needle [, int offset [, string encoding]])
   Find position of last occurrence of a string within another */
PHP_MB_FUNCTION(strrpos)
{
	long offset = 0;
	zval **zoffset = NULL;
	char *haystack;
	int haystack_len;
	char *needle;
	int needle_len;
	char *encoding = NULL;
	int encoding_len;
	UConverter *conv;
	UErrorCode err;
	php_mb2_ustring haystack_ustr;
	php_mb2_ustring needle_ustr;
	const UChar *start;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|Zs", &haystack, &haystack_len, &needle, &needle_len, &zoffset, &encoding, &encoding_len) == FAILURE) {
		return;
	}

	RETVAL_FALSE;

	/* tweak for backwards compatibility */
	if (zoffset) {
		if (Z_TYPE_PP(zoffset) == IS_STRING) {
			char *_encoding   = Z_STRVAL_PP(zoffset);
			int _encoding_len = Z_STRLEN_PP(zoffset);
			double doffset;

			switch (is_numeric_string(_encoding, _encoding_len, &offset, &doffset, 1)) {
			default:
				if (encoding != NULL) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "The fourth argument is not applicable if the third argument is given a string");
					return;
				}
				php_error_docref(NULL TSRMLS_CC, E_DEPRECATED, "Specifying the encoding to the third argument will no longer be supported in the future versions");
				encoding     = _encoding;
				encoding_len = _encoding_len;
				break;

			case IS_LONG:
				break;
			case IS_DOUBLE:
				offset = zend_dval_to_lval(doffset);
			}
		} else {
			convert_to_long_ex(zoffset);
			offset = Z_LVAL_PP(zoffset);
		}
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	if (php_mb2_ustring_ctor(&needle_ustr, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	if (php_mb2_ustring_ctor(&haystack_ustr, 0)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);
	if (U_FAILURE(err)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_mb2_ustring_dtor(&haystack_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, haystack, haystack_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (offset >=0 && !(start = php_mb2_ustring_offset(&haystack_ustr, offset))
			|| (offset < 0 && !(start = php_mb2_ustring_roffset(&haystack_ustr, -offset)))) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offset (%d) is greater than the length of haystack string", offset);
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, needle, needle_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	{
		const UChar *p = u_strFindLast(start, (haystack_ustr.p + haystack_ustr.len) - start, needle_ustr.p, needle_ustr.len);
		if (p) {
			RETVAL_LONG(u_countChar32(haystack_ustr.p, p - haystack_ustr.p));
		}
	}
out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&needle_ustr);
	php_mb2_ustring_dtor(&haystack_ustr);
}
/* }}} */

/* {{{ proto int mb_stripos(string haystack, string needle [, int offset [, string encoding [, bool exclude_special_i]]])
   Finds position of first occurrence of a string within another, case insensitive */
PHP_MB_FUNCTION(stripos)
{
	char *haystack;
	int haystack_len;
	char *needle;
	int needle_len;
	char *encoding = NULL;
	int encoding_len;
	long offset = 0;
	zend_bool exclude_special_i = FALSE;
	UConverter *conv;
	UErrorCode err;
	php_mb2_ustring haystack_ustr;
	php_mb2_ustring needle_ustr;
	const UChar *start;
	uint32_t opt;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|lsb", &haystack, &haystack_len, &needle, &needle_len, &offset, &encoding, &encoding_len, &exclude_special_i) == FAILURE) {
		return;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	opt = exclude_special_i ? U_FOLD_CASE_EXCLUDE_SPECIAL_I: U_FOLD_CASE_DEFAULT;

	RETVAL_FALSE;

	if (php_mb2_ustring_ctor(&needle_ustr, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	if (php_mb2_ustring_ctor(&haystack_ustr, 0)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);
	if (U_FAILURE(err)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_mb2_ustring_dtor(&haystack_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, haystack, haystack_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	{
		int32_t result_len = u_strFoldCase(haystack_ustr.p, haystack_ustr.len, haystack_ustr.p, haystack_ustr.len, opt, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to fold the case of \"haystack\"", encoding);
			goto out;
		}
		if (result_len != haystack_ustr.len) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offsetted search is unreliable because the case-folded version of \"haystack\" is longer than the original", encoding);
			goto out;
		}
	}

	if (offset < 0 || !(start = php_mb2_ustring_offset(&haystack_ustr, offset))) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offset (%d) not contained in string", offset);
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, needle, needle_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	{
		int32_t result_len = u_strFoldCase(needle_ustr.p, needle_ustr.len, needle_ustr.p, needle_ustr.len, opt, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to fold the case of \"needle\"", encoding);
			goto out;
		}
		if (result_len != needle_ustr.len) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offsetted search is unreliable because the case-folded version of \"needle\" is longer than the original", encoding);
			goto out;
		}
	}

	{
		const UChar *p = u_strFindFirst(start, (haystack_ustr.p + haystack_ustr.len) - start, needle_ustr.p, needle_ustr.len);
		if (p) {
			RETVAL_LONG(u_countChar32(haystack_ustr.p, p - haystack_ustr.p));
		}
	}
out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&needle_ustr);
	php_mb2_ustring_dtor(&haystack_ustr);
}
/* }}} */

/* {{{ proto int mb_strripos(string haystack, string needle [, int offset [, string encoding [, bool exclude_special_i]]])
   Finds position of last occurrence of a string within another, case insensitive */
PHP_MB_FUNCTION(strripos)
{
	char *haystack;
	int haystack_len;
	char *needle;
	int needle_len;
	char *encoding = NULL;
	int encoding_len;
	long offset = 0;
	zend_bool exclude_special_i = FALSE;
	UConverter *conv;
	UErrorCode err;
	php_mb2_ustring haystack_ustr;
	php_mb2_ustring needle_ustr;
	const UChar *start;
	uint32_t opt;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|lsb", &haystack, &haystack_len, &needle, &needle_len, &offset, &encoding, &encoding_len, &exclude_special_i) == FAILURE) {
		return;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	opt = exclude_special_i ? U_FOLD_CASE_EXCLUDE_SPECIAL_I: U_FOLD_CASE_DEFAULT;

	RETVAL_FALSE;

	if (php_mb2_ustring_ctor(&needle_ustr, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	if (php_mb2_ustring_ctor(&haystack_ustr, 0)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);
	if (U_FAILURE(err)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_mb2_ustring_dtor(&haystack_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, haystack, haystack_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	{
		int32_t result_len = u_strFoldCase(haystack_ustr.p, haystack_ustr.len, haystack_ustr.p, haystack_ustr.len, opt, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to fold the case of \"haystack\"", encoding);
			goto out;
		}
		if (result_len != haystack_ustr.len) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offsetted search is unreliable because the case-folded version of \"haystack\" is longer than the original", encoding);
			goto out;
		}
	}

	if (offset >=0 && !(start = php_mb2_ustring_offset(&haystack_ustr, offset))
			|| (offset < 0 && !(start = php_mb2_ustring_roffset(&haystack_ustr, -offset)))) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offset (%d) is greater than the length of haystack string", offset);
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, needle, needle_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	{
		int32_t result_len = u_strFoldCase(needle_ustr.p, needle_ustr.len, needle_ustr.p, needle_ustr.len, opt, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to fold the case of \"needle\"", encoding);
			goto out;
		}
		if (result_len != needle_ustr.len) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offsetted search is unreliable because the case-folded version of \"needle\" is longer than the original", encoding);
			goto out;
		}
	}

	{
		const UChar *p = u_strFindLast(start, (haystack_ustr.p + haystack_ustr.len) - start, needle_ustr.p, needle_ustr.len);
		if (p) {
			RETVAL_LONG(u_countChar32(haystack_ustr.p, p - haystack_ustr.p));
		}
	}
out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&needle_ustr);
	php_mb2_ustring_dtor(&haystack_ustr);
}
/* }}} */

/* {{{ proto string mb_strstr(string haystack, string needle[, bool part[, string encoding]])
   Finds first occurrence of a string within another */
PHP_MB_FUNCTION(strstr)
{
	char *haystack;
	int haystack_len;
	char *needle;
	int needle_len;
	zend_bool part = FALSE;
	char *encoding = NULL;
	int encoding_len;
	UConverter *conv;
	UErrorCode err;
	php_mb2_ustring haystack_ustr;
	php_mb2_ustring needle_ustr;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|bs", &haystack, &haystack_len, &needle, &needle_len, &part, &encoding, &encoding_len) == FAILURE) {
		return;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	RETVAL_FALSE;

	if (php_mb2_ustring_ctor(&needle_ustr, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	if (php_mb2_ustring_ctor(&haystack_ustr, 0)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);
	if (U_FAILURE(err)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_mb2_ustring_dtor(&haystack_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, haystack, haystack_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, needle, needle_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	{
		const UChar *p = u_strFindFirst(haystack_ustr.p, haystack_ustr.len, needle_ustr.p, needle_ustr.len);

		if (p) {
			char *output;
			int32_t output_len;
			const UChar *start;
			int32_t len;

			if (part) {
				start = haystack_ustr.p;
				len = p - haystack_ustr.p;
			} else {
				start = p;
				len = haystack_ustr.len - (p - haystack_ustr.p);
			}

			if (SUCCESS == php_mb2_encode(start, len, encoding, &output, &output_len, 0 TSRMLS_CC)) {
				RETVAL_STRINGL(output, output_len, 0);
			}
		}
	}
out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&needle_ustr);
	php_mb2_ustring_dtor(&haystack_ustr);
}
/* }}} */

/* {{{ proto string mb_stristr(string haystack, string needle[, bool part[, string encoding [, bool exclude_special_i]]])
   Finds first occurrence of a string within another, case insensitive */
PHP_MB_FUNCTION(stristr)
{
	char *haystack;
	int haystack_len;
	char *needle;
	int needle_len;
	zend_bool part = FALSE;
	char *encoding = NULL;
	int encoding_len;
	zend_bool exclude_special_i = FALSE;
	UConverter *conv;
	UErrorCode err;
	php_mb2_ustring haystack_ustr;
	php_mb2_ustring folded_haystack_ustr;
	php_mb2_ustring needle_ustr;
	uint32_t opt;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|bsb", &haystack, &haystack_len, &needle, &needle_len, &part, &encoding, &encoding_len, &exclude_special_i) == FAILURE) {
		return;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	opt = exclude_special_i ? U_FOLD_CASE_EXCLUDE_SPECIAL_I: U_FOLD_CASE_DEFAULT;

	RETVAL_FALSE;

	if (php_mb2_ustring_ctor(&needle_ustr, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	if (php_mb2_ustring_ctor(&haystack_ustr, 0)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	if (php_mb2_ustring_ctor(&folded_haystack_ustr, 0)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_mb2_ustring_dtor(&haystack_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}


	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);
	if (U_FAILURE(err)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_mb2_ustring_dtor(&haystack_ustr);
		php_mb2_ustring_dtor(&folded_haystack_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, haystack, haystack_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_reserve(&folded_haystack_ustr, haystack_ustr.len)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		goto out;
	}

	{
		int32_t result_len = u_strFoldCase(folded_haystack_ustr.p, folded_haystack_ustr.nalloc, haystack_ustr.p, haystack_ustr.len, opt, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to fold the case of \"haystack\"", encoding);
			goto out;
		}
		if (result_len != haystack_ustr.len) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offsetted search is unreliable because the case-folded version of \"haystack\" is longer than the original", encoding);
			goto out;
		}
		folded_haystack_ustr.len = result_len;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, needle, needle_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	{
		int32_t result_len = u_strFoldCase(needle_ustr.p, needle_ustr.len, needle_ustr.p, needle_ustr.len, opt, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to fold the case of \"needle\"", encoding);
			goto out;
		}
		if (result_len != needle_ustr.len) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Offsetted search is unreliable because the case-folded version of \"needle\" is longer than the original", encoding);
			goto out;
		}
	}

	{
		const UChar *p = u_strFindFirst(folded_haystack_ustr.p, folded_haystack_ustr.len, needle_ustr.p, needle_ustr.len);

		if (p) {
			char *output;
			int32_t output_len;
			const UChar *start;
			int32_t len;

			if (part) {
				start = haystack_ustr.p;
				len = p - folded_haystack_ustr.p;
			} else {
				size_t offset = p - folded_haystack_ustr.p;
				start = haystack_ustr.p + offset;
				len = haystack_ustr.len - offset;
			}

			if (SUCCESS == php_mb2_encode(start, len, encoding, &output, &output_len, 0 TSRMLS_CC)) {
				RETVAL_STRINGL(output, output_len, 0);
			}
		}
	}
out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&needle_ustr);
	php_mb2_ustring_dtor(&haystack_ustr);
	php_mb2_ustring_dtor(&folded_haystack_ustr);
}
/* }}} */

/* {{{ proto int mb_substr_count(string haystack, string needle [, string encoding])
   Count the number of substring occurrences */
PHP_MB_FUNCTION(substr_count)
{
	char *haystack;
	int haystack_len;
	char *needle;
	int needle_len;
	char *encoding = NULL;
	int encoding_len;
	UConverter *conv;
	UErrorCode err;
	php_mb2_ustring haystack_ustr;
	php_mb2_ustring needle_ustr;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|s", &haystack, &haystack_len, &needle, &needle_len, &encoding, &encoding_len) == FAILURE) {
		return;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	RETVAL_FALSE;

	if (php_mb2_ustring_ctor(&needle_ustr, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	if (php_mb2_ustring_ctor(&haystack_ustr, 0)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate a temporary buffer");
		return;
	}

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);
	if (U_FAILURE(err)) {
		php_mb2_ustring_dtor(&needle_ustr);
		php_mb2_ustring_dtor(&haystack_ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, haystack, haystack_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&haystack_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"haystack\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, needle, needle_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&needle_ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode \"needle\" as %s", encoding);
		goto out;
	}

	{
		long retval = 0;
		const UChar *pl = haystack_ustr.p + haystack_ustr.len, *p, *pn;
		for (p = haystack_ustr.p; p < pl && (pn = u_strFindFirst(p, pl - p, needle_ustr.p, needle_ustr.len)); p = pn + needle_ustr.len) {
			retval++;
		}
		RETVAL_LONG(retval);
	}
out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&needle_ustr);
	php_mb2_ustring_dtor(&haystack_ustr);
}
/* }}} */

/* {{{ proto string mb_substr(string str, int start [, int length [, string encoding]])
   Returns part of a string */
PHP_MB_FUNCTION(substr)
{
	char *str;
	int str_len;
	char *encoding = NULL;
	int encoding_len;
	long from, len;
	php_mb2_ustring ustr;
	const UChar *start;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|ls", &str, &str_len, &from, &len, &encoding, &encoding_len) == FAILURE) {
		return;
	}

	if (FAILURE == php_mb2_ustring_ctor_from_n(&ustr, str, str_len, encoding ? encoding: MBSTR_NG(ini).internal_encoding, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert the input to Unicode");
		RETURN_FALSE;
	}

	if (ZEND_NUM_ARGS() <= 2) {
		len = ustr.len;
	}

	if (from >= 0) {
		start = php_mb2_ustring_offset(&ustr, from);
		if (start == NULL) {
			start = ustr.p + ustr.len;
		}
	} else {
		start = php_mb2_ustring_roffset(&ustr, -from - 1);
		if (start == NULL) {
			start = ustr.p;
		}
	}

	if (len < 0) {
		const UChar *end = php_mb2_ustring_roffset(&ustr, -len);
		len = end ? end - start: 0;
	} else if (start + len > ustr.p + ustr.len) {
		len = (ustr.p + ustr.len) - start;
	}

	{
		char *output;
		int32_t output_len;
		if (FAILURE == php_mb2_encode(start, len, encoding, &output, &output_len, 0 TSRMLS_CC)) {
			RETVAL_FALSE;
		} else {
			RETVAL_STRINGL(output, output_len, 0);
		}
	}

	php_mb2_ustring_dtor(&ustr);
}
/* }}} */

/* {{{ proto string mb_strcut(string str, int start [, int length [, string encoding]])
   Returns part of a string */
PHP_MB_FUNCTION(strcut)
{
	char *str;
	int str_len;
	char *encoding = NULL;
	int encoding_len;
	long from, len;
	php_mb2_ustring result;
	UConverter *conv;
	const UChar *start;
	UErrorCode err;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|ls", &str, &str_len, &from, &len, &encoding, &encoding_len) == FAILURE) {
		return;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	RETVAL_FALSE;

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);
	if (U_FAILURE(err)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	if (ZEND_NUM_ARGS() > 2) {
		if (len < 0) {
			len = str_len + len;
			if (len < 0) {
				len = 0;
			}
		}
	} else {
		len = str_len;
	}

	if (from < 0) {
		from = str_len + from;
		if (from < 0) {
			from = 0;
		}
	} else {
		if (from > str_len) {
			from = str_len;
		}
	}

	if (FAILURE == php_mb2_ustring_ctor(&result, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the result buffer");
		ucnv_close(conv);
		return;
	}

	if (FAILURE == php_mb2_ustring_reserve(&result, str_len)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the result buffer");
		goto out;
	}

	{
		UChar32 c;
		const char *p = str, *e = str + str_len, *po = str + from, *pl;

		assert(po <= e);

		for (;;) {
			const char *prev_p;
			if (p >= po) {
				pl = p + len;
				break;
			}

			prev_p = p;
			err = U_ZERO_ERROR;
			c = ucnv_getNextUChar(conv, &p, e, &err);
			if (U_FAILURE(err)) {
				UErrorCode dummy = U_ZERO_ERROR;
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode the input string as %s (error: %s)", ucnv_getName(conv, &dummy), u_errorName(err));
				goto out;
			}
			if (p > po) {
				UChar buf[U16_MAX_LENGTH];
				int32_t i = 0;
				pl = prev_p + len;
				if (p <= pl) {
					U16_APPEND_UNSAFE(buf, i, c);
					assert(result.nalloc >= i);
					memmove(result.p, buf, sizeof(UChar) * i);
					result.len = i;
				}
				break;
			}
		}

		if (pl > e) {
			pl = e;
		}

		while (p < pl) {
			UChar buf[U16_MAX_LENGTH];
			int32_t i = 0;

			err = U_ZERO_ERROR;
			c = ucnv_getNextUChar(conv, &p, e, &err);
			if (U_FAILURE(err)) {
				UErrorCode dummy = U_ZERO_ERROR;
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode the input string as %s (error: %s)", ucnv_getName(conv, &dummy), u_errorName(err));
				goto out;
			}
			if (p > pl) {
				break;
			}
			U16_APPEND_UNSAFE(buf, i, c);
			if (FAILURE == php_mb2_ustring_reserve(&result, result.len + i)) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the result buffer");
				goto out;
			}
			memmove(result.p + result.len, buf, sizeof(UChar) * i);
			result.len += i;
		}
	}

	{
		char *output;
		int32_t output_len;
		if (SUCCESS == php_mb2_encode(result.p, result.len, encoding, &output, &output_len, 0 TSRMLS_CC)) {
			RETVAL_STRINGL(output, output_len, 0);
		}
	}

out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&result);
}
/* }}} */

/* {{{ proto int mb_strwidth(string str [, string encoding, [ bool ambiguous_as_half]])
   Gets terminal width of a string */
PHP_MB_FUNCTION(strwidth)
{
	char *str;
	int str_len;
	char *encoding = NULL;
	int encoding_len;
	zend_bool ambiguous_as_half = FALSE;
	long retval;
	const char *p, *e;
	UErrorCode err;
	UConverter *conv = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|sb", &str, &str_len, &encoding, &encoding_len, &ambiguous_as_half) == FAILURE) {
		return;
	}

	if (ambiguous_as_half) {
		/* make sure that the integer value is 1 */
		ambiguous_as_half = 1;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	RETVAL_FALSE;

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);

	if (U_FAILURE(err)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		return;
	}

	retval = 0;
	for (p = str, e = str + str_len; p < e; ) {
		UEastAsianWidth ea;

		err = U_ZERO_ERROR;
		UChar32 c = ucnv_getNextUChar(conv, &p, e, &err);

		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode the input string as %s (error: %s)", encoding, u_errorName(err));
			ucnv_close(conv);
			return;
		}

		retval += _php_mb2_get_uchar_eaw(c, ambiguous_as_half);
	}

	ucnv_close(conv);

	RETVAL_LONG(retval);
}
/* }}} */

/* {{{ proto string mb_strimwidth(string str, int start, [int width [, string trimmarker [, string encoding [, bool ambiguous_as_half [, bool offset_by_eaw]]]]])
   Trim the string in terminal width */
PHP_MB_FUNCTION(strimwidth)
{
	char *str;
	int str_len;
	char *encoding = NULL;
	int encoding_len;
	char *marker = "";
	int marker_len = 0;
	long from, len;
	int width, marker_width;
	zend_bool ambiguous_as_half = FALSE, offset_by_eaw = FALSE;
	zend_bool append_marker = FALSE;
	php_mb2_ustring ustr, umarker;
	UConverter *conv = NULL;
	const UChar *start, *end;
	UErrorCode err;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|ls!s!bb", &str, &str_len, &from, &len, &marker, &marker_len, &encoding, &encoding_len, &ambiguous_as_half, &offset_by_eaw) == FAILURE) {
		return;
	}

	if (ambiguous_as_half) {
		/* make sure that the integer value is 1 */
		ambiguous_as_half = 1;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	RETVAL_FALSE;

	if (FAILURE == php_mb2_ustring_ctor(&ustr, FALSE)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the temporary buffer");
		return;
	}

	if (FAILURE == php_mb2_ustring_ctor(&umarker, FALSE)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the temporary buffer");
		php_mb2_ustring_dtor(&ustr);
		return;
	}

	err = U_ZERO_ERROR;
	conv = ucnv_open(encoding, &err);

	if (U_FAILURE(err)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to open the decoder for %s (error: %s)", encoding, u_errorName(err));
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&ustr, str, str_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decoder the input as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&ustr, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decoder the input as %s", encoding);
		goto out;
	}

	ucnv_reset(conv);

	if (FAILURE == php_mb2_ustring_appendn(&umarker, marker, marker_len, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decoder the input as %s", encoding);
		goto out;
	}

	if (FAILURE == php_mb2_ustring_appendn(&umarker, NULL, 0, conv)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decoder the input as %s", encoding);
		goto out;
	}

	ucnv_reset(conv);

	assert(SUCCESS == php_mb2_get_eaw(ustr.p, ustr.len, ambiguous_as_half, &width));
	assert(SUCCESS == php_mb2_get_eaw(umarker.p, umarker.len, ambiguous_as_half, &marker_width));

	if (ZEND_NUM_ARGS() > 2) {
		if (len < 0) {
			len = width + len;
			if (len < 0) {
				len = 0;
			}
		}
	} else {
		len = width;
	}

	if (offset_by_eaw) {
		const UChar *p, *e;
		int eawo = 0, to;

		if (from < 0) {
			from = width + from;
			if (from < 0) {
				from = 0;
			}
		}

		to = from + len;
		if (to < width) {
			if (to - marker_len < from) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "The width of the trim marker (%d) exceeds the width of the range (%d)", marker_width, to - from);
				goto out;
			}
			to -= marker_len;
			append_marker = TRUE;
		}

		p = ustr.p;
		e = ustr.p + ustr.len;
		for (;;) {
			UChar32 c;
			if (eawo <= from) {
				start = p;
			}
			if (eawo <= to) {
				end = p;
			}
			if (p >= e) {
				break;
			}
			if (U16_IS_LEAD(*p)) {
				assert(p + 1 < e && U16_IS_TRAIL(*(p + 1)));
				c = U16_GET_SUPPLEMENTARY(*p, *(p + 1));
				p += 2;
			} else {
				c = *p;
				p++;
			}
			eawo += _php_mb2_get_uchar_eaw(c, ambiguous_as_half);
		}
	} else {
		const UChar *p, *e;
		int eawo = 0, co = 0, to;

		if (from < 0) {
			from = u_countChar32(ustr.p, ustr.len) + from;
			if (from < 0) {
				from = 0;
			}
		}

		to = width;
		start = e = ustr.p + ustr.len;
		p = ustr.p;
		for (;;) {
			UChar32 c;
			if (co == from) {
				start = p;
				to = eawo + len;
				if (to < width) {
					if (to - marker_len < from) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "The width of the trim marker (%d) exceeds the width of the range (%d)", marker_width, to - from);
						goto out;
					}
					to -= marker_len;
					append_marker = TRUE;
				}
			}
		   	if (eawo <= to) {
				end = p;
			}
			if (p >= e) {
				break;
			}
			if (U16_IS_LEAD(*p)) {
				assert(p + 1 < e && U16_IS_TRAIL(*(p + 1)));
				c = U16_GET_SUPPLEMENTARY(*p, *(p + 1));
				p += 2;
			} else {
				c = *p;
				p++;
			}
			co++;
			eawo += _php_mb2_get_uchar_eaw(c, ambiguous_as_half);
		}
	}

	if (!append_marker) {
		char *output;
		int32_t output_len;
		if (SUCCESS == php_mb2_encode(start, end - start, encoding, &output, &output_len, 0 TSRMLS_CC)) {
			RETVAL_STRINGL(output, output_len, 0);
		}
	} else {
		php_mb2_ustring result;
		char *output;
		int32_t output_len;

		if (FAILURE == php_mb2_ustring_ctor(&result, FALSE)) {
			goto out;
		}

		if (FAILURE == php_mb2_ustring_appendu(&result, start, end - start)) {
			php_mb2_ustring_dtor(&result);
			goto out;
		}

		if (FAILURE == php_mb2_ustring_appendu(&result, umarker.p, umarker.len)) {
			php_mb2_ustring_dtor(&result);
			goto out;
		}

		if (SUCCESS == php_mb2_encode(result.p, result.len, encoding, &output, &output_len, 0 TSRMLS_CC)) {
			RETVAL_STRINGL(output, output_len, 0);
		}

		php_mb2_ustring_dtor(&result);
	}

out:
	ucnv_close(conv);
	php_mb2_ustring_dtor(&ustr);
	php_mb2_ustring_dtor(&umarker);
}
/* }}} */

/* {{{ proto string mb_convert_encoding(string str, string to-encoding [, mixed from-encoding])
   Returns converted string in desired encoding */
PHP_MB_FUNCTION(convert_encoding)
{
	char *str;
	char *to_enc;
	int str_len;
	int to_enc_len;
	zval *from_enc;
	php_mb2_char_ptr_list from_encodings;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|z", &str, &str_len, &to_enc, &to_enc_len, &from_enc) == FAILURE) {
		return;
	}

	if (FAILURE == php_mb2_zval_to_encoding_list(from_enc, &from_encodings TSRMLS_CC)) {
		RETURN_FALSE;
	}

	{
		char *result;
		int32_t result_len;
		if (SUCCESS == php_mb2_convert_encoding(str, str_len, to_enc, from_encodings.items, from_encodings.nitems, &result, &result_len, 0 TSRMLS_CC)) {
			RETVAL_STRINGL(result, result_len, 0);
		} else {
			RETVAL_FALSE;
		}
	}

	php_mb2_char_ptr_list_dtor(&from_encodings);
}
/* }}} */

static void _php_mb2_convert_case(int32_t(*case_conv_func)(UChar *, int32_t, const UChar *, int32_t, const char *, UErrorCode *), INTERNAL_FUNCTION_PARAMETERS)
{
	char *str;
	int str_len;
	char *encoding = NULL;
	int encoding_len;
	char *locale = NULL;
	int locale_len;
	int32_t result_len;
	php_mb2_ustring ustr, result;
	UErrorCode err;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s!s!", &str, &str_len,
				&encoding, &encoding_len) == FAILURE) {
		return;
	}

	if (!encoding) {
		encoding = MBSTR_NG(ini).internal_encoding;
	}

	if (!locale) {
		locale = MBSTR_NG(ini).locale;
	}

	if (FAILURE == php_mb2_ustring_ctor_from_n(&ustr, str, str_len, encoding, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert the input to Unicode");
		RETURN_FALSE;
	}

	if (FAILURE == php_mb2_ustring_ctor(&result, 0)) {
		php_mb2_ustring_dtor(&ustr);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the result buffer");
		RETURN_FALSE;
	}

	if (FAILURE == php_mb2_ustring_reserve(&result, ustr.len)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the result buffer");
		RETVAL_FALSE;
		goto out;
	}
	result.len = ustr.len;

	for (;;) {
		err = U_ZERO_ERROR;
		result_len = case_conv_func(result.p, result.len, ustr.p, ustr.len, locale, &err);
		if (U_SUCCESS(err)) {
			result.len = result_len;
			break;
		}
		if (err != U_BUFFER_OVERFLOW_ERROR) {
			RETVAL_FALSE;
			goto out;
		}

		if (FAILURE == php_mb2_ustring_reserve(&result, result.len + 16)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the result buffer");
			RETVAL_FALSE;
			goto out;
		}
		result.len = result.len + 16;
	}

	{
		char *output;
		int32_t output_len;
		if (FAILURE == php_mb2_encode(result.p, result.len, encoding, &output, &output_len, 0 TSRMLS_CC)) {
			RETVAL_FALSE;
		} else {
			RETVAL_STRINGL(output, output_len, 0);
		}
	}

out:
	php_mb2_ustring_dtor(&result);
	php_mb2_ustring_dtor(&ustr);
}

/* {{{ proto string mb_strtoupper(string sourcestring [, string encoding [, string locale]])
 *  Returns a uppercased version of sourcestring
 */
PHP_MB_FUNCTION(strtoupper)
{
	_php_mb2_convert_case(&u_strToUpper, INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

/* {{{ proto string mb_strtolower(string sourcestring [, string encoding [, string locale]])
 *  Returns a lowercased version of sourcestring
 */
PHP_MB_FUNCTION(strtolower)
{
	_php_mb2_convert_case(&u_strToLower, INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

static int32_t _php_mb2_strToTitle_wrapper(UChar *dest, int32_t dest_cap, const UChar *src, int32_t src_len, const char *loc, UErrorCode *err)
{
	return u_strToTitle(dest, dest_cap, src, src_len, NULL, loc, err);
}

/* {{{ proto string mb_strtotitle(string sourcestring [, string encoding [, string locale]])
 *  Returns a titlecased version of sourcestring
 */
PHP_MB_FUNCTION(strtotitle)
{
	_php_mb2_convert_case(&_php_mb2_strToTitle_wrapper, INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

/* {{{ proto string mb_detect_encoding(string str [, mixed encoding_list [, bool strict]])
   Encodings of the given string is returned (as a string) */
PHP_MB_FUNCTION(detect_encoding)
{
	char *str;
	int str_len;
	zend_bool strict=0;
	zval *encoding_list;
	php_mb2_char_ptr_list hints;
	char *retval;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|zb", &str, &str_len, &encoding_list, &strict) == FAILURE) {
		return;
	}

	if (FAILURE == php_mb2_zval_to_encoding_list(encoding_list, &hints TSRMLS_CC)) {
		RETURN_FALSE;
	}

	retval = php_mb2_detect_encoding(str, str_len, hints.items, hints.nitems TSRMLS_CC);
	php_mb2_char_ptr_list_dtor(&hints);
	if (!retval)
		RETURN_FALSE;
	RETURN_STRING(retval, 0);
}
/* }}} */

/* {{{ proto mixed mb_list_encodings()
   Returns an array of all supported entity encodings */
PHP_MB_FUNCTION(list_encodings)
{
	int i, n = ucnv_countAvailable();
	array_init(return_value);
	for (i = 0; i < n; i++) {
		add_next_index_string(return_value, (char *) ucnv_getAvailableName(i), 1);
	}
}
/* }}} */

static void _php_mb2_regex_ereg_exec(INTERNAL_FUNCTION_PARAMETERS, uint32_t flags)
{
	char *pattern;
	int pattern_len;
	char *string;
	int string_len;
	zval **capture = NULL;
	URegularExpression *rex;
	php_mb2_ustring string_u;
	UErrorCode err;
	const char *encoding;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|Z", &pattern, &pattern_len, &string, &string_len, &capture) == FAILURE) {
		return;
	}

	encoding = MBSTR_NG(ini).internal_encoding;

	RETVAL_FALSE;

	rex = php_mb2_regex_open(pattern, pattern_len, encoding, flags TSRMLS_CC);
	if (!rex) {
		return;
	}

	if (FAILURE == php_mb2_ustring_ctor_from_n(&string_u, string, string_len, encoding, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to decode the string as %s", encoding);
		return;
	}

	err = U_ZERO_ERROR;
	uregex_setText(rex, string_u.p, string_u.len, &err);
	if (U_FAILURE(err)) {
		/* unlikely */
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unexpected error (error: %s)", u_errorName(err));
		goto out;
	}

	uregex_useAnchoringBounds(rex, TRUE, &err);
	if (U_FAILURE(err)) {
		/* unlikely */
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unexpected error (error: %s)", u_errorName(err));
		goto out;
	}

	{
		int32_t max_group_idx, i;
		zval _capture;

		UBool matched = uregex_find(rex, -1, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to match the string (error: %s)", u_errorName(err));
			goto out;
		}

		if (!matched) {
			goto out;
		}

		if (!capture) {
			RETVAL_LONG(1);
			goto out;
		}

		_capture = **capture;
		array_init(&_capture);

		max_group_idx = uregex_groupCount(rex, &err);
		if (U_FAILURE(err)) {
			/* unlikely */
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unexpected error (error: %s)", u_errorName(err));
			goto out;
		}

		for (i = 0; i <= max_group_idx; i++) {
			int32_t start, len;
			char *group;
			int32_t group_len;

			start = uregex_start(rex, i, &err);

			if (U_FAILURE(err)) {
				/* unlikely */
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unexpected error (error: %s)", u_errorName(err));
				goto out;
			}

			len = uregex_end(rex, i, &err) - start;
			if (U_FAILURE(err)) {
				/* unlikely */
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unexpected error (error: %s)", u_errorName(err));
				goto out;
			}

			if (i == 0) {
				RETVAL_LONG(len == 0 ? 1: len);
			}

			if (FAILURE == php_mb2_encode(string_u.p + start, len, encoding, &group, &group_len, 0 TSRMLS_CC)) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to encode the capture group #%d", i);
				zval_dtor(&_capture);
				goto out;
			}

			add_index_stringl(&_capture, i, group, group_len, 0);
		}
		zval_dtor(*capture);
		**capture = _capture;
	}

out:
	php_mb2_ustring_dtor(&string_u);
}

/* {{{ proto int mb_ereg(string pattern, string string [, array registers])
   Regular expression match for multibyte string */
PHP_MB_FUNCTION(ereg)
{
	_php_mb2_regex_ereg_exec(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */

/* {{{ proto int mb_eregi(string pattern, string string [, array registers])
   Case-insensitive regular expression match for multibyte string */
PHP_MB_FUNCTION(eregi)
{
	_php_mb2_regex_ereg_exec(INTERNAL_FUNCTION_PARAM_PASSTHRU, UREGEX_CASE_INSENSITIVE);
}
/* }}} */

/* {{{ proto string mb_ereg_replace(string pattern, string replacement, string string [, string option])
   Replace regular expression for multibyte string */
PHP_MB_FUNCTION(ereg_replace)
{
}
/* }}} */

/* {{{ proto string mb_eregi_replace(string pattern, string replacement, string string)
   Case insensitive replace regular expression for multibyte string */
PHP_MB_FUNCTION(eregi_replace)
{
}
/* }}} */

/* {{{ proto array mb_split(string pattern, string string [, int limit])
   split multibyte string into array by regular expression */
PHP_MB_FUNCTION(split)
{
}
/* }}} */

/* {{{ proto bool mb_ereg_match(string pattern, string string [,string option])
   Regular expression match for multibyte string */
PHP_MB_FUNCTION(ereg_match)
{
}
/* }}} */

static void php_mb2_char_ptr_list_ctor(php_mb2_char_ptr_list *list, int persistent)
{
	list->items = NULL;
	list->nitems = 0;
	list->alloc = NULL;
	list->alloc_size = 0;
	list->persistent = persistent;
}

static void php_mb2_char_ptr_list_dtor(php_mb2_char_ptr_list *list)
{
	if (list->items) {
		pefree(list->items, list->persistent);
	}
	if (list->alloc) {
		pefree(list->alloc, list->persistent);
	}
}

static int php_mb2_char_ptr_list_reserve(php_mb2_char_ptr_list *list, size_t new_nitems, size_t new_alloc_size)
{
	const char **new_items;
	char *new_alloc;
	size_t i, n;

	new_items = safe_pemalloc(new_nitems, sizeof(char *), sizeof(char *), list->persistent);
	if (!new_items) {
		return FAILURE;
	}
	new_alloc = safe_pemalloc(new_alloc_size, sizeof(char), sizeof(char), list->persistent);
	if (!new_alloc) {
		pefree(new_items, list->persistent);
		return FAILURE;
	}

	memmove(new_alloc, list->alloc, list->alloc_size);

	for (i = 0, n = list->nitems; i < n; i++) {
		new_items[i] = new_alloc + (list->items[i] - list->alloc);
	}
	new_items[new_nitems] = 0;

	if (list->items) {
		pefree(list->items, list->persistent);
	}
	if (list->alloc) {
		pefree(list->alloc, list->persistent);
	}

	list->items = new_items;
	list->alloc = new_alloc;

	return SUCCESS;
}

static int php_mb2_ustring_ctor(php_mb2_ustring *str, int persistent)
{
	str->p = pemalloc(sizeof(UChar), persistent);
	if (!str->p) {
		return FAILURE;
	}
	str->p[0] = 0;
	str->len = 0;
	str->nalloc = 0;
	str->persistent = persistent;
	return SUCCESS;
}

static int php_mb2_ustring_ctor_from_n(php_mb2_ustring *ustr, const char *val, int32_t len, const char *encoding, int persistent)
{
	UErrorCode err = U_ZERO_ERROR;
	UConverter *conv = NULL;

	conv = ucnv_open(encoding, &err);
	if (!conv) {
		return FAILURE;
	}

	if (FAILURE == php_mb2_ustring_ctor(ustr, persistent)) {
		ucnv_close(conv);
		return FAILURE;
	}

	if (FAILURE == php_mb2_ustring_appendn(ustr, val, len, conv)) {
		goto fail;
	}

	if (FAILURE == php_mb2_ustring_appendn(ustr, NULL, 0, conv)) {
		goto fail;
	}

	ucnv_close(conv);
	return SUCCESS;	
fail:
	if (conv) {
		ucnv_close(conv);
	}
	php_mb2_ustring_dtor(ustr);
	return FAILURE;
}

static void php_mb2_ustring_dtor(php_mb2_ustring *str)
{
	if (str->p) {
		pefree(str->p, str->persistent);
	}
}

static int php_mb2_ustring_reserve(php_mb2_ustring *str, int32_t new_len)
{
	if (new_len < 0) {
		return FAILURE;
	}

	if (new_len >= str->nalloc) {
		UChar *new_p;
		int new_nalloc = str->nalloc > 0 ? str->nalloc: 1;
		do {
			new_nalloc <<= 1;
			if (new_nalloc + 1 < str->nalloc) {
				return FAILURE;
			}
		} while (new_nalloc < new_len);

		new_p = safe_perealloc(str->p, sizeof(UChar), new_nalloc, sizeof(UChar), str->persistent);
		if (!new_p) {
			return FAILURE;
		}

		str->p = new_p;
		str->nalloc = new_nalloc;
	}

	return SUCCESS;
}

static int php_mb2_ustring_appendu(php_mb2_ustring *str, const UChar *ustr, int32_t len)
{
	int32_t new_len = str->len + len;
	if (new_len < str->len) {
		return FAILURE;
	}

	if (FAILURE == php_mb2_ustring_reserve(str, new_len)) {
		return FAILURE;
	}

	memmove(str->p + str->len, ustr, sizeof(UChar) * len);
	str->p[new_len] = 0;
	str->len = new_len;

	return SUCCESS;
}

static int php_mb2_ustring_appendn(php_mb2_ustring *str, const char *src, int32_t len, UConverter *from_conv)
{
	int retval = SUCCESS;
	UChar buf[1024];
	const UChar * const pdl = buf + sizeof(buf) / sizeof(*buf);
	const char * const  psl = src + len;
	UChar *pd;
	const char *ps;
	UErrorCode err;

	if (len == 0) {
		for (pd = buf, ps = src; ps < psl;) {
			err = U_ZERO_ERROR;
			ucnv_toUnicode(from_conv, &pd, pdl, &ps, psl, NULL, 1, &err);
			if (U_SUCCESS(err)) {
				retval = php_mb2_ustring_appendu(str, buf, pd - buf);
				if (FAILURE == retval) {
					return retval;
				}
				break;
			} else if (err == U_BUFFER_OVERFLOW_ERROR) {
				retval = php_mb2_ustring_appendu(str, buf, pd - buf);
				if (FAILURE == retval) {
					return retval;
				}
				pd = buf;
			} else {
				return FAILURE;
			}
		}
	} else {
		for (pd = buf, ps = src; ps < psl;) {
			err = U_ZERO_ERROR;
			ucnv_toUnicode(from_conv, &pd, pdl, &ps, psl, NULL, 0, &err);
			if (U_SUCCESS(err)) {
				retval = php_mb2_ustring_appendu(str, buf, pd - buf);
				if (FAILURE == retval) {
					return retval;
				}
				break;
			} else if (err == U_BUFFER_OVERFLOW_ERROR) {
				retval = php_mb2_ustring_appendu(str, buf, pd - buf);
				if (FAILURE == retval) {
					return retval;
				}
				pd = buf;
			} else {
				return FAILURE;
			}
		}
	}
	return SUCCESS;
}

static const UChar *php_mb2_ustring_offset(const php_mb2_ustring *ustr, int32_t offset)
{
	const UChar *p = ustr->p, *e = ustr->p + ustr->len;
	while (offset > 0) {
		if (p >= e) {
			return NULL;
		}
		if (U16_IS_LEAD(*p)) {
			++p;
			assert(p < e && U16_IS_TRAIL(*p));
			++p;
		} else {
			++p;
		}
		--offset;
	}
	return p;
}

static const UChar *php_mb2_ustring_roffset(const php_mb2_ustring *ustr, int32_t offset)
{
	const UChar *p = ustr->p + ustr->len, *e = ustr->p;
	while (offset > 0) {
		if (p <= e) {
			return NULL;
		}
		--p;
		if (U16_IS_TRAIL(*p)) {
			assert(p > e);
			--p;
		  	assert(U16_IS_LEAD(*--p));
		}
		--offset;
	}
	return p;
}

static void php_mb2_uconverter_from_unicode_callback(const void *_ctx, UConverterFromUnicodeArgs *args, UChar *units, int32_t length, UChar32 codePoint, UConverterCallbackReason reason, UErrorCode *err)
{
	php_mb2_uconverter_callback_ctx *ctx = (void *)_ctx;
#ifdef ZTS
	TSRMLS_D = ctx.TSRMLS_C;
#endif
	switch (reason) {
	case UCNV_UNASSIGNED:
		php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Unrepresentable character U+%06x", codePoint);
		if (ctx->unassigned_subst_str_u && !MBSTR_NG(runtime).in_ucnv_error_handler) {
			const UChar *ps = ctx->unassigned_subst_str_u, *psl = ctx->unassigned_subst_str_u + ctx->unassigned_subst_str_u_len;
			MBSTR_NG(runtime).in_ucnv_error_handler = TRUE;
			for (;;) {
				size_t new_dbuf_size;
				char *new_dbuf;

				*err = U_ZERO_ERROR;
				ucnv_fromUnicode(args->converter, &args->target, args->targetLimit, &ps, psl, NULL, TRUE, err);
				if (*err != U_BUFFER_OVERFLOW_ERROR) {
					break;
				}

				assert(args->targetLimit == ctx->pdl);
				new_dbuf_size = (ctx->pdl - ctx->dbuf) << 1;
				if (new_dbuf_size + 1 < ctx->pdl - ctx->dbuf || !(new_dbuf = perealloc(ctx->dbuf, new_dbuf_size + 1, ctx->persistent))) {
					*err = U_MEMORY_ALLOCATION_ERROR;
					break;
				}
				args->target = new_dbuf + (args->target - ctx->dbuf);
				ctx->dbuf = new_dbuf;
				ctx->pdl = new_dbuf + new_dbuf_size;
			}
			MBSTR_NG(runtime).in_ucnv_error_handler = FALSE;
		}
		*err = U_ZERO_ERROR;
		break;
	case UCNV_IRREGULAR:
	case UCNV_ILLEGAL:
		if (!MBSTR_NG(runtime).in_ucnv_error_handler) {
			const UChar *ps = ctx->illegal_subst_str_u, *psl = ctx->illegal_subst_str_u + ctx->illegal_subst_str_u_len;
			MBSTR_NG(runtime).in_ucnv_error_handler = TRUE;
			for (;;) {
				size_t new_dbuf_size;
				char *new_dbuf;

				*err = U_ZERO_ERROR;
				ucnv_fromUnicode(args->converter, &args->target, args->targetLimit, &ps, psl, NULL, TRUE, err);
				if (*err != U_BUFFER_OVERFLOW_ERROR) {
					break;
				}

				assert(args->targetLimit == ctx->pdl);
				new_dbuf_size = (ctx->pdl - ctx->dbuf) << 1;
				if (new_dbuf_size + 1 < ctx->pdl - ctx->dbuf || !(new_dbuf = perealloc(ctx->dbuf, new_dbuf_size + 1, ctx->persistent))) {
					*err = U_MEMORY_ALLOCATION_ERROR;
					break;
				}
				args->target = new_dbuf + (args->target - ctx->dbuf);
				ctx->dbuf = new_dbuf;
				ctx->pdl = new_dbuf + new_dbuf_size;
			}
			MBSTR_NG(runtime).in_ucnv_error_handler = FALSE;
		}
		break;
	default:
		break;
	}
}

static void php_mb2_uconverter_to_unicode_callback(const void *_ctx, UConverterToUnicodeArgs *args, const char *units, int32_t length, UConverterCallbackReason reason, UErrorCode *err)
{
	php_mb2_uconverter_callback_ctx *ctx = (void *)_ctx;
#ifdef ZTS
	TSRMLS_D = ctx.TSRMLS_C;
#endif
	switch (reason) {
	case UCNV_UNASSIGNED:
	case UCNV_IRREGULAR:
	case UCNV_ILLEGAL:
		if (args->targetLimit - args->target < ctx->illegal_subst_str_u_len) {
			args->targetLimit = args->target + ctx->illegal_subst_str_u_len;
		}
		memmove(args->target, ctx->illegal_subst_str_u, sizeof(UChar) * ctx->illegal_subst_str_u_len);
		args->target += ctx->illegal_subst_str_u_len;
		*err = U_ZERO_ERROR;
		break;
	default:
		break;
	}
}

static int php_mb2_convert_encoding(const char *input, int32_t length, const char *to_encoding, const char * const *from_encodings, size_t num_from_encodings, char **output, int32_t *output_len, int persistent TSRMLS_DC)
{
	static const int32_t pvbuf_basic_len = 1024;
	UErrorCode err = U_ZERO_ERROR;
	const char * const*from_encoding, * const*e;
	UConverter *to_conv = NULL, *from_conv = NULL;
	UChar *pvbuf;
	UChar *ppvs, *ppvd;
	char *pd;
	const char *ps, *psl;
	int use_heap = 0;

	php_mb2_uconverter_callback_ctx ctx;
	ctx.dbuf = NULL;
	ctx.persistent = persistent;
	ctx.unassigned_subst_str_u = MBSTR_NG(ini).subst_string_unassigned.p;
	ctx.unassigned_subst_str_u_len = MBSTR_NG(ini).subst_string_unassigned.len;
	ctx.illegal_subst_str_u = MBSTR_NG(ini).subst_string_illegal.p;
	ctx.illegal_subst_str_u_len = MBSTR_NG(ini).subst_string_illegal.len;
#ifdef ZTS
	ctx.TSRMLS_C = TSRMLS_C;
#endif
	MBSTR_NG(runtime).in_ucnv_error_handler = FALSE;

	if (pvbuf_basic_len + ctx.unassigned_subst_str_u_len < pvbuf_basic_len || sizeof(UChar) * (pvbuf_basic_len + ctx.unassigned_subst_str_u_len) / sizeof(UChar) != pvbuf_basic_len + ctx.unassigned_subst_str_u_len) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate temporary buffer");
		return FAILURE;
	}
	pvbuf = do_alloca_ex(sizeof(UChar) * (pvbuf_basic_len + ctx.unassigned_subst_str_u_len), 4096, use_heap);

	to_conv = ucnv_open(to_encoding, &err);
	if (!to_conv) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to create the encoder for %s (error: %s)", to_encoding, u_errorName(err));
		goto fail;
	}

	ucnv_setFromUCallBack(to_conv, (UConverterFromUCallback)php_mb2_uconverter_from_unicode_callback, &ctx, NULL, NULL, &err);
	if (U_FAILURE(err)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to set the callback for the encoder (error: %s)", u_errorName(err));
		goto fail;
	}

	ctx.dbuf = pemalloc(length + 1, persistent);
	if (!ctx.dbuf)
		goto fail;

	psl = input + length;
	ctx.pdl = ctx.dbuf + length;
	if (ctx.pdl < ctx.dbuf) {
		goto fail;
	}

	for (from_encoding = from_encodings, e = from_encodings + num_from_encodings; from_encoding < e; from_encoding++) {
		err = U_ZERO_ERROR;
		from_conv = ucnv_open(*from_encoding, &err);
		if (!from_conv) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to create the decoder for %s (error: %s)", *from_encoding, u_errorName(err));
			goto fail;
		}

		ucnv_setToUCallBack(from_conv, (UConverterToUCallback)php_mb2_uconverter_to_unicode_callback, &ctx, NULL, NULL, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to set the callback to the decoder (error: %s)", u_errorName(err));
			goto fail;
		}

		ps = input;
		pd = ctx.dbuf;

		ppvs = ppvd = pvbuf;
		ucnv_convertEx(to_conv, from_conv, &pd, ctx.pdl, &ps, psl, pvbuf, &ppvs, &ppvd, pvbuf + pvbuf_basic_len, TRUE, FALSE, &err);
		while (err == U_BUFFER_OVERFLOW_ERROR) {
			int32_t new_dbuf_size;
			char *new_dbuf;
			new_dbuf_size = (ctx.pdl - ctx.dbuf) << 1;
			if (new_dbuf_size + 1 < ctx.pdl - ctx.dbuf || !(new_dbuf = perealloc(ctx.dbuf, new_dbuf_size + 1, persistent))) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the buffer for conversion results");
				goto fail;
			}
			pd = new_dbuf + (pd - ctx.dbuf);
			ctx.dbuf = new_dbuf;
			ctx.pdl = new_dbuf + new_dbuf_size;

			err = U_ZERO_ERROR;
			ucnv_convertEx(to_conv, from_conv, &pd, ctx.pdl, &ps, psl, pvbuf, &ppvs, &ppvd, pvbuf + pvbuf_basic_len, FALSE, FALSE, &err);
		}
		if (U_SUCCESS(err)) {
			for (;;) {
				int32_t new_dbuf_size;
				char *new_dbuf;

				err = U_ZERO_ERROR;
				ucnv_convertEx(to_conv, from_conv, &pd, ctx.pdl, &ps, psl, pvbuf, &ppvs, &ppvd, pvbuf + pvbuf_basic_len, FALSE, TRUE, &err);
				if (U_SUCCESS(err) || err != U_BUFFER_OVERFLOW_ERROR) {
					break;
				}

				new_dbuf_size = (ctx.pdl - ctx.dbuf) << 1;
				if (new_dbuf_size + 1 < ctx.pdl - ctx.dbuf || !(new_dbuf = perealloc(ctx.dbuf, new_dbuf_size + 1, persistent))) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the buffer for conversion results");
					goto fail;
				}
				pd = new_dbuf + (pd - ctx.dbuf);
				ctx.dbuf = new_dbuf;
				ctx.pdl = new_dbuf + new_dbuf_size;
			}
		}

		ucnv_close(from_conv);
		from_conv = NULL;

		if (U_SUCCESS(err)) {
			break;
		}
		if (err != U_ILLEGAL_CHAR_FOUND && err != U_ILLEGAL_ESCAPE_SEQUENCE && err != U_TRUNCATED_CHAR_FOUND && err != U_UNSUPPORTED_ESCAPE_SEQUENCE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert from %s to %s (error: %s)", *from_encoding, to_encoding, u_errorName(err));
			goto fail;
		}
	}

	if (from_encoding == e) {
		if (num_from_encodings == 1) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert from %s to %s (error: %s)", *from_encodings, to_encoding, u_errorName(err));
		} else {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "No suitable converter ifound (last error: %s)", u_errorName(err));
		}
		goto fail;
	}

	*pd = '\0';

	*output = ctx.dbuf;
	*output_len = pd - ctx.dbuf; 

	if (to_conv) {
		ucnv_close(to_conv);
	}

	free_alloca(pvbuf, use_heap);
	return SUCCESS;

fail:
	free_alloca(pvbuf, use_heap);
	if (ctx.dbuf) {
		pefree(ctx.dbuf, persistent);
	}
	if (to_conv) {
		ucnv_close(to_conv);
	}
	if (from_conv) {
		ucnv_close(from_conv);
	}
	return FAILURE;
}

static int php_mb2_encode(const UChar *input, int32_t length, const char *to_encoding, char **output, int32_t *output_len, int persistent TSRMLS_DC)
{
	UErrorCode err = U_ZERO_ERROR;
	UConverter *to_conv = NULL;
	char *pd;
	const UChar *ps, *psl;
	int use_heap = 0;

	php_mb2_uconverter_callback_ctx ctx;
	ctx.dbuf = NULL;
	ctx.persistent = persistent;
	ctx.unassigned_subst_str_u = MBSTR_NG(ini).subst_string_unassigned.p;
	ctx.unassigned_subst_str_u_len = MBSTR_NG(ini).subst_string_unassigned.len;
	ctx.illegal_subst_str_u = MBSTR_NG(ini).subst_string_illegal.p;
	ctx.illegal_subst_str_u_len = MBSTR_NG(ini).subst_string_illegal.len;
#ifdef ZTS
	TSRMLS_C = TSRMLS_C;
#endif
	MBSTR_NG(runtime).in_ucnv_error_handler = FALSE;

	to_conv = ucnv_open(to_encoding, &err);
	if (!to_conv) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to create the encoder for %s (error: %s)", to_encoding, u_errorName(err));
		goto fail;
	}

	ucnv_setFromUCallBack(to_conv, (UConverterFromUCallback)php_mb2_uconverter_from_unicode_callback, &ctx, NULL, NULL, &err);
	if (U_FAILURE(err)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to set the callback for the encoder (error: %s)", u_errorName(err));
		goto fail;
	}

	ctx.dbuf = pemalloc(length + 1, persistent);
	if (!ctx.dbuf)
		goto fail;

	psl = input + length;
	ctx.pdl = ctx.dbuf + length;
	if (ctx.pdl < ctx.dbuf) {
		goto fail;
	}

	ps = input;
	pd = ctx.dbuf;

	for (;;) {
		int32_t new_dbuf_size;
		char *new_dbuf;

		err = U_ZERO_ERROR;
		ucnv_fromUnicode(to_conv, &pd, ctx.pdl, &ps, psl, NULL, FALSE, &err);
		if (U_SUCCESS(err) || err != U_BUFFER_OVERFLOW_ERROR) {
			break;
		}

		new_dbuf_size = (ctx.pdl - ctx.dbuf) << 1;
		if (new_dbuf_size + 1 < ctx.pdl - ctx.dbuf || !(new_dbuf = perealloc(ctx.dbuf, new_dbuf_size + 1, persistent))) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the buffer for conversion results");
			goto fail;
		}
		pd = new_dbuf + (pd - ctx.dbuf);
		ctx.dbuf = new_dbuf;
		ctx.pdl = new_dbuf + new_dbuf_size;
	}
	if (U_SUCCESS(err)) {
		for (;;) {
			int32_t new_dbuf_size;
			char *new_dbuf;

			err = U_ZERO_ERROR;
			ucnv_fromUnicode(to_conv, &pd, ctx.pdl, &ps, psl, NULL, TRUE, &err);
			if (U_SUCCESS(err) || err != U_BUFFER_OVERFLOW_ERROR) {
				break;
			}

			new_dbuf_size = (ctx.pdl - ctx.dbuf) << 1;
			if (new_dbuf_size + 1 < ctx.pdl - ctx.dbuf || !(new_dbuf = perealloc(ctx.dbuf, new_dbuf_size + 1, persistent))) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate the buffer for conversion results");
				goto fail;
			}
			pd = new_dbuf + (pd - ctx.dbuf);
			ctx.dbuf = new_dbuf;
			ctx.pdl = new_dbuf + new_dbuf_size;
		}
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to encode the Unicode string to %s (error: %s)", to_encoding, u_errorName(err));
		goto fail;
	}

	*pd = '\0';

	*output = ctx.dbuf;
	*output_len = pd - ctx.dbuf; 

	if (to_conv) {
		ucnv_close(to_conv);
	}

	return SUCCESS;

fail:
	if (ctx.dbuf) {
		pefree(ctx.dbuf, persistent);
	}
	if (to_conv) {
		ucnv_close(to_conv);
	}
	return FAILURE;
}


static int php_mb2_int32_data_compare(const HashPosition *a, const HashPosition *b TSRMLS_DC)
{
	const int32_t av = *(int32_t *)((*a)->pData), bv = *(int32_t *)((*b)->pData);
	return av > bv ? -1: av < bv ? 1: 0;
}

static char *php_mb2_detect_encoding(const char *input, int32_t length, const char * const *hint_encodings, size_t num_hint_encodings TSRMLS_DC)
{
	UErrorCode err = U_ZERO_ERROR;
	UCharsetDetector *det;
	const char * const *encoding, * const *e;
	char *retval = NULL;
	HashTable score_table;

	if ((int32_t)length < 0) {
		return NULL;
	}

	if (FAILURE == zend_hash_init(&score_table, sizeof(int32_t), NULL, NULL, FALSE)) {
		return NULL;
	}

	det = ucsdet_open(&err);
	if (!det) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to create charset detector (error: %s)", u_errorName(err));
		zend_hash_destroy(&score_table);
		return NULL;
	}

	ucsdet_setText(det, input, length, &err);
	if (U_FAILURE(err)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to feed the input string to the detector (error: %s)", u_errorName(err));
		goto out;
	}

	if (num_hint_encodings > 0) {
		HashPosition pos;
		const char *name;
		uint name_len;

		for (encoding = hint_encodings, e = hint_encodings + num_hint_encodings;
				encoding < e; encoding++) {
			int32_t i, nmatches;
			const UCharsetMatch **matches;

			ucsdet_setDeclaredEncoding(det, *encoding, strlen(*encoding), &err);
			if (U_FAILURE(err)) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to give a hint to the detector (error: %s)", u_errorName(err));
				goto out;
			}

			matches = ucsdet_detectAll(det, &nmatches, &err);
			if (U_FAILURE(err)) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "An error occurred during detecting the encoding (error: %s)", u_errorName(err));
				goto out;
			}

			for (i = 0; i < nmatches; i++) {
				int32_t score, *prev_value;
				ulong name_h;

				name = ucsdet_getName(matches[i], &err);
				if (U_FAILURE(err)) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "An error occurred during detecting the encoding (error: %s)", u_errorName(err));
					goto out;
				}
				score = ucsdet_getConfidence(matches[i], &err);
				if (U_FAILURE(err)) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "An error occurred during detecting the encoding (error: %s)", u_errorName(err));
					goto out;
				}
				name_len = strlen(name) + 1;
				name_h = zend_inline_hash_func(name, name_len);
				if (FAILURE == zend_hash_quick_find(&score_table, name, name_len, name_h, (void **)&prev_value)) {
					zend_hash_quick_add(&score_table, name, name_len, name_h, &score, sizeof(score), NULL);
				} else {
					*prev_value += score;
				}
			}
		}

		zend_hash_sort(&score_table, zend_qsort, (compare_func_t)php_mb2_int32_data_compare, 0 TSRMLS_CC);

		zend_hash_internal_pointer_reset_ex(&score_table, &pos);
		assert(HASH_KEY_NON_EXISTANT != zend_hash_get_current_key_ex(&score_table, (char **)&name, &name_len, NULL, FALSE, &pos));
		retval = estrndup(name, name_len - 1);
	} else {
		const char *name;
		const UCharsetMatch *match;
		
		match = ucsdet_detect(det, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "An error occurred during detecting the encoding (error: %s)", u_errorName(err));
			goto out;
		}

		name = ucsdet_getName(match, &err);
		if (U_FAILURE(err)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "An error occurred during detecting the encoding (error: %s)", u_errorName(err));
			goto out;
		}

		retval = estrdup(name);
	}

out:
	zend_hash_destroy(&score_table);
	if (det) {
		ucsdet_close(det);
	}
	return retval;
}

static int php_mb2_parse_encoding_list(const char *value, int32_t value_length, php_mb2_char_ptr_list *pretval, int persistent)
{
	int n;
	const char *endp, *startp;
	php_mb2_char_ptr_list retval; 

	php_mb2_char_ptr_list_ctor(&retval, persistent);

	startp = value;
	endp = value + value_length;

	n = 1;
	{
		const char *p = startp;
		while ((p = php_memnstr((char*)p, ",", 1, (char*)endp)) != NULL) {
			p++;
			n++;
		}
	}

	retval.items = safe_pemalloc(n, sizeof(char *), sizeof(char *), persistent);
	if (!retval.items) {
		return FAILURE;
	}

	retval.alloc = safe_pemalloc(value_length, sizeof(char), sizeof(char), persistent);
	if (!retval.alloc) {
		pefree(retval.items, persistent);
		return FAILURE;
	}
	retval.alloc_size = value_length;

	{
		char *pa = retval.alloc;
		const char *p1, *p2;
		const char **entry = retval.items;
		int bauto = FALSE;

		n = 0;
		bauto = 0;
		p1 = startp;

		while (p1 < endp) {
			const char *pn = php_memnstr((char*)p1, ",", 1, (char*)endp);
			if (pn == NULL) {
				pn = endp;
			}

			/* trim spaces */
			while (p1 < p2 && (*p1 == ' ' || *p1 == '\t')) {
				p1++;
			}

			p2 = pn;
			do {
				--p2;
			} while (p2 > p1 && (*p2 == ' ' || *p2 == '\t'));
			++p2;

			{
				size_t l = p2 - p1;
				if (l > 0) {
					memmove(pa, p1, l);
					*(pa + l) = '\0';
					*entry++ = pa;
					pa += l + 1;
					retval.nitems++;
				}
			}
			p1 = pn + 1;
		}
		*entry = NULL;
	}

	*pretval = retval;
	return SUCCESS;
}

static int php_mb2_zval_to_encoding_list(zval *repr, php_mb2_char_ptr_list *pretval TSRMLS_DC)
{
	php_mb2_char_ptr_list retval;

	if (Z_TYPE_P(repr) == IS_STRING) {
		if (FAILURE == php_mb2_parse_encoding_list(Z_STRVAL_P(repr), Z_STRLEN_P(repr), &retval, FALSE)) {
			return FAILURE;
		}
	} else if (Z_TYPE_P(repr) == IS_ARRAY) {
		HashTable *ht = Z_ARRVAL_P(repr);
		HashPosition p;
		zval **data;
		size_t i;
		char *pa;
		const char **entry;

		php_mb2_char_ptr_list_ctor(&retval, 0);

		retval.items = safe_pemalloc(ht->nNumOfElements, sizeof(char *), sizeof(char *), retval.persistent);
		if (!retval.items) {
			return FAILURE;
		}
		retval.nitems = ht->nNumOfElements;

		retval.alloc_size = 0;
		for (zend_hash_internal_pointer_reset_ex(ht, &p);
				SUCCESS == zend_hash_get_current_data_ex(ht, (void**)&data, &p);
				zend_hash_move_forward_ex(ht, &p)) {
			if (Z_TYPE_PP(data) != IS_STRING) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Every element of the array must be a string");
				php_mb2_char_ptr_list_dtor(&retval);
				return FAILURE;
			}
			retval.alloc_size += Z_STRLEN_PP(data) + 1;
			if (retval.alloc_size < Z_STRLEN_PP(data) + 1) {
				php_mb2_char_ptr_list_dtor(&retval);
				return FAILURE;
			}
		}

		retval.alloc = safe_pemalloc(retval.alloc_size, sizeof(char), sizeof(char), retval.persistent);
		if (!retval.alloc) {
			php_mb2_char_ptr_list_dtor(&retval);
			return FAILURE;
		}

		pa = retval.alloc;
		entry = retval.items;
		for (zend_hash_internal_pointer_reset_ex(ht, &p);
				SUCCESS == zend_hash_get_current_data_ex(ht, (void**)&data, &p);
				zend_hash_move_forward_ex(ht, &p)) {
			size_t l = Z_STRLEN_PP(data) + 1;
			assert(Z_TYPE_PP(data) == IS_STRING);
			memmove(pa, Z_STRVAL_PP(data), l);
			*entry++ = pa;	
			pa += l;
		}
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Type of the encoding list must be either array or string");
		return FAILURE;
	}
	{
		size_t i, j;
		for (i = 0; i < retval.nitems; i++) {
			if (strcasecmp(retval.items[i], "auto") == 0) {
				char *pa;
				php_mb2_char_ptr_list *detect_order = &MBSTR_NG(ini).detect_order;
				size_t new_nitems = retval.nitems + detect_order->nitems;
				size_t new_alloc_size = retval.alloc_size + detect_order->alloc_size;
				if (new_nitems < retval.nitems || new_alloc_size < retval.alloc_size) {
					php_mb2_char_ptr_list_dtor(&retval);
					return FAILURE;
				}

				if (FAILURE == php_mb2_char_ptr_list_reserve(&retval, new_nitems, new_alloc_size)) {
					php_mb2_char_ptr_list_dtor(&retval);
					return FAILURE;
				}
				memmove(&retval.items[i + 1] + detect_order->nitems, &retval.items[i + 1], sizeof(char *) * (retval.nitems - i - 1));
				pa = retval.alloc + retval.alloc_size;
				for (j = 0; j < detect_order->nitems; j++) {
					size_t l = strlen(detect_order->items[j]) + 1;
					memmove(pa, detect_order->items[j], l);
					retval.items[i + j] = pa;
					pa += l;
				}
				retval.nitems = new_nitems;
				retval.alloc_size = pa - retval.alloc;
				i += detect_order->nitems;
				--i;
			}
		}
	}

	*pretval = retval;
	return SUCCESS;
}

static int php_mb2_get_eaw(const UChar *str, int32_t len, zend_bool ambiguous_as_half, int *retval)
{
	UErrorCode err;
	const UChar *p, *e;
	int _retval = 0;

	if (ambiguous_as_half) {
		ambiguous_as_half = 1;
	}

	for (p = str, e = str + len; p < e; ) {
		UChar32 c;
		if (U16_IS_LEAD(*p)) {
			if (p + 1 >= e || U16_IS_TRAIL(*(p + 1))) {
				return FAILURE;
			}
			c = U16_GET_SUPPLEMENTARY(*p, *(p + 1));
			p += 2;
		} else {
			c = *p;
			p++;
		}
		_retval += _php_mb2_get_uchar_eaw(c, ambiguous_as_half);
	}

	*retval = _retval;
	return SUCCESS;
}

static unsigned int php_mb2_sapi_filter(int arg, char *var, char **val, unsigned int val_len, unsigned int *new_val_len TSRMLS_DC)
{
	char *new_val;
	int32_t _new_val_len;

	*new_val_len = val_len;

	if (!MBSTR_NG(ini).encoding_translation) {
		return 1;
	}

	if (MBSTR_NG(ini).http_input.nitems == 0 || strcasecmp(MBSTR_NG(ini).http_input.items[0], "pass") == 0) {
		return 1;
	}

	if (FAILURE == php_mb2_convert_encoding(*val, val_len, MBSTR_NG(ini).internal_encoding, MBSTR_NG(ini).http_input.items, MBSTR_NG(ini).http_input.nitems, &new_val, &_new_val_len, 0 TSRMLS_CC)) {
		return 0;
	}

	efree(*val);
	if (php_mb2_next_input_filter) {
		char *__new_val = new_val;
		unsigned int __new_val_len;
		if (php_mb2_next_input_filter(arg, var, &__new_val, _new_val_len, &__new_val_len TSRMLS_CC)) {
			new_val = __new_val;
			_new_val_len = __new_val_len;
		}
	}
	*new_val_len = _new_val_len;
	*val = new_val;
	return 1;
}

static unsigned int php_mb2_sapi_filter_init(TSRMLS_D)
{
	if (php_mb2_next_input_filter_init) {
		php_mb2_next_input_filter_init(TSRMLS_C);
	}
}

static php_mb2_register_sapi_filter(TSRMLS_C)
{
	php_mb2_next_input_filter_init = sapi_module.input_filter_init;
	php_mb2_next_input_filter = sapi_module.input_filter;
	sapi_register_input_filter(php_mb2_sapi_filter, php_mb2_sapi_filter_init);
}

static php_mb2_unregister_sapi_filter(TSRMLS_C)
{
	sapi_module.input_filter_init = php_mb2_next_input_filter_init;
	sapi_module.input_filter = php_mb2_next_input_filter;
}

static void php_mb2_mime_type_buf_dtor(php_mb2_mime_type_buf *buf)
{
	if (buf->charset) {
		efree(buf->charset);
	}
	if (buf->type) {
		efree(buf->type);
	}
}

static int php_mb2_parse_mime_type(php_mb2_mime_type_buf *retval, const char *header, size_t header_len)
{
	const char *p = memchr(header, ';', header_len), *pe = header + header_len;
	{
		const char *pl = p == NULL ? pe: p;
		while (pl > header) {
			pl--;
			if (*pl != ' ' && *pl != '\t') {
				pl++;
				break;
			}
		}
		retval->type_len = pl - header;
		retval->type = estrndup(header, pl - header);
	}
	retval->charset = NULL;
	retval->charset_len = 0;
	if (p == NULL) {
		goto done;
	}
	p++;
	while (p < pe) {
		char *value;
		size_t value_len;
		const char *pks, *pke;
		for (;;) {
			if (p == pe) {
				goto done;
			}
			if (*p != ' ' && *p != '\t') {
				break;
			}
			p++;
		}
		pks = p;
		for (;;) {
			if (p == pe) {
				goto done;
			}
			if (*p == '=') {
				break;
			}
			p++;
		}
		{
			pke = p;
			while (pke > header) {
				pke--;
				if (*pke != ' ' && *pke != '\t') {
					pke++;
					break;
				}
			}
		}
		for (;;) {
			p++;
			if (p == pe) {
				goto done;
			}
			if (*p != ' ' && *p != '\t') {
				break;
			}
		}
		if (*p == '"') {
			const char *pvs = ++p;
			for (;;) {
				if (p == pe) {
					return FAILURE;
				}
				if (*p == '"') {
					break;
				}
				if (*p == '\\') {
					p++;
					if (p == pe) {
						return FAILURE;
					}
				}
				p++;
			}
			value = emalloc(p - pvs + 1);
			{
				char *pv = value;
				while (pvs < p) {
					if (*p == '\\') {
						p++;
						assert(p < pvs);
					}
					*pv++ = *p++;
				}
				*pv = '\0';
				value_len = pv - value;
			}
			p++;
		} else {
			const char *pvs = p;
			for (;;) {
				p++;
				if (p == pe || *p == ';' || *p == ' ' || *p == '\t') {
					break;
				}
			}
			value = estrndup(pvs, p - pvs);
			value_len = p - pvs;
		}

		if (pke - pks == sizeof("charset") - 1 && memcmp("charset", pks, sizeof("charset") - 1) == 0) {
			retval->charset = value;
			retval->charset_len = value_len;
		} else {
			efree(value);
		}

		for (;;) {
			if (p == pe) {
				goto done;
			}
			if (*p != ' ' && *p != '\t') {
				break;
			}
			p++;
		}
		if (*p != ';') {
			efree(value);
			return FAILURE;
		}
		p++;
	}
done:
	return SUCCESS;
}

static void php_mb2_zend_hash_remove_first(HashTable *ht)
{
	Bucket *list_head = ht->pListHead;
	uint bucket_idx = list_head->h & ht->nTableMask;
	HANDLE_BLOCK_INTERRUPTIONS();
	if (ht->arBuckets[bucket_idx] == list_head) {
		ht->arBuckets[bucket_idx] = list_head->pNext;
	} else {
		list_head->pLast->pNext = list_head->pNext;
	}
	if (list_head->pNext) {
		list_head->pNext->pLast = list_head->pLast;
	}
	if (list_head->pListLast != NULL) {
		list_head->pListLast->pListNext = list_head->pListNext;
	} else { 
		ht->pListHead = list_head->pListNext;
	}
	if (list_head->pListNext != NULL) {
		list_head->pListNext->pListLast = list_head->pListLast;
	} else {
		ht->pListTail = list_head->pListLast;
	}
	if (ht->pInternalPointer == list_head) {
		ht->pInternalPointer = list_head->pListNext;
	}
	if (ht->pDestructor) {
		ht->pDestructor(list_head->pData);
	}
	if (list_head->pData != &list_head->pDataPtr) {
		pefree(list_head->pData, ht->persistent);
	}
	pefree(list_head, ht->persistent);
	HANDLE_UNBLOCK_INTERRUPTIONS();
	ht->nNumOfElements--;
}

static URegularExpression *php_mb2_regex_open(const char *pattern, int32_t pattern_len, const char *encoding, uint32_t flags TSRMLS_DC)
{
	php_mb2_ustring pattern_u;
	ulong pattern_hash;
	URegularExpression *retval = NULL, **tmp_rex;
	HashTable *regex_cache = &MBSTR_NG(runtime).regex_cache;
	zend_bool need_update_cache = FALSE;

	if (FAILURE == php_mb2_ustring_ctor_from_n(&pattern_u, pattern, pattern_len, encoding, 0)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert pattern string to Unicode");
		return NULL;
	}

	pattern_hash = zend_inline_hash_func((const char *)pattern_u.p, (uint)pattern_u.len);
	{
		UErrorCode err = U_ZERO_ERROR;
		need_update_cache = (FAILURE == zend_hash_quick_find(regex_cache, (const char *)pattern_u.p, (uint)pattern_u.len, pattern_hash, (void **)&tmp_rex)) || (uregex_flags(*tmp_rex, &err) != flags || U_FAILURE(err));
	}

	if (need_update_cache) {
		UErrorCode err = U_ZERO_ERROR;
		UParseError parse_err;
		retval = uregex_open(pattern_u.p, pattern_u.len, flags, &parse_err, &err);
		if (U_FAILURE(err)) {
			zend_bool use_heap;
			char *tmp = do_alloca(pattern_len + 1, use_heap);
			memcpy(tmp, pattern, pattern_len);
			tmp[pattern_len] = '\0';
			if (err >= U_REGEX_ERROR_START && err < U_REGEX_ERROR_LIMIT) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to parse pattern string \"%s\" (offset: %d, error: %s)", tmp, parse_err.offset, u_errorName(err));
			} else {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to compile pattern string \"%s\" (error: %s)", tmp, u_errorName(err));
			}
			free_alloca(tmp, use_heap);
			retval = NULL;
			goto out;
		}
		if (regex_cache->nNumOfElements >= PHP_MBREGEX_MAXCACHE) {
			/* LRU cache */
			php_mb2_zend_hash_remove_first(regex_cache);
		}
		zend_hash_quick_update(regex_cache, (const char *)pattern_u.p, (uint)pattern_u.len, pattern_hash, &retval, sizeof(retval), NULL);
	} else {
		retval = *tmp_rex;
	}
out:
	php_mb2_ustring_dtor(&pattern_u);
	return retval;
}

#endif	/* HAVE_MBSTRING */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: fdm=marker
 * vim: noet sw=4 ts=4
 */
