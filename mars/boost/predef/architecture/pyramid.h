/*
Copyright Rene Rivera 2011-2015
Distributed under the Boost Software License, Version 1.0.
(See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef BOOST_PREDEF_ARCHITECTURE_PYRAMID_H
#define BOOST_PREDEF_ARCHITECTURE_PYRAMID_H

#include <boost/predef/make.h>
#include <boost/predef/version_number.h>

/* tag::reference[]
= `BOOST_ARCH_PYRAMID`

Pyramid 9810 architecture.

[options="header"]
|===
| {predef_symbol} | {predef_version}

| `pyr` | {predef_detection}
|===
*/ // end::reference[]

#define BOOST_ARCH_PYRAMID BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if defined(pyr)
#   undef BOOST_ARCH_PYRAMID
#   define BOOST_ARCH_PYRAMID BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_ARCH_PYRAMID
#   define BOOST_ARCH_PYRAMID_AVAILABLE
#endif

#if BOOST_ARCH_PYRAMID
#   undef BOOST_ARCH_WORD_BITS_32
#   define BOOST_ARCH_WORD_BITS_32 BOOST_VERSION_NUMBER_AVAILABLE
#endif

#define BOOST_ARCH_PYRAMID_NAME "Pyramid 9810"

#endif

#include <boost/predef/detail/test.h>
BOOST_PREDEF_DECLARE_TEST(BOOST_ARCH_PYRAMID,BOOST_ARCH_PYRAMID_NAME)
