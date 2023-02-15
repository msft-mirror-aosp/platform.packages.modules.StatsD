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

#include "utils/DbUtils.h"

#include <gtest/gtest.h>

#include "tests/statsd_test_util.h"

#ifdef __ANDROID__

using namespace std;

namespace android {
namespace os {
namespace statsd {
namespace dbutils {

namespace {
const ConfigKey key = ConfigKey(111, 222);
const int64_t metricId = 111;
const int32_t tagId = 1;

AStatsEvent* makeAStatsEvent(int32_t atomId, int64_t timestampNs) {
    AStatsEvent* statsEvent = AStatsEvent_obtain();
    AStatsEvent_setAtomId(statsEvent, atomId);
    AStatsEvent_overwriteTimestamp(statsEvent, timestampNs);
    return statsEvent;
}

LogEvent makeLogEvent(AStatsEvent* statsEvent) {
    LogEvent event(/*uid=*/0, /*pid=*/0);
    parseStatsEventToLogEvent(statsEvent, &event);
    return event;
}

class DbUtilsTest : public ::testing::Test {
public:
    void TearDown() override {
        deleteDb(key);
    }
};
}  // Anonymous namespace.

TEST_F(DbUtilsTest, TestInsertString) {
    int64_t eventElapsedTimeNs = 10000000000;

    AStatsEvent* statsEvent = makeAStatsEvent(tagId, eventElapsedTimeNs + 10);
    AStatsEvent_writeString(statsEvent, "111");
    LogEvent logEvent = makeLogEvent(statsEvent);
    vector<LogEvent> events{logEvent};

    EXPECT_TRUE(createTableIfNeeded(key, metricId, logEvent));
    EXPECT_TRUE(insert(key, metricId, events));

    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    string zSql = "SELECT * FROM metric_111 ORDER BY elapsedTimestampNs";
    EXPECT_TRUE(query(key, zSql, rows, columnTypes, columnNames, err));

    ASSERT_EQ(rows.size(), 1);
    EXPECT_THAT(rows[0], ElementsAre("1", to_string(eventElapsedTimeNs + 10), _, "111"));
    EXPECT_THAT(columnTypes,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_TEXT));
    EXPECT_THAT(columnNames,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));
}

TEST_F(DbUtilsTest, TestInsertStringNegativeMetricId) {
    int64_t eventElapsedTimeNs = 10000000000;
    int64_t metricId2 = -111;

    AStatsEvent* statsEvent = makeAStatsEvent(tagId, eventElapsedTimeNs + 10);
    AStatsEvent_writeString(statsEvent, "111");
    LogEvent logEvent = makeLogEvent(statsEvent);
    vector<LogEvent> events{logEvent};

    EXPECT_TRUE(createTableIfNeeded(key, metricId2, logEvent));
    EXPECT_TRUE(insert(key, metricId2, events));

    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    string zSql = "SELECT * FROM metric_n111 ORDER BY elapsedTimestampNs";
    EXPECT_TRUE(query(key, zSql, rows, columnTypes, columnNames, err));

    ASSERT_EQ(rows.size(), 1);
    EXPECT_THAT(rows[0], ElementsAre("1", to_string(eventElapsedTimeNs + 10), _, "111"));
    EXPECT_THAT(columnTypes,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_TEXT));
    EXPECT_THAT(columnNames,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));
}

TEST_F(DbUtilsTest, TestInsertInteger) {
    int64_t eventElapsedTimeNs = 10000000000;

    AStatsEvent* statsEvent = makeAStatsEvent(tagId, eventElapsedTimeNs + 10);
    AStatsEvent_writeInt32(statsEvent, 11);
    AStatsEvent_writeInt64(statsEvent, 111);
    LogEvent logEvent = makeLogEvent(statsEvent);
    vector<LogEvent> events{logEvent};

    EXPECT_TRUE(createTableIfNeeded(key, metricId, logEvent));
    EXPECT_TRUE(insert(key, metricId, events));

    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    string zSql = "SELECT * FROM metric_111 ORDER BY elapsedTimestampNs";
    EXPECT_TRUE(query(key, zSql, rows, columnTypes, columnNames, err));

    ASSERT_EQ(rows.size(), 1);
    EXPECT_THAT(rows[0], ElementsAre("1", to_string(eventElapsedTimeNs + 10), _, "11", "111"));
    EXPECT_THAT(columnTypes, ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER,
                                         SQLITE_INTEGER, SQLITE_INTEGER));
    EXPECT_THAT(columnNames, ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs",
                                         "field_1", "field_2"));
}

TEST_F(DbUtilsTest, TestInsertFloat) {
    int64_t eventElapsedTimeNs = 10000000000;

    AStatsEvent* statsEvent = makeAStatsEvent(tagId, eventElapsedTimeNs + 10);
    AStatsEvent_writeFloat(statsEvent, 11.0);
    LogEvent logEvent = makeLogEvent(statsEvent);
    vector<LogEvent> events{logEvent};

    EXPECT_TRUE(createTableIfNeeded(key, metricId, logEvent));
    EXPECT_TRUE(insert(key, metricId, events));

    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    string zSql = "SELECT * FROM metric_111 ORDER BY elapsedTimestampNs";
    EXPECT_TRUE(query(key, zSql, rows, columnTypes, columnNames, err));

    ASSERT_EQ(rows.size(), 1);
    EXPECT_THAT(rows[0], ElementsAre("1", to_string(eventElapsedTimeNs + 10), _, _));
    EXPECT_FLOAT_EQ(/*field1=*/std::stof(rows[0][3]), 11.0);
    EXPECT_THAT(columnTypes,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_FLOAT));
    EXPECT_THAT(columnNames,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));
}

TEST_F(DbUtilsTest, TestInsertTwoEvents) {
    int64_t eventElapsedTimeNs = 10000000000;

    AStatsEvent* statsEvent1 = makeAStatsEvent(tagId, eventElapsedTimeNs + 10);
    AStatsEvent_writeString(statsEvent1, "111");
    LogEvent logEvent1 = makeLogEvent(statsEvent1);

    AStatsEvent* statsEvent2 = makeAStatsEvent(tagId, eventElapsedTimeNs + 20);
    AStatsEvent_writeString(statsEvent2, "222");
    LogEvent logEvent2 = makeLogEvent(statsEvent2);

    vector<LogEvent> events{logEvent1, logEvent2};

    EXPECT_TRUE(createTableIfNeeded(key, metricId, logEvent1));
    EXPECT_TRUE(insert(key, metricId, events));

    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    string zSql = "SELECT * FROM metric_111 ORDER BY elapsedTimestampNs";
    EXPECT_TRUE(query(key, zSql, rows, columnTypes, columnNames, err));

    ASSERT_EQ(rows.size(), 2);
    EXPECT_THAT(rows[0], ElementsAre("1", to_string(eventElapsedTimeNs + 10), _, "111"));
    EXPECT_THAT(rows[1], ElementsAre("1", to_string(eventElapsedTimeNs + 20), _, "222"));
    EXPECT_THAT(columnTypes,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_TEXT));
    EXPECT_THAT(columnNames,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));
}

TEST_F(DbUtilsTest, TestInsertTwoEventsEnforceTtl) {
    int64_t eventElapsedTimeNs = 10000000000;
    int64_t eventWallClockNs = 50000000000;

    AStatsEvent* statsEvent1 = makeAStatsEvent(tagId, eventElapsedTimeNs + 10);
    AStatsEvent_writeString(statsEvent1, "111");
    LogEvent logEvent1 = makeLogEvent(statsEvent1);
    logEvent1.setLogdWallClockTimestampNs(eventWallClockNs);

    AStatsEvent* statsEvent2 = makeAStatsEvent(tagId, eventElapsedTimeNs + 20);
    AStatsEvent_writeString(statsEvent2, "222");
    LogEvent logEvent2 = makeLogEvent(statsEvent2);
    logEvent2.setLogdWallClockTimestampNs(eventWallClockNs + eventElapsedTimeNs);

    vector<LogEvent> events{logEvent1, logEvent2};

    EXPECT_TRUE(createTableIfNeeded(key, metricId, logEvent1));
    sqlite3* db = getDb(key);
    EXPECT_TRUE(insert(db, metricId, events));
    EXPECT_TRUE(flushTtl(db, metricId, eventWallClockNs));
    closeDb(db);

    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    string zSql = "SELECT * FROM metric_111 ORDER BY elapsedTimestampNs";
    EXPECT_TRUE(query(key, zSql, rows, columnTypes, columnNames, err));

    ASSERT_EQ(rows.size(), 1);
    EXPECT_THAT(rows[0], ElementsAre("1", to_string(eventElapsedTimeNs + 20), _, "222"));
    EXPECT_THAT(columnTypes,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_TEXT));
    EXPECT_THAT(columnNames,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));
}

}  // namespace dbutils
}  // namespace statsd
}  // namespace os
}  // namespace android
#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
