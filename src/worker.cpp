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

#include "worker.hpp"

#include <cocaine/context.hpp>
#include <cocaine/logging.hpp>
#include <cocaine/manifest.hpp>
#include <cocaine/profile.hpp>

#include <cocaine/api/sandbox.hpp>

#include <cocaine/traits/unique_id.hpp>

#include <boost/filesystem/path.hpp>

using namespace cocaine;
using namespace cocaine::engine;
using namespace cocaine::io;
using namespace cocaine::logging;

namespace fs = boost::filesystem;

namespace {
    struct upstream_t:
        public api::stream_t
    {
        upstream_t(uint64_t id,
                   worker_t * const worker):
            m_id(id),
            m_worker(worker),
            m_state(state_t::open)
        { }

        virtual
        ~upstream_t() {
            if(m_state != state_t::closed) {
                close();
            }
        }

        virtual
        void
        push(const char * chunk,
             size_t size)
        {
            switch(m_state) {
                case state_t::open:
                    send<rpc::chunk>(std::string(chunk, size));
                    break;

                case state_t::closed:
                    throw cocaine::error_t("the stream has been closed");
            }
        }

        virtual
        void
        error(error_code code,
              const std::string& message)
        {
            switch(m_state) {
                case state_t::open:
                    m_state = state_t::closed;

                    send<rpc::error>(static_cast<int>(code), message);
                    send<rpc::choke>();

                    break;

                case state_t::closed:
                    throw cocaine::error_t("the stream has been closed");
            }
        }

        virtual
        void
        close() {
            switch(m_state) {
                case state_t::open:
                    m_state = state_t::closed;

                    send<rpc::choke>();

                    break;

                case state_t::closed:
                    throw cocaine::error_t("the stream has been closed");
            }
        }

    private:
        template<class Event, typename... Args>
        void
        send(Args&&... args) {
            m_worker->send<Event>(m_id, std::forward<Args>(args)...);
        }

    private:
        const uint64_t m_id;
        worker_t * const m_worker;

        enum class state_t: int {
            open,
            closed
        };

        state_t m_state;
    };
}

worker_t::worker_t(context_t& context,
                   worker_config_t config):
    m_context(context),
    m_log(new log_t(context, cocaine::format("app/%s", config.app))),
    m_id(config.uuid),
    m_channel(context, ZMQ_DEALER, m_id)
{
    std::string endpoint = cocaine::format(
        "ipc://%1%/engines/%2%",
        m_context.config.path.runtime,
        config.app
    );
    
    m_channel.connect(endpoint);
    
    m_watcher.set<worker_t, &worker_t::on_event>(this);
    m_watcher.start(m_channel.fd(), ev::READ);
    m_checker.set<worker_t, &worker_t::on_check>(this);
    m_checker.start();

    m_heartbeat_timer.set<worker_t, &worker_t::on_heartbeat>(this);
    m_heartbeat_timer.start(0.0f, 5.0f);

    // Launching the app

    try {
        m_manifest.reset(new manifest_t(m_context, config.app));
        m_profile.reset(new profile_t(m_context, config.profile));
        
        fs::path path = fs::path(m_context.config.path.spool) / config.app;
         
        m_sandbox = m_context.get<api::sandbox_t>(
            m_manifest->sandbox.type,
            m_context,
            m_manifest->name,
            m_manifest->sandbox.args,
            path.string()
        );
    } catch(const std::exception& e) {
        terminate(rpc::suicide::abnormal, e.what());
        throw;
    } catch(...) {
        terminate(rpc::suicide::abnormal, "unexpected exception");
        throw;
    }
    
    m_disown_timer.set<worker_t, &worker_t::on_disown>(this);
    m_disown_timer.start(m_profile->heartbeat_timeout);
}

worker_t::~worker_t() {
    // Empty.
}

void
worker_t::run() {
    m_loop.loop();
}

void
worker_t::on_event(ev::io&, int) {
    m_checker.stop();

    if(m_channel.pending()) {
        m_checker.start();
        process();
    }
}

void
worker_t::on_check(ev::prepare&, int) {
    m_loop.feed_fd_event(m_channel.fd(), ev::READ);
}

void
worker_t::on_heartbeat(ev::timer&, int) {
    scoped_option<
        options::send_timeout
    > option(m_channel, 0);
    
    send<rpc::heartbeat>();
}

void
worker_t::on_disown(ev::timer&, int) {
    COCAINE_LOG_ERROR(
        m_log,
        "worker %s has lost the controlling engine",
        m_id
    );

    m_loop.unloop(ev::ALL);    
}

void
worker_t::process() {
    int counter = defaults::io_bulk_size;

    zmq::message_t blob;
    io::message_t message;

    while(counter--) {
        {
            scoped_option<
                options::receive_timeout
            > option(m_channel, 0);

            if(!m_channel.recv(blob)) {
                return;
            }
        }

        message = m_codec.unpack(blob);

        COCAINE_LOG_DEBUG(
            m_log,
            "worker %s received type %d message",
            m_id,
            message.id()
        );

        switch(message.id()) {
            case event_traits<rpc::heartbeat>::id:
                m_disown_timer.stop();
                m_disown_timer.start(m_profile->heartbeat_timeout);
                
                break;

            case event_traits<rpc::invoke>::id: {
                uint64_t session_id;
                std::string event;

                message.as<rpc::invoke>(session_id, event);

                boost::shared_ptr<api::stream_t> upstream(
                    boost::make_shared<upstream_t>(session_id, this)
                );

                try {
                    io_pair_t io = {
                        upstream,
                        m_sandbox->invoke(event, upstream)
                    };

                    m_streams.insert(std::make_pair(session_id, io));
                } catch(const std::exception& e) {
                    upstream->error(invocation_error, e.what());
                } catch(...) {
                    upstream->error(invocation_error, "unexpected exception");
                }

                break;
            }

            case event_traits<rpc::chunk>::id: {
                uint64_t session_id;
                std::string chunk;

                message.as<rpc::chunk>(session_id, chunk);

                stream_map_t::iterator it(m_streams.find(session_id));

                // NOTE: This may be a chunk for a failed invocation, in which case there
                // will be no active stream, so drop the message.
                if(it != m_streams.end()) {
                    try {
                        it->second.downstream->push(chunk.data(), chunk.size());
                    } catch(const std::exception& e) {
                        it->second.upstream->error(invocation_error, e.what());
                        m_streams.erase(it);
                    } catch(...) {
                        it->second.upstream->error(invocation_error, "unexpected exception");
                        m_streams.erase(it);
                    }
                }

                break;
            }

            case event_traits<rpc::choke>::id: {
                uint64_t session_id;

                message.as<rpc::choke>(session_id);

                stream_map_t::iterator it = m_streams.find(session_id);

                // NOTE: This may be a choke for a failed invocation, in which case there
                // will be no active stream, so drop the message.
                if(it != m_streams.end()) {
                    try {
                        it->second.downstream->close();
                    } catch(const std::exception& e) {
                        it->second.upstream->error(invocation_error, e.what());
                    } catch(...) {
                        it->second.upstream->error(invocation_error, "unexpected exception");
                    }
                    
                    m_streams.erase(it);
                }

                break;
            }
            
            case event_traits<rpc::terminate>::id:
                terminate(rpc::suicide::normal, "per request");
                break;

            default:
                COCAINE_LOG_WARNING(
                    m_log,
                    "worker %s dropping unknown type %d message", 
                    m_id,
                    message.id()
                );
        }
    }

    // Feed the event loop.
    m_loop.feed_fd_event(m_channel.fd(), ev::READ);
}

void
worker_t::terminate(rpc::suicide::reasons reason,
                    const std::string& message)
{
    send<rpc::suicide>(static_cast<int>(reason), message);
    m_loop.unloop(ev::ALL);
}
