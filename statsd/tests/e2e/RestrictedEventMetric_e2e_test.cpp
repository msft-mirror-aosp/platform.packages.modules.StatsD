// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <vector>

#include "flags/FlagProvider.h"
#include "src/StatsLogProcessor.h"
#include "src/state/StateTracker.h"
#include "src/stats_log_util.h"
#include "stats_annotations.h"
#include "tests/statsd_test_util.h"

namespace android {
namespace os {
namespace statsd {

#ifdef __ANDROID__

namespace {
const int64_t configId = 12345;
const string delegate_package_name = "com.test.restricted.metrics.package";
const int32_t delegate_uid = 1005;
const string config_package_name = "com.test.config.package";
const int32_t config_app_uid = 123;
const ConfigKey configKey(config_app_uid, configId);
const int64_t eightDaysAgo = getWallClockNs() - 8 * 24 * 3600 * NS_PER_SEC;
const int64_t oneDayAgo = getWallClockNs() - 1 * 24 * 3600 * NS_PER_SEC;
}  // anonymous namespace

// Setup for test fixture.
class RestrictedEventMetricE2eTest : public ::testing::Test {
protected:
    shared_ptr<MockStatsQueryCallback> mockStatsQueryCallback;
    vector<string> queryDataResult;
    vector<string> columnNamesResult;
    vector<int32_t> columnTypesResult;
    int32_t rowCountResult = 0;
    string error;
    sp<UidMap> uidMap;
    sp<StatsLogProcessor> processor;
    int32_t atomTag;
    int64_t restrictedMetricId;
    int64_t configAddedTimeNs;
    StatsdConfig config;

private:
    void SetUp() override {
        FlagProvider::getInstance().overrideFuncs(&isAtLeastSFuncTrue);
        FlagProvider::getInstance().overrideFlag(RESTRICTED_METRICS_FLAG, FLAG_TRUE,
                                                 /*isBootFlag=*/true);

        mockStatsQueryCallback = SharedRefBase::make<StrictMock<MockStatsQueryCallback>>();
        EXPECT_CALL(*mockStatsQueryCallback, sendResults(_, _, _, _))
                .Times(AnyNumber())
                .WillRepeatedly(Invoke(
                        [this](const vector<string>& queryData, const vector<string>& columnNames,
                               const vector<int32_t>& columnTypes, int32_t rowCount) {
                            queryDataResult = queryData;
                            columnNamesResult = columnNames;
                            columnTypesResult = columnTypes;
                            rowCountResult = rowCount;
                            error = "";
                            return Status::ok();
                        }));
        EXPECT_CALL(*mockStatsQueryCallback, sendFailure(_))
                .Times(AnyNumber())
                .WillRepeatedly(Invoke([this](const string& err) {
                    error = err;
                    queryDataResult.clear();
                    columnNamesResult.clear();
                    columnTypesResult.clear();
                    rowCountResult = 0;
                    return Status::ok();
                }));

        config.add_allowed_log_source("AID_ROOT");  // LogEvent defaults to UID of root.

        atomTag = 999;
        AtomMatcher restrictedAtomMatcher = CreateSimpleAtomMatcher("restricted_matcher", atomTag);
        *config.add_atom_matcher() = restrictedAtomMatcher;

        EventMetric restrictedEventMetric =
                createEventMetric("RestrictedMetricLogged", restrictedAtomMatcher.id(), nullopt);
        *config.add_event_metric() = restrictedEventMetric;
        restrictedMetricId = restrictedEventMetric.id();

        config.set_restricted_metrics_delegate_package_name(delegate_package_name.c_str());

        const int64_t baseTimeNs = 0;                     // 0:00
        configAddedTimeNs = baseTimeNs + 1 * NS_PER_SEC;  // 0:01

        uidMap = new UidMap();
        uidMap->updateApp(configAddedTimeNs, String16(delegate_package_name.c_str()),
                          /*uid=*/delegate_uid, /*versionCode=*/1,
                          /*versionString=*/String16("v2"),
                          /*installer=*/String16(""), /*certificateHash=*/{});
        uidMap->updateApp(configAddedTimeNs + 1, String16(config_package_name.c_str()),
                          /*uid=*/config_app_uid, /*versionCode=*/1,
                          /*versionString=*/String16("v2"),
                          /*installer=*/String16(""), /*certificateHash=*/{});

        processor = CreateStatsLogProcessor(baseTimeNs, configAddedTimeNs, config, configKey,
                                            /*puller=*/nullptr, /*atomTag=*/0, uidMap);
    }

    void TearDown() override {
        Mock::VerifyAndClear(mockStatsQueryCallback.get());
        queryDataResult.clear();
        columnNamesResult.clear();
        columnTypesResult.clear();
        rowCountResult = 0;
        error = "";
        dbutils::deleteDb(configKey);
        FlagProvider::getInstance().resetOverrides();
    }
};

TEST_F(RestrictedEventMetricE2eTest, TestQueryThreeEvents) {
    std::vector<std::unique_ptr<LogEvent>> events;

    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));
    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 200));
    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 300));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_EQ(rowCountResult, 3);
    EXPECT_THAT(queryDataResult, ElementsAre(to_string(atomTag), to_string(configAddedTimeNs + 100),
                                             _,  // wallClockNs
                                             _,  // field_1
                                             to_string(atomTag), to_string(configAddedTimeNs + 200),
                                             _,  // wallClockNs
                                             _,  // field_1
                                             to_string(atomTag), to_string(configAddedTimeNs + 300),
                                             _,  // wallClockNs
                                             _   // field_1
                                             ));

    EXPECT_THAT(columnNamesResult,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));

    EXPECT_THAT(columnTypesResult,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER));
}

TEST_F(RestrictedEventMetricE2eTest, TestInvalidSchemaIncreasingFieldCount) {
    std::vector<std::unique_ptr<LogEvent>> events;

    AStatsEvent* statsEvent = AStatsEvent_obtain();
    AStatsEvent_setAtomId(statsEvent, atomTag);
    AStatsEvent_addInt32Annotation(statsEvent, ASTATSLOG_ANNOTATION_ID_RESTRICTION_CATEGORY,
                                   ASTATSLOG_RESTRICTION_CATEGORY_DIAGNOSTIC);
    AStatsEvent_overwriteTimestamp(statsEvent, configAddedTimeNs + 200);

    AStatsEvent_writeString(statsEvent, "111");
    AStatsEvent_writeInt32(statsEvent, 11);
    AStatsEvent_writeFloat(statsEvent, 11.0);
    std::unique_ptr<LogEvent> logEvent = std::make_unique<LogEvent>(/*uid=*/0, /*pid=*/0);
    parseStatsEventToLogEvent(statsEvent, logEvent.get());

    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));
    events.push_back(std::move(logEvent));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_EQ(rowCountResult, 1);
    // Event 2 rejected.
    EXPECT_THAT(queryDataResult, ElementsAre(to_string(atomTag), to_string(configAddedTimeNs + 100),
                                             _,  // wallClockNs
                                             _   // field_1
                                             ));

    EXPECT_THAT(columnNamesResult,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));

    EXPECT_THAT(columnTypesResult,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER));
}

TEST_F(RestrictedEventMetricE2eTest, TestInvalidSchemaDecreasingFieldCount) {
    std::vector<std::unique_ptr<LogEvent>> events;

    AStatsEvent* statsEvent = AStatsEvent_obtain();
    AStatsEvent_setAtomId(statsEvent, atomTag);
    AStatsEvent_addInt32Annotation(statsEvent, ASTATSLOG_ANNOTATION_ID_RESTRICTION_CATEGORY,
                                   ASTATSLOG_RESTRICTION_CATEGORY_DIAGNOSTIC);
    AStatsEvent_overwriteTimestamp(statsEvent, configAddedTimeNs + 100);

    AStatsEvent_writeString(statsEvent, "111");
    AStatsEvent_writeInt32(statsEvent, 11);
    std::unique_ptr<LogEvent> logEvent = std::make_unique<LogEvent>(/*uid=*/0, /*pid=*/0);
    parseStatsEventToLogEvent(statsEvent, logEvent.get());

    events.push_back(std::move(logEvent));
    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 200));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_EQ(rowCountResult, 1);
    // Event 2 Rejected
    EXPECT_THAT(queryDataResult, ElementsAre(to_string(atomTag), to_string(configAddedTimeNs + 100),
                                             _,             // wallClockNs
                                             "111",         // field_1
                                             to_string(11)  // field_2
                                             ));

    EXPECT_THAT(columnNamesResult, ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs",
                                               "field_1", "field_2"));

    EXPECT_THAT(columnTypesResult, ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER,
                                               SQLITE_TEXT, SQLITE_INTEGER));
}

TEST_F(RestrictedEventMetricE2eTest, TestOneEventMultipleUids) {
    uidMap->updateApp(configAddedTimeNs, String16(delegate_package_name.c_str()),
                      /*uid=*/delegate_uid + 1, /*versionCode=*/1,
                      /*versionString=*/String16("v2"),
                      /*installer=*/String16(""), /*certificateHash=*/{});
    uidMap->updateApp(configAddedTimeNs + 1, String16(config_package_name.c_str()),
                      /*uid=*/config_app_uid + 1, /*versionCode=*/1,
                      /*versionString=*/String16("v2"),
                      /*installer=*/String16(""), /*certificateHash=*/{});

    std::vector<std::unique_ptr<LogEvent>> events;

    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_EQ(rowCountResult, 1);
    EXPECT_THAT(queryDataResult, ElementsAre(to_string(atomTag), to_string(configAddedTimeNs + 100),
                                             _,  // wallClockNs
                                             _   // field_1
                                             ));
}

TEST_F(RestrictedEventMetricE2eTest, TestTooManyConfigsAmbiguousQuery) {
    ConfigKey key2(config_app_uid + 1, configId);
    processor->OnConfigUpdated(configAddedTimeNs + 1 * NS_PER_SEC, key2, config);

    uidMap->updateApp(configAddedTimeNs, String16(delegate_package_name.c_str()),
                      /*uid=*/delegate_uid + 1, /*versionCode=*/1,
                      /*versionString=*/String16("v2"),
                      /*installer=*/String16(""), /*certificateHash=*/{});
    uidMap->updateApp(configAddedTimeNs + 1, String16(config_package_name.c_str()),
                      /*uid=*/config_app_uid + 1, /*versionCode=*/1,
                      /*versionString=*/String16("v2"),
                      /*installer=*/String16(""), /*certificateHash=*/{});

    std::vector<std::unique_ptr<LogEvent>> events;

    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_EQ(error, "Ambiguous ConfigKey");
}

TEST_F(RestrictedEventMetricE2eTest, TestUnknownConfigPackage) {
    std::vector<std::unique_ptr<LogEvent>> events;

    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/"unknown.config.package",
                        /*callingUid=*/delegate_uid);

    EXPECT_EQ(error, "No configs found matching the config key");
}

TEST_F(RestrictedEventMetricE2eTest, TestUnknownDelegatePackage) {
    std::vector<std::unique_ptr<LogEvent>> events;

    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid + 1);

    EXPECT_EQ(error, "No matching configs for restricted metrics delegate");
}

TEST_F(RestrictedEventMetricE2eTest, TestUnsupportedDatabaseVersion) {
    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/INT_MAX,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_THAT(error, StartsWith("Unsupported sqlite version"));
}

TEST_F(RestrictedEventMetricE2eTest, TestInvalidQuery) {
    std::vector<std::unique_ptr<LogEvent>> events;

    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    std::stringstream query;
    query << "SELECT * FROM invalid_metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_THAT(error, StartsWith("failed to query db"));
}

TEST_F(RestrictedEventMetricE2eTest, TestFlagDisabled) {
    processor->mIsRestrictedMetricsEnabled = false;

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_EQ(error, "Restricted metrics are not enabled");
}

TEST_F(RestrictedEventMetricE2eTest, TestEnforceTtlRemovesOldEvents) {
    int64_t currentWallTimeNs = getWallClockNs();
    // 8 days are used here because the TTL threshold is 7 days.
    int64_t eightDaysAgo = currentWallTimeNs - 8 * 24 * 3600 * NS_PER_SEC;
    int64_t originalEventElapsedTime = configAddedTimeNs + 100;
    std::unique_ptr<LogEvent> event1 = CreateRestrictedLogEvent(atomTag, originalEventElapsedTime);
    event1->setLogdWallClockTimestampNs(eightDaysAgo);

    // Send log events to StatsLogProcessor.
    processor->OnLogEvent(event1.get(), originalEventElapsedTime);
    processor->EnforceDataTtls(currentWallTimeNs, originalEventElapsedTime + 100);

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    EXPECT_TRUE(dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));
    ASSERT_EQ(rows.size(), 0);
}

TEST_F(RestrictedEventMetricE2eTest, TestConfigRemovalDeletesData) {
    std::vector<std::unique_ptr<LogEvent>> events;

    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    processor->OnConfigRemoved(configKey);

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    EXPECT_FALSE(dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));

    EXPECT_THAT(err, StartsWith("unable to open database file"));
}

TEST_F(RestrictedEventMetricE2eTest, TestConfigUpdateRestrictedDelegateCleared) {
    std::vector<std::unique_ptr<LogEvent>> events;
    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    // Update the existing config with no delegate
    config.clear_restricted_metrics_delegate_package_name();
    processor->OnConfigUpdated(configAddedTimeNs + 1 * NS_PER_SEC, configKey, config);

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    EXPECT_FALSE(dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));
    EXPECT_EQ(rows.size(), 0);
    EXPECT_THAT(err, StartsWith("unable to open database file"));
    dbutils::deleteDb(configKey);
}

TEST_F(RestrictedEventMetricE2eTest, TestNonModularConfigUpdateRestrictedDelegate) {
    std::vector<std::unique_ptr<LogEvent>> events;
    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    // Update the existing config without modular update
    processor->OnConfigUpdated(configAddedTimeNs + 1 * NS_PER_SEC, configKey, config, false);

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    EXPECT_FALSE(dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));
    EXPECT_EQ(rows.size(), 0);
    EXPECT_THAT(err, StartsWith("unable to open database file"));
    dbutils::deleteDb(configKey);
}

TEST_F(RestrictedEventMetricE2eTest, TestInvalidConfigUpdateRestrictedDelegate) {
    std::vector<std::unique_ptr<LogEvent>> events;
    events.push_back(CreateRestrictedLogEvent(atomTag, configAddedTimeNs + 100));

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    EventMetric metricWithoutMatcher = createEventMetric("metricWithoutMatcher", 999999, nullopt);
    *config.add_event_metric() = metricWithoutMatcher;
    // Update the existing config with an invalid config update
    processor->OnConfigUpdated(configAddedTimeNs + 1 * NS_PER_SEC, configKey, config);

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    EXPECT_FALSE(dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));
    EXPECT_EQ(rows.size(), 0);
    EXPECT_THAT(err, StartsWith("unable to open database file"));
}

TEST_F(RestrictedEventMetricE2eTest, TestLogEventsEnforceTtls) {
    int64_t currentWallTimeNs = getWallClockNs();
    int64_t originalEventElapsedTime = configAddedTimeNs + 100;
    // 2 hours used here because the TTL check period is 1 hour.
    int64_t newEventElapsedTime = configAddedTimeNs + 2 * 3600 * NS_PER_SEC + 1;  // 2 hrs later
    std::unique_ptr<LogEvent> event1 = CreateRestrictedLogEvent(atomTag, originalEventElapsedTime);
    event1->setLogdWallClockTimestampNs(eightDaysAgo);
    std::unique_ptr<LogEvent> event2 =
            CreateRestrictedLogEvent(atomTag, originalEventElapsedTime + 100);
    event2->setLogdWallClockTimestampNs(oneDayAgo);
    std::unique_ptr<LogEvent> event3 = CreateRestrictedLogEvent(atomTag, newEventElapsedTime);
    event3->setLogdWallClockTimestampNs(currentWallTimeNs);

    processor->mLastTtlTime = originalEventElapsedTime;
    // Send log events to StatsLogProcessor.
    processor->OnLogEvent(event1.get(), originalEventElapsedTime);
    processor->OnLogEvent(event2.get(), newEventElapsedTime);
    processor->OnLogEvent(event3.get(), newEventElapsedTime + 100);

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    EXPECT_TRUE(dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));
    ASSERT_EQ(rows.size(), 2);
    EXPECT_THAT(columnNames,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));
    EXPECT_THAT(columnTypes,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER));
    EXPECT_THAT(rows[0], ElementsAre(to_string(atomTag), to_string(originalEventElapsedTime + 100),
                                     to_string(oneDayAgo), _));
    EXPECT_THAT(rows[1], ElementsAre(to_string(atomTag), to_string(newEventElapsedTime),
                                     to_string(currentWallTimeNs), _));
}

TEST_F(RestrictedEventMetricE2eTest, TestLogEventsDoesNotEnforceTtls) {
    int64_t currentWallTimeNs = getWallClockNs();
    int64_t originalEventElapsedTime = configAddedTimeNs + 100;
    // 30 min used here because the TTL check period is 1 hour.
    int64_t newEventElapsedTime = configAddedTimeNs + (3600 * NS_PER_SEC) / 2;  // 30 min later
    std::unique_ptr<LogEvent> event1 = CreateRestrictedLogEvent(atomTag, originalEventElapsedTime);
    event1->setLogdWallClockTimestampNs(eightDaysAgo);
    std::unique_ptr<LogEvent> event2 = CreateRestrictedLogEvent(atomTag, newEventElapsedTime);
    event2->setLogdWallClockTimestampNs(currentWallTimeNs);

    processor->mLastTtlTime = originalEventElapsedTime;
    // Send log events to StatsLogProcessor.
    processor->OnLogEvent(event1.get(), originalEventElapsedTime);
    processor->OnLogEvent(event2.get(), newEventElapsedTime);

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    string err;
    std::vector<int32_t> columnTypes;
    std::vector<string> columnNames;
    std::vector<std::vector<std::string>> rows;
    EXPECT_TRUE(dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));
    ASSERT_EQ(rows.size(), 2);
    EXPECT_THAT(columnNames,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));
    EXPECT_THAT(columnTypes,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER));
    EXPECT_THAT(rows[0], ElementsAre(to_string(atomTag), to_string(originalEventElapsedTime),
                                     to_string(eightDaysAgo), _));
    EXPECT_THAT(rows[1], ElementsAre(to_string(atomTag), to_string(newEventElapsedTime),
                                     to_string(currentWallTimeNs), _));
}

TEST_F(RestrictedEventMetricE2eTest, TestQueryEnforceTtls) {
    int64_t currentWallTimeNs = getWallClockNs();
    int64_t originalEventElapsedTime = configAddedTimeNs + 100;
    // 30 min used here because the TTL check period is 1 hour.
    int64_t newEventElapsedTime = configAddedTimeNs + (3600 * NS_PER_SEC) / 2;  // 30 min later
    std::unique_ptr<LogEvent> event1 = CreateRestrictedLogEvent(atomTag, originalEventElapsedTime);
    event1->setLogdWallClockTimestampNs(eightDaysAgo);
    std::unique_ptr<LogEvent> event2 = CreateRestrictedLogEvent(atomTag, newEventElapsedTime);
    event2->setLogdWallClockTimestampNs(currentWallTimeNs);

    processor->mLastTtlTime = originalEventElapsedTime;
    // Send log events to StatsLogProcessor.
    processor->OnLogEvent(event1.get(), originalEventElapsedTime);
    processor->OnLogEvent(event2.get(), newEventElapsedTime);

    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(restrictedMetricId);
    processor->querySql(query.str(), /*minSqlClientVersion=*/0,
                        /*policyConfig=*/{}, mockStatsQueryCallback,
                        /*configKey=*/configId, /*configPackage=*/config_package_name,
                        /*callingUid=*/delegate_uid);

    EXPECT_EQ(rowCountResult, 1);
    EXPECT_THAT(queryDataResult, ElementsAre(to_string(atomTag), to_string(newEventElapsedTime),
                                             to_string(currentWallTimeNs),
                                             _  // field_1
                                             ));
    EXPECT_THAT(columnNamesResult,
                ElementsAre("atomId", "elapsedTimestampNs", "wallTimestampNs", "field_1"));
    EXPECT_THAT(columnTypesResult,
                ElementsAre(SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER, SQLITE_INTEGER));
}

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif

}  // namespace statsd
}  // namespace os
}  // namespace android
