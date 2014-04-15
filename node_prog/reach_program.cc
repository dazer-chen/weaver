/*
 * ===============================================================
 *    Description:  Reachability program.
 *
 *        Created:  Sunday 23 April 2013 11:00:03  EDT
 *
 *         Author:  Ayush Dubey, Greg Hill
 *                  dubey@cs.cornell.edu, gdh39@cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ================================================================
 */

//#define weaver_debug_
#include "reach_program.h"

namespace node_prog
{
    inline bool
    check_cache_context(cache_response<reach_cache_value> &cr)
    {
        std::vector<node_cache_context>& contexts = cr.get_context();
        if (contexts.size() == 0) {
            return true;
        }
        reach_cache_value &cv = *cr.get_value();
        // path not valid if broken by:
        for (node_cache_context& node_context : contexts)
        {
            if (node_context.node_deleted){  // node deletion
                WDEBUG  << "Cache entry invalid because of node deletion" << std::endl;
                return false;
            }
            // edge deletion, see if path was broken
            for (size_t i = 1; i < cv.path.size(); i++) {
                if (node_context.node == cv.path.at(i)) {
                    db::element::remote_node &path_next_node = cv.path.at(i-1);
                    for(auto &edge : node_context.edges_deleted){
                        if (edge.nbr == path_next_node) {
                            WDEBUG  << "Cache entry invalid because of edge deletion" << std::endl;
                            return false;
                        }
                    }
                    break; // path not broken here, move on
                }
            }
        }
        WDEBUG  << "Cache entry with context size " << contexts.size() << " valid" << std::endl;
        return true;
    }

    std::vector<std::pair<db::element::remote_node, reach_params>> 
    reach_node_program(
            node &n,
            db::element::remote_node &rn,
            reach_params &params,
            std::function<reach_node_state&()> state_getter,
            std::function<void(std::shared_ptr<reach_cache_value>, // TODO make const
                std::shared_ptr<std::vector<db::element::remote_node>>, uint64_t)>& add_cache_func,
            cache_response<reach_cache_value>*cache_response)
    {
        WDEBUG  << "REACH AT NODE " << rn.id << std::endl;
        reach_node_state &state = state_getter();
        std::vector<std::pair<db::element::remote_node, reach_params>> next;
        bool false_reply = false;
        db::element::remote_node prev_node = params.prev_node;
        params.prev_node = rn;
        if (!params.returning) { // request mode
            if (params.dest == rn.get_id()) {
                // we found the node we are looking for, prepare a reply
                params.returning = true;
                params.reachable = true;
                params.path.emplace_back(rn);
                params._search_cache = false; // never search on way back
                WDEBUG  << "returning to node " << prev_node.id << " because found " << std::endl;
                return {std::make_pair(prev_node, params)};
            } else {
                // have not found it yet so follow all out edges
                if (!state.visited) {
                    state.prev_node = prev_node;
                    state.visited = true;

                    if (MAX_CACHE_ENTRIES)
                    {
                        WDEBUG  << "in cache section with search cache " << params._search_cache << ", cache key = " << params._cache_key << " and cache response "<< cache_response << std::endl;
                        if (params._search_cache /*&& !params.returning */ && cache_response != NULL){
                            // check context, update cache
                            if (check_cache_context(*cache_response)) { // if context is valid
                                // we found the node we are looking for, prepare a reply
                                params.returning = true;
                                params.reachable = true;
                                params._search_cache = false; // don't search on way back

                                // context for cached value contains the nodes in the path to the dest_idination from this node
                                params.path = std::dynamic_pointer_cast<reach_cache_value>(cache_response->get_value())->path; // XXX double check this path
                                WDEBUG  << "Cache worked at node " << rn.id << " with path len " << params.path.size() << std::endl;
                                WDEBUG << "path is "<< std::endl;
                                for (db::element::remote_node &r : params.path) {
                                    WDEBUG <<  r.id << std::endl;
                                }
                                WDEBUG << std::endl;
                                WDEBUG  << "returning to node " << prev_node.id << " in positive cache response " << std::endl;
                                return {std::make_pair(prev_node, params)}; // single length vector
                            } else {
                                cache_response->invalidate();
                            }
                        }
                    }

                    for (edge &e: n.get_edges()) {
                        // checking edge properties
                        if (e.has_all_properties(params.edge_props)) {
                            // e->traverse(); no more traversal recording

                            // propagate reachability request
                            next.emplace_back(std::make_pair(e.get_neighbor(), params));
                            WDEBUG  << "emplacing " << e.get_neighbor().id << " to next" << std::endl;
                            state.out_count++;
                        }
                    }
                    if (state.out_count == 0) {
                        false_reply = true;
                    }
                } else {
                    false_reply = true;
                }
            }
            if (false_reply) {
                params.returning = true;
                params.reachable = false;
                next.emplace_back(std::make_pair(prev_node, params));
            }
        } else { // reply mode
            if (params.reachable) {
                if (state.hops > params.hops) {
                    state.hops = params.hops;
                }
            }
            if (((--state.out_count == 0) || params.reachable) && !state.reachable) {
                state.reachable |= params.reachable;
                if (params.reachable) {
                    params.hops = state.hops + 1;
                    params.path.emplace_back(rn);
                    if (MAX_CACHE_ENTRIES)
                    {
                        // now add to cache
                        WDEBUG << "adding to cache for key " << params.dest << " on way back from dest on node " << rn.id << " with path len " << params.path.size() << std::endl;
                        WDEBUG << "path is "<< std::endl;
                        for (db::element::remote_node &r : params.path) {
                            WDEBUG <<  r.id<< std::endl;
                        }
                        WDEBUG << std::endl;
                        std::shared_ptr<node_prog::reach_cache_value> toCache(new reach_cache_value(params.path));
                        std::shared_ptr<std::vector<db::element::remote_node>> watch_set(new std::vector<db::element::remote_node>(params.path)); // copy return path from params
                        add_cache_func(toCache, watch_set, params.dest);
                    }
                }
                next.emplace_back(std::make_pair(state.prev_node, params));
            }
            if ((int)state.out_count < 0) {
                WDEBUG << "ALERT! Bad state value in reach program" << std::endl;
                next.clear();
            }
        }
        WDEBUG << "propagating to "<< std::endl;
        for (auto &pair : next) {
            WDEBUG << pair.first.id << std::endl;
        }
        WDEBUG << std::endl;
        return next;
    }
}
