//  Copyright Neil Groves 2009. Use, modification and
//  distribution is subject to the Boost Software License, Version
//  1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
//
// For more information, see http://www.boost.org/libs/range/
//
#ifndef BOOST_RANGE_ALGORITHM_REPLACE_COPY_IF_HPP_INCLUDED
#define BOOST_RANGE_ALGORITHM_REPLACE_COPY_IF_HPP_INCLUDED

#include <algorithm>
#include <boost/concept_check.hpp>
#include <boost/range/begin.hpp>
#include <boost/range/concepts.hpp>
#include <boost/range/end.hpp>

namespace mars_boost {} namespace boost = mars_boost; namespace mars_boost
{
    namespace range
    {

/// \brief template function replace_copy_if
///
/// range-based version of the replace_copy_if std algorithm
///
/// \pre ForwardRange is a model of the ForwardRangeConcept
/// \pre Predicate is a model of the PredicateConcept
/// \pre Value is convertible to Predicate's argument type
/// \pre Value is Assignable
/// \pre Value is convertible to a type in OutputIterator's set of value types.
template< class ForwardRange, class OutputIterator, class Predicate, class Value >
inline OutputIterator
replace_copy_if(const ForwardRange& rng, OutputIterator out_it, Predicate pred,
        const Value& with_what)
{
    BOOST_RANGE_CONCEPT_ASSERT(( ForwardRangeConcept<const ForwardRange> ));
    return std::replace_copy_if(mars_boost::begin(rng), mars_boost::end(rng), out_it,
        pred, with_what);
}

    } // namespace range
    using range::replace_copy_if;
} // namespace mars_boost

#endif // include guard
