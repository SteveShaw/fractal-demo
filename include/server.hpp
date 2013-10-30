#ifndef SERVER_HPP
#define SERVER_HPP

#include <set>
#include <map>
#include <vector>
#include <functional>

#include <QLabel>
#include <QObject>
#include <QPicture>
#include <QByteArray>

#include "cppa/cppa.hpp"

#include "imagelabel.h"
#include "mainwidget.hpp"
#include "config_map.hpp"
#include "fractal_request.hpp"
#include "fractal_request_stream.hpp"

class server : public cppa::event_based_actor {

 public:

    server(config_map& config);

    virtual void init() override;

 private:

    std::uint32_t m_interval;   // in msecs
    std::uint32_t m_queuesize;
    std::uint32_t m_next_id;
    std::uint32_t m_assign_id;
    std::uint32_t m_iterations;

    // maximum available workers
    std::uint32_t m_max_normal;
    std::uint32_t m_max_opencl;

    // current workers in use
    std::uint32_t m_cur_normal;
    std::uint32_t m_cur_opencl;

    // limit through controller
    std::uint32_t m_lim_normal;
    std::uint32_t m_lim_opencl;

    // idle workes
    std::vector<cppa::actor_ptr> m_normal_actor_buffer;
    std::vector<cppa::actor_ptr> m_opencl_actor_buffer;

    // distributed jobs
    std::map<cppa::actor_ptr, std::uint32_t> m_current_jobs;

    fractal_request_stream m_stream;

    void init(cppa::actor_ptr image_receiver);

    void send_next_job(const cppa::actor_ptr& worker, bool is_opencl_enabled);

};

#endif
