#pragma once

#include <algorithm>
#include <map>
#include <cassert>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cata
{

/**
 * @brief Sort elements of a sequence by their rating (the smaller the better).
 *
 * @param  begin           An input/output iterator.
 * @param  end             An input/output iterator.
 * @param  rating_func     A functor which calculates ratings (the outcome
 *                         must be comparable using '<' operator).
 *
 * The rating is calculated only once per each element.
 */
template<typename Iterator, typename RatingFunction>
void sort_by_rating( Iterator begin, Iterator end, RatingFunction rating_func )
{
    using value_t = typename Iterator::value_type;
    using pair_t = std::pair<value_t, decltype( rating_func( *begin ) )>;

    std::vector<pair_t> rated_entries;
    rated_entries.reserve( std::distance( begin, end ) );

    std::transform( begin, end, std::back_inserter( rated_entries ),
    [ &rating_func ]( const value_t &elem ) {
        return std::make_pair( elem, rating_func( elem ) );
    } );

    std::sort( rated_entries.begin(), rated_entries.end(), []( const pair_t &lhs, const pair_t &rhs ) {
        return lhs.second < rhs.second;
    } );

    std::transform( rated_entries.begin(), rated_entries.end(), begin, []( const pair_t &elem ) {
        return elem.first;
    } );
}

// Implementation detail of below find_cycles
// This explores one branch of the given graph depth-first
template<typename T>
void find_cycles_impl(
    const std::unordered_map<T, std::vector<T>> &edges,
    const T &v,
    std::unordered_set<T> &visited,
    std::unordered_map<T, T> &on_current_branch,
    std::vector<std::vector<T>> &result )
{
    bool new_vertex = visited.insert( v ).second;

    if( !new_vertex ) {
        return;
    }
    auto it = edges.find( v );
    if( it == edges.end() ) {
        return;
    }

    for( const T &next_v : it->second ) {
        if( next_v == v ) {
            // Trivial self-loop
            result.push_back( { v } );
            continue;
        }
        auto previous_match = on_current_branch.find( next_v );
        if( previous_match != on_current_branch.end() ) {
            // We have looped back to somewhere along the branch we took to
            // reach this vertex, so reconstruct the loop and save it.
            std::vector<T> loop;
            T on_path = v;
            while( true ) {
                loop.push_back( on_path );
                if( on_path == next_v ) {
                    break;
                }
                on_path = on_current_branch[on_path];
            }
            std::reverse( loop.begin(), loop.end() );
            result.push_back( loop );
        } else {
            on_current_branch.emplace( next_v, v );
            find_cycles_impl( edges, next_v, visited, on_current_branch, result );
            on_current_branch.erase( next_v );
        }
    }
}

// Find and return a list of all cycles in a directed graph.
// Each T defines a vertex.
// For a vertex a, edges[a] is a list of all the vertices connected by edges
// from a.
// It is acceptable for some vertex keys to be missing from the edges map, if
// those vertices have no out-edges.
// Complexity should be O(V+E)
// Based on https://www.geeksforgeeks.org/detect-cycle-in-a-graph/
template<typename T>
std::vector<std::vector<T>> find_cycles( const std::unordered_map<T, std::vector<T>> &edges )
{
    std::unordered_set<T> visited;
    std::unordered_map<T, T> on_current_branch;
    std::vector<std::vector<T>> result;

    for( const auto &p : edges ) {
        const T &root = p.first;

        on_current_branch.emplace( root, root );
        find_cycles_impl( edges, root, visited, on_current_branch, result );
        on_current_branch.erase( root );
        assert( on_current_branch.empty() );
    }

    return result;
}

/// @brief Group elements of a container into key-value map by a given selector function.
///
/// @tparam M map type to use for the result. defaults to std::map
/// @tparam C container type, usually inferred
/// @param selector mapped to each container. otuput type is used as a key for the result map
///
/// @return A map of key-value pairs, where the the value is a container of elements grouped by the key.
///
/// @example
/// ```cpp
/// group_by( {1, 2, 3, 4, 5}, []( int i ) -> std::string { return i % 2 == 0 ? "even" : "odd" } )
/// //=> Map{ "even": {2, 4}, "odd": {1, 3, 5} }
/// ```
///
/// @remark poor person's https://kotlinlang.org/api/latest/jvm/stdlib/kotlin.collections/group-by.html
template<template<typename...> typename M = std::map, typename C, typename F>
auto group_by( const C &c, F && selector )
{
    using K = std::invoke_result_t<F, std::decay_t<decltype( *c.begin() )>>;

    auto result = M<K, C> {};
    for( const auto &elem : c ) {
        result[selector( elem )].emplace_back( elem );
    }
    return result;
}

} // namespace cata
