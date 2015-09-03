// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2014-2015 AURA/LSST.
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
  * @brief QueryResource. An XrdSsiService::Resource
  *
  * @author Daniel L. Wang, SLAC
  */

// Class header
#include "qdisp/QueryResource.h"

// System headers
#include <cassert>
#include <iostream>
#include <sstream>

// LSST headers
#include "lsst/log/Log.h"

// Qserv headers
#include "qdisp/JobStatus.h"
#include "qdisp/QueryRequest.h"
#include "qdisp/QueryResource.h"

namespace lsst {
namespace qserv {
namespace qdisp {

QueryResource::QueryResource(std::shared_ptr<JobQuery> jobQuery)
      // this char* must live as long as this object, so copy it on heap
    : Resource(::strdup(jobQuery->getDescription().resource().path().c_str())),
      _xrdSsiSession(NULL), _jobQuery(jobQuery) {
    if (rName == NULL) {
        throw std::bad_alloc();
    }
}

QueryResource::~QueryResource() {
    LOGF_ERROR("~QueryResource() &&&");
    std::free(const_cast<char *>(rName)); // clean up heap allocated resource path copy
}

/// May not throw exceptions because the calling code comes from
/// xrootd land and will not catch any exceptions.
void QueryResource::ProvisionDone(XrdSsiSession* s) { // Step 3
    _provisionDoneHelper(s);
    // freeQueryResource must be called before returning to prevent memory leaks.
    _jobQuery->freeQueryResource();
}

void QueryResource::_provisionDoneHelper(XrdSsiSession* s) {
    if(!s) {
        // Check eInfo in resource for error details
        int code = 0;
        std::string msg = eInfoGet(code);
        _jobQuery->provisioningFailed(msg, code);
        return;
    }
    if(_jobQuery->getCancelled()) {
        return; // Don't bother doing anything if the requester doesn't care.
    }
    _xrdSsiSession = s;

    QueryRequest::Ptr qr = std::make_shared<QueryRequest>(s, _jobQuery);
    _jobQuery->setQueryRequest(qr);

    // Hand off the request.
    _jobQuery->getStatus()->updateInfo(JobStatus::REQUEST);
    _xrdSsiSession->ProcessRequest(qr.get()); // xrootd will not delete the QueryRequest.
    // There are no more requests for this session.
}

const char* QueryResource::eInfoGet(int &code) {
    char const* message = eInfo.Get(code);
    return message ? message : "no message from XrdSsi, code may not be reliable";
}

}}} // lsst::qserv::qdisp
