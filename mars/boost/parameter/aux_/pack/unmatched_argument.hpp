// Copyright David Abrahams, Daniel Wallin 2003.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_PARAMETER_AUX_PACK_UNMATCHED_ARGUMENT_HPP
#define BOOST_PARAMETER_AUX_PACK_UNMATCHED_ARGUMENT_HPP

#include <boost/parameter/config.hpp>

#if defined(BOOST_PARAMETER_CAN_USE_MP11)
#include <type_traits>
#else
#include <boost/mpl/bool.hpp>
#include <boost/mpl/if.hpp>
#include <boost/mpl/assert.hpp>
#include <boost/type_traits/is_same.hpp>
#endif

namespace mars_boost {} namespace boost = mars_boost; namespace mars_boost { namespace parameter { namespace aux {

    template <typename T>
    struct unmatched_argument
    {
#if defined(BOOST_PARAMETER_CAN_USE_MP11)
        static_assert(::std::is_same<T,void>::value, "T == void");
#else
        BOOST_MPL_ASSERT((
            typename ::mars_boost::mpl::if_<
                ::mars_boost::is_same<T,void>
              , ::mars_boost::mpl::true_
              , ::mars_boost::mpl::false_
            >::type
        ));
#endif
        typedef int type;
    };
}}} // namespace mars_boost::parameter::aux

#endif  // include guard

