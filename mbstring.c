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
   | Author: Tsukada Takuya <tsukada@fminn.nagano.nagano.jp>              |
   |         Rui Hirokawa <hirokawa@php.net>                              |
   |         Moriyoshi Koizumi <moriyoshi@php.net>                        |
   +----------------------------------------------------------------------+
 */

/* $Id: mbstring.c,v 1.224.2.22.2.25.2.55 2009/05/27 13:42:17 tony2001 Exp $ */

/* {{{ includes */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unicode/ucnv.h>
#include <unicode/ucsdet.h>

#include "php.h"
#include "php_ini.h"
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
#define STD_PHP_MB_INI_ENTRY(name, default_value, flags, handler, field, type, global) \
	STD_PHP_INI_ENTRY("mbstring2." name, default_value, flags, handler, field, type, global)
#define STD_PHP_MB_INI_BOOLEAN(name, default_value, flags, handler, field, type, global) \
	STD_PHP_INI_BOOLEAN("mbstring2." name, default_value, flags, handler, field, type, global)

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

static PHP_MB_FUNCTION(strtoupper);
static PHP_MB_FUNCTION(strtolower);
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

static PHP_MB_FUNCTION(regex_encoding);
static PHP_MB_FUNCTION(ereg);
static PHP_MB_FUNCTION(eregi);
static PHP_MB_FUNCTION(ereg_replace);
static PHP_MB_FUNCTION(eregi_replace);
static PHP_MB_FUNCTION(split);
static PHP_MB_FUNCTION(ereg_match);
static PHP_MB_FUNCTION(regex_set_options);

static void php_mb2_char_ptr_list_ctor(php_mb2_char_ptr_list *list, int persistent);
static void php_mb2_char_ptr_list_dtor(php_mb2_char_ptr_list *list);
static int php_mb2_char_ptr_list_reserve(php_mb2_char_ptr_list *list, size_t nitems_grow, size_t alloc_size_grow);

static void php_mb2_regex_free_cache(php_mb_regex_t **pre);
static int php_mb2_parse_encoding_list(const char *value, size_t value_length, php_mb2_char_ptr_list *pretval, int persistent);
static int php_mb2_zval_to_encoding_list(zval *repr, php_mb2_char_ptr_list *pretval TSRMLS_DC);
static int php_mb2_convert_encoding(const char *input, size_t length, const char *to_encoding, const char * const *from_encodings, size_t num_from_encodings, char **output, size_t *output_len, int persistent TSRMLS_DC);
static char *php_mb2_detect_encoding(const char *input, size_t length, const char * const *from_encodings, size_t num_from_encodings TSRMLS_DC);
static int php_mb2_ustring_ctor(php_mb2_ustring *str, int persistent);
static int php_mb2_ustring_appendu(php_mb2_ustring *, const UChar *ustr, int32_t len);
static int php_mb2_ustring_appendn(php_mb2_ustring *, const char *str, int32_t len, UConverter *from_conv);
static void php_mb2_ustring_dtor(php_mb2_ustring *str);

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
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_strtolower, 0, 0, 1)
	ZEND_ARG_INFO(0, sourcestring)
	ZEND_ARG_INFO(0, encoding)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_detect_encoding, 0, 0, 1)
	ZEND_ARG_INFO(0, str)
	ZEND_ARG_INFO(0, encoding_list)
	ZEND_ARG_INFO(0, strict)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_mb_list_encodings, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_regex_encoding, 0, 0, 0)
	ZEND_ARG_INFO(0, encoding)
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_mb_regex_set_options, 0, 0, 0)
	ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()
/* }}} */

/* {{{ zend_function_entry mbstring_ng_functions[] */
const zend_function_entry mbstring_ng_functions[] = {
	PHP_MB_FE(strtoupper,			arginfo_mb_strtoupper)
	PHP_MB_FE(strtolower,			arginfo_mb_strtolower)
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
	PHP_MB_FE(regex_set_options,	arginfo_mb_regex_set_options)
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
zend_module_entry mbstring_ng_module_entry = {
	STANDARD_MODULE_HEADER,
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
	STD_PHP_MB_INI_ENTRY("detect_order", "ASCII", PHP_INI_ALL,
						php_mb2_OnUpdateEncodingList, ini.detect_order,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("http_input", "pass", PHP_INI_ALL,
						php_mb2_OnUpdateEncodingList, ini.http_output,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("http_output", "pass", PHP_INI_ALL,
						OnUpdateString, ini.http_output,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("internal_encoding", "UTF-8", PHP_INI_ALL,
						OnUpdateString, ini.internal_encoding,
						zend_mbstring_ng_globals, mbstring_ng_globals)
	STD_PHP_MB_INI_ENTRY("substitute_character", "?", PHP_INI_ALL,
						php_mb2_OnUpdateUnicodeString, ini.substitute_character,
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
	php_mb2_ustring_ctor(&mbstring_ng_globals->ini.substitute_character, 1);

	mbstring_ng_globals->ini.internal_encoding = NULL;
	mbstring_ng_globals->ini.http_output = NULL;

	mbstring_ng_globals->regex.default_mbctype = ONIG_ENCODING_EUC_JP;
	mbstring_ng_globals->regex.current_mbctype = ONIG_ENCODING_EUC_JP;
	mbstring_ng_globals->regex.default_options = ONIG_OPTION_MULTILINE | ONIG_OPTION_SINGLELINE;
	mbstring_ng_globals->regex.default_syntax = ONIG_SYNTAX_RUBY;
	zend_hash_init(&(mbstring_ng_globals->regex.ht_rc), 0, NULL, (void (*)(void *)) php_mb2_regex_free_cache, 1);
}
/* }}} */

/* {{{ PHP_GSHUTDOWN_FUNCTION */
static PHP_GSHUTDOWN_FUNCTION(mbstring_ng)
{
	zend_hash_destroy(&mbstring_ng_globals->regex.ht_rc);

	php_mb2_char_ptr_list_dtor(&mbstring_ng_globals->ini.detect_order);
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION(mbstring_ng) */
static PHP_MINIT_FUNCTION(mbstring_ng)
{
	REGISTER_INI_ENTRIES();
	onig_init();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION(mbstring_ng) */
static PHP_MSHUTDOWN_FUNCTION(mbstring_ng)
{
	UNREGISTER_INI_ENTRIES();
	onig_end();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION(mbstring_ng) */
static PHP_RINIT_FUNCTION(mbstring_ng)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION(mbstring_ng) */
static PHP_RSHUTDOWN_FUNCTION(mbstring_ng)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION(mbstring_ng) */
static PHP_MINFO_FUNCTION(mbstring_ng)
{
	char buf[32];

	php_info_print_table_start();
	php_info_print_table_row(2, "Multibyte Support", "enabled");
	php_info_print_table_row(2, "Multibyte string engine", "libmbfl");
	php_info_print_table_row(2, "HTTP input encoding translation", MBSTR_NG(ini).encoding_translation ? "enabled": "disabled");	
	php_info_print_table_end();

	php_info_print_table_start();
	php_info_print_table_header(1, "mbstring extension makes use of \"streamable kanji code filter and converter\", which is distributed under the GNU Lesser General Public License version 2.1.");
	php_info_print_table_end();

	php_info_print_table_start();
	php_info_print_table_row(2, "Multibyte (japanese) regex support", "enabled");
	snprintf(buf, sizeof(buf), "%d.%d.%d",
			ONIGURUMA_VERSION_MAJOR,
			ONIGURUMA_VERSION_MINOR,
			ONIGURUMA_VERSION_TEENY);
#ifdef PHP_ONIG_BUNDLED
#ifdef USE_COMBINATION_EXPLOSION_CHECK
	php_info_print_table_row(2, "Multibyte regex (oniguruma) backtrack check", "On");
#else	/* USE_COMBINATION_EXPLOSION_CHECK */
	php_info_print_table_row(2, "Multibyte regex (oniguruma) backtrack check", "Off");
#endif	/* USE_COMBINATION_EXPLOSION_CHECK */
#endif /* PHP_BUNDLED_ONIG */
	php_info_print_table_row(2, "Multibyte regex (oniguruma) version", buf);
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
	UErrorCode err;
	UConverter *conv;

	conv = ucnv_open(MBSTR_NG(ini).internal_encoding ? MBSTR_NG(ini).internal_encoding: "ASCII", &err);
	if (!conv) {
		return FAILURE;
	}

	php_mb2_ustring_ctor(&new_value_ustr, 1);

	if (new_value != NULL) {
		if (FAILURE == php_mb2_ustring_appendn(&new_value_ustr, new_value, new_value_length, conv)) {
			ucnv_close(conv);
			php_mb2_ustring_dtor(&new_value_ustr);
			return FAILURE;
		}
		if (FAILURE == php_mb2_ustring_appendn(p, NULL, 0, conv)) {
			ucnv_close(conv);
			php_mb2_ustring_dtor(&new_value_ustr);
			return FAILURE;
		}
	}

	ucnv_close(conv);
	php_mb2_ustring_dtor(p);
	*p = new_value_ustr;

	return SUCCESS;
}
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
	zval *track_vars_array = NULL;
	char *encstr = NULL;
	int encstr_len;

	track_vars_array = NULL;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|z", &encstr, &encstr_len, &track_vars_array) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto string mb_output_handler(string contents, int status)
   Returns string in output buffer converted to the http_output encoding */
PHP_MB_FUNCTION(output_handler)
{
	char *arg_string;
	int arg_string_len;
	long arg_status;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl", &arg_string, &arg_string_len, &arg_status) == FAILURE) {
		return;
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

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &string_val, &string_len, &enc_name, &enc_name_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto int mb_strpos(string haystack, string needle [, int offset [, string encoding]])
   Find position of first occurrence of a string within another */
PHP_MB_FUNCTION(strpos)
{
	char *haystack_val;
	int haystack_len;
	char *needle_val;
	int needle_len;
	char *enc_name = NULL;
	int enc_name_len;
	long offset;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|ls", &haystack_val, &haystack_len, &needle_val, &needle_len, &offset, &enc_name, &enc_name_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto int mb_strrpos(string haystack, string needle [, int offset [, string encoding]])
   Find position of last occurrence of a string within another */
PHP_MB_FUNCTION(strrpos)
{
	int offset;
	zval **zoffset;
	char *haystack_val;
	int haystack_len;
	char *needle_val;
	int needle_len;
	char *enc_name = NULL;
	int enc_name_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|Zs", &haystack_val, &haystack_len, &needle_val, &needle_len, &zoffset, &enc_name, &enc_name_len) == FAILURE) {
		return;
	}

	if (zoffset) {
		if (Z_TYPE_PP(zoffset) == IS_STRING) {
			char *enc_name2     = Z_STRVAL_PP(zoffset);
			int enc_name_len2 = Z_STRLEN_PP(zoffset);
			int str_flg       = 1;

			if (enc_name2 != NULL) {
				switch (*enc_name2) {
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
				case ' ':
				case '-':
				case '.':
					break;
				default :
					str_flg = 0;
					break;
				}
			}

			if (str_flg) {
				convert_to_long_ex(zoffset);
				offset   = Z_LVAL_PP(zoffset);
			} else {
				enc_name     = enc_name2;
				enc_name_len = enc_name_len2;
			}
		} else {
			convert_to_long_ex(zoffset);
			offset = Z_LVAL_PP(zoffset);
		}
	}
}
/* }}} */

/* {{{ proto int mb_stripos(string haystack, string needle [, int offset [, string encoding]])
   Finds position of first occurrence of a string within another, case insensitive */
PHP_MB_FUNCTION(stripos)
{
	char *haystack_val;
	int haystack_len;
	char *needle_val;
	int needle_len;
	long offset = 0;
	char *from_encoding = NULL;
	int from_encoding_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|ls", &haystack_val, &haystack_len, &needle_val, &needle_len, &offset, &from_encoding, &from_encoding_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto int mb_strripos(string haystack, string needle [, int offset [, string encoding]])
   Finds position of last occurrence of a string within another, case insensitive */
PHP_MB_FUNCTION(strripos)
{
	char *haystack_val;
	int haystack_len;
	char *needle_val;
	int needle_len;
	long offset = 0;
	char *from_encoding = NULL;
	int from_encoding_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|ls", &haystack_val, &haystack_len, &needle_val, &needle_len, &offset, &from_encoding, &from_encoding_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto string mb_strstr(string haystack, string needle[, bool part[, string encoding]])
   Finds first occurrence of a string within another */
PHP_MB_FUNCTION(strstr)
{
	char *haystack_val;
	int haystack_len;
	char *needle_val;
	int needle_len;
	zend_bool part;
	char *enc_name = NULL;
	int enc_name_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|bs", &haystack_val, &haystack_len, &needle_val, &needle_len, &part, &enc_name, &enc_name_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto string mb_stristr(string haystack, string needle[, bool part[, string encoding]])
   Finds first occurrence of a string within another, case insensitive */
PHP_MB_FUNCTION(stristr)
{
	char *haystack_val;
	int haystack_len;
	char *needle_val;
	int needle_len;
	zend_bool part;
	char *enc_name = NULL;
	int enc_name_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|bs", &haystack_val, &haystack_len, &needle_val, &needle_len, &part, &enc_name, &enc_name_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto int mb_substr_count(string haystack, string needle [, string encoding])
   Count the number of substring occurrences */
PHP_MB_FUNCTION(substr_count)
{
	char *haystack_val;
	int haystack_len;
	char *needle_val;
	int needle_len;
	char *enc_name = NULL;
	int enc_name_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|s", &haystack_val, &haystack_len, &needle_val, &needle_len, &enc_name, &enc_name_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto string mb_substr(string str, int start [, int length [, string encoding]])
   Returns part of a string */
PHP_MB_FUNCTION(substr)
{
	char *str;
	int str_len;
	char *encoding;
	int encoding_len;
	long from, len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|ls", &str, &str_len, &from, &len, &encoding, &encoding_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto string mb_strcut(string str, int start [, int length [, string encoding]])
   Returns part of a string */
PHP_MB_FUNCTION(strcut)
{
	long from, len;
	char *string_val;
	int string_len;
	char *encoding;
	int encoding_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|ls", &string_val, &string_len, &from, &len, &encoding, &encoding_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto int mb_strwidth(string str [, string encoding])
   Gets terminal width of a string */
PHP_MB_FUNCTION(strwidth)
{
	char *string_val;
	int string_len;
	char *enc_name = NULL;
	int enc_name_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s", &string_val, &string_len, &enc_name, &enc_name_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto string mb_strimwidth(string str, int start, int width [, string trimmarker [, string encoding]])
   Trim the string in terminal width */
PHP_MB_FUNCTION(strimwidth)
{
	char *str;
	char *trimmarker;
	int trimmarker_len;
	char *encoding;
	int encoding_len;
	long from;
	long width;
	int str_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sll|ss", &str, &str_len, &from, &width, &trimmarker, &trimmarker_len, &encoding, &encoding_len) == FAILURE) {
		return;
	}
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
		size_t result_len;
		if (SUCCESS == php_mb2_convert_encoding(str, str_len, to_enc, from_encodings.items, from_encodings.nitems, &result, &result_len, 0 TSRMLS_CC)) {
			RETVAL_STRINGL(result, result_len, 0);
		} else {
			RETVAL_FALSE;
		}
	}

	php_mb2_char_ptr_list_dtor(&from_encodings);
}
/* }}} */

/* {{{ proto string mb_strtoupper(string sourcestring [, string encoding])
 *  Returns a uppercased version of sourcestring
 */
PHP_MB_FUNCTION(strtoupper)
{
	char *str;
	int str_len;
	char **from_encoding;
	int from_encoding_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s!", &str, &str_len,
				&from_encoding, &from_encoding_len) == FAILURE) {
		return;
	}
}
/* }}} */

/* {{{ proto string mb_strtolower(string sourcestring [, string encoding])
 *  Returns a lowercased version of sourcestring
 */
PHP_MB_FUNCTION(strtolower)
{
	char *str;
	int str_len;
	char *from_encoding = NULL;
	int from_encoding_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|s!", &str, &str_len,
				&from_encoding, &from_encoding_len) == FAILURE) {
		return;
	}
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

/* {{{ encoding name map */
typedef struct _php_mb_regex_enc_name_map_t {
	const char *names;
	OnigEncoding code;
} php_mb_regex_enc_name_map_t;

php_mb_regex_enc_name_map_t enc_name_map[] = {
#ifdef ONIG_ENCODING_EUC_JP
	{
		"EUC-JP\0EUCJP\0X-EUC-JP\0UJIS\0EUCJP\0EUCJP-WIN\0",
		ONIG_ENCODING_EUC_JP
	},
#endif
#ifdef ONIG_ENCODING_UTF8
	{
		"UTF-8\0UTF8\0",
		ONIG_ENCODING_UTF8
	},
#endif
#ifdef ONIG_ENCODING_UTF16_BE
	{
		"UTF-16\0UTF-16BE\0",
		ONIG_ENCODING_UTF16_BE
	},
#endif
#ifdef ONIG_ENCODING_UTF16_LE
	{
		"UTF-16LE\0",
		ONIG_ENCODING_UTF16_LE
	},
#endif
#ifdef ONIG_ENCODING_UTF32_BE
	{
		"UCS-4\0UTF-32\0UTF-32BE\0",
		ONIG_ENCODING_UTF32_BE
	},
#endif
#ifdef ONIG_ENCODING_UTF32_LE
	{
		"UCS-4LE\0UTF-32LE\0",
		ONIG_ENCODING_UTF32_LE
	},
#endif
#ifdef ONIG_ENCODING_SJIS
	{
		"SJIS\0CP932\0MS932\0SHIFT_JIS\0SJIS-WIN\0WINDOWS-31J\0",
		ONIG_ENCODING_SJIS
	},
#endif
#ifdef ONIG_ENCODING_BIG5
	{
		"BIG5\0BIG-5\0BIGFIVE\0CN-BIG5\0BIG-FIVE\0",
		ONIG_ENCODING_BIG5
	},
#endif
#ifdef ONIG_ENCODING_EUC_CN
	{
		"EUC-CN\0EUCCN\0EUC_CN\0GB-2312\0GB2312\0",
		ONIG_ENCODING_EUC_CN
	},
#endif
#ifdef ONIG_ENCODING_EUC_TW
	{
		"EUC-TW\0EUCTW\0EUC_TW\0",
		ONIG_ENCODING_EUC_TW
	},
#endif
#ifdef ONIG_ENCODING_EUC_KR
	{
		"EUC-KR\0EUCKR\0EUC_KR\0",
		ONIG_ENCODING_EUC_KR
	},
#endif
#if defined(ONIG_ENCODING_KOI8) && !PHP_ONIG_BAD_KOI8_ENTRY
	{
		"KOI8\0KOI-8\0",
		ONIG_ENCODING_KOI8
	},
#endif
#ifdef ONIG_ENCODING_KOI8_R
	{
		"KOI8R\0KOI8-R\0KOI-8R\0",
		ONIG_ENCODING_KOI8_R
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_1
	{
		"ISO-8859-1\0ISO8859-1\0ISO_8859_1\0ISO8859_1\0",
		ONIG_ENCODING_ISO_8859_1
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_2
	{
		"ISO-8859-2\0ISO8859-2\0ISO_8859_2\0ISO8859_2\0",
		ONIG_ENCODING_ISO_8859_2
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_3
	{
		"ISO-8859-3\0ISO8859-3\0ISO_8859_3\0ISO8859_3\0",
		ONIG_ENCODING_ISO_8859_3
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_4
	{
		"ISO-8859-4\0ISO8859-4\0ISO_8859_4\0ISO8859_4\0",
		ONIG_ENCODING_ISO_8859_4
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_5
	{
		"ISO-8859-5\0ISO8859-5\0ISO_8859_5\0ISO8859_5\0",
		ONIG_ENCODING_ISO_8859_5
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_6
	{
		"ISO-8859-6\0ISO8859-6\0ISO_8859_6\0ISO8859_6\0",
		ONIG_ENCODING_ISO_8859_6
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_7
	{
		"ISO-8859-7\0ISO8859-7\0ISO_8859_7\0ISO8859_7\0",
		ONIG_ENCODING_ISO_8859_7
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_8
	{
		"ISO-8859-8\0ISO8859-8\0ISO_8859_8\0ISO8859_8\0",
		ONIG_ENCODING_ISO_8859_8
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_9
	{
		"ISO-8859-9\0ISO8859-9\0ISO_8859_9\0ISO8859_9\0",
		ONIG_ENCODING_ISO_8859_9
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_10
	{
		"ISO-8859-10\0ISO8859-10\0ISO_8859_10\0ISO8859_10\0",
		ONIG_ENCODING_ISO_8859_10
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_11
	{
		"ISO-8859-11\0ISO8859-11\0ISO_8859_11\0ISO8859_11\0",
		ONIG_ENCODING_ISO_8859_11
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_13
	{
		"ISO-8859-13\0ISO8859-13\0ISO_8859_13\0ISO8859_13\0",
		ONIG_ENCODING_ISO_8859_13
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_14
	{
		"ISO-8859-14\0ISO8859-14\0ISO_8859_14\0ISO8859_14\0",
		ONIG_ENCODING_ISO_8859_14
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_15
	{
		"ISO-8859-15\0ISO8859-15\0ISO_8859_15\0ISO8859_15\0",
		ONIG_ENCODING_ISO_8859_15
	},
#endif
#ifdef ONIG_ENCODING_ISO_8859_16
	{
		"ISO-8859-16\0ISO8859-16\0ISO_8859_16\0ISO8859_16\0",
		ONIG_ENCODING_ISO_8859_16
	},
#endif
#ifdef ONIG_ENCODING_ASCII
	{
		"ASCII\0US-ASCII\0US_ASCII\0ISO646\0",
		ONIG_ENCODING_ASCII
	},
#endif
	{ NULL, ONIG_ENCODING_UNDEF }
};
/* }}} */

/* {{{ php_mb_regex_name2mbctype */
static OnigEncoding _php_mb_regex_name2mbctype(const char *pname)
{
	const char *p;
	php_mb_regex_enc_name_map_t *mapping;

	if (pname == NULL) {
		return ONIG_ENCODING_UNDEF;
	}

	for (mapping = enc_name_map; mapping->names != NULL; mapping++) {
		for (p = mapping->names; *p != '\0'; p += (strlen(p) + 1)) {
			if (strcasecmp(p, pname) == 0) {
				return mapping->code;
			}
		}
	}

	return ONIG_ENCODING_UNDEF;
}
/* }}} */

/* {{{ php_mb_regex_mbctype2name */
static const char *_php_mb_regex_mbctype2name(OnigEncoding mbctype)
{
	php_mb_regex_enc_name_map_t *mapping;

	for (mapping = enc_name_map; mapping->names != NULL; mapping++) {
		if (mapping->code == mbctype) {
			return mapping->names;
		}
	}

	return NULL;
}
/* }}} */

/* {{{ php_mb_regex_set_mbctype */
int php_mb_regex_set_mbctype(const char *encname TSRMLS_DC)
{
	OnigEncoding mbctype = _php_mb_regex_name2mbctype(encname);
	if (mbctype == ONIG_ENCODING_UNDEF) {
		return FAILURE;
	}
	MBSTR_NG(regex).current_mbctype = mbctype;
	return SUCCESS;
}
/* }}} */

/* {{{ php_mb_regex_set_default_mbctype */
int php_mb_regex_set_default_mbctype(const char *encname TSRMLS_DC)
{
	OnigEncoding mbctype = _php_mb_regex_name2mbctype(encname);
	if (mbctype == ONIG_ENCODING_UNDEF) {
		return FAILURE;
	}
	MBSTR_NG(regex).default_mbctype = mbctype;
	return SUCCESS;
}
/* }}} */

/* {{{ php_mb_regex_get_mbctype */
const char *php_mb_regex_get_mbctype(TSRMLS_D)
{
	return _php_mb_regex_mbctype2name(MBSTR_NG(regex).current_mbctype);
}
/* }}} */

/* {{{ php_mb_regex_get_default_mbctype */
const char *php_mb_regex_get_default_mbctype(TSRMLS_D)
{
	return _php_mb_regex_mbctype2name(MBSTR_NG(regex).default_mbctype);
}
/* }}} */

/*
 * regex cache
 */
/* {{{ php_mbregex_compile_pattern */
static php_mb_regex_t *php_mbregex_compile_pattern(const char *pattern, int patlen, OnigOptionType options, OnigEncoding enc, OnigSyntaxType *syntax TSRMLS_DC)
{
	int err_code = 0;
	int found = 0;
	php_mb_regex_t *retval = NULL, **rc = NULL;
	OnigErrorInfo err_info;
	OnigUChar err_str[ONIG_MAX_ERROR_MESSAGE_LEN];

	found = zend_hash_find(&MBSTR_NG(regex).ht_rc, (char *)pattern, patlen+1, (void **) &rc);
	if (found == FAILURE || (*rc)->options != options || (*rc)->enc != enc || (*rc)->syntax != syntax) {
		if ((err_code = onig_new(&retval, (OnigUChar *)pattern, (OnigUChar *)(pattern + patlen), options, enc, syntax, &err_info)) != ONIG_NORMAL) {
			onig_error_code_to_str(err_str, err_code, err_info);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "mbregex compile err: %s", err_str);
			retval = NULL;
			goto out;
		}
		zend_hash_update(&MBSTR_NG(regex).ht_rc, (char *) pattern, patlen + 1, (void *) &retval, sizeof(retval), NULL);
	} else if (found == SUCCESS) {
		retval = *rc;
	}
out:
	return retval; 
}
/* }}} */

/* {{{ _php_mb_regex_get_option_string */
static size_t _php_mb_regex_get_option_string(char *str, size_t len, OnigOptionType option, OnigSyntaxType *syntax)
{
	size_t len_left = len;
	size_t len_req = 0;
	char *p = str;
	char c;

	if ((option & ONIG_OPTION_IGNORECASE) != 0) {
		if (len_left > 0) {
			--len_left;
			*(p++) = 'i';
		}
		++len_req;	
	}

	if ((option & ONIG_OPTION_EXTEND) != 0) {
		if (len_left > 0) {
			--len_left;
			*(p++) = 'x';
		}
		++len_req;	
	}

	if ((option & (ONIG_OPTION_MULTILINE | ONIG_OPTION_SINGLELINE)) ==
			(ONIG_OPTION_MULTILINE | ONIG_OPTION_SINGLELINE)) {
		if (len_left > 0) {
			--len_left;
			*(p++) = 'p';
		}
		++len_req;	
	} else {
		if ((option & ONIG_OPTION_MULTILINE) != 0) {
			if (len_left > 0) {
				--len_left;
				*(p++) = 'm';
			}
			++len_req;	
		}

		if ((option & ONIG_OPTION_SINGLELINE) != 0) {
			if (len_left > 0) {
				--len_left;
				*(p++) = 's';
			}
			++len_req;	
		}
	}	
	if ((option & ONIG_OPTION_FIND_LONGEST) != 0) {
		if (len_left > 0) {
			--len_left;
			*(p++) = 'l';
		}
		++len_req;	
	}
	if ((option & ONIG_OPTION_FIND_NOT_EMPTY) != 0) {
		if (len_left > 0) {
			--len_left;
			*(p++) = 'n';
		}
		++len_req;	
	}

	c = 0;

	if (syntax == ONIG_SYNTAX_JAVA) {
		c = 'j';
	} else if (syntax == ONIG_SYNTAX_GNU_REGEX) {
		c = 'u';
	} else if (syntax == ONIG_SYNTAX_GREP) {
		c = 'g';
	} else if (syntax == ONIG_SYNTAX_EMACS) {
		c = 'c';
	} else if (syntax == ONIG_SYNTAX_RUBY) {
		c = 'r';
	} else if (syntax == ONIG_SYNTAX_PERL) {
		c = 'z';
	} else if (syntax == ONIG_SYNTAX_POSIX_BASIC) {
		c = 'b';
	} else if (syntax == ONIG_SYNTAX_POSIX_EXTENDED) {
		c = 'd';
	}

	if (c != 0) {
		if (len_left > 0) {
			--len_left;
			*(p++) = c;
		}
		++len_req;
	}


	if (len_left > 0) {
		--len_left;
		*(p++) = '\0';
	}
	++len_req;	
	if (len < len_req) {
		return len_req;
	}

	return 0;
}
/* }}} */

/* {{{ _php_mb_regex_init_options */
static void
_php_mb_regex_init_options(const char *parg, int narg, OnigOptionType *option, OnigSyntaxType **syntax, int *eval) 
{
	int n;
	char c;
	int optm = 0; 

	*syntax = ONIG_SYNTAX_RUBY;

	if (parg != NULL) {
		n = 0;
		while(n < narg) {
			c = parg[n++];
			switch (c) {
				case 'i':
					optm |= ONIG_OPTION_IGNORECASE;
					break;
				case 'x':
					optm |= ONIG_OPTION_EXTEND;
					break;
				case 'm':
					optm |= ONIG_OPTION_MULTILINE;
					break;
				case 's':
					optm |= ONIG_OPTION_SINGLELINE;
					break;
				case 'p':
					optm |= ONIG_OPTION_MULTILINE | ONIG_OPTION_SINGLELINE;
					break;
				case 'l':
					optm |= ONIG_OPTION_FIND_LONGEST;
					break;
				case 'n':
					optm |= ONIG_OPTION_FIND_NOT_EMPTY;
					break;
				case 'j':
					*syntax = ONIG_SYNTAX_JAVA;
					break;
				case 'u':
					*syntax = ONIG_SYNTAX_GNU_REGEX;
					break;
				case 'g':
					*syntax = ONIG_SYNTAX_GREP;
					break;
				case 'c':
					*syntax = ONIG_SYNTAX_EMACS;
					break;
				case 'r':
					*syntax = ONIG_SYNTAX_RUBY;
					break;
				case 'z':
					*syntax = ONIG_SYNTAX_PERL;
					break;
				case 'b':
					*syntax = ONIG_SYNTAX_POSIX_BASIC;
					break;
				case 'd':
					*syntax = ONIG_SYNTAX_POSIX_EXTENDED;
					break;
				case 'e':
					if (eval != NULL) *eval = 1; 
					break;
				default:
					break;
			}
		}
		if (option != NULL) *option|=optm; 
	}
}
/* }}} */

/* {{{ static void php_mb_regex_free_cache() */
static void php_mb2_regex_free_cache(php_mb_regex_t **pre) 
{
	onig_free(*pre);
}
/* }}} */

/* {{{ proto string mb_regex_encoding([string encoding])
   Returns the current encoding for regex as a string. */
PHP_MB_FUNCTION(regex_encoding)
{
	size_t argc = ZEND_NUM_ARGS();
	char *encoding;
	int encoding_len;
	OnigEncoding mbctype;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s", &encoding, &encoding_len) == FAILURE) {
		return;
	}

	if (argc == 0) {
		const char *retval = _php_mb_regex_mbctype2name(MBSTR_NG(regex).current_mbctype);

		if (retval == NULL) {
			return;
		}

		RETURN_STRING((char *)retval, 1);
	} else if (argc == 1) {
		mbctype = _php_mb_regex_name2mbctype(encoding);

		if (mbctype == ONIG_ENCODING_UNDEF) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown encoding \"%s\"", encoding);
			return;
		}

		MBSTR_NG(regex).current_mbctype = mbctype;
		RETURN_TRUE;
	}
}
/* }}} */

/* {{{ _php_mb_regex_ereg_exec */
static void _php_mb_regex_ereg_exec(INTERNAL_FUNCTION_PARAMETERS, int icase)
{
	zval **arg_pattern, *array;
	char *string;
	int string_len;
	php_mb_regex_t *re;
	OnigRegion *regs = NULL;
	int i, match_len, beg, end;
	OnigOptionType options;
	char *str;

	array = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Zs|z", &arg_pattern, &string, &string_len, &array) == FAILURE) {
		return;
	}

	options = MBSTR_NG(regex).default_options;
	if (icase) {
		options |= ONIG_OPTION_IGNORECASE;
	}

	/* compile the regular expression from the supplied regex */
	if (Z_TYPE_PP(arg_pattern) != IS_STRING) {
		/* we convert numbers to integers and treat them as a string */
		if (Z_TYPE_PP(arg_pattern) == IS_DOUBLE) {
			convert_to_long_ex(arg_pattern);	/* get rid of decimal places */
		}
		convert_to_string_ex(arg_pattern);
		/* don't bother doing an extended regex with just a number */
	}

	if (!Z_STRVAL_PP(arg_pattern) || Z_STRLEN_PP(arg_pattern) == 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "empty pattern");
		RETVAL_FALSE;
		goto out;
	}

	re = php_mbregex_compile_pattern(Z_STRVAL_PP(arg_pattern), Z_STRLEN_PP(arg_pattern), options, MBSTR_NG(regex).current_mbctype, MBSTR_NG(regex).default_syntax TSRMLS_CC);
	if (re == NULL) {
		RETVAL_FALSE;
		goto out;
	}

	regs = onig_region_new();

	/* actually execute the regular expression */
	if (onig_search(re, (OnigUChar *)string, (OnigUChar *)(string + string_len), (OnigUChar *)string, (OnigUChar *)(string + string_len), regs, 0) < 0) {
		RETVAL_FALSE;
		goto out;
	}

	match_len = 1;
	str = string;
	if (array != NULL) {
		match_len = regs->end[0] - regs->beg[0];
		zval_dtor(array);
		array_init(array);
		for (i = 0; i < regs->num_regs; i++) {
			beg = regs->beg[i];
			end = regs->end[i];
			if (beg >= 0 && beg < end && end <= string_len) {
				add_index_stringl(array, i, (char *)&str[beg], end - beg, 1);
			} else {
				add_index_bool(array, i, 0);
			}
		}
	}

	if (match_len == 0) {
		match_len = 1;
	}
	RETVAL_LONG(match_len);
out:
	if (regs != NULL) {
		onig_region_free(regs, 1);
	}
}
/* }}} */

/* {{{ proto int mb_ereg(string pattern, string string [, array registers])
   Regular expression match for multibyte string */
PHP_MB_FUNCTION(ereg)
{
	_php_mb_regex_ereg_exec(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */

/* {{{ proto int mb_eregi(string pattern, string string [, array registers])
   Case-insensitive regular expression match for multibyte string */
PHP_MB_FUNCTION(eregi)
{
	_php_mb_regex_ereg_exec(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}
/* }}} */

/* {{{ _php_mb_regex_ereg_replace_exec */
static void _php_mb_regex_ereg_replace_exec(INTERNAL_FUNCTION_PARAMETERS, OnigOptionType options)
{
	zval **arg_pattern_zval;

	char *arg_pattern;
	int arg_pattern_len;

	char *replace;
	int replace_len;

	char *string;
	int string_len;

	char *p;
	php_mb_regex_t *re;
	OnigSyntaxType *syntax;
	OnigRegion *regs = NULL;
	smart_str out_buf = { 0 };
	smart_str eval_buf = { 0 };
	smart_str *pbuf;
	int i, err, eval, n;
	OnigUChar *pos;
	OnigUChar *string_lim;
	char *description = NULL;
	char pat_buf[2];

	eval = 0;
	{
		char *option_str = NULL;
		int option_str_len = 0;

		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Zss|s",
									&arg_pattern_zval,
									&replace, &replace_len,
									&string, &string_len,
									&option_str, &option_str_len) == FAILURE) {
			return;
		}

		if (option_str != NULL) {
			_php_mb_regex_init_options(option_str, option_str_len, &options, &syntax, &eval);
		} else {
			options |= MBSTR_NG(regex).default_options;
			syntax = MBSTR_NG(regex).default_syntax;
		}
	}
	if (Z_TYPE_PP(arg_pattern_zval) == IS_STRING) {
		arg_pattern = Z_STRVAL_PP(arg_pattern_zval);
		arg_pattern_len = Z_STRLEN_PP(arg_pattern_zval);
	} else {
		/* FIXME: this code is not multibyte aware! */
		convert_to_long_ex(arg_pattern_zval);
		pat_buf[0] = (char)Z_LVAL_PP(arg_pattern_zval);	
		pat_buf[1] = '\0';

		arg_pattern = pat_buf;
		arg_pattern_len = 1;	
	}
	/* create regex pattern buffer */
	re = php_mbregex_compile_pattern(arg_pattern, arg_pattern_len, options, MBSTR_NG(regex).current_mbctype, syntax TSRMLS_CC);
	if (re == NULL) {
		return;
	}

	if (eval) {
		pbuf = &eval_buf;
		description = zend_make_compiled_string_description("mbregex replace" TSRMLS_CC);
	} else {
		pbuf = &out_buf;
		description = NULL;
	}

	/* do the actual work */
	err = 0;
	pos = (OnigUChar *)string;
	string_lim = (OnigUChar*)(string + string_len);
	regs = onig_region_new();
	while (err >= 0) {
		err = onig_search(re, (OnigUChar *)string, (OnigUChar *)string_lim, pos, (OnigUChar *)string_lim, regs, 0);
		if (err <= -2) {
			OnigUChar err_str[ONIG_MAX_ERROR_MESSAGE_LEN];
			onig_error_code_to_str(err_str, err);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "mbregex search failure in php_mbereg_replace_exec(): %s", err_str);
			break;
		}
		if (err >= 0) {
#if moriyoshi_0
			if (regs->beg[0] == regs->end[0]) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Empty regular expression");
				break;
			}
#endif
			/* copy the part of the string before the match */
			smart_str_appendl(&out_buf, pos, (size_t)((OnigUChar *)(string + regs->beg[0]) - pos));
			/* copy replacement and backrefs */
			i = 0;
			p = replace;
			while (i < replace_len) {
				int fwd = 1; /* FIXME */
				n = -1;
				if ((replace_len - i) >= 2 && fwd == 1 &&
					p[0] == '\\' && p[1] >= '0' && p[1] <= '9') {
					n = p[1] - '0';
				}
				if (n >= 0 && n < regs->num_regs) {
					if (regs->beg[n] >= 0 && regs->beg[n] < regs->end[n] && regs->end[n] <= string_len) {
						smart_str_appendl(pbuf, string + regs->beg[n], regs->end[n] - regs->beg[n]);
					}
					p += 2;
					i += 2;
				} else {
					smart_str_appendl(pbuf, p, fwd);
					p += fwd;
					i += fwd;
				}
			}
			if (eval) {
				zval v;
				/* null terminate buffer */
				smart_str_0(&eval_buf);
				/* do eval */
				if (zend_eval_stringl(eval_buf.c, eval_buf.len, &v, description TSRMLS_CC) == FAILURE) {
					efree(description);
					php_error_docref(NULL TSRMLS_CC,E_ERROR, "Failed evaluating code: %s%s", PHP_EOL, eval_buf.c);
					/* zend_error() does not return in this case */
				}

				/* result of eval */
				convert_to_string(&v);
				smart_str_appendl(&out_buf, Z_STRVAL(v), Z_STRLEN(v));
				/* Clean up */
				eval_buf.len = 0;
				zval_dtor(&v);
			}
			n = regs->end[0];
			if ((pos - (OnigUChar *)string) < n) {
				pos = (OnigUChar *)string + n;
			} else {
				if (pos < string_lim) {
					smart_str_appendl(&out_buf, pos, 1); 
				}
				pos++;
			}
		} else { /* nomatch */
			/* stick that last bit of string on our output */
			if (string_lim - pos > 0) {
				smart_str_appendl(&out_buf, pos, string_lim - pos);
			}
		}
		onig_region_free(regs, 0);
	}

	if (description) {
		efree(description);
	}
	if (regs != NULL) {
		onig_region_free(regs, 1);
	}
	smart_str_free(&eval_buf);

	if (err <= -2) {
		smart_str_free(&out_buf);	
		RETVAL_FALSE;
	} else {
		smart_str_appendc(&out_buf, '\0');
		RETVAL_STRINGL((char *)out_buf.c, out_buf.len - 1, 0);
	}
}
/* }}} */

/* {{{ proto string mb_ereg_replace(string pattern, string replacement, string string [, string option])
   Replace regular expression for multibyte string */
PHP_MB_FUNCTION(ereg_replace)
{
	_php_mb_regex_ereg_replace_exec(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */

/* {{{ proto string mb_eregi_replace(string pattern, string replacement, string string)
   Case insensitive replace regular expression for multibyte string */
PHP_MB_FUNCTION(eregi_replace)
{
	_php_mb_regex_ereg_replace_exec(INTERNAL_FUNCTION_PARAM_PASSTHRU, ONIG_OPTION_IGNORECASE);
}
/* }}} */

/* {{{ proto array mb_split(string pattern, string string [, int limit])
   split multibyte string into array by regular expression */
PHP_MB_FUNCTION(split)
{
	char *arg_pattern;
	int arg_pattern_len;
	php_mb_regex_t *re;
	OnigRegion *regs = NULL;
	char *string;
	OnigUChar *pos;
	int string_len;

	int n, err;
	long count = -1;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|l", &arg_pattern, &arg_pattern_len, &string, &string_len, &count) == FAILURE) {
		return;
	} 

	if (count == 0) {
		count = 1;
	}

	/* create regex pattern buffer */
	if ((re = php_mbregex_compile_pattern(arg_pattern, arg_pattern_len, MBSTR_NG(regex).default_options, MBSTR_NG(regex).current_mbctype, MBSTR_NG(regex).default_syntax TSRMLS_CC)) == NULL) {
		return;
	}

	array_init(return_value);

	pos = (OnigUChar *)string;
	err = 0;
	regs = onig_region_new();
	/* churn through str, generating array entries as we go */
	while ((--count != 0) &&
		   (err = onig_search(re, (OnigUChar *)string, (OnigUChar *)(string + string_len), pos, (OnigUChar *)(string + string_len), regs, 0)) >= 0) {
		if (regs->beg[0] == regs->end[0]) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Empty regular expression");
			break;
		}

		/* add it to the array */
		if (regs->beg[0] < string_len && regs->beg[0] >= (pos - (OnigUChar *)string)) {
			add_next_index_stringl(return_value, (char *)pos, ((OnigUChar *)(string + regs->beg[0]) - pos), 1);
		} else {
			err = -2;
			break;
		}
		/* point at our new starting point */
		n = regs->end[0];
		if ((pos - (OnigUChar *)string) < n) {
			pos = (OnigUChar *)string + n;
		}
		if (count < 0) {
			count = 0;
		}
		onig_region_free(regs, 0);
	}

	onig_region_free(regs, 1);

	/* see if we encountered an error */
	if (err <= -2) {
		OnigUChar err_str[ONIG_MAX_ERROR_MESSAGE_LEN];
		onig_error_code_to_str(err_str, err);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "mbregex search failure in mbsplit(): %s", err_str);
		zval_dtor(return_value);
		return;
	}

	/* otherwise we just have one last element to add to the array */
	n = ((OnigUChar *)(string + string_len) - pos);
	if (n > 0) {
		add_next_index_stringl(return_value, (char *)pos, n, 1);
	} else {
		add_next_index_stringl(return_value, "", 0, 1);
	}
}
/* }}} */

/* {{{ proto bool mb_ereg_match(string pattern, string string [,string option])
   Regular expression match for multibyte string */
PHP_MB_FUNCTION(ereg_match)
{
	char *arg_pattern;
	int arg_pattern_len;

	char *string;
	int string_len;

	php_mb_regex_t *re;
	OnigSyntaxType *syntax;
	OnigOptionType option = 0;
	int err;

	{
		char *option_str = NULL;
		int option_str_len = 0;

		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|s",
		                          &arg_pattern, &arg_pattern_len, &string, &string_len,
		                          &option_str, &option_str_len)==FAILURE) {
			return;
		}

		if (option_str != NULL) {
			_php_mb_regex_init_options(option_str, option_str_len, &option, &syntax, NULL);
		} else {
			option |= MBSTR_NG(regex).default_options;
			syntax = MBSTR_NG(regex).default_syntax;
		}
	}

	if ((re = php_mbregex_compile_pattern(arg_pattern, arg_pattern_len, option, MBSTR_NG(regex).current_mbctype, syntax TSRMLS_CC)) == NULL) {
		return;
	}

	/* match */
	err = onig_match(re, (OnigUChar *)string, (OnigUChar *)(string + string_len), (OnigUChar *)string, NULL, 0);
	if (err >= 0) {
		RETVAL_TRUE;
	} else {
		RETVAL_FALSE;
	}
}
/* }}} */

/* {{{ php_mb_regex_set_options */
static void _php_mb_regex_set_options(OnigOptionType options, OnigSyntaxType *syntax, OnigOptionType *prev_options, OnigSyntaxType **prev_syntax TSRMLS_DC) 
{
	if (prev_options != NULL) {
		*prev_options = MBSTR_NG(regex).default_options;
	}
	if (prev_syntax != NULL) {
		*prev_syntax = MBSTR_NG(regex).default_syntax;
	}
	MBSTR_NG(regex).default_options = options;
	MBSTR_NG(regex).default_syntax = syntax;
}
/* }}} */

/* {{{ proto string mb_regex_set_options([string options])
   Set or get the default options for mbregex functions */
PHP_MB_FUNCTION(regex_set_options)
{
	OnigOptionType opt;
	OnigSyntaxType *syntax;
	char *string = NULL;
	int string_len;
	char buf[16];

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s",
	                          &string, &string_len) == FAILURE) {
		return;
	}
	if (string != NULL) {
		opt = 0;
		syntax = NULL;
		_php_mb_regex_init_options(string, string_len, &opt, &syntax, NULL);
		_php_mb_regex_set_options(opt, syntax, NULL, NULL TSRMLS_CC);
	} else {
		opt = MBSTR_NG(regex).default_options;
		syntax = MBSTR_NG(regex).default_syntax;
	}
	_php_mb_regex_get_option_string(buf, sizeof(buf), opt, syntax);

	RETVAL_STRING(buf, 1);
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

static void php_mb2_ustring_dtor(php_mb2_ustring *str)
{
	if (str->p) {
		pefree(str->p, str->persistent);
	}
}

static int php_mb2_ustring_appendu(php_mb2_ustring *str, const UChar *ustr, int32_t len)
{
	int32_t new_len = str->len + len;
	if (new_len < str->len) {
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

typedef struct php_mb2_uconverter_callback_ctx {
	char *dbuf;
	char *pdl;
	int persistent;
	const UChar *subst_char_u;
	int32_t subst_char_u_len;
#ifdef ZTS
	TSRMLS_D;
#endif
} php_mb2_uconverter_callback_ctx;

static void php_mb2_uconverter_from_unicode_callback(const void *_ctx, UConverterFromUnicodeArgs *args, UChar *units, int32_t length, UChar32 codePoint, UConverterCallbackReason reason, UErrorCode *err)
{
	php_mb2_uconverter_callback_ctx *ctx = (void *)_ctx;
#ifdef ZTS
	TSRMLS_D = ctx.TSRMLS_C;
#endif
	switch (reason) {
	case UCNV_UNASSIGNED:
		php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Unrepresentable character U+%06x", codePoint);
		if (!MBSTR_NG(runtime).in_ucnv_error_handler) {
			const UChar *ps = ctx->subst_char_u, *psl = ctx->subst_char_u + ctx->subst_char_u_len;
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
	case UCNV_IRREGULAR:
	case UCNV_ILLEGAL:
		if (!MBSTR_NG(runtime).in_ucnv_error_handler) {
			const UChar *ps = ctx->subst_char_u, *psl = ctx->subst_char_u + ctx->subst_char_u_len;
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
		if (args->targetLimit - args->target < ctx->subst_char_u_len) {
			args->targetLimit = args->target + ctx->subst_char_u_len;
		}
		memmove(args->target, ctx->subst_char_u, sizeof(UChar) * ctx->subst_char_u_len);
		args->target += ctx->subst_char_u_len;
		*err = U_ZERO_ERROR;
		break;
	default:
		break;
	}
}

static int php_mb2_convert_encoding(const char *input, size_t length, const char *to_encoding, const char * const *from_encodings, size_t num_from_encodings, char **output, size_t *output_len, int persistent TSRMLS_DC)
{
	static const size_t pvbuf_basic_len = 1024;
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
	ctx.subst_char_u = MBSTR_NG(ini).substitute_character.p;
	ctx.subst_char_u_len = MBSTR_NG(ini).substitute_character.len;
#ifdef ZTS
	TSRMLS_C = TSRMLS_C;
#endif
	MBSTR_NG(runtime).in_ucnv_error_handler = FALSE;

	if (pvbuf_basic_len + ctx.subst_char_u_len < pvbuf_basic_len || sizeof(UChar) * (pvbuf_basic_len + ctx.subst_char_u_len) / sizeof(UChar) != pvbuf_basic_len + ctx.subst_char_u_len) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to allocate temporary buffer", to_encoding, u_errorName(err));
		return FAILURE;
	}
	pvbuf = do_alloca_ex(sizeof(UChar) * (pvbuf_basic_len + ctx.subst_char_u_len), 4096, use_heap);

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
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to set the callback to the decoder (error: %s)", *from_encoding, u_errorName(err));
			goto fail;
		}

		ps = input;
		pd = ctx.dbuf;

		ucnv_convertEx(to_conv, from_conv, &pd, ctx.pdl, &ps, psl, pvbuf, &ppvs, &ppvd, pvbuf + pvbuf_basic_len, TRUE, FALSE, &err);
		while (err == U_BUFFER_OVERFLOW_ERROR) {
			size_t new_dbuf_size;
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
				size_t new_dbuf_size;
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
		if (U_SUCCESS(err)) {
			break;
		}
		if (err != U_ILLEGAL_CHAR_FOUND && err != U_ILLEGAL_ESCAPE_SEQUENCE && err != U_TRUNCATED_CHAR_FOUND && err != U_UNSUPPORTED_ESCAPE_SEQUENCE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to convert from %s to %s (error: %s)", *from_encoding, to_encoding, u_errorName(err));
			goto fail;
		}

		ucnv_close(from_conv);
		from_conv = NULL;
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

static int php_mb2_int32_data_compare(const HashPosition *a, const HashPosition *b TSRMLS_DC)
{
	const int32_t av = *(int32_t *)((*a)->pData), bv = *(int32_t *)((*b)->pData);
	return av > bv ? -1: av < bv ? 1: 0;
}

static char *php_mb2_detect_encoding(const char *input, size_t length, const char * const *hint_encodings, size_t num_hint_encodings TSRMLS_DC)
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

static int php_mb2_parse_encoding_list(const char *value, size_t value_length, php_mb2_char_ptr_list *pretval, int persistent)
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
		if (FAILURE == php_mb2_parse_encoding_list(Z_STRVAL_P(repr), (size_t)Z_STRLEN_P(repr), &retval, FALSE)) {
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

#endif	/* HAVE_MBSTRING */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: fdm=marker
 * vim: noet sw=4 ts=4
 */
