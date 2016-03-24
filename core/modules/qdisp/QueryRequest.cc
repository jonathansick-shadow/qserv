// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2014-2016 AURA/LSST.
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
  * @file
  *
  * @brief QueryRequest. XrdSsiRequest impl for czar query dispatch
  *
  * @author Daniel L. Wang, SLAC
  */

// Class header
#include "qdisp/QueryRequest.h"

// System headers
#include <cstddef>
#include <iostream>

// LSST headers
#include "lsst/log/Log.h"

// Qserv headers
#include "qdisp/JobStatus.h"
#include "qdisp/ResponseHandler.h"
#include "util/common.h"

namespace {
LOG_LOGGER _log = LOG_GET("lsst.qserv.qdisp.QueryRequest");

std::atomic<int> clQueryRequestInstCount{0};

}

namespace lsst {
namespace qserv {
namespace qdisp {

////////////////////////////////////////////////////////////////////////
// QueryRequest
////////////////////////////////////////////////////////////////////////
QueryRequest::QueryRequest( XrdSsiSession* session, std::shared_ptr<JobQuery> const& jobQuery) :
  _session{session}, _jobQuery{jobQuery},
  _jobIdStr{jobQuery->getIdStr()} {
    LOGS(_log, LOG_LVL_DEBUG, "&&& QueryRequestInstCount=" << ++clQueryRequestInstCount);
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr <<" New QueryRequest with payload:"
         << _jobQuery->getDescription().payload().size());
}

QueryRequest::~QueryRequest() {
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " ~QueryRequest");
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << "&&& ~QueryRequestInstCount=" << --clQueryRequestInstCount);
    if (_session) {
          if (_session->Unprovision()) {
              LOGS(_log, LOG_LVL_DEBUG, "Unprovision ok.");
          } else {
              LOGS(_log, LOG_LVL_ERROR, "Unprovision Error.");
          }
      }
}

// content of request data
char* QueryRequest::GetRequest(int& requestLength) {
    std::lock_guard<std::mutex> lock(_finishStatusMutex);
    if (_finishStatus != ACTIVE) {
        LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " QueryRequest::GetRequest called after job finished (cancelled?)");
        requestLength = 0;
        return const_cast<char*>("");
    }
    requestLength = _jobQuery->getDescription().payload().size();
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " Requesting, payload size: " << requestLength);
    // Andy promises that his code won't corrupt it.
    return const_cast<char*>(_jobQuery->getDescription().payload().data());
}

// Deleting the buffer (payload) would cause us problems, as this class is not the owner.
void QueryRequest::RelRequestBuffer() {
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " RelRequestBuffer");
}
// precondition: rInfo.rType != isNone
// Must not throw exceptions: calling thread cannot trap them.
// Callback function for XrdSsiRequest.
// See QueryResource::ProvisionDone which invokes ProcessRequest(QueryRequest*))
bool QueryRequest::ProcessResponse(XrdSsiRespInfo const& rInfo, bool isOk) {
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " ProcessResponse");
    std::string errorDesc = _jobIdStr + " ";
    if (isCancelled()) {
        LOGS(_log, LOG_LVL_WARN, _jobIdStr << " QueryRequest::ProcessResponse job already cancelled");
        cancel(); // calls _errorFinish()
        return true;
    }

    // Make a copy of the _jobQuery shared_ptr in case _jobQuery gets reset by a call to  cancel()
    auto jq = _jobQuery;
    {
        std::lock_guard<std::mutex> lock(_finishStatusMutex);
        if (_finishStatus != ACTIVE) {
            LOGS(_log, LOG_LVL_WARN,
                 _jobIdStr << " QueryRequest::GetRequest called after job finished (cancelled?)");
            return true;
        }
    }
    if (!isOk) {
        std::ostringstream os;
        os << _jobIdStr << "ProcessResponse request failed " << getXrootdErr(nullptr);
        jq->getDescription().respHandler()->errorFlush(os.str(), -1);
        jq->getStatus()->updateInfo(JobStatus::RESPONSE_ERROR);
        _errorFinish();
        return true;
    }
    switch(rInfo.rType) {
    case XrdSsiRespInfo::isNone: // All responses are non-null right now
        errorDesc += "Unexpected XrdSsiRespInfo.rType == isNone";
        break;
    case XrdSsiRespInfo::isData: // Local-only
        errorDesc += "Unexpected XrdSsiRespInfo.rType == isData";
        break;
    case XrdSsiRespInfo::isError: // isOk == true
        jq->getStatus()->updateInfo(JobStatus::RESPONSE_ERROR, rInfo.eNum, std::string(rInfo.eMsg));
        return _importError(std::string(rInfo.eMsg), rInfo.eNum);
    case XrdSsiRespInfo::isFile: // Local-only
        errorDesc += "Unexpected XrdSsiRespInfo.rType == isFile";
        break;
    case XrdSsiRespInfo::isStream: // All remote requests
        jq->getStatus()->updateInfo(JobStatus::RESPONSE_READY);
        return _importStream(jq);
    default:
        errorDesc += "Out of range XrdSsiRespInfo.rType";
    }
    return _importError(errorDesc, -1);
}

/// Retrieve and process results in using the XrdSsi stream mechanism
/// Uses a copy of JobQuery::Ptr instead of _jobQuery as a call to cancel() would reset _jobQuery.
bool QueryRequest::_importStream(JobQuery::Ptr const& jq) {
    bool success = false;
    // Pass ResponseHandler's buffer directly.
    std::vector<char>& buffer = jq->getDescription().respHandler()->nextBuffer();
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " _importStream buffer.size=" << buffer.size());
    const void* pbuf = (void*)(&buffer[0]);
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " _importStream->GetResponseData size="
         << buffer.size() << " " << pbuf << " " << util::prettyCharList(buffer, 5));
    success = GetResponseData(&buffer[0], buffer.size());
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " Initiated request " << (success ? "ok" : "err"));

    if (!success) {
        jq->getStatus()->updateInfo(JobStatus::RESPONSE_DATA_ERROR);
        if (Finished()) {
            jq->getStatus()->updateInfo(JobStatus::RESPONSE_DATA_ERROR_OK);
            LOGS_ERROR(_jobIdStr << " QueryRequest::_importStream Finished() !ok " << getXrootdErr(nullptr));
        } else {
            jq->getStatus()->updateInfo(JobStatus::RESPONSE_DATA_ERROR_CORRUPT);
        }
        _errorFinish();
        return false;
    } else {
        return true;
    }
}

/// Process an incoming error.
bool QueryRequest::_importError(std::string const& msg, int code) {
    {
        std::lock_guard<std::mutex> lock(_finishStatusMutex);
        if (_finishStatus != ACTIVE) {
            LOGS_WARN(_jobIdStr << " QueryRequest::_importError code=" << code
                      << " msg=" << msg << " not passed");
            return false;
        }
        _jobQuery->getDescription().respHandler()->errorFlush(msg, code);
    }
    _errorFinish();
    return true;
}

void QueryRequest::ProcessResponseData(char *buff, int blen, bool last) { // Step 7
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " ProcessResponseData with buflen=" << blen
         << " " << (last ? "(last)" : "(more)"));
    // Work with a copy of _jobQuery so it doesn't get reset underneath us by a call to cancel().
    JobQuery::Ptr jq = _jobQuery;
    {
        std::lock_guard<std::mutex> lock(_finishStatusMutex);
        if (_finishStatus != ACTIVE) {
            return;
        }
    }
    if (blen < 0) { // error, check errinfo object.
        int eCode;
        auto reason = getXrootdErr(&eCode);
        jq->getStatus()->updateInfo(JobStatus::RESPONSE_DATA_NACK, eCode, reason);
        LOGS(_log, LOG_LVL_ERROR, _jobIdStr << " ProcessResponse[data] error(" << eCode
             << " " << reason << ")");
        jq->getDescription().respHandler()->errorFlush("Couldn't retrieve response data:" + reason + " " + _jobIdStr, eCode);
        _errorFinish();
        return;
    }
    jq->getStatus()->updateInfo(JobStatus::RESPONSE_DATA);
    bool flushOk = jq->getDescription().respHandler()->flush(blen, last);
    if (flushOk) {
        if (last) {
            auto sz = jq->getDescription().respHandler()->nextBuffer().size();
            if (last && sz != 0) {
                LOGS(_log, LOG_LVL_WARN,
                     _jobIdStr << " Connection closed when more information expected sz=" << sz);
            }
            jq->getStatus()->updateInfo(JobStatus::COMPLETE);
            _finish();
        } else {
            std::vector<char>& buffer = jq->getDescription().respHandler()->nextBuffer();
            const void* pbuf = (void*)(&buffer[0]);
            LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << "_importStream->GetResponseData size=" << buffer.size()
                 << " " << pbuf << " " << util::prettyCharList(buffer, 5));
            if (!GetResponseData(&buffer[0], buffer.size())) {
                _errorFinish();
                return;
            }
        }
    } else {
        LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " ProcessResponse data flush failed");
        ResponseHandler::Error err = jq->getDescription().respHandler()->getError();
        jq->getStatus()->updateInfo(JobStatus::MERGE_ERROR, err.getCode(), err.getMsg());
        // @todo DM-2378 Take a closer look at what causes this error and take
        // appropriate action. There could be cases where this is recoverable.
        _retried.store(true); // Do not retry
        _errorFinish();
    }
}

void QueryRequest::cancel() {
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " QueryRequest::cancel");
    {
        std::lock_guard<std::mutex> lock(_finishStatusMutex);
        if (_cancelled) {
            LOGS(_log, LOG_LVL_DEBUG, _jobIdStr <<" QueryRequest::cancel already cancelled, ignoring");
            return; // Don't do anything if already cancelled.
        }
        _cancelled = true;
        _retried.store(true); // Prevent retries.
        // Only call the following if the job is NOT already done.
        if (_finishStatus == ACTIVE) {
            _jobQuery->getStatus()->updateInfo(JobStatus::CANCEL);
        }
    }
    _errorFinish(true);
}

bool QueryRequest::isCancelled() {
    std::lock_guard<std::mutex> lock(_finishStatusMutex);
    return _cancelled;
}

/// Cleanup pointers so class can be deleted and this should only be called by _finish or _errorFinish.
void QueryRequest::cleanup() {
    LOGS_DEBUG(_jobIdStr << " QueryRequest::cleanup()");
    {
        std::lock_guard<std::mutex> lock(_finishStatusMutex);
        if (_finishStatus == ACTIVE) {
            LOGS_ERROR(_jobIdStr << " QueryRequest::cleanup called before _finish or _errorFinish");
            return;
        }
    }
    // These need to be outside the mutex lock, or you could delete
    // _finishStatusMutex before it is unlocked.
    // This should reset _jobquery and _keepAlive without risk of either being deleted
    // before being reset.
    std::shared_ptr<JobQuery> jq(std::move(_jobQuery));
    std::shared_ptr<QueryRequest> keep(std::move(_keepAlive));
}

/// Finalize under error conditions and retry or report completion
/// This function will destroy this object.
void QueryRequest::_errorFinish(bool shouldCancel) {
    LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " QueryRequest::_errorFinish() shouldCancel=" << shouldCancel);
    {
        // Running _errorFinish more than once could cause errors.
        std::lock_guard<std::mutex> lock(_finishStatusMutex);
        if (_finishStatus != ACTIVE) {
            // Either _finish or _errorFinish has already been called.
            LOGS_DEBUG(_jobIdStr << " QueryRequest::_errorFinish() job no longer ACTIVE, ignoring");
            return;
        }
        _finishStatus = ERROR;
    }

    // Make the calls outside of the mutex lock.
    bool ok = Finished(shouldCancel);
    if (!ok) {
        LOGS(_log, LOG_LVL_ERROR, _jobIdStr << " QueryRequest::_errorFinish !ok " << getXrootdErr(nullptr));
    } else {
        LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " QueryRequest::_errorFinish ok");
    }

    if (!_retried.exchange(true) && !shouldCancel) {
        // There's a slight race condition here. _jobQuery::runJob() creates a
        // new QueryResource object which is used to create a new QueryRequest object
        // which will replace this one in _jobQuery. The replacement could show up
        // before this one's cleanup is called, so this will keep this alive.
        LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " QueryRequest::_errorFinish retrying");
        _keepAlive = _jobQuery->getQueryRequest(); // shared pointer to this
        if (!_jobQuery->runJob()) {
            // Retry failed, nothing left to try.
            LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << "errorFinish retry failed");
            _callMarkComplete(false);
        }
    } else {
        _callMarkComplete(false);
    }
    cleanup(); // Reset smart pointers so this object can be deleted.
}

/// Finalize under success conditions and report completion.
void QueryRequest::_finish() {
    LOGS_DEBUG(_jobIdStr << " QueryRequest::_finish");
    {
        // Running _finish more than once would cause errors.
        std::lock_guard<std::mutex> lock(_finishStatusMutex);
        if (_finishStatus != ACTIVE) {
            // Either _finish or _errorFinish has already been called.
            LOGS_WARN(_jobIdStr << " QueryRequest::_finish called when not ACTIVE, ignoring");
            return;
        }
        _finishStatus = FINISHED;
    }
    bool ok = Finished();
    if (!ok) {
        LOGS(_log, LOG_LVL_ERROR, _jobIdStr << " QueryRequest::finish Finished() !ok " << getXrootdErr(nullptr));
    } else {
        LOGS(_log, LOG_LVL_DEBUG, _jobIdStr << " QueryRequest::finish Finished() ok.");
    }

    _callMarkComplete(true);
    cleanup();
}

/// Inform the Executive that this query completed, and
// Call MarkCompleteFunc only once, it should only be called from _finish() or _errorFinish.
void QueryRequest::_callMarkComplete(bool success) {
    if (!_calledMarkComplete.exchange(true)) {
        _jobQuery->getMarkCompleteFunc()->operator ()(success);
    }
}

std::ostream& operator<<(std::ostream& os, QueryRequest const& qr) {
    os << "QueryRequest " << qr._jobIdStr;
    return os;
}


/// @return The error text and code that xrootd set.
/// if eCode != nullptr, it is set to the error code set by xrootd.
std::string QueryRequest::getXrootdErr(int *eCode) {
    int errNum;
    auto errText = eInfo.Get(errNum);
    if (eCode != nullptr) {
        *eCode = errNum;
    }
    if (errText==nullptr) errText = "";
    std::ostringstream os;
    os << "xrootdErr(" << errNum << ":" << errText << ")";
    return os.str();
}

}}} // lsst::qserv::qdisp
