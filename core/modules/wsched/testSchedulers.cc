// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2012-2016 LSST Corporation.
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
  /**
  *
  * @brief Simple testing for class FifoScheduler
  *
  * @author Daniel L. Wang, SLAC
  */

// Third-party headers

// Qserv headers
#include "memman/MemManNone.h"
#include "proto/ScanTableInfo.h"
#include "proto/worker.pb.h"
#include "wbase/Task.h"
#include "wsched/ChunkDisk.h"
#include "wsched/BlendScheduler.h"
#include "wsched/FifoScheduler.h"
#include "wsched/GroupScheduler.h"
#include "wsched/ScanScheduler.h"

// Boost unit test header
#define BOOST_TEST_MODULE FifoScheduler_1
#include "boost/test/included/unit_test.hpp"

namespace test = boost::test_tools;
namespace wsched = lsst::qserv::wsched;

using lsst::qserv::proto::TaskMsg;
using lsst::qserv::wbase::Task;
using lsst::qserv::wbase::SendChannel;


Task::Ptr makeTask(std::shared_ptr<TaskMsg> tm) {
    return std::make_shared<Task>(tm, std::shared_ptr<SendChannel>());
}
struct SchedulerFixture {
    typedef std::shared_ptr<TaskMsg> TaskMsgPtr;

    SchedulerFixture(void) {
        counter = 20;
    }
    ~SchedulerFixture(void) { }

    TaskMsgPtr newTaskMsg(int seq) {
        TaskMsgPtr t = std::make_shared<TaskMsg>();
        t->set_session(123456);
        t->set_chunkid(seq);
        t->set_db("elephant");
        for(int i=0; i < 3; ++i) {
            TaskMsg::Fragment* f = t->add_fragment();
            f->add_query("Hello, this is a query.");
            f->mutable_subchunks()->add_id(100+i);
            f->set_resulttable("r_341");
        }
        ++counter;
        return t;
    }
    TaskMsgPtr nextTaskMsg() {
        return newTaskMsg(counter++);
    }

    TaskMsgPtr newTaskMsgSimple(int seq) {
        TaskMsgPtr t = std::make_shared<TaskMsg>();
        t->set_session(123456);
        t->set_chunkid(seq);
        t->set_db("moose");
        ++counter;
        return t;
    }

    TaskMsgPtr newTaskMsgScan(int seq, int priority) {
        auto taskMsg = newTaskMsg(seq);
        taskMsg->set_scanpriority(priority);
        auto sTbl = taskMsg->add_scantable();
        sTbl->set_db("elephant");
        sTbl->set_table("whatever");
        sTbl->set_scanrating(priority);
        sTbl->set_lockinmemory(true);
        return taskMsg;
    }

    Task::Ptr queMsgWithChunkId(wsched::GroupScheduler &gs, int chunkId) {
        Task::Ptr t = makeTask(newTaskMsg(chunkId));
        gs.queCmd(t);
        return t;
    }

    int counter;
};


BOOST_FIXTURE_TEST_SUITE(SchedulerSuite, SchedulerFixture)

BOOST_AUTO_TEST_CASE(Grouping) {
    // Test grouping by chunkId. Max entries added to a single group set to 3.
    wsched::GroupScheduler gs{"GroupSchedA", 100, 0, 3, 0};
    // chunk Ids
    int a = 50;
    int b = 11;
    int c = 75;
    int d = 4;

    BOOST_CHECK(gs.empty() == true);
    BOOST_CHECK(gs.ready() == false);

    Task::Ptr a1 = queMsgWithChunkId(gs, a);
    BOOST_CHECK(gs.empty() == false);
    BOOST_CHECK(gs.ready() == true);

    Task::Ptr b1 = queMsgWithChunkId(gs, b);
    Task::Ptr c1 = queMsgWithChunkId(gs, c);
    Task::Ptr b2 = queMsgWithChunkId(gs, b);
    Task::Ptr b3 = queMsgWithChunkId(gs, b);
    Task::Ptr b4 = queMsgWithChunkId(gs, b);
    Task::Ptr a2 = queMsgWithChunkId(gs, a);
    Task::Ptr a3 = queMsgWithChunkId(gs, a);
    Task::Ptr b5 = queMsgWithChunkId(gs, b);
    Task::Ptr d1 = queMsgWithChunkId(gs, d);
    BOOST_CHECK(gs.getSize() == 5);
    BOOST_CHECK(gs.ready() == true);

    // Should get all the first 3 'a' commands in order
    auto aa1 = gs.getCmd(false);
    auto aa2 = gs.getCmd(false);
    Task::Ptr a4 = queMsgWithChunkId(gs, a); // this should get its own group
    auto aa3 = gs.getCmd(false);
    BOOST_CHECK(a1.get() == aa1.get());
    BOOST_CHECK(a2.get() == aa2.get());
    BOOST_CHECK(a3.get() == aa3.get());
    BOOST_CHECK(gs.getInFlight() == 3);
    BOOST_CHECK(gs.ready() == true);

    // Should get the first 3 'b' commands in order
    auto bb1 = gs.getCmd(false);
    auto bb2 = gs.getCmd(false);
    auto bb3 = gs.getCmd(false);
    BOOST_CHECK(b1.get() == bb1.get());
    BOOST_CHECK(b2.get() == bb2.get());
    BOOST_CHECK(b3.get() == bb3.get());
    BOOST_CHECK(gs.getInFlight() == 6);
    BOOST_CHECK(gs.ready() == true);

    // Verify that commandFinish reduces in flight count.
    gs.commandFinish(a1);
    BOOST_CHECK(gs.getInFlight() == 5);

    // Should get the only 'c' command
    auto cc1 = gs.getCmd(false);
    BOOST_CHECK(c1.get() == cc1.get());
    BOOST_CHECK(gs.getInFlight() == 6);

    // Should get the last 2 'b' commands
    auto bb4 = gs.getCmd(false);
    auto bb5 = gs.getCmd(false);
    BOOST_CHECK(b4.get() == bb4.get());
    BOOST_CHECK(b5.get() == bb5.get());
    BOOST_CHECK(gs.getInFlight() == 8);
    BOOST_CHECK(gs.ready() == true);

    // Get the 'd' command
    auto dd1 = gs.getCmd(false);
    BOOST_CHECK(d1.get() == d1.get());
    BOOST_CHECK(gs.getInFlight() == 9);
    BOOST_CHECK(gs.ready() == true);

    // Get the last 'a' command
    auto aa4 = gs.getCmd(false);
    BOOST_CHECK(a4.get() == aa4.get());
    BOOST_CHECK(gs.getInFlight() == 10);
    BOOST_CHECK(gs.ready() == false);
    BOOST_CHECK(gs.empty() == true);
}


BOOST_AUTO_TEST_CASE(GroupMaxThread) {
    // Test that maxThreads is meaningful.
    wsched::GroupScheduler gs{"GroupSchedB", 3, 0, 100, 0};
    int a = 42;
    Task::Ptr a1 = queMsgWithChunkId(gs, a);
    Task::Ptr a2 = queMsgWithChunkId(gs, a);
    Task::Ptr a3 = queMsgWithChunkId(gs, a);
    Task::Ptr a4 = queMsgWithChunkId(gs, a);
    BOOST_CHECK(gs.ready() == true);
    auto aa1 = gs.getCmd(false);
    BOOST_CHECK(a1.get() == aa1.get());

    BOOST_CHECK(gs.ready() == true);
    auto aa2 = gs.getCmd(false);
    BOOST_CHECK(a2.get() == aa2.get());

    BOOST_CHECK(gs.ready() == true);
    auto aa3 = gs.getCmd(false);
    BOOST_CHECK(a3.get() == aa3.get());
    BOOST_CHECK(gs.getInFlight() == 3);
    BOOST_CHECK(gs.ready() == false);

    gs.commandFinish(a3);
    BOOST_CHECK(gs.ready() == true);
    auto aa4 = gs.getCmd(false);
    BOOST_CHECK(a4.get() == aa4.get());
    BOOST_CHECK(gs.ready() == false);
}

BOOST_AUTO_TEST_CASE(DiskMinHeap) {
    wsched::ChunkDisk::MinHeap minHeap{};

    BOOST_CHECK(minHeap.empty() == true);

    Task::Ptr a47 = makeTask(newTaskMsg(47));
    minHeap.push(a47);
    BOOST_CHECK(minHeap.top().get() == a47.get());
    BOOST_CHECK(minHeap.empty() == false);

    Task::Ptr a42 = makeTask(newTaskMsg(42));
    minHeap.push(a42);
    BOOST_CHECK(minHeap.top().get() == a42.get());

    Task::Ptr a60 = makeTask(newTaskMsg(60));
    minHeap.push(a60);
    BOOST_CHECK(minHeap.top().get() == a42.get());

    Task::Ptr a18 = makeTask(newTaskMsg(18));
    minHeap.push(a18);
    BOOST_CHECK(minHeap.top().get() == a18.get());

    BOOST_CHECK(minHeap.pop().get() == a18.get());
    BOOST_CHECK(minHeap.pop().get() == a42.get());
    BOOST_CHECK(minHeap.pop().get() == a47.get());
    BOOST_CHECK(minHeap.pop().get() == a60.get());
    BOOST_CHECK(minHeap.empty() == true);
}

BOOST_AUTO_TEST_CASE(ChunkDiskMemManNoneTest) {
    auto memMan = std::make_shared<lsst::qserv::memman::MemManNone>(1, false);
    wsched::ChunkDisk cDisk(memMan);

    BOOST_CHECK(cDisk.empty() == true);
    BOOST_CHECK(cDisk.getSize() == 0);
    BOOST_CHECK(cDisk.ready(true) == false);

    Task::Ptr a47 = makeTask(newTaskMsgScan(47,0));
    cDisk.enqueue(a47); // should go on pending
    BOOST_CHECK(cDisk.empty() == false);
    BOOST_CHECK(cDisk.getSize() == 1);
    // call to ready swaps active and passive.
    BOOST_CHECK(cDisk.ready(false) == false);
    // This call to read will cause a47 to be flagged as having resources to run.
    BOOST_CHECK(cDisk.ready(true) == true);


    Task::Ptr a42 = makeTask(newTaskMsgScan(42,0));
    cDisk.enqueue(a42); // should go on pending
    BOOST_CHECK(cDisk.empty() == false);
    BOOST_CHECK(cDisk.getSize() == 2);
    // a47 at top of active has been flagged as ok to run.
    BOOST_CHECK(cDisk.ready(false) == true);

    Task::Ptr b42 = makeTask(newTaskMsgScan(42, 0));
    cDisk.enqueue(b42); // should go on pending
    BOOST_CHECK(cDisk.empty() == false);
    BOOST_CHECK(cDisk.getSize() == 3);
    BOOST_CHECK(cDisk.ready(false) == true);

    // Get the first task
    auto aa47 = cDisk.getTask(false);
    BOOST_CHECK(aa47.get() == a47.get());
    BOOST_CHECK(cDisk.empty() == false);
    BOOST_CHECK(cDisk.getSize() == 2);
    // useFlexibleLock should fail since MemManNone always denies LOCK requests for scans.
    BOOST_CHECK(cDisk.ready(false) == false);
    // MemManNone always grants FLEXIBLELOCK requests
    BOOST_CHECK(cDisk.ready(true) == true);
    // Since MemManNone already ok'ed the task last time ready was called, ready should be true.
    BOOST_CHECK(cDisk.ready(false) == true);

    // After calling ready, a42 is at top
    Task::Ptr a18 = makeTask(newTaskMsgScan(18, 0));
    cDisk.enqueue(a18); // should go on pending
    BOOST_CHECK(cDisk.empty() == false);
    BOOST_CHECK(cDisk.getSize() == 3);
    BOOST_CHECK(cDisk.ready(false) == true);

    // The last task should still be flagged as being ok'ed by MemManNone
    auto aa42 = cDisk.getTask(false);
    BOOST_CHECK(aa42.get() == a42.get());
    BOOST_CHECK(cDisk.empty() == false);
    BOOST_CHECK(cDisk.getSize() == 2);
    BOOST_CHECK(cDisk.ready(false) == false);

    auto bb42 = cDisk.getTask(false);
    BOOST_CHECK(bb42.get() == nullptr);
    bb42 = cDisk.getTask(true);
    BOOST_CHECK(bb42.get() == b42.get());
    BOOST_CHECK(cDisk.empty() == false);
    BOOST_CHECK(cDisk.getSize() == 1);
    BOOST_CHECK(cDisk.ready(false) == false);

    auto aa18 = cDisk.getTask(false);
    BOOST_CHECK(aa18.get() == nullptr);
    aa18 = cDisk.getTask(true);
    BOOST_CHECK(aa18.get() == a18.get());
    BOOST_CHECK(cDisk.empty() == true);
    BOOST_CHECK(cDisk.getSize() == 0);
    BOOST_CHECK(cDisk.ready(true) == false);

}


BOOST_AUTO_TEST_CASE(ScanScheduleTest) {
    auto memMan = std::make_shared<lsst::qserv::memman::MemManNone>(1, false);
    wsched::ScanScheduler sched{"ScanSchedA", 2, 1, 0, memMan, 0, 100};

    // Test ready state as Tasks added and removed.
    BOOST_CHECK(sched.ready() == false);

    Task::Ptr a38 = makeTask(newTaskMsgScan(38, 0));
    sched.queCmd(a38);
    // Calling read swaps active and pending heaps, putting a38 at the top of the active.
    BOOST_CHECK(sched.ready() == true);

    Task::Ptr a40 = makeTask(newTaskMsgScan(40, 0)); // goes on active
    sched.queCmd(a40);

    // Making a non-scan message so MemManNone will grant it an empty Handle
    Task::Ptr b41 = makeTask(newTaskMsg(41)); // goes on active
    sched.queCmd(b41);

    // Making a non-scan message so MemManNone will grant it an empty Handle
    Task::Ptr a33 = makeTask(newTaskMsg(33)); // goes on pending.
    sched.queCmd(a33);

    BOOST_CHECK(sched.ready() == true);
    auto aa38 = sched.getCmd(false);
    BOOST_CHECK(aa38.get() == a38.get());
    BOOST_CHECK(sched.getInFlight() == 1);
    sched.commandStart(aa38);
    BOOST_CHECK(sched.getInFlight() == 1);
    BOOST_CHECK(sched.ready() == false);
    sched.commandFinish(aa38);
    BOOST_CHECK(sched.getInFlight() == 0);

    BOOST_CHECK(sched.ready() == true);
    auto tsk1 = sched.getCmd(false);
    BOOST_CHECK(sched.getInFlight() == 1);
    sched.commandStart(tsk1);
    BOOST_CHECK(sched.ready() == true);
    auto tsk2 = sched.getCmd(false);
    BOOST_CHECK(sched.getInFlight() == 2);
    sched.commandStart(tsk2);
    // Test max of 2 tasks running at a time
    BOOST_CHECK(sched.ready() == false);
    sched.commandFinish(tsk2);
    BOOST_CHECK(sched.getInFlight() == 1);
    BOOST_CHECK(sched.ready() == true);
    auto tsk3 = sched.getCmd(false);
    BOOST_CHECK(sched.getInFlight() == 2);
    BOOST_CHECK(sched.ready() == false);
    sched.commandStart(tsk3);
    sched.commandFinish(tsk3);
    BOOST_CHECK(sched.getInFlight() == 1);
    BOOST_CHECK(sched.ready() == false);
    sched.commandFinish(tsk1);
    BOOST_CHECK(sched.getInFlight() == 0);
    BOOST_CHECK(sched.ready() == false);

}


BOOST_AUTO_TEST_CASE(BlendScheduleTest) {
    // Test that space is appropriately reserved for each scheduler as Tasks are started and finished.
    // In this case, memMan->lock(..) always returns true (really HandleType::ISEMPTY).
    // ChunkIds matter as they control the order Tasks come off individual schedulers.
    int const fastest = lsst::qserv::proto::ScanInfo::Rating::FASTEST;
    int const fast    = lsst::qserv::proto::ScanInfo::Rating::FAST;
    int const medium  = lsst::qserv::proto::ScanInfo::Rating::MEDIUM;
    int const slow    = lsst::qserv::proto::ScanInfo::Rating::SLOW;
    int maxThreads = 9;
    auto memMan = std::make_shared<lsst::qserv::memman::MemManNone>(1, true);
    int priority = 1;
    auto group = std::make_shared<wsched::GroupScheduler>("GroupSched", maxThreads, 2, 3, priority++);
    auto scanSlow = std::make_shared<wsched::ScanScheduler>(
        "ScanSlow", maxThreads, 2, priority++, memMan, medium+1, slow);
    auto scanMed  = std::make_shared<wsched::ScanScheduler>(
        "ScanMed",  maxThreads, 2, priority++, memMan, fast+1, medium);
    auto scanFast = std::make_shared<wsched::ScanScheduler>(
        "ScanFast", maxThreads, 3, priority++, memMan, fastest, fast);
    std::vector<wsched::ScanScheduler::Ptr> scanSchedulers{scanFast, scanMed, scanSlow};
    wsched::BlendScheduler::Ptr blend =
        std::make_shared<wsched::BlendScheduler>("blendSched", maxThreads, group, scanSchedulers);

    BOOST_CHECK(blend->ready() == false);
    BOOST_CHECK(blend->calcAvailableTheads() == 5);

    // Put one message on each scheduler except ScanFast, which gets 2.
    Task::Ptr g1 = makeTask(newTaskMsgSimple(40));
    blend->queCmd(g1);
    BOOST_CHECK(group->getSize() == 1);
    BOOST_CHECK(blend->ready() == true);

    auto taskMsg = newTaskMsgScan(27, lsst::qserv::proto::ScanInfo::Rating::FAST);
    Task::Ptr sF1 = makeTask(taskMsg);
    blend->queCmd(sF1);
    BOOST_CHECK(scanFast->getSize() == 1);
    BOOST_CHECK(blend->ready() == true);

    taskMsg = newTaskMsgScan(40, lsst::qserv::proto::ScanInfo::Rating::FAST);
    Task::Ptr sF2 = makeTask(taskMsg);
    blend->queCmd(sF2);
    BOOST_CHECK(scanFast->getSize() == 2);
    BOOST_CHECK(blend->ready() == true);


    taskMsg = newTaskMsgScan(34, lsst::qserv::proto::ScanInfo::Rating::SLOW );
    Task::Ptr sS1 = makeTask(taskMsg);
    blend->queCmd(sS1);
    BOOST_CHECK(scanSlow->getSize() == 1);
    BOOST_CHECK(blend->ready() == true);

    taskMsg = newTaskMsgScan(31, lsst::qserv::proto::ScanInfo::Rating::MEDIUM );
    Task::Ptr sM1 = makeTask(taskMsg);
    blend->queCmd(sM1);
    BOOST_CHECK(scanMed->getSize() == 1);
    BOOST_CHECK(blend->ready() == true);

    BOOST_CHECK(blend->getSize() == 5);
    BOOST_CHECK(blend->calcAvailableTheads() == 5);

    // Start all the Tasks.
    // Tasks should come out in order of scheduler priority.
    auto og1 = blend->getCmd(false);
    BOOST_CHECK(og1.get() == g1.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 4);
    auto osF1 = blend->getCmd(false);
    BOOST_CHECK(osF1.get() == sF1.get()); // sF1 has lower chunkId than sF2
    BOOST_CHECK(blend->calcAvailableTheads() == 3);
    auto osF2 = blend->getCmd(false);
    BOOST_CHECK(osF2.get() == sF2.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 2);
    auto osM1 = blend->getCmd(false);
    BOOST_CHECK(osM1.get() == sM1.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 1);
    auto osS1 = blend->getCmd(false);
    BOOST_CHECK(osS1.get() == sS1.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 0);
    BOOST_CHECK(blend->getSize() == 0);
    BOOST_CHECK(blend->ready() == false);

    // All threads should now be in use or reserved, should be able to start one
    // Task for each scheduler but second Task should remain on queue.
    Task::Ptr g2 = makeTask(newTaskMsgSimple(41));
    blend->queCmd(g2);
    BOOST_CHECK(group->getSize() == 1);
    BOOST_CHECK(blend->getSize() == 1);
    BOOST_CHECK(blend->ready() == true);

    Task::Ptr g3 = makeTask(newTaskMsgSimple(12));
    blend->queCmd(g3);
    BOOST_CHECK(group->getSize() == 2);
    BOOST_CHECK(blend->getSize() == 2);
    BOOST_CHECK(blend->ready() == true);

    taskMsg = newTaskMsgScan(70, lsst::qserv::proto::ScanInfo::Rating::FAST);
    Task::Ptr sF3 = makeTask(taskMsg);
    blend->queCmd(sF3);
    BOOST_CHECK(scanFast->getSize() == 1);
    BOOST_CHECK(blend->getSize() == 3);
    BOOST_CHECK(blend->ready() == true);

    taskMsg = newTaskMsgScan(72, lsst::qserv::proto::ScanInfo::Rating::FAST);
    Task::Ptr sF4 = makeTask(taskMsg);
    blend->queCmd(sF4);
    BOOST_CHECK(scanFast->getSize() == 2);
    BOOST_CHECK(blend->getSize() == 4);
    BOOST_CHECK(blend->ready() == true);

    taskMsg = newTaskMsgScan(13, lsst::qserv::proto::ScanInfo::Rating::MEDIUM);
    Task::Ptr sM2 = makeTask(taskMsg);
    blend->queCmd(sM2);
    BOOST_CHECK(scanMed->getSize() == 1);
    BOOST_CHECK(blend->getSize() == 5);
    BOOST_CHECK(blend->ready() == true);

    taskMsg = newTaskMsgScan(15, lsst::qserv::proto::ScanInfo::Rating::MEDIUM);
    Task::Ptr sM3 = makeTask(taskMsg);
    blend->queCmd(sM3);
    BOOST_CHECK(scanMed->getSize() == 2);
    BOOST_CHECK(blend->getSize() == 6);
    BOOST_CHECK(blend->ready() == true);

    taskMsg = newTaskMsgScan(5, lsst::qserv::proto::ScanInfo::Rating::SLOW);
    Task::Ptr sS2 = makeTask(taskMsg);
    blend->queCmd(sS2);
    BOOST_CHECK(scanSlow->getSize() == 1);
    BOOST_CHECK(blend->getSize() == 7);
    BOOST_CHECK(blend->ready() == true);

    taskMsg = newTaskMsgScan(6, lsst::qserv::proto::ScanInfo::Rating::SLOW);
    Task::Ptr sS3 = makeTask(taskMsg);
    blend->queCmd(sS3);
    BOOST_CHECK(scanSlow->getSize() == 2);
    BOOST_CHECK(blend->getSize() == 8);
    BOOST_CHECK(blend->ready() == true);

    // Expect 1 group, 1 fast, 1 medium, and 1 slow in that order
    auto og2 = blend->getCmd(false);
    BOOST_CHECK(og2.get() == g2.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 0);
    auto osF3 = blend->getCmd(false);
    BOOST_CHECK(osF3.get() == sF3.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 0);
    BOOST_CHECK(blend->ready() == true);
    auto osM2 = blend->getCmd(false);
    BOOST_CHECK(osM2.get() == sM2.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 0);
    BOOST_CHECK(blend->ready() == true);
    auto osS2 = blend->getCmd(false);
    BOOST_CHECK(osS2.get() == sS2.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 0);
    BOOST_CHECK(blend->getSize() == 4);
    BOOST_CHECK(blend->ready() == false); // all threads in use

    // Finishing a fast Task should allow the last fast Task to go.
    blend->commandFinish(osF3);
    auto osF4 = blend->getCmd(false);
    BOOST_CHECK(osF4.get() == sF4.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 0);
    BOOST_CHECK(blend->ready() == false);

    // Finishing 2 fast Tasks should allow a group Task to go.
    blend->commandFinish(osF1);
    BOOST_CHECK(blend->calcAvailableTheads() == 0);
    blend->commandFinish(osF2);
    BOOST_CHECK(blend->calcAvailableTheads() == 1);
    auto og3 = blend->getCmd(false);
    BOOST_CHECK(og3.get() == g3.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 1);
    BOOST_CHECK(blend->ready() == false);

    // Finishing the last fast Task should let a medium Task go.
    blend->commandFinish(osF4);
    BOOST_CHECK(blend->calcAvailableTheads() == 2);
    auto osM3 = blend->getCmd(false);
    BOOST_CHECK(osM3.get() == sM3.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 2);
    BOOST_CHECK(blend->ready() == false);
    BOOST_CHECK(blend->getCmd(false) == nullptr);

    // Finishing a group Task should allow a slow Task to got (only remaining Task)
    BOOST_CHECK(blend->getSize() == 1);
    blend->commandFinish(og1);
    BOOST_CHECK(blend->calcAvailableTheads() == 2);
    auto osS3 = blend->getCmd(false);
    BOOST_CHECK(osS3.get() == sS3.get());
    BOOST_CHECK(blend->calcAvailableTheads() == 2);
    BOOST_CHECK(blend->getSize() == 0);
    BOOST_CHECK(blend->ready() == false);

    // Close out all tasks and check counts.
    blend->commandFinish(og2);
    BOOST_CHECK(blend->calcAvailableTheads() == 2);
    BOOST_CHECK(blend->getInFlight() == 7);
    blend->commandFinish(og3);
    BOOST_CHECK(blend->calcAvailableTheads() == 3);
    BOOST_CHECK(blend->getInFlight() == 6);
    blend->commandFinish(osM1);
    BOOST_CHECK(blend->calcAvailableTheads() == 3);
    BOOST_CHECK(blend->getInFlight() == 5);
    blend->commandFinish(osM2);
    BOOST_CHECK(blend->calcAvailableTheads() == 3);
    blend->commandFinish(osM3);
    BOOST_CHECK(blend->calcAvailableTheads() == 4);
    blend->commandFinish(osS1);
    BOOST_CHECK(blend->calcAvailableTheads() == 4);
    blend->commandFinish(osS2);
    BOOST_CHECK(blend->calcAvailableTheads() == 4);
    blend->commandFinish(osS3);
    BOOST_CHECK(blend->calcAvailableTheads() == 5);
    BOOST_CHECK(blend->getInFlight() == 0);

    // Test that only 6 threads can be started on a single ScanScheduler
    // This leaves 3 threads available, 1 for each other scheduler.
    BOOST_CHECK(blend->ready() == false);
    std::vector<Task::Ptr> scanTasks;
    for (int j=0; j<7; ++j) {
        blend->queCmd(makeTask(newTaskMsgScan(j, lsst::qserv::proto::ScanInfo::Rating::MEDIUM)));
        if (j < 6) {
            BOOST_CHECK(blend->ready() == true);
            auto cmd = blend->getCmd(false);
            BOOST_CHECK(cmd != nullptr);
            auto task = std::dynamic_pointer_cast<lsst::qserv::wbase::Task>(cmd);
            scanTasks.push_back(task);
        }
        if (j == 6) {
            BOOST_CHECK(blend->ready() == false);
            BOOST_CHECK(blend->getCmd(false) == nullptr);
        }
    }
    {
        // Finishing one task should allow the 7th one to run.
        blend->commandFinish(scanTasks[0]);
        BOOST_CHECK(blend->ready() == true);
        auto cmd = blend->getCmd(false);
        BOOST_CHECK(cmd != nullptr);
        auto task = std::dynamic_pointer_cast<lsst::qserv::wbase::Task>(cmd);
        scanTasks.push_back(task);
    }
    // Finish all the scanTasks, scanTasks[0] is already finished.
    for (int j=1; j<7; ++j) blend->commandFinish(scanTasks[j]);
    BOOST_CHECK(blend->getInFlight() == 0);
    BOOST_CHECK(blend->ready() == false);

    // Test that only 6 threads can be started on a single GroupScheduler
    // This leaves 3 threads available, 1 for each other scheduler.
    std::vector<Task::Ptr> groupTasks;
    for (int j=0; j<7; ++j) {
        blend->queCmd(makeTask(newTaskMsg(j)));
        if (j < 6) {
            BOOST_CHECK(blend->ready() == true);
            auto cmd = blend->getCmd(false);
            BOOST_CHECK(cmd != nullptr);
            auto task = std::dynamic_pointer_cast<lsst::qserv::wbase::Task>(cmd);
            groupTasks.push_back(task);
        }
        if (j == 6) {
            BOOST_CHECK(blend->ready() == false);
            BOOST_CHECK(blend->getCmd(false) == nullptr);
        }
    }
    {
        // Finishing one task should allow the 7th one to run.
        blend->commandFinish(groupTasks[1]);
        BOOST_CHECK(blend->ready() == true);
        auto cmd = blend->getCmd(false);
        BOOST_CHECK(cmd != nullptr);
        auto task = std::dynamic_pointer_cast<lsst::qserv::wbase::Task>(cmd);
        groupTasks.push_back(task);
    }
    // Finish all the groupTasks, groupTasks[1] is already finished.
    for (int j=1; j<7; ++j) blend->commandFinish(groupTasks[j]);
    BOOST_CHECK(blend->getInFlight() == 0);
    BOOST_CHECK(blend->ready() == false);
}


BOOST_AUTO_TEST_SUITE_END()
