//
// lw_mutex_test.cpp
//
// Copyright 2005 Peter Dimov
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/detail/lightweight_mutex.hpp>

// Sanity check only

mars_boost::detail::lightweight_mutex m1;

int main()
{
    mars_boost::detail::lightweight_mutex::scoped_lock lock1( m1 );

    mars_boost::detail::lightweight_mutex m2;
    mars_boost::detail::lightweight_mutex m3;

    mars_boost::detail::lightweight_mutex::scoped_lock lock2( m2 );
    mars_boost::detail::lightweight_mutex::scoped_lock lock3( m3 );

    return 0;
}
