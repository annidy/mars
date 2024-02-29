
//  (C) Copyright John Maddock 2018. 
//  Use, modification and distribution are subject to the Boost Software License,
//  Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt).
//
//  See http://www.boost.org/libs/type_traits for most recent version including documentation.

#ifndef BOOST_TT_IS_FUNCTION_MSVC10_FIX_HPP_INCLUDED
#define BOOST_TT_IS_FUNCTION_MSVC10_FIX_HPP_INCLUDED

namespace mars_boost {} namespace boost = mars_boost; namespace mars_boost {

template <class R> struct is_function<R(&&)()> : public false_type {};
template <class R> struct is_function<R(&&)(...)> : public false_type {};
template <class R, class Arg1> struct is_function<R(&&)(Arg1)> : public false_type {};
template <class R, class Arg1> struct is_function<R(&&)(Arg1, ...)> : public false_type {};
template <class R, class Arg1, class Arg2> struct is_function<R(&&)(Arg1, Arg2)> : public false_type {};
template <class R, class Arg1, class Arg2> struct is_function<R(&&)(Arg1, Arg2, ...)> : public false_type {};
template <class R, class Arg1, class Arg2, class Arg3> struct is_function<R(&&)(Arg1, Arg2, Arg3)> : public false_type {};
template <class R, class Arg1, class Arg2, class Arg3> struct is_function<R(&&)(Arg1, Arg2, Arg3, ...)> : public false_type {};
template <class R, class Arg1, class Arg2, class Arg3, class Arg4> struct is_function<R(&&)(Arg1, Arg2, Arg3, Arg4)> : public false_type {};
template <class R, class Arg1, class Arg2, class Arg3, class Arg4> struct is_function<R(&&)(Arg1, Arg2, Arg3, Arg4, ...)> : public false_type {};
template <class R, class Arg1, class Arg2, class Arg3, class Arg4, class Arg5> struct is_function<R(&&)(Arg1, Arg2, Arg3, Arg4, Arg5)> : public false_type {};
template <class R, class Arg1, class Arg2, class Arg3, class Arg4, class Arg5> struct is_function<R(&&)(Arg1, Arg2, Arg3, Arg4, Arg5, ...)> : public false_type {};

} // namespace mars_boost

#endif // BOOST_TT_IS_REFERENCE_HPP_INCLUDED

