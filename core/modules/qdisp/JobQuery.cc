/*
 * LSST Data Management System
 * Copyright 2015-2016 AURA/LSST.
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

// System headers
#include <sstream>

// Third-party headers

// Class header
#include "qdisp/JobQuery.h"

// LSST headers
#include "lsst/log/Log.h"

// Qserv headers
#include "qdisp/Executive.h"
#include "qdisp/QueryRequest.h"
#include "qdisp/QueryResource.h"

namespace {
LOG_LOGGER _log = LOG_GET("lsst.qserv.qdisp.JobQuery");

void logErr(std::string const& msg, lsst::qserv::qdisp::JobQuery* jq) {
    LOGS(_log, LOG_LVL_ERROR, msg << " " << *jq);
}

std::atomic<int> clJobQueryInstCount{0}; // &&&

} // anonymous namespace

namespace lsst {
namespace qserv {
namespace qdisp {


JobQuery::JobQuery(Executive* executive, JobDescription const& jobDescription,
                   JobStatus::Ptr const& jobStatus,
                   std::shared_ptr<MarkCompleteFunc> const& markCompleteFunc,
                   qmeta::QueryId qid) :
  _executive(executive), _jobDescription(jobDescription),
  _markCompleteFunc(markCompleteFunc), _jobStatus(jobStatus),
  _qid{qid},
  _idStr{qmeta::QueryIdHelper::makeIdStr(qid, getIdInt())} {
    LOGS(_log, LOG_LVL_DEBUG, "JobQuery " << getIdStr() << " desc=" << _jobDescription);
    LOGS(_log,LOG_LVL_DEBUG, "&&& clJobQueryInstCount=" << ++clJobQueryInstCount);
}

JobQuery::~JobQuery() {
    LOGS(_log, LOG_LVL_DEBUG, "~JobQuery " << getIdStr());
    LOGS(_log,LOG_LVL_DEBUG, "~&&& clJobQueryInstCount=" << --clJobQueryInstCount);
}

/** Attempt to run the job on a worker.
 * @return - false if it can not setup the job or the maximum number of retries has been reached.
 */
bool JobQuery::runJob() {
    LOGS(_log, LOG_LVL_DEBUG, "runJob " << *this);
    if (_executive == nullptr) {
        logErr("runJob failed _executive=nullptr", this);
        return false;
    }
    bool cancelled = _executive->getCancelled();
    bool handlerReset = _jobDescription.respHandler()->reset();
    if (!cancelled && handlerReset) {
        auto qr = std::make_shared<QueryResource>(shared_from_this());
        std::lock_guard<std::recursive_mutex> lock(_rmutex);
        if ( _runAttemptsCount < _getMaxRetries() ) {
            ++_runAttemptsCount;
            if (_executive == nullptr) {
                logErr("JobQuery couldn't run job as executive is null", this);
                return false;
            }
        } else {
            logErr("JobQuery hit maximum number of retries!", this);
            return false;
        }
        _jobStatus->updateInfo(JobStatus::PROVISION);

        // To avoid a cancellation race condition, _queryResourcePtr = qr if and
        // only if the executive has not already been cancelled. The cancellation
        // procedure changes significantly once the executive calls xrootd's Provision().
        bool success = _executive->xrdSsiProvision(_queryResourcePtr, qr);
        if (success) {
            return true;
        }
    }

    LOGS_WARN("JobQuery Failed to RunJob failed. cancelled=" << cancelled << " reset=" << handlerReset);
    return false;
}

void JobQuery::provisioningFailed(std::string const& msg, int code) {
    LOGS(_log, LOG_LVL_ERROR, "Error provisioning, " << getIdStr() << " msg=" << msg
         << " code=" << code << " " << *this << "\n    desc=" << _jobDescription);
    _jobStatus->updateInfo(JobStatus::PROVISION_NACK, code, msg);
    _jobDescription.respHandler()->errorFlush(msg, code);
}

/// Cancel response handling. Return true if this is the first time cancel has been called.
bool JobQuery::cancel() {
    LOGS_DEBUG(getIdStr() << " JobQuery::cancel()");
    if (_cancelled.exchange(true) == false) {
        std::lock_guard<std::recursive_mutex> lock(_rmutex);
        // If _queryRequestPtr is not nullptr, then this job has been passed to xrootd and cancellation is complicated.
        if (_queryRequestPtr != nullptr) {
            LOGS_DEBUG(getIdStr() << " cancel QueryRequest in progress");
            _queryRequestPtr->cancel();
        } else {
            std::ostringstream os;
            os << getIdStr() <<" cancel before QueryRequest" ;
            LOGS_DEBUG(os.str());
            getDescription().respHandler()->errorFlush(os.str(), -1);
            _executive->markCompleted(getIdInt(), false);
        }
        _jobDescription.respHandler()->processCancel();
        return true;
    }
    LOGS_DEBUG(getIdStr() << " cancel, skipping, already cancelled.");
    return false;
}


/// Reset the QueryResource pointer, but only if called by the current QueryResource.
void JobQuery::freeQueryResource(QueryResource* qr) {
    std::lock_guard<std::recursive_mutex> lock(_rmutex);
    // There is the possibility during a retry that _queryResourcePtr would be set
    // to the new object before the old thread calls this. This check prevents us
    // reseting the pointer in that case.
    if (qr == _queryResourcePtr.get()) {
        _queryResourcePtr.reset();
    } else {
        LOGS(_log, LOG_LVL_WARN, "freeQueryResource called by wrong QueryResource.");
    }
}

std::ostream& operator<<(std::ostream& os, JobQuery const& jq) {
    return os << "{" << jq.getIdStr() << jq._jobDescription << " " << *jq._jobStatus << "}";
}


}}} // end namespace
