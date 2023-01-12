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

#include <sqlite3.h>

#include "config/ConfigKey.h"
#include "logd/LogEvent.h"

using std::string;
using std::vector;

namespace android {
namespace os {
namespace statsd {
namespace dbutils {

// TODO(b/264407489): Update this to a new directory once ready.
#define STATS_METADATA_DIR "/data/misc/stats-metadata"

/* Creates a new data table for a specified metric if one does not yet exist. */
bool createTableIfNeeded(const ConfigKey& key, const int64_t metricId, const LogEvent& event);

/* Deletes a data table for the specified metric. */
bool deleteTable(const ConfigKey& key, const int64_t metricId);

/* Deletes the SQLite db data file. */
void deleteDb(const ConfigKey& key);

/* Inserts new data into the specified metric data table. */
bool insert(const ConfigKey& key, const int64_t metricId, const vector<LogEvent>& events);

/* Executes a sql query on the specified SQLite db. */
bool query(const ConfigKey& key, const string& zSql, vector<vector<string>>& rows,
           vector<int32_t>& columnTypes);

}  // namespace dbutils
}  // namespace statsd
}  // namespace os
}  // namespace android