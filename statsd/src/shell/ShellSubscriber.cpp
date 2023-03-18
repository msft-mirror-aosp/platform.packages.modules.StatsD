/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "ShellSubscriber.h"

#include <android-base/file.h>
#include <inttypes.h>

#include "stats_log_util.h"

using aidl::android::os::IStatsSubscriptionCallback;

namespace android {
namespace os {
namespace statsd {

ShellSubscriber::~ShellSubscriber() {
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mClientSet.clear();
    }
    mThreadSleepCV.notify_one();
    if (mThread.joinable()) {
        mThread.join();
    }
}

bool ShellSubscriber::startNewSubscription(int in, int out, int64_t timeoutSec) {
    std::unique_lock<std::mutex> lock(mMutex);
    ALOGD("ShellSubscriber: new subscription has come in");
    if (mClientSet.size() >= kMaxSubscriptions) {
        ALOGE("ShellSubscriber: cannot have another active subscription. Current Subscriptions: "
              "%zu. Limit: %zu",
              mClientSet.size(), kMaxSubscriptions);
        return false;
    }

    return startNewSubscriptionLocked(ShellSubscriberClient::create(
            in, out, timeoutSec, getElapsedRealtimeSec(), mUidMap, mPullerMgr));
}

bool ShellSubscriber::startNewSubscription(const vector<uint8_t>& subscriptionConfig,
                                           const shared_ptr<IStatsSubscriptionCallback>& callback) {
    std::unique_lock<std::mutex> lock(mMutex);
    ALOGD("ShellSubscriber: new subscription has come in");
    if (mClientSet.size() >= kMaxSubscriptions) {
        ALOGE("ShellSubscriber: cannot have another active subscription. Current Subscriptions: "
              "%zu. Limit: %zu",
              mClientSet.size(), kMaxSubscriptions);
        return false;
    }

    return startNewSubscriptionLocked(ShellSubscriberClient::create(
            subscriptionConfig, callback, getElapsedRealtimeSec(), mUidMap, mPullerMgr));
}

bool ShellSubscriber::startNewSubscriptionLocked(unique_ptr<ShellSubscriberClient> client) {
    if (client == nullptr) return false;

    // Add new valid client to the client set
    mClientSet.insert(std::move(client));

    // Only spawn one thread to manage pulling atoms and sending
    // heartbeats.
    if (!mThreadAlive) {
        mThreadAlive = true;
        if (mThread.joinable()) {
            mThread.join();
        }
        mThread = thread([this] { pullAndSendHeartbeats(); });
    }

    return true;
}

// Sends heartbeat signals and sleeps between doing work
void ShellSubscriber::pullAndSendHeartbeats() {
    ALOGD("ShellSubscriber: helper thread starting");
    std::unique_lock<std::mutex> lock(mMutex);
    while (true) {
        int64_t sleepTimeMs = INT_MAX;
        int64_t nowSecs = getElapsedRealtimeSec();
        int64_t nowMillis = getElapsedRealtimeMillis();
        int64_t nowNanos = getElapsedRealtimeNs();
        for (auto clientIt = mClientSet.begin(); clientIt != mClientSet.end();) {
            int64_t subscriptionSleepMs =
                    (*clientIt)->pullAndSendHeartbeatsIfNeeded(nowSecs, nowMillis, nowNanos);
            sleepTimeMs = std::min(sleepTimeMs, subscriptionSleepMs);
            if ((*clientIt)->isAlive()) {
                ++clientIt;
            } else {
                ALOGD("ShellSubscriber: removing client!");
                clientIt = mClientSet.erase(clientIt);
            }
        }
        if (mClientSet.empty()) {
            mThreadAlive = false;
            ALOGD("ShellSubscriber: helper thread done!");
            return;
        }
        VLOG("ShellSubscriber: helper thread sleeping for %" PRId64 "ms", sleepTimeMs);
        mThreadSleepCV.wait_for(lock, sleepTimeMs * 1ms, [this] { return mClientSet.empty(); });
    }
}

void ShellSubscriber::onLogEvent(const LogEvent& event) {
    std::unique_lock<std::mutex> lock(mMutex);
    for (auto clientIt = mClientSet.begin(); clientIt != mClientSet.end();) {
        (*clientIt)->onLogEvent(event);
        if ((*clientIt)->isAlive()) {
            ++clientIt;
        } else {
            ALOGD("ShellSubscriber: removing client!");
            clientIt = mClientSet.erase(clientIt);
        }
    }
}

void ShellSubscriber::flushSubscription(const shared_ptr<IStatsSubscriptionCallback>& callback) {
    std::unique_lock<std::mutex> lock(mMutex);

    // TODO(b/268822860): Consider storing callback clients in a map keyed by
    // IStatsSubscriptionCallback to avoid this linear search.
    for (auto clientIt = mClientSet.begin(); clientIt != mClientSet.end(); ++clientIt) {
        if ((*clientIt)->hasCallback(callback)) {
            (*clientIt)->flush();
            return;
        }
    }
}

void ShellSubscriber::unsubscribe(const shared_ptr<IStatsSubscriptionCallback>& callback) {
    std::unique_lock<std::mutex> lock(mMutex);

    // TODO(b/268822860): Consider storing callback clients in a map keyed by
    // IStatsSubscriptionCallback to avoid this linear search.
    for (auto clientIt = mClientSet.begin(); clientIt != mClientSet.end();) {
        if ((*clientIt)->hasCallback(callback)) {
            (*clientIt)->onUnsubscribe();

            ALOGD("ShellSubscriber: removing client!");
            clientIt = mClientSet.erase(clientIt);
            return;
        } else {
            ++clientIt;
        }
    }
}

}  // namespace statsd
}  // namespace os
}  // namespace android
