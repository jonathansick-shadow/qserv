/*
 * LSST Data Management System
 * Copyright 2013 LSST Corporation.
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
  * @file PredicateFactory.cc
  *
  * @brief PredicateFactory is a factory class for Predicate objects that get
  * placed (typically) in WhereClause objects.
  *
  * @author Daniel L. Wang, SLAC
  */

#include "query/Predicate.h"
#include <boost/algorithm/string.hpp>
#include "parser/PredicateFactory.h"
#include "parser/ValueExprFactory.h"
#include "SqlSQL2Parser.hpp" // (generated) SqlSQL2TokenTypes

#include "log/Logger.h"

namespace lsst {
namespace qserv {
namespace master {

boost::shared_ptr<CompPredicate>
PredicateFactory::newCompPredicate(antlr::RefAST a) {
    boost::shared_ptr<CompPredicate> p(new CompPredicate());
    if(a->getType() == SqlSQL2TokenTypes::COMP_PREDICATE) { a = a->getFirstChild(); }
    RefAST left = a;
    RefAST op = left->getNextSibling();
    RefAST right = op->getNextSibling();
    p->left = _vf.newExpr(left->getFirstChild());
    p->op = op->getType();
    p->right = _vf.newExpr(right->getFirstChild());
    return p;
}
boost::shared_ptr<BetweenPredicate> PredicateFactory::newBetweenPredicate(antlr::RefAST a) {
    boost::shared_ptr<BetweenPredicate> p(new BetweenPredicate());
    if(a->getType() == SqlSQL2TokenTypes::BETWEEN_PREDICATE) { a = a->getFirstChild(); }
    RefAST betweenToken = a->getNextSibling();
    RefAST minValue = betweenToken->getNextSibling();
    RefAST andToken = minValue->getNextSibling();
    RefAST maxValue = andToken->getNextSibling();
    p->value = _vf.newExpr(a->getFirstChild());
    p->minValue = _vf.newExpr(minValue->getFirstChild());
    p->maxValue = _vf.newExpr(maxValue->getFirstChild());
    return p;
}
boost::shared_ptr<InPredicate> PredicateFactory::newInPredicate(antlr::RefAST a) {
    boost::shared_ptr<InPredicate> p(new InPredicate());
    if(a->getType() == SqlSQL2TokenTypes::IN_PREDICATE) { a = a->getFirstChild(); }
    RefAST value = a;
    RefAST inToken = value->getNextSibling();
    RefAST leftParen = inToken->getNextSibling();
    RefAST firstElement = leftParen->getNextSibling();
    p->value = _vf.newExpr(value->getFirstChild());
    for(RefAST i=firstElement;
        i.get() && i->getType() != SqlSQL2TokenTypes::RIGHT_PAREN;
        i = i->getNextSibling()) {
        if(i->getType() == SqlSQL2TokenTypes::COMMA) {
            i = i->getNextSibling();
        }
        p->cands.push_back(_vf.newExpr(i->getFirstChild()));
    }
    InPredicate& ip = *p;
    return p;
}

boost::shared_ptr<LikePredicate> PredicateFactory::newLikePredicate(antlr::RefAST a) {
    boost::shared_ptr<LikePredicate> p(new LikePredicate());
    if(a->getType() == SqlSQL2TokenTypes::LIKE_PREDICATE) { a = a->getFirstChild(); }
    RefAST value = a;
    RefAST likeToken = value->getNextSibling();
    RefAST pattern = likeToken->getNextSibling();

    p->value = _vf.newExpr(value->getFirstChild());
    p->charValue = _vf.newExpr(pattern->getFirstChild());
    LikePredicate& lp = *p;
    return p;
}

boost::shared_ptr<NullPredicate> PredicateFactory::newNullPredicate(antlr::RefAST a) {
    boost::shared_ptr<NullPredicate> p(new NullPredicate());
    std::cout << walkIndentedString(a);

    if(a->getType() == SqlSQL2TokenTypes::NULL_PREDICATE) { a = a->getFirstChild(); }
    RefAST value = a;
    RefAST isToken = value->getNextSibling();
    RefAST nullToken = isToken->getNextSibling();
    std::string notCand = tokenText(nullToken);
    boost::to_upper(notCand);
    if(notCand == "NOT") {
        p->hasNot = true;
        nullToken = nullToken->getNextSibling();
    } else {
        p->hasNot = false;
    }

    p->value = _vf.newExpr(value->getFirstChild());
    NullPredicate& np = *p; // for debugging
    return p;
}

}}} // lsst::qserv::master
