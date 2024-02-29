#ifndef BOOST_TYPE_TRAITS_DETAIL_MP_DEFER_HPP_INCLUDED
#define BOOST_TYPE_TRAITS_DETAIL_MP_DEFER_HPP_INCLUDED

//
//  Copyright 2015 Peter Dimov
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt
//

#include <boost/type_traits/conditional.hpp>
#include <boost/type_traits/integral_constant.hpp>

namespace mars_boost {} namespace boost = mars_boost; namespace mars_boost
{

namespace type_traits_detail
{

// mp_valid
// implementation by Bruno Dutra (by the name is_evaluable)

template<template<class...> class F, class... T>
struct mp_valid_impl
{
    template<template<class...> class G, class = G<T...>>
    static mars_boost::true_type check_s(int);

    template<template<class...> class>
    static mars_boost::false_type check_s(...);

    using type = decltype(check_s<F>(0));
};

template<template<class...> class F, class... T>
using mp_valid = typename mp_valid_impl<F, T...>::type;

// mp_defer

struct mp_empty
{
};

template<template<class...> class F, class... T> struct mp_defer_impl
{
    using type = F<T...>;
};

template<template<class...> class F, class... T> using mp_defer = typename mars_boost::conditional<mp_valid<F, T...>::value, mp_defer_impl<F, T...>, mp_empty>::type;

} // namespace type_traits_detail

} // namespace mars_boost

#endif // #ifndef BOOST_TYPE_TRAITS_DETAIL_MP_DEFER_HPP_INCLUDED
