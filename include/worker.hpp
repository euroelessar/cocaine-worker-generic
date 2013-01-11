/*
    Copyright (c) 2011-2012 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2012 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>. 
*/

#ifndef COCAINE_GENERIC_WORKER_HPP
#define COCAINE_GENERIC_WORKER_HPP

#include <cocaine/common.hpp>
#include <cocaine/asio.hpp>
#include <cocaine/rpc.hpp>
#include <cocaine/unique_id.hpp>

#include <cocaine/api/stream.hpp>

namespace cocaine { namespace engine {

struct worker_config_t {
    std::string app;
    std::string profile;
    std::string uuid;
};

class worker_t:
    public boost::noncopyable
{
    public:
        worker_t(context_t& context,
                 worker_config_t config);

        ~worker_t();

        void
        run();

        template<class Event, typename... Args>
        void
        send(Args&&... args);

    private:
        void
        on_event(ev::io&, int);
        
        void
        on_check(ev::prepare&, int);
        
        void
        on_heartbeat(ev::timer&, int);

        void
        on_disown(ev::timer&, int);

        void
        process();
        
        void
        terminate(io::rpc::suicide::reasons reason,
                  const std::string& message);

    private:
        context_t& m_context;
        std::unique_ptr<logging::log_t> m_log;

        // Configuration

        const unique_id_t m_id;

        // Engine I/O

        io::unique_channel_t m_channel;
        
        // Event loop

        ev::default_loop m_loop;
        
        ev::io m_watcher;
        ev::prepare m_checker;
        
        ev::timer m_heartbeat_timer,
                  m_disown_timer;
        
        // The app

        std::unique_ptr<const manifest_t> m_manifest;
        std::unique_ptr<const profile_t> m_profile;
        std::unique_ptr<api::sandbox_t> m_sandbox;

        struct io_pair_t {
            boost::shared_ptr<api::stream_t> upstream;
            boost::shared_ptr<api::stream_t> downstream;
        };

    #if BOOST_VERSION >= 103600
        typedef boost::unordered_map<
    #else
        typedef std::map<
    #endif
            uint64_t,
            io_pair_t
        > stream_map_t;

        // Session streams.
        stream_map_t m_streams;
};

template<class Event, typename... Args>
void
worker_t::send(Args&&... args) {
    m_channel.send<Event>(std::forward<Args>(args)...);
}

}} // namespace cocaine::engine

#endif
