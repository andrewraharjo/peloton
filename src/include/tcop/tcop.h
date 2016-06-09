//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// tcop.h
//
// Identification: src/include/tcop/tcop.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <mutex>
#include <vector>

#include "common/portal.h"
#include "common/statement.h"
#include "common/types.h"

namespace peloton {
namespace tcop {

//===--------------------------------------------------------------------===//
// TRAFFIC COP
//===--------------------------------------------------------------------===//

class TrafficCop {

  TrafficCop(TrafficCop const &) = delete;

 public:

  // global singleton
  static TrafficCop &GetInstance(void);

  TrafficCop();
  ~TrafficCop();

  // PortalExec - Execute query string
  Result ExecuteStatement(const std::string& query,
                    std::vector<ResultType> &result,
                    std::vector<FieldInfoType> &info,
                    int &rows_changed,
                    std::string &error_message);

  // ExecPrepStmt - Execute a statement from a prepared and bound statement
  Result ExecutePreparedStatement(const std::shared_ptr<PreparedStatement>& prepared_statement,
                                  bool unnamed,
                                  std::vector<ResultType> &result,
                                  int &rows_change,
                                  std::string &error_message);

  // InitBindPrepStmt - Prepare and bind a query from a query string
  std::shared_ptr<PreparedStatement> PrepareStatement(const std::string& query,
                                                      std::string &error_message);

  int BindParameters(std::vector<std::pair<int, std::string>> &parameters,
                     PreparedStatement **stmt,
                     std::string &error_message);

};

}  // End tcop namespace
}  // End peloton namespace