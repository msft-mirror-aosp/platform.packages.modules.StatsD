#include <gtest/gtest.h>

#include "flags/FlagProvider.h"
#include "storage/StorageManager.h"
#include "tests/statsd_test_util.h"

namespace android {
namespace os {
namespace statsd {

#ifdef __ANDROID__

namespace {
StatsdConfig CreateSimpleConfig() {
    StatsdConfig config;
    config.add_allowed_log_source("AID_ROOT");
    AtomMatcher wakelockAcquireMatcher = CreateAcquireWakelockAtomMatcher();
    *config.add_atom_matcher() = wakelockAcquireMatcher;

    EventMetric wakelockEventMetric =
            createEventMetric("EventWakelockStateChanged", wakelockAcquireMatcher.id(), nullopt);
    *config.add_event_metric() = wakelockEventMetric;

    return config;
}

std::vector<std::unique_ptr<LogEvent>> CreateLogEvents() {
    std::vector<std::unique_ptr<LogEvent>> events;

    int app1Uid = 123;
    vector<int> attributionUids = {app1Uid};
    std::vector<string> attributionTags = {"App1"};

    events.push_back(
            CreateAcquireWakelockEvent(10 * NS_PER_SEC, attributionUids, attributionTags, "wl1"));
    events.push_back(
            CreateAcquireWakelockEvent(20 * NS_PER_SEC, attributionUids, attributionTags, "wl1"));
    events.push_back(
            CreateAcquireWakelockEvent(30 * NS_PER_SEC, attributionUids, attributionTags, "wl2"));
    return events;
}

}  // Anonymous namespace

class RestrictedConfigE2ETest : public StatsServiceConfigTest {
protected:
    void SetUp() override {
        StatsServiceConfigTest::SetUp();
        FlagProvider::getInstance().overrideFuncs(&isAtLeastSFuncTrue);
        FlagProvider::getInstance().overrideFlag(RESTRICTED_METRICS_FLAG, FLAG_TRUE,
                                                 /*isBootFlag=*/true);
    }
    void TearDown() override {
        StatsServiceConfigTest::TearDown();
        FlagProvider::getInstance().resetOverrides();
    }
};

TEST_F(RestrictedConfigE2ETest, RestrictedConfigNoReport) {
    StatsdConfig config = CreateSimpleConfig();
    config.set_restricted_metrics_delegate_package_name("delegate");
    sendConfig(config);

    for (auto& event : CreateLogEvents()) {
        service->OnLogEvent(event.get());
    }

    vector<uint8_t> output;
    ConfigKey configKey(kCallingUid, kConfigKey);
    service->getData(kConfigKey, kCallingUid, &output);

    EXPECT_TRUE(output.empty());
}

TEST_F(RestrictedConfigE2ETest, NonRestrictedConfigGetReport) {
    StatsdConfig config = CreateSimpleConfig();
    sendConfig(config);

    for (auto& event : CreateLogEvents()) {
        service->OnLogEvent(event.get());
    }

    ConfigMetricsReport report = getReports(service->mProcessor, /*timestamp=*/10);
    EXPECT_EQ(report.metrics_size(), 1);
}

TEST_F(RestrictedConfigE2ETest, RestrictedShutdownDoNotWriteDataToDisk) {
    StatsdConfig config = CreateSimpleConfig();
    config.set_restricted_metrics_delegate_package_name("delegate");
    sendConfig(config);
    for (auto& event : CreateLogEvents()) {
        service->OnLogEvent(event.get());
    }

    service->informDeviceShutdown();

    EXPECT_FALSE(StorageManager::hasConfigMetricsReport(ConfigKey(kCallingUid, kConfigKey)));
}

TEST_F(RestrictedConfigE2ETest, NonRestrictedOnShutdownWriteDataToDisk) {
    StatsdConfig config = CreateSimpleConfig();
    sendConfig(config);
    for (auto& event : CreateLogEvents()) {
        service->OnLogEvent(event.get());
    }

    service->informDeviceShutdown();

    EXPECT_TRUE(StorageManager::hasConfigMetricsReport(ConfigKey(kCallingUid, kConfigKey)));
}

TEST_F(RestrictedConfigE2ETest, RestrictedConfigOnTerminateDoNotWriteDataToDisk) {
    StatsdConfig config = CreateSimpleConfig();
    config.set_restricted_metrics_delegate_package_name("delegate");
    sendConfig(config);
    for (auto& event : CreateLogEvents()) {
        service->OnLogEvent(event.get());
    }

    service->Terminate();

    EXPECT_FALSE(StorageManager::hasConfigMetricsReport(ConfigKey(kCallingUid, kConfigKey)));
}

TEST_F(RestrictedConfigE2ETest, NonRestrictedConfigOnTerminateWriteDataToDisk) {
    StatsdConfig config = CreateSimpleConfig();
    sendConfig(config);
    for (auto& event : CreateLogEvents()) {
        service->OnLogEvent(event.get());
    }

    service->Terminate();

    EXPECT_TRUE(StorageManager::hasConfigMetricsReport(ConfigKey(kCallingUid, kConfigKey)));
}

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif

}  // namespace statsd
}  // namespace os
}  // namespace android