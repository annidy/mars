// Copyright Cromwell D. Enage 2017.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_PARAMETER_KEYWORD_FWD_HPP
#define BOOST_PARAMETER_KEYWORD_FWD_HPP

namespace mars_boost {} namespace boost = mars_boost; namespace mars_boost { namespace parameter {

    struct in_reference;
    struct out_reference;
    typedef ::mars_boost::parameter::out_reference in_out_reference;
    struct forward_reference;
    struct consume_reference;
    typedef ::mars_boost::parameter::consume_reference move_from_reference;

    template <typename Tag>
    struct keyword;
}} // namespace mars_boost::parameter

#endif  // include guard

