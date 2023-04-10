// Copyright (C) 2018 The Android Open Source Project
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

#include "src/shell/ShellSubscriber.h"

#include <gtest/gtest.h>
#include <stdio.h>
#include <unistd.h>

#include <vector>

#include "frameworks/proto_logging/stats/atoms.pb.h"
#include "src/shell/shell_config.pb.h"
#include "src/shell/shell_data.pb.h"
#include "stats_event.h"
#include "tests/metrics/metrics_test_helper.h"
#include "tests/statsd_test_util.h"

using android::sp;
using std::vector;
using testing::_;
using testing::Invoke;
using testing::NaggyMock;
using testing::StrictMock;

namespace android {
namespace os {
namespace statsd {

#ifdef __ANDROID__

namespace {

int kUid1 = 1000;
int kUid2 = 2000;

int kCpuTime1 = 100;
int kCpuTime2 = 200;

int64_t kCpuActiveTimeEventTimestampNs = 1111L;

// Number of clients running simultaneously

// Just a single client
const int kSingleClient = 1;
// One more client than allowed binder threads
const int kNumClients = 11;

// Utility to make an expected pulled atom shell data
ShellData getExpectedPulledData() {
    ShellData shellData;
    auto* atom1 = shellData.add_atom()->mutable_cpu_active_time();
    atom1->set_uid(kUid1);
    atom1->set_time_millis(kCpuTime1);
    shellData.add_timestamp_nanos(kCpuActiveTimeEventTimestampNs);

    auto* atom2 = shellData.add_atom()->mutable_cpu_active_time();
    atom2->set_uid(kUid2);
    atom2->set_time_millis(kCpuTime2);
    shellData.add_timestamp_nanos(kCpuActiveTimeEventTimestampNs);

    return shellData;
}

// Utility to make a pulled atom Shell Config
ShellSubscription getPulledConfig() {
    ShellSubscription config;
    auto* pull_config = config.add_pulled();
    pull_config->mutable_matcher()->set_atom_id(10016);
    pull_config->set_freq_millis(2000);
    return config;
}

// Utility to adjust CPU time for pulled events
shared_ptr<LogEvent> makeCpuActiveTimeAtom(int32_t uid, int64_t timeMillis) {
    AStatsEvent* statsEvent = AStatsEvent_obtain();
    AStatsEvent_setAtomId(statsEvent, 10016);
    AStatsEvent_overwriteTimestamp(statsEvent, kCpuActiveTimeEventTimestampNs);
    AStatsEvent_writeInt32(statsEvent, uid);
    AStatsEvent_writeInt64(statsEvent, timeMillis);

    std::shared_ptr<LogEvent> logEvent = std::make_shared<LogEvent>(/*uid=*/0, /*pid=*/0);
    parseStatsEventToLogEvent(statsEvent, logEvent.get());
    return logEvent;
}

// Utility to create pushed atom LogEvents
vector<std::shared_ptr<LogEvent>> getPushedEvents() {
    vector<std::shared_ptr<LogEvent>> pushedList;
    // Create the LogEvent from an AStatsEvent
    std::unique_ptr<LogEvent> logEvent1 = CreateScreenStateChangedEvent(
            1000 /*timestamp*/, ::android::view::DisplayStateEnum::DISPLAY_STATE_ON);
    std::unique_ptr<LogEvent> logEvent2 = CreateScreenStateChangedEvent(
            2000 /*timestamp*/, ::android::view::DisplayStateEnum::DISPLAY_STATE_OFF);
    std::unique_ptr<LogEvent> logEvent3 = CreateBatteryStateChangedEvent(
            3000 /*timestamp*/, BatteryPluggedStateEnum::BATTERY_PLUGGED_USB);
    std::unique_ptr<LogEvent> logEvent4 = CreateBatteryStateChangedEvent(
            4000 /*timestamp*/, BatteryPluggedStateEnum::BATTERY_PLUGGED_NONE);
    pushedList.push_back(std::move(logEvent1));
    pushedList.push_back(std::move(logEvent2));
    pushedList.push_back(std::move(logEvent3));
    pushedList.push_back(std::move(logEvent4));
    return pushedList;
}

// Utility to read & return ShellData proto, skipping heartbeats.
static ShellData readData(int fd) {
    ssize_t dataSize = 0;
    while (dataSize == 0) {
        read(fd, &dataSize, sizeof(dataSize));
    }
    // Read that much data in proto binary format.
    vector<uint8_t> dataBuffer(dataSize);
    EXPECT_EQ((int)dataSize, read(fd, dataBuffer.data(), dataSize));

    // Make sure the received bytes can be parsed to an atom.
    ShellData receivedAtom;
    EXPECT_TRUE(receivedAtom.ParseFromArray(dataBuffer.data(), dataSize) != 0);
    return receivedAtom;
}

void runShellTest(ShellSubscription config, sp<MockUidMap> uidMap,
                  sp<MockStatsPullerManager> pullerManager,
                  const vector<std::shared_ptr<LogEvent>>& pushedEvents,
                  const vector<ShellData>& expectedData, int numClients) {
    sp<ShellSubscriber> shellManager = new ShellSubscriber(uidMap, pullerManager);

    size_t bufferSize = config.ByteSize();
    vector<uint8_t> buffer(bufferSize);
    config.SerializeToArray(&buffer[0], bufferSize);

    int fds_configs[numClients][2];
    int fds_datas[numClients][2];
    for (int i = 0; i < numClients; i++) {
        // set up 2 pipes for read/write config and data
        ASSERT_EQ(0, pipe2(fds_configs[i], O_CLOEXEC));
        ASSERT_EQ(0, pipe2(fds_datas[i], O_CLOEXEC));

        // write the config to pipe, first write size of the config
        write(fds_configs[i][1], &bufferSize, sizeof(bufferSize));
        // then write config itself
        write(fds_configs[i][1], buffer.data(), bufferSize);
        close(fds_configs[i][1]);

        shellManager->startNewSubscription(fds_configs[i][0], fds_datas[i][1], /*timeoutSec=*/-1);
        close(fds_configs[i][0]);
        close(fds_datas[i][1]);
    }

    // send a log event that matches the config.
    for (const auto& event : pushedEvents) {
        shellManager->onLogEvent(*event);
    }

    for (int i = 0; i < numClients; i++) {
        vector<ShellData> actualData;
        for (int j = 1; j <= expectedData.size(); j++) {
            actualData.push_back(readData(fds_datas[i][0]));
        }

        EXPECT_THAT(expectedData, UnorderedPointwise(ProtoEq(), actualData));
    }

    // Not closing fds_datas[i][0] because this causes writes within ShellSubscriberClient to hang
}

}  // namespace

TEST(ShellSubscriberTest, testPushedSubscription) {
    sp<MockUidMap> uidMap = new NaggyMock<MockUidMap>();
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();

    vector<std::shared_ptr<LogEvent>> pushedList = getPushedEvents();

    // create a simple config to get screen events
    ShellSubscription config;
    config.add_pushed()->set_atom_id(29);

    // this is the expected screen event atom.
    vector<ShellData> expectedData;
    ShellData shellData1;
    shellData1.add_atom()->mutable_screen_state_changed()->set_state(
            ::android::view::DisplayStateEnum::DISPLAY_STATE_ON);
    shellData1.add_timestamp_nanos(pushedList[0]->GetElapsedTimestampNs());
    ShellData shellData2;
    shellData2.add_atom()->mutable_screen_state_changed()->set_state(
            ::android::view::DisplayStateEnum::DISPLAY_STATE_OFF);
    shellData2.add_timestamp_nanos(pushedList[1]->GetElapsedTimestampNs());
    expectedData.push_back(shellData1);
    expectedData.push_back(shellData2);

    // Test with single client
    TRACE_CALL(runShellTest, config, uidMap, pullerManager, pushedList, expectedData,
               kSingleClient);

    // Test with multiple client
    TRACE_CALL(runShellTest, config, uidMap, pullerManager, pushedList, expectedData, kNumClients);
}

TEST(ShellSubscriberTest, testPulledSubscription) {
    sp<MockUidMap> uidMap = new NaggyMock<MockUidMap>();
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();

    const vector<int32_t> uids = {AID_SYSTEM};
    EXPECT_CALL(*pullerManager, Pull(10016, uids, _, _))
            .WillRepeatedly(Invoke([](int tagId, const vector<int32_t>&, const int64_t,
                                      vector<std::shared_ptr<LogEvent>>* data) {
                data->clear();
                data->push_back(makeCpuActiveTimeAtom(/*uid=*/kUid1, /*timeMillis=*/kCpuTime1));
                data->push_back(makeCpuActiveTimeAtom(/*uid=*/kUid2, /*timeMillis=*/kCpuTime2));
                return true;
            }));

    // Test with single client
    TRACE_CALL(runShellTest, getPulledConfig(), uidMap, pullerManager, /*pushedEvents=*/{},
               {getExpectedPulledData()}, kSingleClient);

    // Test with multiple clients.
    TRACE_CALL(runShellTest, getPulledConfig(), uidMap, pullerManager, {},
               {getExpectedPulledData()}, kNumClients);
}

TEST(ShellSubscriberTest, testBothSubscriptions) {
    sp<MockUidMap> uidMap = new NaggyMock<MockUidMap>();
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();

    const vector<int32_t> uids = {AID_SYSTEM};
    EXPECT_CALL(*pullerManager, Pull(10016, uids, _, _))
            .WillRepeatedly(Invoke([](int tagId, const vector<int32_t>&, const int64_t,
                                      vector<std::shared_ptr<LogEvent>>* data) {
                data->clear();
                data->push_back(makeCpuActiveTimeAtom(/*uid=*/kUid1, /*timeMillis=*/kCpuTime1));
                data->push_back(makeCpuActiveTimeAtom(/*uid=*/kUid2, /*timeMillis=*/kCpuTime2));
                return true;
            }));

    vector<std::shared_ptr<LogEvent>> pushedList = getPushedEvents();

    ShellSubscription config = getPulledConfig();
    config.add_pushed()->set_atom_id(29);

    vector<ShellData> expectedData;
    ShellData shellData1;
    shellData1.add_atom()->mutable_screen_state_changed()->set_state(
            ::android::view::DisplayStateEnum::DISPLAY_STATE_ON);
    shellData1.add_timestamp_nanos(pushedList[0]->GetElapsedTimestampNs());
    ShellData shellData2;
    shellData2.add_atom()->mutable_screen_state_changed()->set_state(
            ::android::view::DisplayStateEnum::DISPLAY_STATE_OFF);
    shellData2.add_timestamp_nanos(pushedList[1]->GetElapsedTimestampNs());
    expectedData.push_back(getExpectedPulledData());
    expectedData.push_back(shellData1);
    expectedData.push_back(shellData2);

    // Test with single client
    TRACE_CALL(runShellTest, config, uidMap, pullerManager, pushedList, expectedData,
               kSingleClient);

    // Test with multiple client
    TRACE_CALL(runShellTest, config, uidMap, pullerManager, pushedList, expectedData, kNumClients);
}

TEST(ShellSubscriberTest, testMaxSizeGuard) {
    sp<MockUidMap> uidMap = new NaggyMock<MockUidMap>();
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();
    sp<ShellSubscriber> shellManager = new ShellSubscriber(uidMap, pullerManager);

    // set up 2 pipes for read/write config and data
    int fds_config[2];
    ASSERT_EQ(0, pipe2(fds_config, O_CLOEXEC));

    int fds_data[2];
    ASSERT_EQ(0, pipe2(fds_data, O_CLOEXEC));

    // write invalid size of the config
    size_t invalidBufferSize = (shellManager->getMaxSizeKb() * 1024) + 1;
    write(fds_config[1], &invalidBufferSize, sizeof(invalidBufferSize));
    close(fds_config[1]);
    close(fds_data[0]);

    EXPECT_FALSE(shellManager->startNewSubscription(fds_config[0], fds_data[1], /*timeoutSec=*/-1));
    close(fds_config[0]);
    close(fds_data[1]);
}

TEST(ShellSubscriberTest, testMaxSubscriptionsGuard) {
    sp<MockUidMap> uidMap = new NaggyMock<MockUidMap>();
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();
    sp<ShellSubscriber> shellManager = new ShellSubscriber(uidMap, pullerManager);

    // create a simple config to get screen events
    ShellSubscription config;
    config.add_pushed()->set_atom_id(29);

    size_t bufferSize = config.ByteSize();
    vector<uint8_t> buffer(bufferSize);
    config.SerializeToArray(&buffer[0], bufferSize);

    size_t maxSubs = shellManager->getMaxSubscriptions();
    int fds_configs[maxSubs + 1][2];
    int fds_datas[maxSubs + 1][2];
    for (int i = 0; i < maxSubs; i++) {
        // set up 2 pipes for read/write config and data
        ASSERT_EQ(0, pipe2(fds_configs[i], O_CLOEXEC));
        ASSERT_EQ(0, pipe2(fds_datas[i], O_CLOEXEC));

        // write the config to pipe, first write size of the config
        write(fds_configs[i][1], &bufferSize, sizeof(bufferSize));
        // then write config itself
        write(fds_configs[i][1], buffer.data(), bufferSize);
        close(fds_configs[i][1]);

        EXPECT_TRUE(shellManager->startNewSubscription(fds_configs[i][0], fds_datas[i][1],
                                                       /*timeoutSec=*/-1));
        close(fds_configs[i][0]);
        close(fds_datas[i][1]);
    }
    ASSERT_EQ(0, pipe2(fds_configs[maxSubs], O_CLOEXEC));
    ASSERT_EQ(0, pipe2(fds_datas[maxSubs], O_CLOEXEC));

    // write the config to pipe, first write size of the config
    write(fds_configs[maxSubs][1], &bufferSize, sizeof(bufferSize));
    // then write config itself
    write(fds_configs[maxSubs][1], buffer.data(), bufferSize);
    close(fds_configs[maxSubs][1]);

    EXPECT_FALSE(shellManager->startNewSubscription(fds_configs[maxSubs][0], fds_datas[maxSubs][1],
                                                    /*timeoutSec=*/-1));
    close(fds_configs[maxSubs][0]);
    close(fds_datas[maxSubs][1]);

    // Not closing fds_datas[i][0] because this causes writes within ShellSubscriberClient to hang
}

TEST(ShellSubscriberTest, testDifferentConfigs) {
    sp<MockUidMap> uidMap = new NaggyMock<MockUidMap>();
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();
    sp<ShellSubscriber> shellManager = new ShellSubscriber(uidMap, pullerManager);

    // number of different configs
    int numConfigs = 2;

    // create a simple config to get screen events
    ShellSubscription configs[numConfigs];
    configs[0].add_pushed()->set_atom_id(29);
    configs[1].add_pushed()->set_atom_id(32);

    vector<vector<uint8_t>> configBuffers;
    for (int i = 0; i < numConfigs; i++) {
        size_t bufferSize = configs[i].ByteSize();
        vector<uint8_t> buffer(bufferSize);
        configs[i].SerializeToArray(&buffer[0], bufferSize);
        configBuffers.push_back(buffer);
    }

    int fds_configs[numConfigs][2];
    int fds_datas[numConfigs][2];
    for (int i = 0; i < numConfigs; i++) {
        // set up 2 pipes for read/write config and data
        ASSERT_EQ(0, pipe2(fds_configs[i], O_CLOEXEC));
        ASSERT_EQ(0, pipe2(fds_datas[i], O_CLOEXEC));

        size_t configSize = configBuffers[i].size();
        // write the config to pipe, first write size of the config
        write(fds_configs[i][1], &configSize, sizeof(configSize));
        // then write config itself
        write(fds_configs[i][1], configBuffers[i].data(), configSize);
        close(fds_configs[i][1]);

        EXPECT_TRUE(shellManager->startNewSubscription(fds_configs[i][0], fds_datas[i][1],
                                                       /*timeoutSec=*/-1));
        close(fds_configs[i][0]);
        close(fds_datas[i][1]);
    }

    // send a log event that matches the config.
    vector<std::shared_ptr<LogEvent>> pushedList = getPushedEvents();
    for (const auto& event : pushedList) {
        shellManager->onLogEvent(*event);
    }

    // Validate Config 1
    ShellData actual1 = readData(fds_datas[0][0]);
    ShellData expected1;
    expected1.add_atom()->mutable_screen_state_changed()->set_state(
            ::android::view::DisplayStateEnum::DISPLAY_STATE_ON);
    expected1.add_timestamp_nanos(pushedList[0]->GetElapsedTimestampNs());
    EXPECT_THAT(expected1, ProtoEq(actual1));

    ShellData actual2 = readData(fds_datas[0][0]);
    ShellData expected2;
    expected2.add_atom()->mutable_screen_state_changed()->set_state(
            ::android::view::DisplayStateEnum::DISPLAY_STATE_OFF);
    expected2.add_timestamp_nanos(pushedList[1]->GetElapsedTimestampNs());
    EXPECT_THAT(expected2, ProtoEq(actual2));

    // Validate Config 2, repeating the process
    ShellData actual3 = readData(fds_datas[1][0]);
    ShellData expected3;
    expected3.add_atom()->mutable_plugged_state_changed()->set_state(
            BatteryPluggedStateEnum::BATTERY_PLUGGED_USB);
    expected3.add_timestamp_nanos(pushedList[2]->GetElapsedTimestampNs());
    EXPECT_THAT(expected3, ProtoEq(actual3));

    ShellData actual4 = readData(fds_datas[1][0]);
    ShellData expected4;
    expected4.add_atom()->mutable_plugged_state_changed()->set_state(
            BatteryPluggedStateEnum::BATTERY_PLUGGED_NONE);
    expected4.add_timestamp_nanos(pushedList[3]->GetElapsedTimestampNs());
    EXPECT_THAT(expected4, ProtoEq(actual4));

    // Not closing fds_datas[i][0] because this causes writes within ShellSubscriberClient to hang
}

TEST(ShellSubscriberTest, testPushedSubscriptionRestrictedEvent) {
    sp<MockUidMap> uidMap = new NaggyMock<MockUidMap>();
    sp<MockStatsPullerManager> pullerManager = new StrictMock<MockStatsPullerManager>();

    std::vector<shared_ptr<LogEvent>> pushedList;
    pushedList.push_back(CreateRestrictedLogEvent(/*atomTag=*/10, /*timestamp=*/1000));

    // create a simple config to get screen events
    ShellSubscription config;
    config.add_pushed()->set_atom_id(10);

    // expect empty data
    vector<ShellData> expectedData;

    // Test with single client
    runShellTest(config, uidMap, pullerManager, pushedList, expectedData, kSingleClient);

    // Test with multiple client
    runShellTest(config, uidMap, pullerManager, pushedList, expectedData, kNumClients);
}

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif

}  // namespace statsd
}  // namespace os
}  // namespace android
