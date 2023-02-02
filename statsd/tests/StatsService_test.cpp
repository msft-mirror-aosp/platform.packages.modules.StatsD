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

#include "StatsService.h"

#include <android/binder_interface_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdio.h>

#include "config/ConfigKey.h"
#include "packages/UidMap.h"
#include "src/statsd_config.pb.h"
#include "tests/statsd_test_util.h"

using namespace android;
using namespace testing;

namespace android {
namespace os {
namespace statsd {

using android::util::ProtoOutputStream;
using ::ndk::SharedRefBase;

#ifdef __ANDROID__

TEST(StatsServiceTest, TestAddConfig_simple) {
    const sp<UidMap> uidMap = new UidMap();
    shared_ptr<StatsService> service =
            SharedRefBase::make<StatsService>(uidMap, /* queue */ nullptr);
    const int kConfigKey = 12345;
    const int kCallingUid = 123;
    StatsdConfig config;
    config.set_id(kConfigKey);
    string serialized = config.SerializeAsString();

    EXPECT_TRUE(service->addConfigurationChecked(kCallingUid, kConfigKey,
                                                 {serialized.begin(), serialized.end()}));
    service->removeConfiguration(kConfigKey, kCallingUid);
    ConfigKey configKey(kCallingUid, kConfigKey);
    service->mProcessor->onDumpReport(configKey, getElapsedRealtimeNs(),
                                      false /* include_current_bucket*/, true /* erase_data */,
                                      ADB_DUMP, NO_TIME_CONSTRAINTS, nullptr);
}

TEST(StatsServiceTest, TestAddConfig_empty) {
    const sp<UidMap> uidMap = new UidMap();
    shared_ptr<StatsService> service =
            SharedRefBase::make<StatsService>(uidMap, /* queue */ nullptr);
    string serialized = "";
    const int kConfigKey = 12345;
    const int kCallingUid = 123;
    EXPECT_TRUE(service->addConfigurationChecked(kCallingUid, kConfigKey,
                                                 {serialized.begin(), serialized.end()}));
    service->removeConfiguration(kConfigKey, kCallingUid);
    ConfigKey configKey(kCallingUid, kConfigKey);
    service->mProcessor->onDumpReport(configKey, getElapsedRealtimeNs(),
                                      false /* include_current_bucket*/, true /* erase_data */,
                                      ADB_DUMP, NO_TIME_CONSTRAINTS, nullptr);
}

TEST(StatsServiceTest, TestAddConfig_invalid) {
    const sp<UidMap> uidMap = new UidMap();
    shared_ptr<StatsService> service =
            SharedRefBase::make<StatsService>(uidMap, /* queue */ nullptr);
    string serialized = "Invalid config!";

    EXPECT_FALSE(
            service->addConfigurationChecked(123, 12345, {serialized.begin(), serialized.end()}));
}

TEST(StatsServiceTest, TestGetUidFromArgs) {
    Vector<String8> args;
    args.push(String8("-1"));
    args.push(String8("0"));
    args.push(String8("1"));
    args.push(String8("a1"));
    args.push(String8(""));

    int32_t uid;

    const sp<UidMap> uidMap = new UidMap();
    shared_ptr<StatsService> service =
            SharedRefBase::make<StatsService>(uidMap, /* queue */ nullptr);
    service->mEngBuild = true;

    // "-1"
    EXPECT_FALSE(service->getUidFromArgs(args, 0, uid));

    // "0"
    EXPECT_TRUE(service->getUidFromArgs(args, 1, uid));
    EXPECT_EQ(0, uid);

    // "1"
    EXPECT_TRUE(service->getUidFromArgs(args, 2, uid));
    EXPECT_EQ(1, uid);

    // "a1"
    EXPECT_FALSE(service->getUidFromArgs(args, 3, uid));

    // ""
    EXPECT_FALSE(service->getUidFromArgs(args, 4, uid));

    // For a non-userdebug, uid "1" cannot be impersonated.
    service->mEngBuild = false;
    EXPECT_FALSE(service->getUidFromArgs(args, 2, uid));
}

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif

}  // namespace statsd
}  // namespace os
}  // namespace android
