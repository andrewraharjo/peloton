//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// wbl_backend_logger.h
//
// Identification: src/backend/logging/loggers/wbl_backend_logger.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include "backend/common/types.h"
#include "backend/logging/backend_logger.h"

namespace peloton {
namespace logging {

//===--------------------------------------------------------------------===//
// WBL Backend Logger
//===--------------------------------------------------------------------===//

class WriteBehindBackendLogger : public BackendLogger {
 public:
  WriteBehindBackendLogger(const WriteBehindBackendLogger &) = delete;
  WriteBehindBackendLogger &operator=(const WriteBehindBackendLogger &) =
      delete;
  WriteBehindBackendLogger(WriteBehindBackendLogger &&) = delete;
  WriteBehindBackendLogger &operator=(WriteBehindBackendLogger &&) = delete;

  WriteBehindBackendLogger() { logging_type = LOGGING_TYPE_NVM_WBL; }

  void Log(LogRecord *record);

  void TruncateLocalQueue(oid_t offset);

  LogRecord *GetTupleRecord(LogRecordType log_record_type, txn_id_t txn_id,
                            oid_t table_oid, oid_t db_oid,
                            ItemPointer insert_location,
                            ItemPointer delete_location,
                            const void *data = nullptr);

  // FIXME temporarily defined for wbl_backend_logger to compile code
  std::pair<cid_t, cid_t> PrepareLogBuffers() {
    return std::pair<cid_t, cid_t>(INVALID_CID, INVALID_CID);
  }

  void GrantEmptyBuffer(__attribute__((unused)) std::unique_ptr<LogBuffer>) {
    // FIXME temporarily defined for wbl_backend_logger to compile code
  }

 private:
  CopySerializeOutput output_buffer;
};

}  // namespace logging
}  // namespace peloton
