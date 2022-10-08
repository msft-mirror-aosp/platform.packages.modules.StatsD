/*
 * Copyright (C) 2022 The Android Open Source Project
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

#pragma once

#include <android-base/file.h>
#include <android/util/ProtoOutputStream.h>
#include <private/android_filesystem_config.h>

#include "external/StatsPullerManager.h"
#include "logd/LogEvent.h"
#include "packages/UidMap.h"
#include "src/shell/shell_config.pb.h"
#include "src/statsd_config.pb.h"

namespace android {
namespace os {
namespace statsd {

// ShellSubscriberClient is not thread-safe. All calls must be
// guarded by the mutex in ShellSubscriber.h
class ShellSubscriberClient {
public:
    ShellSubscriberClient(int in, int out, int64_t timeoutSec, int64_t startTimeSec,
                          sp<UidMap> uidMap, sp<StatsPullerManager> pullerMgr)
        : mUidMap(uidMap),
          mPullerMgr(pullerMgr),
          mDupIn(dup(in)),
          mDupOut(dup(out)),
          mTimeoutSec(timeoutSec),
          mStartTimeSec(startTimeSec){};

    void onLogEvent(const LogEvent& event);

    bool readConfig();

    int64_t pullAndSendHeartbeatsIfNeeded(int64_t nowSecs, int64_t nowMillis, int64_t nowNanos);

    bool isAlive() const {
        return mClientAlive;
    }

    static size_t getMaxSizeKb() {
        return kMaxSizeKb;
    }

private:
    struct PullInfo {
        PullInfo(const SimpleAtomMatcher& matcher, int64_t interval,
                 const std::vector<std::string>& packages, const std::vector<int32_t>& uids)
            : mPullerMatcher(matcher),
              mInterval(interval),
              mPrevPullElapsedRealtimeMs(0),
              mPullPackages(packages),
              mPullUids(uids) {
        }
        SimpleAtomMatcher mPullerMatcher;
        int64_t mInterval;
        int64_t mPrevPullElapsedRealtimeMs;
        std::vector<std::string> mPullPackages;
        std::vector<int32_t> mPullUids;
    };

    void writePulledAtomsLocked(const vector<std::shared_ptr<LogEvent>>& data,
                                const SimpleAtomMatcher& matcher);

    void attemptWriteToPipeLocked(size_t dataSize);

    void getUidsForPullAtom(vector<int32_t>* uids, const PullInfo& pullInfo);

    const int32_t DEFAULT_PULL_UID = AID_SYSTEM;

    sp<UidMap> mUidMap;

    sp<StatsPullerManager> mPullerMgr;

    android::base::unique_fd mDupIn;

    android::base::unique_fd mDupOut;

    android::util::ProtoOutputStream mProto;

    std::vector<SimpleAtomMatcher> mPushedMatchers;

    std::vector<PullInfo> mPulledInfo;

    int64_t mTimeoutSec;

    int64_t mStartTimeSec;

    bool mClientAlive = true;

    int64_t mLastWriteMs = 0;

    static constexpr int64_t kMsBetweenHeartbeats = 1000;

    // Cap the buffer size of configs to guard against bad allocations
    static constexpr size_t kMaxSizeKb = 50;
};

}  // namespace statsd
}  // namespace os
}  // namespace android
