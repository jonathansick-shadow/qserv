/* 
 * LSST Data Management System
 * Copyright 2011, 2012 LSST Corporation.
 * 
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the LSST License Statement and 
 * the GNU General Public License along with this program.  If not, 
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */
/// TodoList.h
/// A class that contains a collection of tasks to be executed by the
/// Qserv-worker.  Allows selection and prioritization on top of a 
/// generic container.
/// @author Daniel L. Wang (danielw)
#ifndef LSST_QSERV_WORKER_TODOLIST_H
#define LSST_QSERV_WORKER_TODOLIST_H
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>

#include <deque>
#include "lsst/qserv/worker.pb.h"

#include "lsst/qserv/worker/Base.h"

namespace lsst {
namespace qserv {
namespace worker {
class QueryRunnerArg;    // forward

class TodoList : public TaskAcceptor {
public:
    typedef boost::shared_ptr<TodoList> Ptr;

    typedef lsst::qserv::TaskMsg_Fragment Fragment;
    typedef boost::shared_ptr<Fragment> FragmentPtr;

    typedef lsst::qserv::TaskMsg TaskMsg;
    typedef boost::shared_ptr<TaskMsg> TaskMsgPtr;
    struct Task;
    typedef boost::shared_ptr<Task> TaskPtr;
    typedef std::deque<TaskPtr> TaskQueue;

    class Watcher {
    public:
        typedef boost::shared_ptr<Watcher> Ptr;
        virtual ~Watcher() {}
        virtual void handleAccept(TodoList& t) = 0; // Must not block.
    };

    TodoList() {
    }
    
    virtual ~TodoList() {}
    virtual bool accept(boost::shared_ptr<TaskMsg> msg);

    // Reusing existing QueryRunnerArg for now.
    boost::shared_ptr<QueryRunnerArg> popTask();
    
    class MatchF {
    public: 
        virtual ~MatchF();
        virtual bool operator()(TaskMsg const& tm) = 0;
    };
    boost::shared_ptr<QueryRunnerArg> popTask(MatchF& m);

private:    
    typedef std::deque<Watcher::Ptr> WatcherQueue;
    
    void _notifyWatchers();

    TaskQueue _tasks;
    WatcherQueue _watchers;
    boost::mutex _watchersMutex;
    boost::mutex _tasksMutex;
    
};
}}} // lsst::qserv::worker
#endif // LSST_QSERV_WORKER_TODOLIST_H
