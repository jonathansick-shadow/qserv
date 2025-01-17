// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2013-2016 LSST Corporation.
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
#ifndef LSST_QSERV_WSCHED_BLENDSCHEDULER_H
#define LSST_QSERV_WSCHED_BLENDSCHEDULER_H

// System headers
#include <map>

// Qserv headers
#include "wsched/SchedulerBase.h"

// Forward declarations
namespace lsst {
namespace qserv {
namespace wsched {
    class GroupScheduler;
    class ScanScheduler;
}}} // End of forward declarations


namespace lsst {
namespace qserv {
namespace wsched {

/// BlendScheduler is a scheduler that places queries in one of
/// 4 sub-schedulers. Interactive queries are placed on the GroupScheduler
/// _group, which has the highest priority. Other queries, which are
/// expected to require all, or most, of the chunks on this node, go
/// to one of the ScanSchedulers: _scanFast, _scanMedium, _scanSlow.
/// The priority is _group, _scanFast, _scanMedium, _scaneSlow. This
/// should match the list in _schedulers.
///
/// There are several constraints BlendSheduler places on the sub-schedulers.
/// The schedulers run Tasks in a limited pool of threads. At any time,
/// all sub-schedulers should be able to run at least one thread. This is to
/// keep sub-schedulers from getting jammed by heavy loads, or prevent
/// high priority/fast sub-schedulers being stuck waiting for low priority/slow
/// sub-schedulers to finish a Task.
///
/// Each sub-scheduler wants to have some number of threads reserved for it.
/// The ScanScheduler schedulers work better with 2 Tasks running at the same time
/// as the Tasks running at the same time should be sharing some resources and
/// are unlikely to finish at the same time. The resources the 2 were
/// using remain locked when 1 stops, and a new Task that uses the same resources
/// can start immediately. If only one Task for a ScanScheduler is running,
/// and it finishes, it's resources would be unlocked, and if the next Task
/// needed those resources, it would have to lock them again.
///
/// Since we might only have a few threads available, say 12, and 3 schedulers
/// not running any Tasks, reserving 6 threads could seriously hurt throughput.
/// So, each scheduler will only reserve 1 more thread than it has Tasks inFlight,
/// leaving at most 3 threads unavailable at any given time.
///
/// Secondly, the ScanScheduler schedulers are only allowed to advance to a new chunk
/// if resources are available to read the chunk into memory, or if the sub-scheduler
/// has no Tasks inFlight.
class BlendScheduler : public wsched::SchedulerBase {
public:
    using Ptr = std::shared_ptr<BlendScheduler>;

    // This scheduler will have difficulty with less than 10 threads.
    static int getMinPoolSize(){ return 10; }

    BlendScheduler(std::string const& name,
                   int subSchedMaxThreads,
                   std::shared_ptr<GroupScheduler> const& group,
                   std::vector<std::shared_ptr<ScanScheduler>> const& scanSchedulers);
    virtual ~BlendScheduler();

    void queCmd(util::Command::Ptr const& cmd) override;
    util::Command::Ptr getCmd(bool wait) override;

    void commandStart(util::Command::Ptr const& cmd) override;
    void commandFinish(util::Command::Ptr const& cmd) override;

    // SchedulerBase overrides methods.
    std::size_t getSize() const override;
    int getInFlight() const override;
    bool ready() override;
    int applyAvailableThreads(int tempMax) override { return tempMax;} //< does nothing

    void setFlagReorderScans() { _flagReorderScans = true; }
    wcontrol::Scheduler* lookup(wbase::Task::Ptr p);
    int calcAvailableTheads();

private:
    int _getAdjustedMaxThreads(int oldAdjMax, int inFlight);
    bool _ready();
    void _sortScanSchedulers();

    int _schedMaxThreads; //< maximum number of threads that can run.

    // Sub-schedulers.
    std::shared_ptr<GroupScheduler> _group;
    std::shared_ptr<ScanScheduler> _scanFast;
    std::vector<SchedulerBase::Ptr> _schedulers;
    bool _lastCmdFromScan{false};
    std::map<wbase::Task*, SchedulerBase*> _map;
    std::mutex _mapMutex;

    std::atomic<bool> _flagReorderScans{false};
    std::atomic<bool> _infoChanged{true}; //< Used to limit debug logging.
};

}}} // namespace lsst::qserv::wsched

extern lsst::qserv::wsched::BlendScheduler* dbgBlendScheduler; ///< A symbol for gdb

#endif // LSST_QSERV_WSCHED_BLENDSCHEDULER_H
