/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define STATSD_DEBUG false  // STOPSHIP if true

#include "Log.h"

#include "utils/DbUtils.h"

#include "FieldValue.h"
#include "android-base/stringprintf.h"
#include "storage/StorageManager.h"

namespace android {
namespace os {
namespace statsd {
namespace dbutils {

using ::android::os::statsd::FLOAT;
using ::android::os::statsd::INT;
using ::android::os::statsd::LONG;
using ::android::os::statsd::StorageManager;
using ::android::os::statsd::STRING;
using base::StringPrintf;

static string getDbName(const ConfigKey& key) {
    return StringPrintf("%s/%d_%lld.db", STATS_METADATA_DIR, key.GetUid(), (long long)key.GetId());
}

static string getCreateSqlString(const int64_t metricId, const LogEvent& event) {
    string result = StringPrintf("CREATE TABLE IF NOT EXISTS metric_%lld", (long long)metricId);
    result += "(atomId INTEGER,elapsedTimestampNs INTEGER,wallTimestampNs INTEGER,";
    for (size_t fieldId = 1; fieldId <= event.getValues().size(); ++fieldId) {
        const FieldValue& fieldValue = event.getValues()[fieldId - 1];
        if (fieldValue.mField.getDepth() > 0) {
            // Repeated fields are not supported.
            continue;
        }
        switch (fieldValue.mValue.getType()) {
            case INT:
            case LONG:
                result += StringPrintf("field_%zu INTEGER,", fieldId);
                break;
            case STRING:
                result += StringPrintf("field_%zu TEXT,", fieldId);
                break;
            case FLOAT:
                result += StringPrintf("field_%zu REAL,", fieldId);
                break;
            default:
                // Byte array fields are not supported.
                result += StringPrintf("field_%zu NULL,", fieldId);
        }
    }
    result.pop_back();
    result += ");";
    return result;
}

bool createTableIfNeeded(const ConfigKey& key, const int64_t metricId, const LogEvent& event) {
    const string dbName = getDbName(key);
    sqlite3* db;
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    char* error = nullptr;
    string zSql = getCreateSqlString(metricId, event);
    sqlite3_exec(db, zSql.c_str(), nullptr, nullptr, &error);
    sqlite3_close(db);
    if (error) {
        ALOGW("Failed to create table to db: %s", error);
        return false;
    }
    return true;
}

bool deleteTable(const ConfigKey& key, const int64_t metricId) {
    const string dbName = getDbName(key);
    sqlite3* db;
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    string zSql = StringPrintf("DROP TABLE metric_%lld", (long long)metricId);
    char* error = nullptr;
    sqlite3_exec(db, zSql.c_str(), nullptr, nullptr, &error);
    sqlite3_close(db);
    if (error) {
        ALOGW("Failed to drop table from db: %s", error);
        return false;
    }
    return true;
}

void deleteDb(const ConfigKey& key) {
    const string dbName = getDbName(key);
    StorageManager::deleteFile(dbName.c_str());
}

static string getInsertSqlString(const int64_t metricId, const vector<LogEvent>& events) {
    string result = StringPrintf("INSERT INTO metric_%lld VALUES", (long long)metricId);
    for (auto& logEvent : events) {
        result += StringPrintf("(%d, %lld, %lld,", logEvent.GetTagId(),
                               (long long)logEvent.GetElapsedTimestampNs(),
                               (long long)logEvent.GetLogdTimestampNs());
        for (auto& fieldValue : logEvent.getValues()) {
            if (fieldValue.mField.getDepth() > 0) {
                // Repeated fields are not supported.
                continue;
            }
            switch (fieldValue.mValue.getType()) {
                case INT:
                    result += StringPrintf("%s,", to_string(fieldValue.mValue.int_value).c_str());
                    break;
                case LONG:
                    result += StringPrintf("%s,", to_string(fieldValue.mValue.long_value).c_str());
                    break;
                case STRING:
                    result += StringPrintf("%s,", fieldValue.mValue.str_value.c_str());
                    break;
                case FLOAT:
                    result += StringPrintf("%s,", to_string(fieldValue.mValue.float_value).c_str());
                    break;
                default:
                    // Byte array fields are not supported.
                    result += "null,";
            }
        }
        result.pop_back();
        result += "),";
    }
    result.pop_back();
    result += ";";
    return result;
}

bool insert(const ConfigKey& key, const int64_t metricId, const vector<LogEvent>& events) {
    const string dbName = getDbName(key);
    sqlite3* db;
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    char* error = nullptr;
    string zSql = getInsertSqlString(metricId, events);
    sqlite3_exec(db, zSql.c_str(), nullptr, nullptr, &error);
    sqlite3_close(db);
    if (error) {
        ALOGW("Failed to insert data to db: %s", error);
        return false;
    }
    return true;
}

bool query(const ConfigKey& key, const string& zSql, vector<vector<string>>& rows,
           vector<int32_t>& columnTypes) {
    const string dbName = getDbName(key);
    sqlite3* db;
    if (sqlite3_open(dbName.c_str(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, zSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    int result = sqlite3_step(stmt);
    bool firstIter = true;
    while (result == SQLITE_ROW) {
        int colCount = sqlite3_column_count(stmt);
        vector<string> rowData(colCount);
        for (int i = 0; i < colCount; ++i) {
            if (firstIter) {
                columnTypes.push_back(sqlite3_column_type(stmt, i));
            }
            const unsigned char* textResult = sqlite3_column_text(stmt, i);
            string colData = string(reinterpret_cast<const char*>(textResult));
            rowData[i] = std::move(colData);
        }
        rows.push_back(std::move(rowData));
        firstIter = false;
        result = sqlite3_step(stmt);
    }
    sqlite3_close(db);
    if (result != SQLITE_DONE) {
        return false;
    }
    return true;
}

}  // namespace dbutils
}  // namespace statsd
}  // namespace os
}  // namespace android