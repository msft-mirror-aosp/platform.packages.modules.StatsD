// Copyright (C) 2020 The Android Open Source Project
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

#include "src/external/StatsPullerManager.h"

#include <aidl/android/os/IPullAtomResultReceiver.h>
#include <aidl/android/util/StatsEventParcel.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>

#include "stats_event.h"
#include "tests/statsd_test_util.h"

using aidl::android::util::StatsEventParcel;
using ::ndk::SharedRefBase;
using std::make_shared;
using std::shared_ptr;
using std::vector;

namespace android {
namespace os {
namespace statsd {

namespace {

int pullTagId1 = 10101;
int pullTagId2 = 10102;
int uid1 = 9999;
int uid2 = 8888;
ConfigKey configKey(50, 12345);
ConfigKey badConfigKey(60, 54321);
int unregisteredUid = 98765;
int64_t coolDownNs = NS_PER_SEC;
int64_t timeoutNs = NS_PER_SEC / 2;

AStatsEvent* createSimpleEvent(int32_t atomId, int32_t value) {
    AStatsEvent* event = AStatsEvent_obtain();
    AStatsEvent_setAtomId(event, atomId);
    AStatsEvent_writeInt32(event, value);
    AStatsEvent_build(event);
    return event;
}

class FakePullAtomCallback : public BnPullAtomCallback {
public:
    FakePullAtomCallback(int32_t uid, int32_t pullDurationNs = 0)
        : mUid(uid), mDurationNs(pullDurationNs) {};
    Status onPullAtom(int atomTag,
                      const shared_ptr<IPullAtomResultReceiver>& resultReceiver) override {
        onPullAtomCalled(atomTag);

        vector<StatsEventParcel> parcels;
        AStatsEvent* event = createSimpleEvent(atomTag, mUid);
        size_t size;
        uint8_t* buffer = AStatsEvent_getBuffer(event, &size);

        StatsEventParcel p;
        // vector.assign() creates a copy, but this is inevitable unless
        // stats_event.h/c uses a vector as opposed to a buffer.
        p.buffer.assign(buffer, buffer + size);
        parcels.push_back(std::move(p));
        AStatsEvent_release(event);

        if (mDurationNs > 0) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(mDurationNs));
        }

        resultReceiver->pullFinished(atomTag, /*success*/ true, parcels);
        return Status::ok();
    }
    int32_t mUid;
    int32_t mDurationNs;

    virtual void onPullAtomCalled(int atomTag) const {};
};

class FakePullUidProvider : public PullUidProvider {
public:
    vector<int32_t> getPullAtomUids(int atomId) override {
        if (atomId == pullTagId1) {
            return {uid2, uid1};
        } else if (atomId == pullTagId2) {
            return {uid2};
        }
        return {};
    }
};

class MockPullAtomCallback : public FakePullAtomCallback {
public:
    MockPullAtomCallback(int32_t uid, int32_t pullDurationNs = 0)
        : FakePullAtomCallback(uid, pullDurationNs) {
    }

    MOCK_METHOD(void, onPullAtomCalled, (int), (const override));
};

class MockPullDataReceiver : public PullDataReceiver {
public:
    virtual ~MockPullDataReceiver() = default;

    MOCK_METHOD(void, onDataPulled,
                (const std::vector<std::shared_ptr<LogEvent>>&, PullResult, int64_t), (override));

    bool isPullNeeded() const override {
        return true;
    };
};

sp<StatsPullerManager> createPullerManagerAndRegister(int32_t pullDurationMs = 0) {
    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    shared_ptr<FakePullAtomCallback> cb1 =
            SharedRefBase::make<FakePullAtomCallback>(uid1, pullDurationMs);
    pullerManager->RegisterPullAtomCallback(uid1, pullTagId1, coolDownNs, timeoutNs, {}, cb1);
    shared_ptr<FakePullAtomCallback> cb2 =
            SharedRefBase::make<FakePullAtomCallback>(uid2, pullDurationMs);
    pullerManager->RegisterPullAtomCallback(uid2, pullTagId1, coolDownNs, timeoutNs, {}, cb2);
    pullerManager->RegisterPullAtomCallback(uid1, pullTagId2, coolDownNs, timeoutNs, {}, cb1);
    return pullerManager;
}
}  // anonymous namespace

TEST(StatsPullerManagerTest, TestPullInvalidUid) {
    sp<StatsPullerManager> pullerManager = createPullerManagerAndRegister();

    vector<shared_ptr<LogEvent>> data;
    EXPECT_FALSE(pullerManager->Pull(pullTagId1, {unregisteredUid}, /*timestamp =*/1, &data));
}

TEST(StatsPullerManagerTest, TestPullChoosesCorrectUid) {
    sp<StatsPullerManager> pullerManager = createPullerManagerAndRegister();

    vector<shared_ptr<LogEvent>> data;
    EXPECT_TRUE(pullerManager->Pull(pullTagId1, {uid1}, /*timestamp =*/1, &data));
    ASSERT_EQ(data.size(), 1);
    EXPECT_EQ(data[0]->GetTagId(), pullTagId1);
    ASSERT_EQ(data[0]->getValues().size(), 1);
    EXPECT_EQ(data[0]->getValues()[0].mValue.int_value, uid1);
}

TEST(StatsPullerManagerTest, TestPullInvalidConfigKey) {
    sp<StatsPullerManager> pullerManager = createPullerManagerAndRegister();
    sp<FakePullUidProvider> uidProvider = new FakePullUidProvider();
    pullerManager->RegisterPullUidProvider(configKey, uidProvider);

    vector<shared_ptr<LogEvent>> data;
    EXPECT_FALSE(pullerManager->Pull(pullTagId1, badConfigKey, /*timestamp =*/1, &data));
}

TEST(StatsPullerManagerTest, TestPullConfigKeyGood) {
    sp<StatsPullerManager> pullerManager = createPullerManagerAndRegister();
    sp<FakePullUidProvider> uidProvider = new FakePullUidProvider();
    pullerManager->RegisterPullUidProvider(configKey, uidProvider);

    vector<shared_ptr<LogEvent>> data;
    EXPECT_TRUE(pullerManager->Pull(pullTagId1, configKey, /*timestamp =*/1, &data));
    EXPECT_EQ(data[0]->GetTagId(), pullTagId1);
    ASSERT_EQ(data[0]->getValues().size(), 1);
    EXPECT_EQ(data[0]->getValues()[0].mValue.int_value, uid2);
}

TEST(StatsPullerManagerTest, TestPullConfigKeyNoPullerWithUid) {
    sp<StatsPullerManager> pullerManager = createPullerManagerAndRegister();
    sp<FakePullUidProvider> uidProvider = new FakePullUidProvider();
    pullerManager->RegisterPullUidProvider(configKey, uidProvider);

    vector<shared_ptr<LogEvent>> data;
    EXPECT_FALSE(pullerManager->Pull(pullTagId2, configKey, /*timestamp =*/1, &data));
}

TEST(StatsPullerManagerTest, TestSameAtomIsPulledInABatch) {
    // define 2 puller callbacks with small duration each to guaranty that
    // call sequence callback A + callback B will invalidate pull cache
    // for callback A if PullerManager does not group receivers by tagId

    const int64_t pullDurationNs = (int)(timeoutNs * 0.9);

    sp<StatsPullerManager> pullerManager = new StatsPullerManager();
    auto cb1 = SharedRefBase::make<StrictMock<MockPullAtomCallback>>(uid1, pullDurationNs);
    pullerManager->RegisterPullAtomCallback(uid1, pullTagId1, coolDownNs, timeoutNs, {}, cb1);
    auto cb2 = SharedRefBase::make<StrictMock<MockPullAtomCallback>>(uid2, pullDurationNs);
    pullerManager->RegisterPullAtomCallback(uid2, pullTagId2, coolDownNs, timeoutNs, {}, cb2);

    sp<FakePullUidProvider> uidProvider = new FakePullUidProvider();
    pullerManager->RegisterPullUidProvider(configKey, uidProvider);

    const int64_t bucketBoundary = NS_PER_SEC * 60 * 60 * 1;  // 1 hour

    // create 10 receivers to simulate 10 distinct metrics for pulled atoms
    // add 10 metric where 5 depends on atom A and 5 on atom B
    vector<sp<MockPullDataReceiver>> receivers;
    receivers.reserve(10);
    for (int i = 0; i < 10; i++) {
        auto receiver = new StrictMock<MockPullDataReceiver>();
        EXPECT_CALL(*receiver, onDataPulled(_, _, _)).Times(1);
        receivers.push_back(receiver);

        const int32_t atomTag = i % 2 == 0 ? pullTagId1 : pullTagId2;
        pullerManager->RegisterReceiver(atomTag, configKey, receiver, bucketBoundary,
                                        bucketBoundary);
    }

    // check that only 2 pulls will be done and remaining 8 pulls from cache
    EXPECT_CALL(*cb1, onPullAtomCalled(pullTagId1)).Times(1);
    EXPECT_CALL(*cb2, onPullAtomCalled(pullTagId2)).Times(1);

    // validate that created 2 receivers groups just for 2 atoms with 5 receivers in each
    ASSERT_EQ(pullerManager->mReceivers.size(), 2);
    ASSERT_EQ(pullerManager->mReceivers.begin()->second.size(), 5);
    ASSERT_EQ(pullerManager->mReceivers.rbegin()->second.size(), 5);

    // simulate pulls
    pullerManager->OnAlarmFired(bucketBoundary + 1);

    // to allow async pullers to complete + some extra time
    std::this_thread::sleep_for(std::chrono::nanoseconds(pullDurationNs * 3));
}

}  // namespace statsd
}  // namespace os
}  // namespace android
