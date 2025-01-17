// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2011-2016 LSST Corporation.
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
/// Task.h
/// @author Daniel L. Wang (danielw)
#ifndef LSST_QSERV_WBASE_TASK_H
#define LSST_QSERV_WBASE_TASK_H

// System headers
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

// Qserv headers
#include "memman/MemMan.h"
#include "proto/ScanTableInfo.h"
#include "qmeta/types.h"
#include "util/EventThread.h"
#include "util/threadSafe.h"

// Forward declarations
namespace lsst {
namespace qserv {
namespace wbase {
    struct ScriptMeta;
    class SendChannel;
}
namespace proto {
    class TaskMsg;
    class TaskMsg_Fragment;
}}} // End of forward declarations

namespace lsst {
namespace qserv {
namespace wbase {

/// Base class for tracking a database query for a worker Task.
class TaskQueryRunner {
public:
    using Ptr = std::shared_ptr<TaskQueryRunner>;
    virtual ~TaskQueryRunner() {};
    virtual bool runQuery()=0;
    virtual void cancel()=0; ///< Repeated calls to cancel() must be harmless.
};

class Task;

/// Base class for scheduling Tasks.
/// Allows the scheduler to take appropriate action when a task is cancelled.
class TaskScheduler {
public:
    using Ptr = std::shared_ptr<TaskScheduler>;
    virtual ~TaskScheduler() {}
    virtual void taskCancelled(Task*)=0;///< Repeated calls must be harmless.
};

/// Used to find tasks that are in process for debugging with Task::_idStr.
/// This is largely meant to track down incomplete tasks in a possible intermittent
/// failure and should probably be removed when it is no longer needed.
/// It depends on code in BlendScheduler to work. If the decision is made to keep it
/// forever, dependency on BlendScheduler needs to be re-worked.
struct IdSet {
    void add(std::string const& id) {
        std::lock_guard<std::mutex> lock(mx);
        _ids.insert(id);
    }
    void remove(std::string const& id) {
        std::lock_guard<std::mutex> lock(mx);
        _ids.erase(id);
    }
    std::atomic<int> maxDisp{5}; //< maximum number of entries to show with operator<<
    friend std::ostream& operator<<(std::ostream& os, IdSet const& idSet);
private:
    std::set<std::string> _ids;
    std::mutex mx;
};

/// struct Task defines a query task to be done, containing a TaskMsg
/// (over-the-wire) additional concrete info related to physical
/// execution conditions.
/// Task is non-copyable
/// Task encapsulates nearly zero logic, aside from:
/// * constructors
class Task : public util::Command {
public:
    static std::string const defaultUser;
    using Ptr =  std::shared_ptr<Task>;
    using TaskMsgPtr = std::shared_ptr<proto::TaskMsg>;

    struct ChunkEqual {
        bool operator()(Task::Ptr const& x, Task::Ptr const& y);
    };
    struct ChunkIdGreater {
        bool operator()(Ptr const& x, Ptr const& y);
    };

    explicit Task(TaskMsgPtr const& t, std::shared_ptr<SendChannel> const& sc);
    Task& operator=(const Task&) = delete;
    Task(const Task&) = delete;
    virtual ~Task();

    TaskMsgPtr msg; ///< Protobufs Task spec
    std::shared_ptr<SendChannel> sendChannel; ///< For result reporting
    std::string hash; ///< hash of TaskMsg
    std::string user; ///< Incoming username
    time_t entryTime {0}; ///< Timestamp for task admission
    char timestr[100]; ///< ::ctime_r(&t.entryTime, timestr)
    // Note that manpage spec of "26 bytes"  is insufficient

    void cancel();
    bool getCancelled() const { return _cancelled; }

    bool setTaskQueryRunner(TaskQueryRunner::Ptr const& taskQueryRunner); ///< return true if already cancelled.
    void freeTaskQueryRunner(TaskQueryRunner *tqr);
    void setTaskScheduler(TaskScheduler::Ptr const& scheduler) { _taskScheduler = scheduler; }
    friend std::ostream& operator<<(std::ostream& os, Task const& t);

    // Shared scan information
    int getChunkId();
    proto::ScanInfo& getScanInfo() { return _scanInfo; }
    bool hasMemHandle() const { return _memHandle != memman::MemMan::HandleType::INVALID; }
    memman::MemMan::Handle getMemHandle() { return _memHandle; }
    void setMemHandle(memman::MemMan::Handle handle) { _memHandle = handle; }

    static IdSet allIds; // set of all task jobId numbers that are not complete.
    std::string getIdStr() {return _idStr;}

private:
    uint64_t const    _qId{0}; //< queryId from czar
    int      const    _jId{0}; //< jobId from czar
    std::string const _idStr{qmeta::QueryIdHelper::makeIdStr(0, 0, true)}; // < for logging only

    std::atomic<bool> _cancelled{false};
    TaskQueryRunner::Ptr _taskQueryRunner;
    std::weak_ptr<TaskScheduler> _taskScheduler;
    proto::ScanInfo _scanInfo;
    std::atomic<memman::MemMan::Handle> _memHandle{memman::MemMan::HandleType::INVALID};
};

/// MsgProcessor implementations handle incoming Task objects.
struct MsgProcessor {
    virtual ~MsgProcessor() {}
    virtual void processTask(std::shared_ptr<wbase::Task> const& task) = 0;
};

}}} // namespace lsst::qserv::wbase

#endif // LSST_QSERV_WBASE_TASK_H
