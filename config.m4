dnl
dnl $Id: config.m4,v 1.58.2.4.2.11.2.8 2009/04/20 15:39:48 jani Exp $
dnl

PHP_ARG_ENABLE(mbstring_ng, whether to enable multibyte string support,
[  --enable-mbstring-ng     Enable multibyte string support])

if test "$PHP_MBSTRING" != "no"; then  
  AC_DEFINE([HAVE_MBSTRING_NG],1,[whether to have multibyte string support])

  PHP_SETUP_ICU(MBSTRING_NG_SHARED_LIBADD)
  PHP_REQUIRE_CXX()

  PHP_NEW_EXTENSION(mbstring_ng, [mbstring.c], $ext_shared)
  PHP_SUBST(MBSTRING_NG_SHARED_LIBADD)

  for dir in $PHP_MBSTRING_NG_EXTRA_BUILD_DIRS; do
    PHP_ADD_BUILD_DIR([$ext_builddir/$dir], 1)
  done
  
  for dir in $PHP_MBSTRING_NG_EXTRA_INCLUDES; do
    PHP_ADD_INCLUDE([$ext_srcdir/$dir])
    PHP_ADD_INCLUDE([$ext_builddir/$dir])
  done

  PHP_INSTALL_HEADERS([ext/mbstring], [php_mbstring.h])
fi

# vim600: sts=2 sw=2 et
