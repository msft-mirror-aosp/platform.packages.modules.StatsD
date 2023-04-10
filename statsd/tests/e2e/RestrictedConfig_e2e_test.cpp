#include <gtest/gtest.h>

#include "flags/FlagProvider.h"
#include "storage/StorageManager.h"
#include "tests/statsd_test_util.h"

namespace android {
namespace os {
namespace statsd {

#ifdef __ANDROID__

namespace {
const int32_t atomTag = 666;
const string delegate_package_name = "com.test.restricted.metrics.package";
const int32_t delegate_uid = 1005;
const string config_package_name = "com.test.config.package";
int64_t metricId;
int64_t anotherMetricId;

StatsdConfig CreateConfigWithOneMetric() {
    StatsdConfig config;
    config.add_allowed_log_source("AID_ROOT");
    AtomMatcher atomMatcher = CreateSimpleAtomMatcher("testmatcher", atomTag);
    *config.add_atom_matcher() = atomMatcher;

    EventMetric eventMetric = createEventMetric("EventMetric", atomMatcher.id(), nullopt);
    metricId = eventMetric.id();
    *config.add_event_metric() = eventMetric;
    return config;
}
StatsdConfig CreateConfigWithTwoMetrics() {
    StatsdConfig config;
    config.add_allowed_log_source("AID_ROOT");
    AtomMatcher atomMatcher = CreateSimpleAtomMatcher("testmatcher", atomTag);
    *config.add_atom_matcher() = atomMatcher;

    EventMetric eventMetric = createEventMetric("EventMetric", atomMatcher.id(), nullopt);
    metricId = eventMetric.id();
    *config.add_event_metric() = eventMetric;
    EventMetric anotherEventMetric =
            createEventMetric("AnotherEventMetric", atomMatcher.id(), nullopt);
    anotherMetricId = anotherEventMetric.id();
    *config.add_event_metric() = anotherEventMetric;
    return config;
}

std::vector<std::unique_ptr<LogEvent>> CreateLogEvents(int64_t configAddedTimeNs) {
    std::vector<std::unique_ptr<LogEvent>> events;
    events.push_back(CreateNonRestrictedLogEvent(atomTag, configAddedTimeNs + 10 * NS_PER_SEC));
    events.push_back(CreateNonRestrictedLogEvent(atomTag, configAddedTimeNs + 20 * NS_PER_SEC));
    events.push_back(CreateNonRestrictedLogEvent(atomTag, configAddedTimeNs + 30 * NS_PER_SEC));
    return events;
}

}  // Anonymous namespace

class RestrictedConfigE2ETest : public StatsServiceConfigTest {
protected:
    shared_ptr<MockStatsQueryCallback> mockStatsQueryCallback;
    const ConfigKey configKey = ConfigKey(kCallingUid, kConfigKey);
    vector<string> queryDataResult;
    vector<string> columnNamesResult;
    vector<int32_t> columnTypesResult;
    int32_t rowCountResult = 0;
    string error;

    void SetUp() override {
        FlagProvider::getInstance().overrideFuncs(&isAtLeastSFuncTrue);
        FlagProvider::getInstance().overrideFlag(RESTRICTED_METRICS_FLAG, FLAG_TRUE,
                                                 /*isBootFlag=*/true);
        StatsServiceConfigTest::SetUp();

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

        int64_t startTimeNs = getElapsedRealtimeNs();
        service->mUidMap->updateApp(startTimeNs, String16(delegate_package_name.c_str()),
                                    /*uid=*/delegate_uid, /*versionCode=*/1,
                                    /*versionString=*/String16("v2"),
                                    /*installer=*/String16(""), /*certificateHash=*/{});
        service->mUidMap->updateApp(startTimeNs + 1, String16(config_package_name.c_str()),
                                    /*uid=*/kCallingUid, /*versionCode=*/1,
                                    /*versionString=*/String16("v2"),
                                    /*installer=*/String16(""), /*certificateHash=*/{});
    }
    void TearDown() override {
        Mock::VerifyAndClear(mockStatsQueryCallback.get());
        queryDataResult.clear();
        columnNamesResult.clear();
        columnTypesResult.clear();
        rowCountResult = 0;
        error = "";
        StatsServiceConfigTest::TearDown();
        FlagProvider::getInstance().resetOverrides();
        dbutils::deleteDb(configKey);
    }

    void verifyRestrictedData(int32_t expectedNumOfMetrics, int64_t metricIdToVerify = metricId,
                              bool shouldExist = true) {
        std::stringstream query;
        query << "SELECT * FROM metric_" << dbutils::reformatMetricId(metricIdToVerify);
        string err;
        std::vector<int32_t> columnTypes;
        std::vector<string> columnNames;
        std::vector<std::vector<std::string>> rows;
        if (shouldExist) {
            EXPECT_TRUE(
                    dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));
            EXPECT_EQ(rows.size(), expectedNumOfMetrics);
        } else {
            // Expect that table is deleted.
            EXPECT_FALSE(
                    dbutils::query(configKey, query.str(), rows, columnTypes, columnNames, err));
        }
    }
};

TEST_F(RestrictedConfigE2ETest, RestrictedConfigNoReport) {
    StatsdConfig config = CreateConfigWithOneMetric();
    config.set_restricted_metrics_delegate_package_name("delegate");
    sendConfig(config);
    int64_t configAddedTimeNs = getElapsedRealtimeNs();

    for (auto& event : CreateLogEvents(configAddedTimeNs)) {
        service->OnLogEvent(event.get());
    }

    vector<uint8_t> output;
    ConfigKey configKey(kCallingUid, kConfigKey);
    service->getData(kConfigKey, kCallingUid, &output);

    EXPECT_TRUE(output.empty());
}

TEST_F(RestrictedConfigE2ETest, NonRestrictedConfigGetReport) {
    StatsdConfig config = CreateConfigWithOneMetric();
    sendConfig(config);
    int64_t configAddedTimeNs = getElapsedRealtimeNs();

    for (auto& event : CreateLogEvents(configAddedTimeNs)) {
        service->OnLogEvent(event.get());
    }

    ConfigMetricsReport report = getReports(service->mProcessor, /*timestamp=*/10);
    EXPECT_EQ(report.metrics_size(), 1);
}

TEST_F(RestrictedConfigE2ETest, RestrictedShutdownFlushToRestrictedDB) {
    StatsdConfig config = CreateConfigWithOneMetric();
    config.set_restricted_metrics_delegate_package_name("delegate");
    sendConfig(config);
    int64_t configAddedTimeNs = getElapsedRealtimeNs();
    std::vector<std::unique_ptr<LogEvent>> logEvents = CreateLogEvents(configAddedTimeNs);
    for (const auto& e : logEvents) {
        service->OnLogEvent(e.get());
    }

    service->informDeviceShutdown();

    // Should not be written to non-restricted storage.
    EXPECT_FALSE(StorageManager::hasConfigMetricsReport(ConfigKey(kCallingUid, kConfigKey)));
    verifyRestrictedData(logEvents.size());
}

TEST_F(RestrictedConfigE2ETest, NonRestrictedOnShutdownWriteDataToDisk) {
    StatsdConfig config = CreateConfigWithOneMetric();
    sendConfig(config);
    int64_t configAddedTimeNs = getElapsedRealtimeNs();
    for (auto& event : CreateLogEvents(configAddedTimeNs)) {
        service->OnLogEvent(event.get());
    }

    service->informDeviceShutdown();

    EXPECT_TRUE(StorageManager::hasConfigMetricsReport(ConfigKey(kCallingUid, kConfigKey)));
}

TEST_F(RestrictedConfigE2ETest, RestrictedConfigOnTerminateFlushToRestrictedDB) {
    StatsdConfig config = CreateConfigWithOneMetric();
    config.set_restricted_metrics_delegate_package_name("delegate");
    sendConfig(config);
    int64_t configAddedTimeNs = getElapsedRealtimeNs();
    std::vector<std::unique_ptr<LogEvent>> logEvents = CreateLogEvents(configAddedTimeNs);
    for (auto& event : logEvents) {
        service->OnLogEvent(event.get());
    }

    service->Terminate();

    EXPECT_FALSE(StorageManager::hasConfigMetricsReport(ConfigKey(kCallingUid, kConfigKey)));
    verifyRestrictedData(logEvents.size());
}

TEST_F(RestrictedConfigE2ETest, NonRestrictedConfigOnTerminateWriteDataToDisk) {
    StatsdConfig config = CreateConfigWithOneMetric();
    sendConfig(config);
    int64_t configAddedTimeNs = getElapsedRealtimeNs();
    for (auto& event : CreateLogEvents(configAddedTimeNs)) {
        service->OnLogEvent(event.get());
    }

    service->Terminate();

    EXPECT_TRUE(StorageManager::hasConfigMetricsReport(ConfigKey(kCallingUid, kConfigKey)));
}

TEST_F(RestrictedConfigE2ETest, RestrictedConfigOnUpdateWithMetricRemoval) {
    StatsdConfig complexConfig = CreateConfigWithTwoMetrics();
    complexConfig.set_restricted_metrics_delegate_package_name(delegate_package_name);
    sendConfig(complexConfig);
    int64_t configAddedTimeNs = getElapsedRealtimeNs();
    std::vector<std::unique_ptr<LogEvent>> logEvents = CreateLogEvents(configAddedTimeNs);
    for (auto& event : logEvents) {
        service->OnLogEvent(event.get());
    }

    // Use query API to make sure data is flushed.
    std::stringstream query;
    query << "SELECT * FROM metric_" << dbutils::reformatMetricId(metricId);
    service->querySql(query.str(), /*minSqlClientVersion=*/0,
                      /*policyConfig=*/{}, mockStatsQueryCallback,
                      /*configKey=*/kConfigKey, /*configPackage=*/config_package_name,
                      /*callingUid=*/delegate_uid);
    EXPECT_EQ(error, "");
    EXPECT_EQ(rowCountResult, logEvents.size());
    verifyRestrictedData(logEvents.size(), anotherMetricId, true);

    // Update config to have only one metric
    StatsdConfig config = CreateConfigWithOneMetric();
    config.set_restricted_metrics_delegate_package_name(delegate_package_name);
    sendConfig(config);

    // Make sure metric data is deleted.
    verifyRestrictedData(logEvents.size(), metricId, true);
    verifyRestrictedData(logEvents.size(), anotherMetricId, false);
}

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif

}  // namespace statsd
}  // namespace os
}  // namespace android