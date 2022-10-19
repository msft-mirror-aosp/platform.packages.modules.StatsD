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
#define STATSD_DEBUG false  // STOPSHIP if true
#include "Log.h"

#include "ShellSubscriberClient.h"

#include "matchers/matcher_util.h"
#include "stats_log_util.h"

using android::base::unique_fd;
using android::util::ProtoOutputStream;

namespace android {
namespace os {
namespace statsd {

const static int FIELD_ID_ATOM = 1;

// Called by ShellSubscriber when a pushed event occurs
void ShellSubscriberClient::onLogEvent(const LogEvent& event) {
    mProto.clear();
    for (const auto& matcher : mPushedMatchers) {
        if (matchesSimple(mUidMap, matcher, event)) {
            uint64_t atomToken = mProto.start(util::FIELD_TYPE_MESSAGE |
                                              util::FIELD_COUNT_REPEATED | FIELD_ID_ATOM);
            event.ToProto(mProto);
            mProto.end(atomToken);
            attemptWriteToPipeLocked(mProto.size());
        }
    }
}

// Read and parse single config. There should only one config per input.
bool ShellSubscriberClient::readConfig() {
    // Read the size of the config.
    size_t bufferSize;
    if (!android::base::ReadFully(mDupIn.get(), &bufferSize, sizeof(bufferSize))) {
        return false;
    }

    // Check bufferSize
    if (bufferSize > (kMaxSizeKb * 1024)) {
        ALOGE("ShellSubscriberClient: received config (%zu bytes) is larger than the max size (%zu "
              "bytes)",
              bufferSize, (kMaxSizeKb * 1024));
        return false;
    }

    // Read the config.
    vector<uint8_t> buffer(bufferSize);
    if (!android::base::ReadFully(mDupIn.get(), buffer.data(), bufferSize)) {
        return false;
    }

    // Parse the config.
    ShellSubscription config;
    if (!config.ParseFromArray(buffer.data(), bufferSize)) {
        return false;
    }

    // Update SubscriptionInfo with state from config
    for (const auto& pushed : config.pushed()) {
        mPushedMatchers.push_back(pushed);
    }

    for (const auto& pulled : config.pulled()) {
        vector<string> packages;
        vector<int32_t> uids;
        for (const string& pkg : pulled.packages()) {
            auto it = UidMap::sAidToUidMapping.find(pkg);
            if (it != UidMap::sAidToUidMapping.end()) {
                uids.push_back(it->second);
            } else {
                packages.push_back(pkg);
            }
        }

        mPulledInfo.emplace_back(pulled.matcher(), pulled.freq_millis(), packages, uids);
        ALOGD("ShellSubscriberClient: adding matcher for pulled atom %d",
              pulled.matcher().atom_id());
    }

    return true;
}

// The pullAndHeartbeat threads sleep for the minimum time
// among all clients' input
int64_t ShellSubscriberClient::pullAndSendHeartbeatsIfNeeded(int64_t nowSecs, int64_t nowMillis,
                                                             int64_t nowNanos) {
    int64_t sleepTimeMs = kMsBetweenHeartbeats;

    if ((nowSecs - mStartTimeSec >= mTimeoutSec) && (mTimeoutSec > 0)) {
        mClientAlive = false;
        return sleepTimeMs;
    }

    for (PullInfo& pullInfo : mPulledInfo) {
        if (pullInfo.mPrevPullElapsedRealtimeMs + pullInfo.mInterval >= nowMillis) {
            continue;
        }

        vector<int32_t> uids;
        getUidsForPullAtom(&uids, pullInfo);

        vector<std::shared_ptr<LogEvent>> data;
        mPullerMgr->Pull(pullInfo.mPullerMatcher.atom_id(), uids, nowNanos, &data);
        VLOG("ShellSubscriberClient: pulled %zu atoms with id %d", data.size(),
             pullInfo.mPullerMatcher.atom_id());
        writePulledAtomsLocked(data, pullInfo.mPullerMatcher);

        pullInfo.mPrevPullElapsedRealtimeMs = nowMillis;
    }

    // Send a heartbeat, consisting of a data size of 0, if the user hasn't recently received
    // data from statsd. When it receives the data size of 0, the user will not expect any
    // atoms and recheck whether the subscription should end.
    if (nowMillis - mLastWriteMs >= kMsBetweenHeartbeats) {
        attemptWriteToPipeLocked(/*dataSize=*/0);
        if (!mClientAlive) return kMsBetweenHeartbeats;
    }

    // Determine how long to sleep before doing more work.
    for (PullInfo& pullInfo : mPulledInfo) {
        int64_t nextPullTime = pullInfo.mPrevPullElapsedRealtimeMs + pullInfo.mInterval;
        int64_t timeBeforePull = nextPullTime - nowMillis;  // guaranteed to be non-negative
        sleepTimeMs = std::min(sleepTimeMs, timeBeforePull);
    }
    int64_t timeBeforeHeartbeat = (mLastWriteMs + kMsBetweenHeartbeats) - nowMillis;
    sleepTimeMs = std::min(sleepTimeMs, timeBeforeHeartbeat);
    return sleepTimeMs;
}

void ShellSubscriberClient::writePulledAtomsLocked(const vector<std::shared_ptr<LogEvent>>& data,
                                                   const SimpleAtomMatcher& matcher) {
    mProto.clear();
    int count = 0;
    for (const auto& event : data) {
        if (matchesSimple(mUidMap, matcher, *event)) {
            count++;
            uint64_t atomToken = mProto.start(util::FIELD_TYPE_MESSAGE |
                                              util::FIELD_COUNT_REPEATED | FIELD_ID_ATOM);
            event->ToProto(mProto);
            mProto.end(atomToken);
        }
    }

    if (count > 0) attemptWriteToPipeLocked(mProto.size());
}

// Tries to write the atom encoded in mProto to the pipe. If the write fails
// because the read end of the pipe has closed, change the client status so
// the manager knows the subscription is no longer active
void ShellSubscriberClient::attemptWriteToPipeLocked(size_t dataSize) {
    // First, write the payload size.
    if (!android::base::WriteFully(mDupOut, &dataSize, sizeof(dataSize))) {
        mClientAlive = false;
        return;
    }
    // Then, write the payload if this is not just a heartbeat.
    if (dataSize > 0 && !mProto.flush(mDupOut.get())) {
        mClientAlive = false;
        return;
    }
    mLastWriteMs = getElapsedRealtimeMillis();
}

void ShellSubscriberClient::getUidsForPullAtom(vector<int32_t>* uids, const PullInfo& pullInfo) {
    uids->insert(uids->end(), pullInfo.mPullUids.begin(), pullInfo.mPullUids.end());
    // This is slow. Consider storing the uids per app and listening to uidmap updates.
    for (const string& pkg : pullInfo.mPullPackages) {
        set<int32_t> uidsForPkg = mUidMap->getAppUid(pkg);
        uids->insert(uids->end(), uidsForPkg.begin(), uidsForPkg.end());
    }
    uids->push_back(DEFAULT_PULL_UID);
}

}  // namespace statsd
}  // namespace os
}  // namespace android
