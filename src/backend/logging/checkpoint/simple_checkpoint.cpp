/*-------------------------------------------------------------------------
 *
 * simple_checkpoint.cpp
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /peloton/src/backend/logging/checkpoint/simple_checkpoint.cpp
 *
 *-------------------------------------------------------------------------
 */

#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>

#include "backend/bridge/dml/mapper/mapper.h"
#include "backend/logging/checkpoint/simple_checkpoint.h"
#include "backend/logging/loggers/wal_frontend_logger.h"
#include "backend/logging/records/tuple_record.h"
#include "backend/logging/records/transaction_record.h"
#include "backend/logging/log_record.h"
#include "backend/logging/checkpoint_tile_scanner.h"

#include "backend/concurrency/transaction_manager_factory.h"
#include "backend/concurrency/transaction_manager.h"
#include "backend/concurrency/transaction.h"
#include "backend/executor/executors.h"
#include "backend/executor/executor_context.h"
#include "backend/planner/seq_scan_plan.h"
#include "backend/catalog/manager.h"

#include "backend/common/logger.h"
#include "backend/common/types.h"

namespace peloton {
namespace logging {
//===--------------------------------------------------------------------===//
// Utility functions
//===--------------------------------------------------------------------===//

size_t GetLogFileSize(int log_file_fd);

bool IsFileTruncated(FILE *log_file, size_t size_to_read, size_t log_file_size);

size_t GetNextFrameSize(FILE *log_file, size_t log_file_size);

LogRecordType GetNextLogRecordType(FILE *log_file, size_t log_file_size);

int ExtractNumberFromFileName(const char *name);

bool ReadTransactionRecordHeader(TransactionRecord &txn_record, FILE *log_file,
                                 size_t log_file_size);

bool ReadTupleRecordHeader(TupleRecord &tuple_record, FILE *log_file,
                           size_t log_file_size);

storage::Tuple *ReadTupleRecordBody(catalog::Schema *schema, VarlenPool *pool,
                                    FILE *log_file, size_t log_file_size);

void SkipTupleRecordBody(FILE *log_file, size_t log_file_size);

// Wrappers
storage::DataTable *GetTable(TupleRecord &tupleRecord);

//===--------------------------------------------------------------------===//
// Simple Checkpoint
//===--------------------------------------------------------------------===//

SimpleCheckpoint::SimpleCheckpoint() : Checkpoint() {
  InitDirectory();
  InitVersionNumber();
}

SimpleCheckpoint::~SimpleCheckpoint() {
  for (auto &record : records_) {
    record.reset();
  }
  records_.clear();
}

void SimpleCheckpoint::DoCheckpoint() {
  auto &log_manager = LogManager::GetInstance();
  // XXX get default backend logger
  if (logger_ == nullptr) {
    logger_ = BackendLogger::GetBackendLogger(LOGGING_TYPE_NVM_WAL);
  }

  // FIXME make sure everything up to start_cid is not garbage collected
  start_commit_id = log_manager.GetPersistentFlushedCommitId();
  LOG_INFO("DoCheckpoint cid = %lu", start_commit_id);

  // Add txn begin record
  std::shared_ptr<LogRecord> begin_record(
      new TransactionRecord(LOGRECORD_TYPE_TRANSACTION_BEGIN, start_commit_id));
  CopySerializeOutput begin_output_buffer;
  begin_record->Serialize(begin_output_buffer);
  records_.push_back(begin_record);

  auto &catalog_manager = catalog::Manager::GetInstance();
  auto database_count = catalog_manager.GetDatabaseCount();

  // loop all databases
  for (oid_t database_idx = 0; database_idx < database_count; database_idx++) {
    auto database = catalog_manager.GetDatabase(database_idx);
    auto table_count = database->GetTableCount();
    auto database_oid = database->GetOid();

    // loop all tables
    for (oid_t table_idx = 0; table_idx < table_count; table_idx++) {
      // Get the target table
      storage::DataTable *target_table = database->GetTable(table_idx);
      assert(target_table);
      LOG_INFO("SeqScan: database oid %lu table oid %lu: %s", database_idx,
               table_idx, target_table->GetName().c_str());
      Scan(target_table, database_oid);
    }
  }

  // if anything other than begin record is added
  if (records_.size() > 1) {
    std::shared_ptr<LogRecord> commit_record(new TransactionRecord(
        LOGRECORD_TYPE_TRANSACTION_COMMIT, start_commit_id));
    CopySerializeOutput commit_output_buffer;
    commit_record->Serialize(commit_output_buffer);
    records_.push_back(commit_record);

    CreateFile();
    Persist();
    Cleanup();
  }
}

cid_t SimpleCheckpoint::DoRecovery() {
  // open log file and file descriptor
  // we open it in read + binary mode
  if (checkpoint_version < 0) {
    return 0;
  }
  std::string file_name = ConcatFileName(checkpoint_dir, checkpoint_version);
  checkpoint_file_ = fopen(file_name.c_str(), "rb");

  if (checkpoint_file_ == NULL) {
    LOG_ERROR("Checkpoint File is NULL");
    return 0;
  }

  // also, get the descriptor
  checkpoint_file_fd_ = fileno(checkpoint_file_);
  if (checkpoint_file_fd_ == INVALID_FILE_DESCRIPTOR) {
    LOG_ERROR("checkpoint_file_fd_ is -1");
    return 0;
  }

  checkpoint_file_size_ = GetLogFileSize(checkpoint_file_fd_);
  assert(checkpoint_file_size_ > 0);
  bool should_stop = false;
  cid_t commit_id = 0;
  while (!should_stop) {
    auto record_type =
        GetNextLogRecordType(checkpoint_file_, checkpoint_file_size_);
    switch (record_type) {
      case LOGRECORD_TYPE_WAL_TUPLE_INSERT: {
        LOG_TRACE("Read checkpoint insert entry");
        InsertTuple(commit_id);
        break;
      }
      case LOGRECORD_TYPE_TRANSACTION_COMMIT: {
        should_stop = true;
        break;
      }
      case LOGRECORD_TYPE_TRANSACTION_BEGIN: {
        LOG_TRACE("Read checkpoint begin entry");
        TransactionRecord txn_rec(record_type);
        if (ReadTransactionRecordHeader(txn_rec, checkpoint_file_,
                                        checkpoint_file_size_) == false) {
          LOG_ERROR("Failed to read checkpoint begin entry");
          return false;
        }
        commit_id = txn_rec.GetTransactionId();
        break;
      }
      default: {
        LOG_ERROR("Invalid checkpoint entry");
        should_stop = true;
        break;
      }
    }
  }

  // After finishing recovery, set the next oid with maximum oid
  // observed during the recovery
  auto &manager = catalog::Manager::GetInstance();
  if (max_oid_ > manager.GetNextOid()) {
    manager.SetNextOid(max_oid_);
  }

  // FIXME this is not thread safe for concurrent checkpoint recovery
  concurrency::TransactionManagerFactory::GetInstance().SetNextCid(commit_id);
  return commit_id;
}

void SimpleCheckpoint::InsertTuple(cid_t commit_id) {
  TupleRecord tuple_record(LOGRECORD_TYPE_WAL_TUPLE_INSERT);

  // Check for torn log write
  if (ReadTupleRecordHeader(tuple_record, checkpoint_file_,
                            checkpoint_file_size_) == false) {
    LOG_ERROR("Could not read tuple record header.");
    return;
  }

  auto table = GetTable(tuple_record);
  if (!table) {
    // the table was deleted
    SkipTupleRecordBody(checkpoint_file_, checkpoint_file_size_);
    return;
  }

  // Read off the tuple record body from the log
  std::unique_ptr<storage::Tuple> tuple(ReadTupleRecordBody(
      table->GetSchema(), pool.get(), checkpoint_file_, checkpoint_file_size_));
  // Check for torn log write
  if (tuple == nullptr) {
    LOG_ERROR("Torn checkpoint write.");
    return;
  }
  auto target_location = tuple_record.GetInsertLocation();
  auto tile_group_id = target_location.block;
  RecoverTuple(tuple.get(), table, target_location, commit_id);
  if (max_oid_ < target_location.block) {
    max_oid_ = tile_group_id;
  }
  LOG_TRACE("Inserted a tuple from checkpoint: (%lu, %lu)",
            target_location.block, target_location.offset);
}

void SimpleCheckpoint::Scan(storage::DataTable *target_table,
                            oid_t database_oid) {
  auto schema = target_table->GetSchema();
  assert(schema);
  std::vector<oid_t> column_ids;
  column_ids.resize(schema->GetColumnCount());
  std::iota(column_ids.begin(), column_ids.end(), 0);

  oid_t current_tile_group_offset = START_OID;
  auto table_tile_group_count = target_table->GetTileGroupCount();
  CheckpointTileScanner scanner;

  while (current_tile_group_offset < table_tile_group_count) {
    // Retrieve a tile group
    auto tile_group = target_table->GetTileGroup(current_tile_group_offset);

    // Retrieve a logical tile
    std::unique_ptr<executor::LogicalTile> logical_tile(
        scanner.Scan(tile_group, column_ids, start_commit_id));

    // Empty result
    if (!logical_tile) {
      current_tile_group_offset++;
      continue;
    }

    auto tile_group_id = logical_tile->GetColumnInfo(0)
                             .base_tile->GetTileGroup()
                             ->GetTileGroupId();

    // Go over the logical tile
    for (oid_t tuple_id : *logical_tile) {
      expression::ContainerTuple<executor::LogicalTile> cur_tuple(
          logical_tile.get(), tuple_id);

      // Logging
      {
        // construct a physical tuple from the logical tuple
        std::unique_ptr<storage::Tuple> tuple(new storage::Tuple(schema, true));
        for (auto column_id : column_ids) {
          tuple->SetValue(column_id, cur_tuple.GetValue(column_id),
                          this->pool.get());
        }
        ItemPointer location(tile_group_id, tuple_id);
        assert(logger_);
        std::shared_ptr<LogRecord> record(logger_->GetTupleRecord(
            LOGRECORD_TYPE_TUPLE_INSERT, INITIAL_TXN_ID, target_table->GetOid(),
            database_oid, location, INVALID_ITEMPOINTER, tuple.get()));
        assert(record);
        CopySerializeOutput output_buffer;
        record->Serialize(output_buffer);
        LOG_TRACE("Insert a new record for checkpoint (%lu, %lu)",
                  tile_group_id, tuple_id);
        records_.push_back(record);
      }
    }
    current_tile_group_offset++;
  }
}

void SimpleCheckpoint::SetLogger(BackendLogger *logger) { logger_ = logger; }

std::vector<std::shared_ptr<LogRecord>> SimpleCheckpoint::GetRecords() {
  return records_;
}

// Private Functions

void SimpleCheckpoint::CreateFile() {
  // open checkpoint file and file descriptor
  std::string file_name = ConcatFileName(checkpoint_dir, ++checkpoint_version);
  checkpoint_file_ = fopen(file_name.c_str(), "ab+");
  if (checkpoint_file_ == NULL) {
    LOG_ERROR("Checkpoint File is NULL");
    return;
  }
  assert(checkpoint_file_);
  // also, get the descriptor
  checkpoint_file_fd_ = fileno(checkpoint_file_);
  if (checkpoint_file_fd_ == INVALID_FILE_DESCRIPTOR) {
    LOG_ERROR("log_file_fd is -1");
  }
}

// Only called when checkpoint has actual contents
void SimpleCheckpoint::Persist() {
  assert(checkpoint_file_);
  assert(records_.size() > 2);
  assert(checkpoint_file_fd_ != INVALID_FILE_DESCRIPTOR);

  LOG_INFO("Persisting %lu checkpoint entries", records_.size());
  // First, write all the record in the queue
  for (auto record : records_) {
    assert(record);
    assert(record->GetMessageLength() > 0);
    fwrite(record->GetMessage(), sizeof(char), record->GetMessageLength(),
           checkpoint_file_);
  }

  // Then, flush
  int ret = fflush(checkpoint_file_);
  if (ret != 0) {
    LOG_ERROR("Error occured in fflush(%d)", ret);
  }

  // Finally, sync
  ret = fsync(checkpoint_file_fd_);
  if (ret != 0) {
    LOG_ERROR("Error occured in fsync(%d)", ret);
  }
}

void SimpleCheckpoint::Cleanup() {
  // Clean up the record queue
  for (auto record : records_) {
    record.reset();
  }
  records_.clear();

  // Remove previous version
  if (checkpoint_version > 0) {
    auto previous_version =
        ConcatFileName(checkpoint_dir, checkpoint_version - 1).c_str();
    if (remove(previous_version) != 0) {
      LOG_INFO("Failed to remove file %s", previous_version);
    }
  }

  // Truncate logs
  // auto frontend_logger = LogManager::GetInstance().GetFrontendLogger();
  // assert(frontend_logger);
  // reinterpret_cast<WriteAheadFrontendLogger *>(frontend_logger)
  //  ->TruncateLog(start_commit_id);
  LogManager::GetInstance().TruncateLogs(start_commit_id);
}

void SimpleCheckpoint::InitVersionNumber() {
  // Get checkpoint version
  LOG_INFO("Trying to read checkpoint directory");
  struct dirent *file;
  auto dirp = opendir(checkpoint_dir.c_str());
  if (dirp == nullptr) {
    LOG_INFO("Opendir failed: Errno: %d, error: %s", errno, strerror(errno));
    return;
  }

  while ((file = readdir(dirp)) != NULL) {
    if (strncmp(file->d_name, FILE_PREFIX.c_str(), FILE_PREFIX.length()) == 0) {
      // found a checkpoint file!
      LOG_INFO("Found a checkpoint file with name %s", file->d_name);
      int version = ExtractNumberFromFileName(file->d_name);
      if (version > checkpoint_version) {
        checkpoint_version = version;
      }
    }
  }
  closedir(dirp);
  LOG_INFO("set checkpoint version to: %d", checkpoint_version);
}

}  // namespace logging
}  // namespace peloton
