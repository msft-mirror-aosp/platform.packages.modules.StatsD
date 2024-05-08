// Copyright (C) 2017 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <private/android_filesystem_config.h>
#include <stdio.h>

#include <set>
#include <unordered_map>
#include <vector>

#include "metrics/metrics_test_helper.h"
#include "src/condition/ConditionTracker.h"
#include "src/matchers/AtomMatchingTracker.h"
#include "src/metrics/CountMetricProducer.h"
#include "src/metrics/GaugeMetricProducer.h"
#include "src/metrics/MetricProducer.h"
#include "src/metrics/NumericValueMetricProducer.h"
#include "src/metrics/parsing_utils/metrics_manager_util.h"
#include "src/state/StateManager.h"
#include "src/stats_log.pb.h"
#include "src/statsd_config.pb.h"
#include "statsd_test_util.h"
#include "statslog_statsdtest.h"

using namespace testing;
using android::sp;
using android::os::statsd::Predicate;
using std::map;
using std::set;
using std::unordered_map;
using std::vector;

#ifdef __ANDROID__

namespace android {
namespace os {
namespace statsd {

namespace {
const int kConfigId = 12345;
const ConfigKey kConfigKey(0, kConfigId);

const long timeBaseSec = 1000;

StatsdConfig buildEventConfig(bool isRestricted) {
    StatsdConfig config;
    config.set_id(kConfigId);
    if (isRestricted) {
        config.set_restricted_metrics_delegate_package_name("delegate");
    }

    AtomMatcher* eventMatcher = config.add_atom_matcher();
    eventMatcher->set_id(StringToId("SCREEN_IS_ON"));
    SimpleAtomMatcher* simpleAtomMatcher = eventMatcher->mutable_simple_atom_matcher();
    simpleAtomMatcher->set_atom_id(2 /*SCREEN_STATE_CHANGE*/);

    EventMetric* metric = config.add_event_metric();
    metric->set_id(3);
    metric->set_what(StringToId("SCREEN_IS_ON"));
    return config;
}

StatsdConfig buildGoodRestrictedConfig() {
    return buildEventConfig(/*isRestricted*/ true);
}

StatsdConfig buildGoodEventConfig() {
    return buildEventConfig(/*isRestricted*/ false);
}

set<int32_t> unionSet(const vector<set<int32_t>> sets) {
    set<int32_t> toRet;
    for (const set<int32_t>& s : sets) {
        toRet.insert(s.begin(), s.end());
    }
    return toRet;
}

SocketLossInfo createSocketLossInfo(int32_t uid, int32_t atomId) {
    SocketLossInfo lossInfo;
    lossInfo.uid = uid;
    lossInfo.errors.push_back(-11);
    lossInfo.atomIds.push_back(atomId);
    lossInfo.counts.push_back(1);
    return lossInfo;
}

// helper API to create STATS_SOCKET_LOSS_REPORTED LogEvent
LogEvent createSocketLossInfoLogEvent(int32_t uid, int32_t lossAtomId) {
    const SocketLossInfo lossInfo = createSocketLossInfo(uid, lossAtomId);

    AStatsEvent* statsEvent = AStatsEvent_obtain();
    AStatsEvent_setAtomId(statsEvent, util::STATS_SOCKET_LOSS_REPORTED);
    AStatsEvent_writeInt32(statsEvent, lossInfo.uid);
    AStatsEvent_addBoolAnnotation(statsEvent, ASTATSLOG_ANNOTATION_ID_IS_UID, true);
    AStatsEvent_writeInt64(statsEvent, lossInfo.firstLossTsNanos);
    AStatsEvent_writeInt64(statsEvent, lossInfo.lastLossTsNanos);
    AStatsEvent_writeInt32(statsEvent, lossInfo.overflowCounter);
    AStatsEvent_writeInt32Array(statsEvent, lossInfo.errors.data(), lossInfo.errors.size());
    AStatsEvent_writeInt32Array(statsEvent, lossInfo.atomIds.data(), lossInfo.atomIds.size());
    AStatsEvent_writeInt32Array(statsEvent, lossInfo.counts.data(), lossInfo.counts.size());

    LogEvent event(uid /* uid */, 0 /* pid */);
    parseStatsEventToLogEvent(statsEvent, &event);
    return event;
}

}  // anonymous namespace

TEST(MetricsManagerTest, TestLogSources) {
    string app1 = "app1";
    set<int32_t> app1Uids = {1111, 11111};
    string app2 = "app2";
    set<int32_t> app2Uids = {2222};
    string app3 = "app3";
    set<int32_t> app3Uids = {3333, 1111};

    map<string, set<int32_t>> pkgToUids;
    pkgToUids[app1] = app1Uids;
    pkgToUids[app2] = app2Uids;
    pkgToUids[app3] = app3Uids;

    int32_t atom1 = 10, atom2 = 20, atom3 = 30;
    sp<MockUidMap> uidMap = new StrictMock<MockUidMap>();
    EXPECT_CALL(*uidMap, getAppUid(_))
            .Times(4)
            .WillRepeatedly(Invoke([&pkgToUids](const string& pkg) {
                const auto& it = pkgToUids.find(pkg);
                if (it != pkgToUids.end()) {
                    return it->second;
                }
                return set<int32_t>();
            }));
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();
    EXPECT_CALL(*pullerManager, RegisterPullUidProvider(kConfigKey, _)).Times(1);
    EXPECT_CALL(*pullerManager, UnregisterPullUidProvider(kConfigKey, _)).Times(1);

    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;

    StatsdConfig config;
    config.add_allowed_log_source("AID_SYSTEM");
    config.add_allowed_log_source(app1);
    config.add_default_pull_packages("AID_SYSTEM");
    config.add_default_pull_packages("AID_ROOT");

    const set<int32_t> defaultPullUids = {AID_SYSTEM, AID_ROOT};

    PullAtomPackages* pullAtomPackages = config.add_pull_atom_packages();
    pullAtomPackages->set_atom_id(atom1);
    pullAtomPackages->add_packages(app1);
    pullAtomPackages->add_packages(app3);

    pullAtomPackages = config.add_pull_atom_packages();
    pullAtomPackages->set_atom_id(atom2);
    pullAtomPackages->add_packages(app2);
    pullAtomPackages->add_packages("AID_STATSD");

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);
    EXPECT_TRUE(metricsManager.isConfigValid());

    EXPECT_THAT(metricsManager.mAllowedUid, ElementsAre(AID_SYSTEM));
    EXPECT_THAT(metricsManager.mAllowedPkg, ElementsAre(app1));
    EXPECT_THAT(metricsManager.mAllowedLogSources,
                ContainerEq(unionSet(vector<set<int32_t>>({app1Uids, {AID_SYSTEM}}))));
    EXPECT_THAT(metricsManager.mDefaultPullUids, ContainerEq(defaultPullUids));

    vector<int32_t> atom1Uids = metricsManager.getPullAtomUids(atom1);
    EXPECT_THAT(atom1Uids,
                UnorderedElementsAreArray(unionSet({defaultPullUids, app1Uids, app3Uids})));

    vector<int32_t> atom2Uids = metricsManager.getPullAtomUids(atom2);
    EXPECT_THAT(atom2Uids,
                UnorderedElementsAreArray(unionSet({defaultPullUids, app2Uids, {AID_STATSD}})));

    vector<int32_t> atom3Uids = metricsManager.getPullAtomUids(atom3);
    EXPECT_THAT(atom3Uids, UnorderedElementsAreArray(defaultPullUids));
}

TEST(MetricsManagerTest, TestLogSourcesOnConfigUpdate) {
    string app1 = "app1";
    set<int32_t> app1Uids = {11110, 11111};
    string app2 = "app2";
    set<int32_t> app2Uids = {22220};
    string app3 = "app3";
    set<int32_t> app3Uids = {33330, 11110};

    map<string, set<int32_t>> pkgToUids;
    pkgToUids[app1] = app1Uids;
    pkgToUids[app2] = app2Uids;
    pkgToUids[app3] = app3Uids;

    int32_t atom1 = 10, atom2 = 20, atom3 = 30;
    sp<MockUidMap> uidMap = new StrictMock<MockUidMap>();
    EXPECT_CALL(*uidMap, getAppUid(_))
            .Times(8)
            .WillRepeatedly(Invoke([&pkgToUids](const string& pkg) {
                const auto& it = pkgToUids.find(pkg);
                if (it != pkgToUids.end()) {
                    return it->second;
                }
                return set<int32_t>();
            }));
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();
    EXPECT_CALL(*pullerManager, RegisterPullUidProvider(kConfigKey, _)).Times(1);
    EXPECT_CALL(*pullerManager, UnregisterPullUidProvider(kConfigKey, _)).Times(1);

    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;

    StatsdConfig config;
    config.add_allowed_log_source(app1);
    config.add_default_pull_packages("AID_SYSTEM");
    config.add_default_pull_packages("AID_ROOT");

    PullAtomPackages* pullAtomPackages = config.add_pull_atom_packages();
    pullAtomPackages->set_atom_id(atom1);
    pullAtomPackages->add_packages(app1);
    pullAtomPackages->add_packages(app3);

    pullAtomPackages = config.add_pull_atom_packages();
    pullAtomPackages->set_atom_id(atom2);
    pullAtomPackages->add_packages(app2);
    pullAtomPackages->add_packages("AID_STATSD");

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);
    EXPECT_TRUE(metricsManager.isConfigValid());

    // Update with new allowed log sources.
    StatsdConfig newConfig;
    newConfig.add_allowed_log_source(app2);
    newConfig.add_default_pull_packages("AID_SYSTEM");
    newConfig.add_default_pull_packages("AID_STATSD");

    pullAtomPackages = newConfig.add_pull_atom_packages();
    pullAtomPackages->set_atom_id(atom2);
    pullAtomPackages->add_packages(app1);
    pullAtomPackages->add_packages(app3);

    pullAtomPackages = newConfig.add_pull_atom_packages();
    pullAtomPackages->set_atom_id(atom3);
    pullAtomPackages->add_packages(app2);
    pullAtomPackages->add_packages("AID_ADB");

    metricsManager.updateConfig(newConfig, timeBaseSec, timeBaseSec, anomalyAlarmMonitor,
                                periodicAlarmMonitor);
    EXPECT_TRUE(metricsManager.isConfigValid());

    EXPECT_THAT(metricsManager.mAllowedPkg, ElementsAre(app2));
    EXPECT_THAT(metricsManager.mAllowedLogSources,
                ContainerEq(unionSet(vector<set<int32_t>>({app2Uids}))));
    const set<int32_t> defaultPullUids = {AID_SYSTEM, AID_STATSD};
    EXPECT_THAT(metricsManager.mDefaultPullUids, ContainerEq(defaultPullUids));

    vector<int32_t> atom1Uids = metricsManager.getPullAtomUids(atom1);
    EXPECT_THAT(atom1Uids, UnorderedElementsAreArray(defaultPullUids));

    vector<int32_t> atom2Uids = metricsManager.getPullAtomUids(atom2);
    EXPECT_THAT(atom2Uids,
                UnorderedElementsAreArray(unionSet({defaultPullUids, app1Uids, app3Uids})));

    vector<int32_t> atom3Uids = metricsManager.getPullAtomUids(atom3);
    EXPECT_THAT(atom3Uids,
                UnorderedElementsAreArray(unionSet({defaultPullUids, app2Uids, {AID_ADB}})));
}

struct MetricsManagerServerFlagParam {
    string flagValue;
    string label;
};

class MetricsManagerTest_SPlus
    : public ::testing::Test,
      public ::testing::WithParamInterface<MetricsManagerServerFlagParam> {
protected:
    void SetUp() override {
        if (shouldSkipTest()) {
            GTEST_SKIP() << skipReason();
        }
    }

    bool shouldSkipTest() const {
        return !isAtLeastS();
    }

    string skipReason() const {
        return "Skipping MetricsManagerTest_SPlus because device is not S+";
    }

    std::optional<string> originalFlagValue;
};

INSTANTIATE_TEST_SUITE_P(
        MetricsManagerTest_SPlus, MetricsManagerTest_SPlus,
        testing::ValuesIn<MetricsManagerServerFlagParam>({
                // Server flag values
                {FLAG_TRUE, "ServerFlagTrue"},
                {FLAG_FALSE, "ServerFlagFalse"},
        }),
        [](const testing::TestParamInfo<MetricsManagerTest_SPlus::ParamType>& info) {
            return info.param.label;
        });

TEST(MetricsManagerTest, TestCheckLogCredentialsWhitelistedAtom) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;

    StatsdConfig config;
    config.add_whitelisted_atom_ids(3);
    config.add_whitelisted_atom_ids(4);

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    const int32_t customAppUid = AID_APP_START + 1;
    LogEvent event(customAppUid /* uid */, 0 /* pid */);
    CreateNoValuesLogEvent(&event, 10 /* atom id */, 0 /* timestamp */);
    EXPECT_FALSE(metricsManager.checkLogCredentials(event));

    CreateNoValuesLogEvent(&event, 3 /* atom id */, 0 /* timestamp */);
    EXPECT_TRUE(metricsManager.checkLogCredentials(event));

    CreateNoValuesLogEvent(&event, 4 /* atom id */, 0 /* timestamp */);
    EXPECT_TRUE(metricsManager.checkLogCredentials(event));
}

TEST(MetricsManagerTest, TestOnLogEventLossForAllowedFromAnyUidAtom) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;

    StatsdConfig config = buildGoodEventConfig();
    config.add_whitelisted_atom_ids(2);

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    const int32_t customAppUid = AID_APP_START + 1;
    LogEvent eventOfInterest(customAppUid /* uid */, 0 /* pid */);
    CreateNoValuesLogEvent(&eventOfInterest, 2 /* atom id */, 0 /* timestamp */);
    EXPECT_TRUE(metricsManager.checkLogCredentials(eventOfInterest));
    EXPECT_FALSE(metricsManager.mAllMetricProducers[0]->mDataCorruptedDueToSocketLoss);

    LogEvent eventSocketLossReported = createSocketLossInfoLogEvent(customAppUid, 2);

    // the STATS_SOCKET_LOSS_REPORTED on its own will not be propagated/consumed by any metric
    EXPECT_FALSE(metricsManager.checkLogCredentials(eventSocketLossReported));

    // the loss info for an atom of interest (2) will be evaluated even when
    // STATS_SOCKET_LOSS_REPORTED atom is not explicitly in allowed list
    metricsManager.onLogEvent(eventSocketLossReported);

    // check that corresponding event metric was updated with loss info
    // the invariant is there is only one metric in the config
    EXPECT_TRUE(metricsManager.mAllMetricProducers[0]->mDataCorruptedDueToSocketLoss);
}

TEST(MetricsManagerTest, TestOnLogEventLossForNotAllowedAtom) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;

    StatsdConfig config = buildGoodEventConfig();

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    const int32_t customAppUid = AID_APP_START + 1;
    LogEvent eventOfInterest(customAppUid /* uid */, 0 /* pid */);
    CreateNoValuesLogEvent(&eventOfInterest, 2 /* atom id */, 0 /* timestamp */);
    EXPECT_FALSE(metricsManager.checkLogCredentials(eventOfInterest));
    EXPECT_FALSE(metricsManager.mAllMetricProducers[0]->mDataCorruptedDueToSocketLoss);

    LogEvent eventSocketLossReported = createSocketLossInfoLogEvent(customAppUid, 2);

    // the STATS_SOCKET_LOSS_REPORTED on its own will not be propagated/consumed by any metric
    EXPECT_FALSE(metricsManager.checkLogCredentials(eventSocketLossReported));

    // the loss info for an atom of interest (2) will not be evaluated since atom of interest does
    // not pass credential check
    metricsManager.onLogEvent(eventSocketLossReported);

    // check that corresponding event metric was not updated with loss info
    // the invariant is there is only one metric in the config
    EXPECT_FALSE(metricsManager.mAllMetricProducers[0]->mDataCorruptedDueToSocketLoss);
}

TEST(MetricsManagerTest, TestWhitelistedAtomStateTracker) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;

    StatsdConfig config = buildGoodConfig(kConfigId);
    config.add_allowed_log_source("AID_SYSTEM");
    config.add_whitelisted_atom_ids(3);
    config.add_whitelisted_atom_ids(4);

    State state;
    state.set_id(1);
    state.set_atom_id(3);

    *config.add_state() = state;

    config.mutable_count_metric(0)->add_slice_by_state(state.id());

    StateManager::getInstance().clear();

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    EXPECT_EQ(0, StateManager::getInstance().getStateTrackersCount());
    EXPECT_FALSE(metricsManager.isConfigValid());
}

TEST_P(MetricsManagerTest_SPlus, TestRestrictedMetricsConfig) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;

    StatsdConfig config = buildGoodRestrictedConfig();
    config.add_allowed_log_source("AID_SYSTEM");
    config.set_restricted_metrics_delegate_package_name("rm");

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    if (isAtLeastU()) {
        EXPECT_TRUE(metricsManager.isConfigValid());
    } else {
        EXPECT_EQ(metricsManager.mInvalidConfigReason,
                  INVALID_CONFIG_REASON_RESTRICTED_METRIC_NOT_ENABLED);
        ASSERT_FALSE(metricsManager.isConfigValid());
    }
}

TEST_P(MetricsManagerTest_SPlus, TestRestrictedMetricsConfigUpdate) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;

    StatsdConfig config = buildGoodRestrictedConfig();
    config.add_allowed_log_source("AID_SYSTEM");
    config.set_restricted_metrics_delegate_package_name("rm");

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    StatsdConfig config2 = buildGoodRestrictedConfig();
    metricsManager.updateConfig(config, timeBaseSec, timeBaseSec, anomalyAlarmMonitor,
                                periodicAlarmMonitor);

    if (isAtLeastU()) {
        EXPECT_TRUE(metricsManager.isConfigValid());
    } else {
        EXPECT_EQ(metricsManager.mInvalidConfigReason,
                  INVALID_CONFIG_REASON_RESTRICTED_METRIC_NOT_ENABLED);
        ASSERT_FALSE(metricsManager.isConfigValid());
    }
}

TEST(MetricsManagerTest, TestMaxMetricsMemoryKb) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;
    size_t memoryLimitKb = 8 * 1024;

    StatsdConfig config = buildGoodConfig(kConfigId);
    config.add_allowed_log_source("AID_SYSTEM");
    config.set_max_metrics_memory_kb(memoryLimitKb);

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    EXPECT_EQ(memoryLimitKb, metricsManager.getMaxMetricsBytes() / 1024);
    EXPECT_TRUE(metricsManager.isConfigValid());
}

TEST(MetricsManagerTest, TestMaxMetricsMemoryKbOnConfigUpdate) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;
    size_t memoryLimitKb = 8 * 1024;

    StatsdConfig config = buildGoodConfig(kConfigId);
    config.add_allowed_log_source("AID_SYSTEM");
    config.set_max_metrics_memory_kb(memoryLimitKb);

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    EXPECT_EQ(memoryLimitKb, metricsManager.getMaxMetricsBytes() / 1024);
    EXPECT_TRUE(metricsManager.isConfigValid());

    // Update with new memory limit
    size_t newMemoryLimitKb = 10 * 1024;
    StatsdConfig newConfig;
    newConfig.add_allowed_log_source("AID_SYSTEM");
    newConfig.set_max_metrics_memory_kb(newMemoryLimitKb);

    metricsManager.updateConfig(newConfig, timeBaseSec, timeBaseSec, anomalyAlarmMonitor,
                                periodicAlarmMonitor);
    EXPECT_EQ(newMemoryLimitKb, metricsManager.getMaxMetricsBytes() / 1024);
    EXPECT_TRUE(metricsManager.isConfigValid());
}

TEST(MetricsManagerTest, TestMaxMetricsMemoryKbInvalid) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;
    size_t memoryLimitKb = (StatsdStats::kHardMaxMetricsBytesPerConfig / 1024) + 1;
    size_t defaultMemoryLimit = StatsdStats::kDefaultMaxMetricsBytesPerConfig;

    StatsdConfig config = buildGoodConfig(kConfigId);
    config.add_allowed_log_source("AID_SYSTEM");
    config.set_max_metrics_memory_kb(memoryLimitKb);

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    // Since 20MB + 1B is invalid for the memory limit, we default back to 2MB
    EXPECT_EQ(defaultMemoryLimit, metricsManager.getMaxMetricsBytes());
    EXPECT_TRUE(metricsManager.isConfigValid());
}

TEST(MetricsManagerTest, TestGetTriggerMemoryKb) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;
    size_t memoryLimitKb = 8 * 1024;

    StatsdConfig config = buildGoodConfig(kConfigId);
    config.add_allowed_log_source("AID_SYSTEM");
    config.set_soft_metrics_memory_kb(memoryLimitKb);

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    EXPECT_EQ(memoryLimitKb, metricsManager.getTriggerGetDataBytes() / 1024);
    EXPECT_TRUE(metricsManager.isConfigValid());
}

TEST(MetricsManagerTest, TestGetTriggerMemoryKbOnConfigUpdate) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;
    size_t memoryLimitKb = 8 * 1024;

    StatsdConfig config = buildGoodConfig(kConfigId);
    config.add_allowed_log_source("AID_SYSTEM");
    config.set_soft_metrics_memory_kb(memoryLimitKb);

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    EXPECT_EQ(memoryLimitKb, metricsManager.getTriggerGetDataBytes() / 1024);
    EXPECT_TRUE(metricsManager.isConfigValid());

    // Update with new memory limit
    size_t newMemoryLimitKb = 9 * 1024;
    StatsdConfig newConfig;
    newConfig.add_allowed_log_source("AID_SYSTEM");
    newConfig.set_soft_metrics_memory_kb(newMemoryLimitKb);

    metricsManager.updateConfig(newConfig, timeBaseSec, timeBaseSec, anomalyAlarmMonitor,
                                periodicAlarmMonitor);
    EXPECT_EQ(newMemoryLimitKb, metricsManager.getTriggerGetDataBytes() / 1024);
    EXPECT_TRUE(metricsManager.isConfigValid());
}

TEST(MetricsManagerTest, TestGetTriggerMemoryKbInvalid) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;
    size_t memoryLimitKb = (StatsdStats::kHardMaxTriggerGetDataBytes / 1024) + 1;
    size_t defaultMemoryLimit = StatsdStats::kDefaultBytesPerConfigTriggerGetData;

    StatsdConfig config = buildGoodConfig(kConfigId);
    config.add_allowed_log_source("AID_SYSTEM");
    config.set_soft_metrics_memory_kb(memoryLimitKb);

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    // Since 10MB + 1B is invalid for the memory limit, we default back to 192KB
    EXPECT_EQ(defaultMemoryLimit, metricsManager.getTriggerGetDataBytes());
    EXPECT_TRUE(metricsManager.isConfigValid());
}

TEST(MetricsManagerTest, TestGetTriggerMemoryKbUnset) {
    sp<UidMap> uidMap;
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    sp<AlarmMonitor> anomalyAlarmMonitor;
    sp<AlarmMonitor> periodicAlarmMonitor;
    size_t defaultMemoryLimit = StatsdStats::kDefaultBytesPerConfigTriggerGetData;

    StatsdConfig config = buildGoodConfig(kConfigId);
    config.add_allowed_log_source("AID_SYSTEM");

    MetricsManager metricsManager(kConfigKey, config, timeBaseSec, timeBaseSec, uidMap,
                                  pullerManager, anomalyAlarmMonitor, periodicAlarmMonitor);

    // Since the memory limit is unset, we default back to 192KB
    EXPECT_EQ(defaultMemoryLimit, metricsManager.getTriggerGetDataBytes());
    EXPECT_TRUE(metricsManager.isConfigValid());
}

class MetricsManagerQueueOverflowInfoPropagationTest : public testing::Test {
protected:
    const int64_t kBucketStartTimeNs = 10000000000;
    const int64_t kReportRequestTimeNs = kBucketStartTimeNs + 100;
    const int64_t kAtomsLogTimeNs = kBucketStartTimeNs + 10;

    StatsdConfig mConfig;
    std::shared_ptr<MetricsManager> mMetricsManager;

    const int32_t kAtomId = 2;
    const int32_t kInterestAtomId = 3;
    const int32_t kInterestedMetricId = 4;

    void SetUp() override {
        StatsdStats::getInstance().reset();

        sp<UidMap> uidMap;
        sp<StatsPullerManager> pullerManager = new StatsPullerManager();
        sp<AlarmMonitor> anomalyAlarmMonitor;
        sp<AlarmMonitor> periodicAlarmMonitor;

        // there will be one event metric interested in kInterestAtomId
        mConfig = buildGoodEventConfig();
        mConfig.add_whitelisted_atom_ids(kAtomId);
        mConfig.add_whitelisted_atom_ids(kInterestAtomId);

        // add one more event metric interested in kInterestAtomId
        addEventMetricForAnAtom(mConfig, kInterestAtomId, kInterestedMetricId);

        mMetricsManager = std::make_shared<MetricsManager>(
                kConfigKey, mConfig, timeBaseSec, timeBaseSec, uidMap, pullerManager,
                anomalyAlarmMonitor, periodicAlarmMonitor);
    }

    void TearDown() override {
        StatsdStats::getInstance().reset();
    }

    ConfigMetricsReport getMetricsReport() {
        ProtoOutputStream output;
        std::set<string> str_set;
        mMetricsManager->onDumpReport(kReportRequestTimeNs, kReportRequestTimeNs,
                                      /*include_current_partial_bucket*/ true, /*erase_data*/ true,
                                      /*dumpLatency*/ NO_TIME_CONSTRAINTS, &str_set, &output);

        vector<uint8_t> bytes;
        output.serializeToVector(&bytes);
        ConfigMetricsReport metricsReport;
        metricsReport.ParseFromArray(bytes.data(), bytes.size());
        return metricsReport;
    }

    static void makeLogEvent(LogEvent* logEvent, int32_t atomId, int64_t timestampNs) {
        AStatsEvent* statsEvent = AStatsEvent_obtain();
        AStatsEvent_setAtomId(statsEvent, atomId);
        AStatsEvent_overwriteTimestamp(statsEvent, timestampNs);
        AStatsEvent_writeString(statsEvent, "demoString");
        EXPECT_TRUE(parseStatsEventToLogEvent(statsEvent, logEvent));
    }

private:
    static void addEventMetricForAnAtom(StatsdConfig& config, int32_t atomId, int64_t metricId) {
        const int64_t matcherId = StringToId("CUSTOM_EVENT" + std::to_string(atomId));
        AtomMatcher* eventMatcher = config.add_atom_matcher();
        eventMatcher->set_id(matcherId);
        SimpleAtomMatcher* simpleAtomMatcher = eventMatcher->mutable_simple_atom_matcher();
        simpleAtomMatcher->set_atom_id(atomId);

        EventMetric* metric = config.add_event_metric();
        metric->set_id(metricId);
        metric->set_what(matcherId);
    }
};

TEST_F(MetricsManagerQueueOverflowInfoPropagationTest, TestNotifyOnlyInterestedMetrics) {
    EXPECT_TRUE(mMetricsManager->isConfigValid());

    StatsdStats::getInstance().noteEventQueueOverflow(kAtomsLogTimeNs, kInterestAtomId,
                                                      /*isSkipped*/ false);

    const int32_t someUnusedAtomId = kInterestAtomId + 100;
    StatsdStats::getInstance().noteEventQueueOverflow(kAtomsLogTimeNs, someUnusedAtomId,
                                                      /*isSkipped*/ false);

    ConfigMetricsReport metricsReport = getMetricsReport();
    ASSERT_EQ(metricsReport.metrics_size(), 2);

    for (const auto& statsLogReport : metricsReport.metrics()) {
        if (statsLogReport.metric_id() == kInterestedMetricId) {
            EXPECT_EQ(statsLogReport.data_corrupted_reason_size(), 1);
            EXPECT_EQ(statsLogReport.data_corrupted_reason(0), DATA_CORRUPTED_EVENT_QUEUE_OVERFLOW);
        } else {
            EXPECT_EQ(statsLogReport.data_corrupted_reason_size(), 0);
        }
    }
}

TEST_F(MetricsManagerQueueOverflowInfoPropagationTest, TestDoNotNotifyInterestedMetricsIfNoUpdate) {
    EXPECT_TRUE(mMetricsManager->isConfigValid());

    EXPECT_TRUE(mMetricsManager->mQueueOverflowAtomsStats.empty());

    ConfigMetricsReport metricsReport = getMetricsReport();
    ASSERT_EQ(metricsReport.metrics_size(), 2);
    EXPECT_TRUE(mMetricsManager->mQueueOverflowAtomsStats.empty());

    for (const auto& statsLogReport : metricsReport.metrics()) {
        if (statsLogReport.metric_id() == kInterestedMetricId) {
            EXPECT_EQ(statsLogReport.data_corrupted_reason_size(), 0);
        }
    }

    StatsdStats::getInstance().noteEventQueueOverflow(kAtomsLogTimeNs, kInterestAtomId,
                                                      /*isSkipped*/ false);

    metricsReport = getMetricsReport();
    ASSERT_EQ(metricsReport.metrics_size(), 2);
    ASSERT_EQ(mMetricsManager->mQueueOverflowAtomsStats.size(), 1);
    ASSERT_EQ(mMetricsManager->mQueueOverflowAtomsStats[kInterestAtomId], 1);

    for (const auto& statsLogReport : metricsReport.metrics()) {
        if (statsLogReport.metric_id() == kInterestedMetricId) {
            EXPECT_EQ(statsLogReport.data_corrupted_reason_size(), 1);
            EXPECT_EQ(statsLogReport.data_corrupted_reason(0), DATA_CORRUPTED_EVENT_QUEUE_OVERFLOW);
        }
    }

    // no more dropped events as result event metric should not be notified about loss events

    metricsReport = getMetricsReport();
    ASSERT_EQ(metricsReport.metrics_size(), 2);
    ASSERT_EQ(mMetricsManager->mQueueOverflowAtomsStats.size(), 1);
    ASSERT_EQ(mMetricsManager->mQueueOverflowAtomsStats[kInterestAtomId], 1);

    for (const auto& statsLogReport : metricsReport.metrics()) {
        EXPECT_EQ(statsLogReport.data_corrupted_reason_size(), 0);
    }
}

}  // namespace statsd
}  // namespace os
}  // namespace android

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
