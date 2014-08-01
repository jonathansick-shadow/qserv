// -*- LSST-C++ -*-

/*
 * LSST Data Management System
 * Copyright 2014 LSST Corporation.
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
  * @brief Interface to the Common State System - zookeeper-based implementation.
  *
  * @Author Jacek Becla, SLAC
  */

/*
 * Based on:
 * http://zookeeper.apache.org/doc/r3.3.4/zookeeperProgrammers.html#ZooKeeper+C+client+API
 *
 * To do:
 *  - perhaps switch to async (seems to be recommended by zookeeper)
 */

#include "css/KvInterfaceImplZoo.h"

// System headers
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string.h> // for memset

// Local headers
#include "css/CssError.h"
#include "log/Logger.h"

using std::endl;
using std::exception;
using std::ostringstream;
using std::string;
using std::vector;

namespace lsst {
namespace qserv {
namespace css {


/**
 * Initialize the interface.
 *
 * @param connInfo connection information
 */
KvInterfaceImplZoo::KvInterfaceImplZoo(string const& connInfo) {
    zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
    _zh = zookeeper_init(connInfo.c_str(), 0, 10000, 0, 0, 0);
    if ( !_zh ) {
        throw ConnError();
    }
}

KvInterfaceImplZoo::~KvInterfaceImplZoo() {
    try {
        int rc = zookeeper_close(_zh);
        if ( rc != ZOK ) {
#ifdef NEWLOG
            LOGF_ERROR("*** ~KvInterfaceImplZoo: zookeeper error %1% when closing connection."
                     % rc);
#else
            LOGGER_ERR << "*** ~KvInterfaceImplZoo - zookeeper error " << rc
                       << "when closing connection" << endl;
#endif
        }
    } catch (...) {
#ifdef NEWLOG
        LOGF_ERROR("*** ~KvInterfaceImplZoo: zookeeper exception when closing connection.");
#else
        LOGGER_ERR << "*** ~KvInterfaceImplZoo - zookeeper exception "
                   << "when closing connection" << endl;
#endif
    }
}

void
KvInterfaceImplZoo::create(string const& key, string const& value) {
#ifdef NEWLOG
    LOGF_INFO("*** KvInterfaceImplZoo::create(%1%, %2%)" % key % value);
#else
    LOGGER_INF << "*** KvInterfaceImplZoo::create(), " << key << " --> "
               << value << endl;
#endif
    char buffer[512];
    int rc = zoo_create(_zh, key.c_str(), value.c_str(), value.length(),
                        &ZOO_OPEN_ACL_UNSAFE, 0, buffer, sizeof(buffer)-1);
    if (rc!=ZOK) {
        _throwZooFailure(rc, "create", key);
    }
}

bool
KvInterfaceImplZoo::exists(string const& key) {
    struct Stat stat;
    memset(&stat, 0, sizeof(Stat));
    int rc = zoo_exists(_zh, key.c_str(), 0,  &stat);
    if (rc==ZOK) {
#ifdef NEWLOG
        LOGF_INFO("*** KvInterfaceImplZoo::exists(%1%): yes" % key);
#else
        LOGGER_INF << "*** KvInterfaceImplZoo::exists(" 
                   << key << "): yes" << endl;
#endif
        return true;
    }
    if (rc==ZNONODE) {
#ifdef NEWLOG
        LOGF_INFO("*** KvInterfaceImplZoo::exists(%1%): no" % key);
#else
        LOGGER_INF << "*** KvInterfaceImplZoo::exists(" 
                   << key << "): no" << endl;
#endif
        return false;
    }
    _throwZooFailure(rc, "exists", key);
    return false;
}

string
KvInterfaceImplZoo::get(string const& key) {
#ifdef NEWLOG
    LOGF_INFO("*** KvInterfaceImplZoo::get(%1%)" % key);
#else    
    LOGGER_INF << "*** KvInterfaceImplZoo::get(), key: " << key << endl;
#endif
    char buffer[512];
    int bufLen = static_cast<int>(sizeof(buffer));
    memset(buffer, 0, bufLen);
    struct Stat stat;
    memset(&stat, 0, sizeof(Stat));
    int rc = zoo_get(_zh, key.c_str(), 0, buffer, &bufLen, &stat);
    if (rc!=ZOK) {
        _throwZooFailure(rc, "get", key);
    }
#ifdef NEWLOG
    LOGF_INFO("*** got: %1%" % buffer);
#else
    LOGGER_INF << "*** got: '" << buffer << "'" << endl;
#endif
    return string(buffer);
}

string
KvInterfaceImplZoo::get(string const& key, string const& defaultValue) {
    char buffer[512];
    int bufLen = static_cast<int>(sizeof(buffer));
    memset(buffer, 0, bufLen);
    struct Stat stat;
    memset(&stat, 0, sizeof(Stat));
    int rc = zoo_get(_zh, key.c_str(), 0, buffer, &bufLen, &stat);
    if (rc!=ZOK) {
        if (rc==ZNONODE) {
#ifdef NEWLOG
            LOGF_INFO("*** KvInterfaceImplZoo::get(%1%, %2%), returns default."
                      % key % defaultValue);
#else
            LOGGER_INF << "*** KvInterfaceImplZoo::get2(), key: " << key << endl;
#endif
            return defaultValue;
        } else {
            _throwZooFailure(rc, "get", key);
        }
    }
#ifdef NEWLOG
    LOGF_INFO("*** KvInterfaceImplZoo::get(%1%, %2%), returns '%3%'." 
              % key % defaultValue % buffer);
#else
    LOGGER_INF << "*** got: '" << buffer << "'" << endl;
#endif
    return string(buffer);
}

vector<string>
KvInterfaceImplZoo::getChildren(string const& key) {
    LOGGER_INF << "*** KvInterfaceImplZoo::getChildren(), key: " << key << endl;
    struct String_vector strings;
    memset(&strings, 0, sizeof(strings));
    int rc = zoo_get_children(_zh, key.c_str(), 0, &strings);
    if (rc!=ZOK) {
#ifdef NEWLOG
        LOGF_INFO("*** KvInterfaceImplZoo::getChildren(%1%), got zoo failure %2%." % key % rc);
#else
        LOGGER_INF << "*** KvInterfaceImplZoo::getChildren(" << key << "), "
                   << "zoo failure " << rc << endl;
#endif
        _throwZooFailure(rc, "getChildren", key);
    }
#ifdef NEWLOG
    LOGF_INFO(" got %1% children" % strings.count);
#else
    LOGGER_INF << "got " << strings.count << " children" << endl;
#endif
    vector<string> v;
    try {
        int i;
        for (i=0 ; i<strings.count ; i++) {
            LOGGER_INF << "   " << i+1 << ": " << strings.data[i] << endl;
            v.push_back(strings.data[i]);
        }
        deallocate_String_vector(&strings);
    } catch (const std::bad_alloc& ba) {
        deallocate_String_vector(&strings);
    }
    return v;
}

void
KvInterfaceImplZoo::deleteKey(string const& key) {
    int rc = zoo_delete(_zh, key.c_str(), -1);
    if (rc!=ZOK) {
#ifdef NEWLOG
        LOGF_INFO("*** KvInterfaceImplZoo::deleteKey(%1%) - zoo failure %2%." % key % rc)
#else
        LOGGER_INF << "*** KvInterfaceImplMZoo::deleteKey(" 
                   << key << ") - zoo failure " << rc << endl;
#endif
        _throwZooFailure(rc, "deleteKey", key);
    }
#ifdef NEWLOG
    LOGF_INFO("Key '%1%' deleted." % key);
#else
    LOGGER_INF << "Key '" << key << "' deleted." << endl;
#endif
}

/**
  * @param rc       return code returned by zookeeper
  * @param fName    function name where the error happened
  * @param extraMsg optional extra message to include in the error message
  */
void
KvInterfaceImplZoo::_throwZooFailure(int rc, string const& fName,
                                     string const& key) {
    string ffName = "*** css::KvInterfaceImplZoo::" + fName + "(). ";
    if (rc==ZNONODE) {
#ifdef NEWLOG
        LOGF_INFO("%1%, key '%2%' does not exist." % ffName % key);
#else
        LOGGER_INF << ffName << "Key '" << key << "' does not exist." << endl;
#endif
        throw (key);
    } else if (rc==ZCONNECTIONLOSS) {
#ifdef NEWLOG
        LOGF_INFO("%1% Can't connect to zookeeper." % ffName);
#else
        LOGGER_INF << ffName << "Can't connect to zookeeper." << endl;
#endif
        throw ConnError();
    } else if (rc==ZNOAUTH) {
#ifdef NEWLOG
        LOGF_INFO("%1% Zookeeper authorization failure." % ffName);
#else
        LOGGER_INF << ffName << "Zookeeper authorization failure." << endl;
#endif
        throw AuthError();
    } else if (rc==ZBADARGUMENTS) {
#ifdef NEWLOG
        LOGF_INFO("%1% Invalid key passed to zookeeper." % ffName);
#else
        LOGGER_INF << ffName << "Invalid key passed to zookeeper." << endl;
#endif
        throw NoSuchKey(key);
    }
    ostringstream s;
    s << ffName << "Zookeeper error #" << rc << ". Key: '" << key << "'.";
#ifdef NEWLOG
    LOGF_INFO("%1% Zookeeper error #%2%. Key: '%3%'" % ffName % rc % key);
#else
    LOGGER_INF << s.str() << endl;
#endif
    throw CssError(s.str());
}

}}} // namespace lsst::qserv::css
