/*
 * ===============================================================
 *    Description:  Client methods implementation.
 *
 *        Created:  03/19/2014 11:58:26 PM
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#include <random>

#define weaver_debug_
#include "common/message.h"
#include "common/config_constants.h"
#include "client/client_constants.h"
#include "client/client.h"

// ugly, declared only to get rid of undefined symbol error
// not used in client
DECLARE_CONFIG_CONSTANTS;

using cl::client;
using transaction::pending_update;

client :: client(const char *coordinator="127.0.0.1", uint16_t port=5200, const char *config_file="/etc/weaver.yaml")
    : m_sm(coordinator, port)
    , cur_tx_id(UINT64_MAX)
    , tx_id_ctr(0)
    , handle_ctr(0)
{
    //google::InitGoogleLogging("weaver-client");
    //google::InstallFailureSignalHandler();
    //google::LogToStderr();

    if (!init_config_constants(config_file)) {
        WDEBUG << "error in init_config_constants, exiting now." << std::endl;
        exit(-1);
    }

    std::random_device rd;
    std::default_random_engine generator(rd());
    std::uniform_int_distribution<uint64_t> distribution(0, NumVts-1);
    vtid = distribution(generator);

    assert(m_sm.get_replid(myid) && "get repl_id");
    assert(myid > MaxNumServers);
    myid_str = std::to_string(myid);

    int try_sm = 0;
    while (!maintain_sm_connection()) {
        WDEBUG << "retry sm connection " << try_sm++ << std::endl;
    }

    comm.reset(new cl::comm_wrapper(myid, *m_sm.config()));
}

bool
client :: begin_tx()
{
    if (cur_tx_id != UINT64_MAX) {
        WDEBUG << "only one concurrent transaction per client, currently processing " << cur_tx_id << std::endl;
        return false;
    } else {
        cur_tx_id = ++tx_id_ctr;
        return true;
    }
}

bool
client :: check_active_tx()
{
    if (cur_tx_id == UINT64_MAX) {
        WDEBUG << "No active transaction on this client.  You need to enclose ops in begin_tx-end_tx block." << std::endl;
        return false;
    } else {
        return true;
    }
}

#define CHECK_AUX_INDEX \
    if (!AuxIndex) { \
        WDEBUG << "Cannot access edge by edge_handle without auxiliary indexing turned on.  See weaver.yaml config file." << std::endl; \
        return false; \
    }

void
client :: print_cur_tx()
{
    for (auto upd: cur_tx) {
        switch (upd->type) {
            case transaction::NODE_CREATE_REQ:
                WDEBUG << "NODE CREATE" << std::endl;
                WDEBUG << "\thandle = " << upd->handle <<  std::endl;
                break;

            case transaction::EDGE_CREATE_REQ:
                WDEBUG << "EDGE CREATE" << std::endl;
                WDEBUG << "\thandle = " << upd->handle;
                WDEBUG << "\tstart node,alias = " << upd->handle1 << "," << upd->alias1
                       << " end node,alias = " << upd->handle2 << "," << upd->alias2 << std::endl;
                break;

            case transaction::NODE_DELETE_REQ:
                WDEBUG << "NODE DELETE" << std::endl;
                WDEBUG << "\tnode,alias = " << upd->handle1 << "," << upd->alias1 << std::endl;
                break;

            case transaction::EDGE_DELETE_REQ:
                WDEBUG << "EDGE DELETE" << std::endl;
                WDEBUG << "\tedge = " << upd->handle1 << std::endl;
                WDEBUG << "\tnode,alias = " << upd->handle2 << "," << upd->alias2 << std::endl;
                break;

            case transaction::NODE_SET_PROPERTY:
                WDEBUG << "NODE SET PROPERTY" << std::endl;
                WDEBUG << "\tnode,alias = " << upd->handle1 << "," << upd->alias1 << std::endl;
                WDEBUG << "\tkey,value = " << *upd->key << "," << *upd->value << std::endl;
                break;

            case transaction::EDGE_SET_PROPERTY:
                WDEBUG << "EDGE SET PROPERTY" << std::endl;
                WDEBUG << "\tedge = " << upd->handle1 << std::endl;
                WDEBUG << "\tnode,alias = " << upd->handle2 << "," << upd->alias2 << std::endl;
                WDEBUG << "\tkey,value = " << *upd->key << "," << *upd->value << std::endl;
                break;

            case transaction::ADD_AUX_INDEX:
                WDEBUG << "ADD ALIAS" << std::endl;
                WDEBUG << "\tnode,alias = " << upd->handle1 << "," << upd->handle << std::endl;
                break;
        }
    }
}

bool
client :: create_node(std::string &handle, const std::vector<std::string> &aliases)
{
    if (!check_active_tx()) {
        return false;
    }

    std::shared_ptr<pending_update> upd = std::make_shared<pending_update>();
    upd->type = transaction::NODE_CREATE_REQ;
    if (handle == "") {
        handle = generate_handle();
    }
    upd->handle = handle;
    cur_tx.emplace_back(upd);

    for (const std::string &a: aliases) {
        add_alias(a, upd->handle);
    }

    return true;
}

bool
client :: create_edge(std::string &handle, const std::string &node1, const std::string &node1_alias, const std::string &node2, const std::string &node2_alias)
{
    if (!check_active_tx()) {
        return false;
    }

    std::shared_ptr<pending_update> upd = std::make_shared<pending_update>();
    upd->type = transaction::EDGE_CREATE_REQ;
    if (handle == "") {
        handle = generate_handle();
    }
    upd->handle = handle;
    upd->handle1 = node1;
    upd->handle2 = node2;
    upd->alias1 = node1_alias;
    upd->alias2 = node2_alias;
    cur_tx.emplace_back(upd);
    return true;
}

bool
client :: delete_node(const std::string &node, const std::string &alias)
{
    if (!check_active_tx()) {
        return false;
    }

    std::shared_ptr<pending_update> upd = std::make_shared<pending_update>();
    upd->type = transaction::NODE_DELETE_REQ;
    upd->handle1 = node;
    upd->alias1 = alias;
    cur_tx.emplace_back(upd);

    return true;
}

bool
client :: delete_edge(const std::string &edge, const std::string &node, const std::string &alias)
{
    if (!check_active_tx()) {
        return false;
    }

    std::shared_ptr<pending_update> upd = std::make_shared<pending_update>();
    upd->type = transaction::EDGE_DELETE_REQ;
    upd->handle1 = edge;
    upd->handle2 = node;
    upd->alias2 = alias;
    cur_tx.emplace_back(upd);

    return true;
}

bool
client :: set_node_property(const std::string &node, const std::string &alias, std::string key, std::string value)
{
    if (!check_active_tx()) {
        return false;
    }

    std::shared_ptr<pending_update> upd = std::make_shared<pending_update>();
    upd->type = transaction::NODE_SET_PROPERTY;
    upd->handle1 = node;
    upd->alias1 = alias;
    upd->key.reset(new std::string(std::move(key)));
    upd->value.reset(new std::string(std::move(value)));
    cur_tx.emplace_back(upd);

    return true;
}

bool
client :: set_edge_property(const std::string &node, const std::string &alias, const std::string &edge,
    std::string key, std::string value)
{
    if (!check_active_tx()) {
        return false;
    }

    std::shared_ptr<pending_update> upd = std::make_shared<pending_update>();
    upd->type = transaction::EDGE_SET_PROPERTY;
    upd->handle1 = edge;
    upd->handle2 = node;
    upd->alias2 = alias;
    upd->key.reset(new std::string(std::move(key)));
    upd->value.reset(new std::string(std::move(value)));
    cur_tx.emplace_back(upd);

    return true;
}

bool
client :: add_alias(const std::string &alias, const std::string &node)
{
    if (!check_active_tx()) {
        return false;
    }
    CHECK_AUX_INDEX;

    std::shared_ptr<pending_update> upd = std::make_shared<pending_update>();
    upd->type = transaction::ADD_AUX_INDEX;
    upd->handle = alias;
    upd->handle1 = node;
    cur_tx.emplace_back(upd);

    return true;
}

#undef CHECK_AUX_INDEX

bool
client :: end_tx()
{
    if (cur_tx_id == UINT64_MAX) {
        WDEBUG << "no active tx to commit" << std::endl;
        return false;
    }

    bool retry;
    bool success;
    message::message recv_msg;
    // currently no retry on timeout/disrupted, pass error to client
    // so it is responsibility of client to ensure that they do not reexec tx that was completed
    do {
        message::message msg;
        msg.prepare_message(message::CLIENT_TX_INIT, cur_tx_id, cur_tx);
        busybee_returncode send_code = send_coord(msg.buf);

        if (send_code == BUSYBEE_DISRUPTED) {
            reconfigure();
            WDEBUG << "end_tx() send disrupted" << std::endl;
            return false;
        } else if (send_code != BUSYBEE_SUCCESS) {
            WDEBUG << "end_tx() got busybee_send code " << send_code << ", failing tx" << std::endl;
            return false;
        }

        busybee_returncode recv_code = recv_coord(&recv_msg.buf);

        switch (recv_code) {
            case BUSYBEE_TIMEOUT:
            case BUSYBEE_DISRUPTED:
            reconfigure();
            return false;
            WDEBUG << "end_tx() recv timeout/disrupted" << std::endl;
            break;

            case BUSYBEE_SUCCESS:
            success = true;
            retry = false;
            break;

            default:
            WDEBUG << "end_tx() got busybee_recv code " << recv_code << ", failing tx" << std::endl;
            success = false;
            retry = false;
        }

        if (retry) {
            WDEBUG << "retry end_tx" << std::endl;
        }
    } while (retry);

    if (success) {
        message::msg_type mtype = recv_msg.unpack_message_type();
        assert(mtype == message::CLIENT_TX_SUCCESS || mtype == message::CLIENT_TX_ABORT);
        if (mtype == message::CLIENT_TX_ABORT) {
            success = false;
            WDEBUG << "client abort" << std::endl;
        }
    }

    if (!success) {
        print_cur_tx();
    }

    cur_tx_id = UINT64_MAX;
    cur_tx.clear();

    return success;
}

template <typename ParamsType>
std::unique_ptr<ParamsType>
client :: run_node_program(node_prog::prog_type prog_to_run, std::vector<std::pair<std::string, ParamsType>> &initial_args)
{
    message::message msg;
    busybee_returncode send_code, recv_code;

#ifdef weaver_benchmark_

    msg.prepare_message(message::CLIENT_NODE_PROG_REQ, prog_to_run, initial_args);
    send_code = send_coord(msg.buf);

    if (send_code != BUSYBEE_SUCCESS) {
        WDEBUG << "node prog send msg fail, send_code: " << send_code << std::endl;
        return nullptr;
    }

    recv_code = recv_coord(&msg.buf);

    if (recv_code != BUSYBEE_SUCCESS) {
        WDEBUG << "node prog return msg fail, recv_code: " << recv_code << std::endl;
        return nullptr;
    }

#else

    bool retry;
    do {
        msg.prepare_message(message::CLIENT_NODE_PROG_REQ, prog_to_run, initial_args);
        send_code = send_coord(msg.buf);

        if (send_code == BUSYBEE_DISRUPTED) {
            reconfigure();
            retry = true;
            continue;
        } else if (send_code != BUSYBEE_SUCCESS) {
            WDEBUG << "node prog send msg fail, send_code: " << send_code << std::endl;
            return nullptr;
        }

        recv_code = recv_coord(&msg.buf);

        switch (recv_code) {
            case BUSYBEE_TIMEOUT:
            case BUSYBEE_DISRUPTED:
            reconfigure();
            retry = true;
            break;

            case BUSYBEE_SUCCESS:
            if (msg.unpack_message_type() == message::NODE_PROG_RETRY) {
                retry = true;
            } else {
                retry = false;
            }
            break;

            default:
            WDEBUG << "node prog return msg fail, recv_code: " << recv_code << std::endl;
            return nullptr;
        }
    } while (retry);

#endif

    uint64_t ignore_req_id, ignore_vt_ptr;
    node_prog::prog_type ignore_type;
    auto ret_status = msg.unpack_message_type();
    if (ret_status == message::NODE_PROG_RETURN) {
        std::unique_ptr<ParamsType> toRet(new ParamsType());
        msg.unpack_message(message::NODE_PROG_RETURN, ignore_type, ignore_req_id, ignore_vt_ptr, *toRet);
        return std::move(toRet);
    } else {
        assert(ret_status == message::NODE_PROG_NOTFOUND);
        return nullptr;
    }
}

#define SPECIFIC_NODE_PROG(type) \
    auto ret = run_node_program(type, initial_args); \
    if (ret) { \
        return_param = *ret; \
        return true; \
    } else { \
        return false; \
    }

bool
client :: run_reach_program(std::vector<std::pair<std::string, node_prog::reach_params>> &initial_args, node_prog::reach_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::REACHABILITY);
}

bool
client :: run_pathless_reach_program(std::vector<std::pair<std::string, node_prog::pathless_reach_params>> &initial_args, node_prog::pathless_reach_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::PATHLESS_REACHABILITY);
}

bool
client :: run_clustering_program(std::vector<std::pair<std::string, node_prog::clustering_params>> &initial_args, node_prog::clustering_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::CLUSTERING);
}

bool
client :: run_two_neighborhood_program(std::vector<std::pair<std::string, node_prog::two_neighborhood_params>> &initial_args, node_prog::two_neighborhood_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::TWO_NEIGHBORHOOD);
}

bool
client :: read_node_props_program(std::vector<std::pair<std::string, node_prog::read_node_props_params>> &initial_args, node_prog::read_node_props_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::READ_NODE_PROPS);
}

bool
client :: read_n_edges_program(std::vector<std::pair<std::string, node_prog::read_n_edges_params>> &initial_args, node_prog::read_n_edges_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::READ_N_EDGES);
}

bool
client :: edge_count_program(std::vector<std::pair<std::string, node_prog::edge_count_params>> &initial_args, node_prog::edge_count_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::EDGE_COUNT);
}

bool
client :: edge_get_program(std::vector<std::pair<std::string, node_prog::edge_get_params>> &initial_args, node_prog::edge_get_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::EDGE_GET);
}

bool
client :: node_get_program(std::vector<std::pair<std::string, node_prog::node_get_params>> &initial_args, node_prog::node_get_params &return_param)
{
    SPECIFIC_NODE_PROG(node_prog::NODE_GET);
}

bool
client :: traverse_props_program(std::vector<std::pair<std::string, node_prog::traverse_props_params>> &initial_args, node_prog::traverse_props_params &return_param)
{
    for (auto &p: initial_args) {
        if (p.second.node_props.size() != (p.second.edge_props.size()+1)) {
            WDEBUG << "bad params, #node_props should be (#edge_props + 1)" << std::endl;
            return false;
        }
    }
    SPECIFIC_NODE_PROG(node_prog::TRAVERSE_PROPS);
}

#undef SPECIFIC_NODE_PROG

void
client :: start_migration()
{
    message::message msg;
    msg.prepare_message(message::START_MIGR);
    send_coord(msg.buf);
}

void
client :: single_stream_migration()
{
    message::message msg;
    msg.prepare_message(message::ONE_STREAM_MIGR);
    send_coord(msg.buf);

    if (recv_coord(&msg.buf) != BUSYBEE_SUCCESS) {
        WDEBUG << "single stream migration return msg fail" << std::endl;
    }
}


void
client :: exit_weaver()
{
    message::message msg;
    msg.prepare_message(message::EXIT_WEAVER);
    send_coord(msg.buf);
}

std::vector<uint64_t>
client :: get_node_count()
{
    std::vector<uint64_t> node_count;

    while(true) {
        message::message msg;
        msg.prepare_message(message::CLIENT_NODE_COUNT);
        busybee_returncode send_code = send_coord(msg.buf);

        if (send_code == BUSYBEE_DISRUPTED) {
            reconfigure();
            continue;
        } else if (send_code != BUSYBEE_SUCCESS) {
            WDEBUG << "node count send msg fail with " << send_code << std::endl;
            return node_count;
        }

        busybee_returncode recv_code = recv_coord(&msg.buf);

        switch (recv_code) {
            case BUSYBEE_DISRUPTED:
            case BUSYBEE_TIMEOUT:
            reconfigure();
            break;

            case BUSYBEE_SUCCESS:
            msg.unpack_message(message::NODE_COUNT_REPLY, node_count);
            return node_count;

            default:
            WDEBUG << "node count recv msg fail with " << recv_code << std::endl;
            return node_count;
        }
    }
}

bool
client :: aux_index()
{
    return AuxIndex;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
busybee_returncode
client :: send_coord(std::auto_ptr<e::buffer> buf)
{
    return comm->send(vtid, buf);
}

busybee_returncode
client :: recv_coord(std::auto_ptr<e::buffer> *buf)
{
    busybee_returncode ret;
    while (true) {
        comm->quiesce_thread();
        ret = comm->recv(buf);
        switch (ret) {
            case BUSYBEE_SUCCESS:
            case BUSYBEE_TIMEOUT:
                return ret;

            case BUSYBEE_INTERRUPTED:
                continue;

            default:
                return ret;
        }
    }
}
#pragma GCC diagnostic pop

std::string
client :: generate_handle()
{
    std::string s = std::to_string(handle_ctr++);
    s += myid_str;
    return s;
}

bool
client :: maintain_sm_connection()
{
    replicant_returncode rc;

    if (!m_sm.ensure_configuration(&rc))
    {
        if (rc == REPLICANT_INTERRUPTED)
        {
            WDEBUG << "signal received";
        }
        else if (rc == REPLICANT_TIMEOUT)
        {
            WDEBUG << "operation timed out";
        }
        else
        {
            WDEBUG << "coordinator failure: " << m_sm.error().msg();
        }

        return false;
    }

    return true;
}

void
client :: reconfigure()
{
    uint32_t try_sm = 0;
    while (!maintain_sm_connection()) {
        WDEBUG << "retry sm connection " << try_sm++ << std::endl;
    }

    comm.reset(new cl::comm_wrapper(myid, *m_sm.config()));
    comm->reconfigure(*m_sm.config());
}
