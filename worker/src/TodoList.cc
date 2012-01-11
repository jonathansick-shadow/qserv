/* 
 * LSST Data Management System
 * Copyright 2008, 2009, 2010 LSST Corporation.
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
 
#include "lsst/qserv/worker/TodoList.h"
#include "lsst/qserv/worker/Base.h"
#include "lsst/qserv/TaskMsgDigest.h"

namespace qWorker = lsst::qserv::worker;

////////////////////////////////////////////////////////////////////////
// anonymous helpers
////////////////////////////////////////////////////////////////////////
namespace {
    boost::shared_ptr<qWorker::QueryRunnerArg> 
    convertToArg(qWorker::TodoList::TaskMsgPtr const m) {
    }
}
////////////////////////////////////////////////////////////////////////
// class TodoList::Task
////////////////////////////////////////////////////////////////////////
struct qWorker::TodoList::Task {
public:
    TaskMsgPtr msg;
    std::string hash;
    std::string dbName;
    std::string resultPath;
};

////////////////////////////////////////////////////////////////////////
// TodoList implementation
////////////////////////////////////////////////////////////////////////
bool qWorker::TodoList::accept(boost::shared_ptr<TaskMsg> msg) {
    TaskPtr t(new Task());
    t->hash = hashTaskMsg(*msg);
    t->dbName = "q_" + t->hash;
    t->resultPath = hashToResultPath(t->hash);
    {
        boost::lock_guard<boost::mutex> m(_tasksMutex);
        _tasks.push_back(t);
    }
    _notifyWatchers();    
}

boost::shared_ptr<qWorker::QueryRunnerArg> qWorker::TodoList::popTask() {
    
}

boost::shared_ptr<qWorker::QueryRunnerArg> 
qWorker::TodoList::popTask(qWorker::TodoList::MatchF& m) {
    boost::lock_guard<boost::mutex> mutex(_tasksMutex);
#if 0 
    if((!isOverloaded()) && (!_args.empty())) {
    if(true) { // Simple version
            (*af)(_getQueueHead()); // Switch to new query
            _popQueueHead();
        } else { // Prefer same chunk, if possible. 
            // For now, don't worry about starving other chunks.
            ArgQueue::iterator i;
            i = std::find_if(_args.begin(), _args.end(), argMatch(lastChunkId));
            if(i != _args.end()) {
                (*af)(*i);
                _args.erase(i);
            } else {
                (*af)(_getQueueHead()); // Switch to new query
                _popQueueHead();            
            }
    }
    return true;
    
#endif
    return boost::shared_ptr<qWorker::QueryRunnerArg>(); //FIXME
    

}

void qWorker::TodoList::_notifyWatchers() {
    boost::lock_guard<boost::mutex> m(_watchersMutex);
    for(WatcherQueue::iterator i = _watchers.begin();
        i != _watchers.end(); ++i) {
        (**i).handleAccept(*this);
    }
}
