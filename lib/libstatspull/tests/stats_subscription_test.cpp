/*
 * Copyright (C) 2023, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <binder/ProcessState.h>
#include <gmock/gmock.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <stats_subscription.h>
#include <stdint.h>
#include <utils/Looper.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "packages/modules/StatsD/statsd/src/shell/shell_config.pb.h"
#include "packages/modules/StatsD/statsd/src/shell/shell_data.pb.h"
#include "statslog_statsdtest.h"

#ifdef __ANDROID__

using namespace testing;
using android::Looper;
using android::ProcessState;
using android::sp;
using android::os::statsd::Atom;
using android::os::statsd::ShellData;
using android::os::statsd::ShellSubscription;
using android::os::statsd::util::SCREEN_BRIGHTNESS_CHANGED;
using android::os::statsd::util::stats_write;
using google::protobuf::util::MessageDifferencer;
using std::string;
using std::vector;
using std::this_thread::sleep_for;

namespace {

// Checks equality on explicitly set values.
MATCHER_P(ProtoEq, otherArg, "") {
    return MessageDifferencer::Equals(arg, otherArg);
}

class SubscriptionTest : public Test {
public:
    SubscriptionTest() : looper(Looper::prepare(/*opts=*/0)) {
        const TestInfo* const test_info = UnitTest::GetInstance()->current_test_info();
        ALOGD("**** Setting up for %s.%s\n", test_info->test_case_name(), test_info->name());
    }

    ~SubscriptionTest() {
        const TestInfo* const test_info = UnitTest::GetInstance()->current_test_info();
        ALOGD("**** Tearing down after %s.%s\n", test_info->test_case_name(), test_info->name());
    }

protected:
    void SetUp() override {
        // Start the Binder thread pool.
        ProcessState::self()->startThreadPool();
    }

    void TearDown() {
        // Clear any dangling subscriptions from statsd.
        if (__builtin_available(android __STATSD_SUBS_MIN_API__, *)) {
            AStatsManager_removeSubscription(subId);
        }
    }

    int32_t subId;

private:
    sp<Looper> looper;
};

// Stores arguments passed in subscription callback.
struct CallbackData {
    int32_t subId;
    AStatsManager_SubscriptionCallbackReason reason;
    uint8_t* payload;
    size_t numBytes;
    int count;  // Stores number of times the callback is invoked.
};

static void callback(int32_t subscription_id, AStatsManager_SubscriptionCallbackReason reason,
                     uint8_t* _Nonnull payload, size_t num_bytes, void* _Nullable cookie) {
    CallbackData* data = static_cast<CallbackData*>(cookie);
    data->subId = subscription_id;
    data->reason = reason;
    data->payload = payload;
    data->numBytes = num_bytes;
    data->count++;
}

TEST_F(SubscriptionTest, TestSubscription) {
    if (__builtin_available(android __STATSD_SUBS_MIN_API__, *)) {
        ShellSubscription config;
        config.add_pushed()->set_atom_id(SCREEN_BRIGHTNESS_CHANGED);

        string configBytes;
        config.SerializeToString(&configBytes);

        CallbackData callbackData{/*subId=*/0,
                                  ASTATSMANAGER_SUBSCRIPTION_CALLBACK_REASON_SUBSCRIPTION_ENDED,
                                  /*payload=*/nullptr,
                                  /*numBytes=*/0,
                                  /*count=*/0};

        subId = AStatsManager_addSubscription(reinterpret_cast<const uint8_t*>(configBytes.data()),
                                              configBytes.size(), &callback, &callbackData);
        ASSERT_GT(subId, 0);
        sleep_for(std::chrono::milliseconds(2000));

        stats_write(SCREEN_BRIGHTNESS_CHANGED, 100);
        sleep_for(std::chrono::milliseconds(2000));

        EXPECT_EQ(callbackData.subId, subId);
        EXPECT_EQ(callbackData.reason, ASTATSMANAGER_SUBSCRIPTION_CALLBACK_REASON_STATSD_INITIATED);
        EXPECT_EQ(callbackData.count, 1);
        ASSERT_GT(callbackData.numBytes, 0);

        ShellData actualShellData;
        ASSERT_TRUE(actualShellData.ParseFromArray(callbackData.payload, callbackData.numBytes));
        ASSERT_EQ(actualShellData.atom_size(), 1);

        Atom expectedAtom;
        auto* screenBrightnessChanged = expectedAtom.mutable_screen_brightness_changed();
        screenBrightnessChanged->set_level(100);
        EXPECT_THAT(actualShellData.atom(0), ProtoEq(expectedAtom));

        ASSERT_EQ(actualShellData.timestamp_nanos_size(), 1);
        EXPECT_GT(actualShellData.timestamp_nanos(0), 0LL);

        AStatsManager_flushSubscription(subId);
        sleep_for(std::chrono::milliseconds(2000));

        EXPECT_EQ(callbackData.subId, subId);
        EXPECT_EQ(callbackData.reason, ASTATSMANAGER_SUBSCRIPTION_CALLBACK_REASON_FLUSH_REQUESTED);
        EXPECT_EQ(callbackData.count, 2);
        EXPECT_EQ(callbackData.numBytes, 0);

        AStatsManager_removeSubscription(subId);
        sleep_for(std::chrono::milliseconds(2000));

        EXPECT_EQ(callbackData.subId, subId);
        EXPECT_EQ(callbackData.reason,
                  ASTATSMANAGER_SUBSCRIPTION_CALLBACK_REASON_SUBSCRIPTION_ENDED);
        EXPECT_EQ(callbackData.count, 3);
        EXPECT_EQ(callbackData.numBytes, 0);
    } else {
        GTEST_SKIP();
    }
}

}  // namespace

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
