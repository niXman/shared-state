
#ifndef __shared_state_server__session_manager_hpp__included
#define __shared_state_server__session_manager_hpp__included

#include "utils.hpp"
#include "session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include <list>

/**********************************************************************************************************************/

struct session_manager: noncopyable_nonmovable {
    session_manager(ba::io_context &ioc)
        :m_strand{ioc}
        ,m_list{}
    {}

    holder_ptr create(tcp::socket sock) {
        auto sptr = std::make_shared<session>(std::move(sock));
        std::weak_ptr<session> weak = sptr;
        ba::post(
             m_strand
            ,[this, weak]
             ()
             { m_list.push_back(weak); }
        );

        return sptr;
    }

    void broadcast(shared_buffer msg) {
        ba::post(
             m_strand
            ,[this, msg=std::move(msg)](){
                for ( auto it = m_list.begin(); it != m_list.end(); ) {
                    if ( auto session = it->lock(); session ) {
                        ++it;
                        auto *sptr = session.get();
                        sptr->send(
                             [](){}
                            ,[](const char *, int, const char *, error_category, bs::error_code){}
                            ,msg
                            ,std::move(session)
                        );
                    } else {
                        it = m_list.erase(it);
                    }
                }
            }
        );
    }

private:
    ba::io_context::strand m_strand;
    std::list<std::weak_ptr<session>> m_list;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__session_manager_hpp__included
