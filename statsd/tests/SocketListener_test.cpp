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
#include <gtest/gtest.h>

#include "socket/StatsSocketListener.h"
#include "tests/statsd_test_util.h"

#ifdef __ANDROID__

namespace android {
namespace os {
namespace statsd {

namespace {

constexpr uint32_t kTestUid = 1001;
constexpr uint32_t kTestPid = 1002;
constexpr int kEventCount = 1000;
constexpr int kEventFilteredCount = 500;
constexpr int kAtomId = 1000;

class AStatsEventWrapper final {
    AStatsEvent* statsEvent = nullptr;

public:
    AStatsEventWrapper(int atomId) {
        statsEvent = AStatsEvent_obtain();
        createStatsEvent(statsEvent, INT64_TYPE, /*atomId=*/atomId);
        AStatsEvent_build(statsEvent);
    }

    std::pair<const uint8_t*, size_t> getBuffer() const {
        size_t size;
        const uint8_t* buf = AStatsEvent_getBuffer(statsEvent, &size);
        return std::make_pair(buf, size);
    }

    ~AStatsEventWrapper() {
        AStatsEvent_release(statsEvent);
    }
};

}  //  namespace

void generateAtomLogging(StatsSocketListener& socketListener, int eventCount, int startAtomId) {
    // create number of AStatsEvent
    for (int i = 0; i < eventCount; i++) {
        AStatsEventWrapper event(startAtomId + i);
        auto [buf, size] = event.getBuffer();
        EXPECT_TRUE(socketListener.processMessage(buf, size, kTestUid, kTestPid));
    }
}

class SocketListenerTestNoFiltering : public testing::TestWithParam<bool> {
protected:
    std::shared_ptr<LogEventQueue> mEventQueue;
    std::shared_ptr<LogEventFilter> mLogEventFilter;
    StatsSocketListener mSocketListener;

public:
    SocketListenerTestNoFiltering()
        : mEventQueue(std::make_shared<LogEventQueue>(kEventCount /*buffer limit*/)),
          mLogEventFilter(GetParam() ? std::make_shared<LogEventFilter>() : nullptr),
          mSocketListener(mEventQueue, mLogEventFilter) {
    }

    static std::string ToString(testing::TestParamInfo<bool> info) {
        return info.param ? "WithEventFilter" : "NoEventFilter";
    }
};

INSTANTIATE_TEST_SUITE_P(SocketListenerTestNoFiltering, SocketListenerTestNoFiltering,
                         testing::Bool(), SocketListenerTestNoFiltering::ToString);

TEST_P(SocketListenerTestNoFiltering, TestProcessMessageNoFiltering) {
    if (GetParam()) {
        mLogEventFilter->setFilteringEnabled(false);
    }

    // create number of AStatsEvent
    for (int i = 0; i < kEventCount; i++) {
        AStatsEventWrapper event(kAtomId + i);
        auto [buf, size] = event.getBuffer();
        EXPECT_TRUE(mSocketListener.processMessage(buf, size, kTestUid, kTestPid));
    }

    generateAtomLogging(mSocketListener, kEventCount, kAtomId);

    // check content of the queue
    EXPECT_EQ(kEventCount, mEventQueue->mQueue.size());
    for (int i = 0; i < kEventCount; i++) {
        auto logEvent = mEventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_FALSE(logEvent->isParsedHeaderOnly());
    }
}

TEST_P(SocketListenerTestNoFiltering, TestProcessMessageNoFilteringWithEmptySetExplicitSet) {
    if (GetParam()) {
        mLogEventFilter->setFilteringEnabled(false);
        LogEventFilter::AtomIdSet idsList;
        mLogEventFilter->setAtomIds(idsList, nullptr);
    }

    generateAtomLogging(mSocketListener, kEventCount, kAtomId);

    // check content of the queue
    EXPECT_EQ(kEventCount, mEventQueue->mQueue.size());
    for (int i = 0; i < kEventCount; i++) {
        auto logEvent = mEventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_FALSE(logEvent->isParsedHeaderOnly());
    }
}

TEST(SocketListenerTest, TestProcessMessageFilterEmptySet) {
    std::shared_ptr<LogEventQueue> eventQueue =
            std::make_shared<LogEventQueue>(kEventCount /*buffer limit*/);

    // Setup socket
    std::shared_ptr<LogEventFilter> logEventFilter = std::make_shared<LogEventFilter>();
    StatsSocketListener socketListener(eventQueue, logEventFilter);

    generateAtomLogging(socketListener, kEventCount, kAtomId);

    // check content of the queue
    for (int i = 0; i < kEventCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_TRUE(logEvent->isParsedHeaderOnly());
    }
}

TEST(SocketListenerTest, TestProcessMessageFilterEmptySetExplicitSet) {
    std::shared_ptr<LogEventQueue> eventQueue =
            std::make_shared<LogEventQueue>(kEventCount /*buffer limit*/);

    // Setup socket
    std::shared_ptr<LogEventFilter> logEventFilter = std::make_shared<LogEventFilter>();
    StatsSocketListener socketListener(eventQueue, logEventFilter);
    LogEventFilter::AtomIdSet idsList;
    logEventFilter->setAtomIds(idsList, nullptr);

    generateAtomLogging(socketListener, kEventCount, kAtomId);

    // check content of the queue
    for (int i = 0; i < kEventCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_TRUE(logEvent->isParsedHeaderOnly());
    }
}

TEST(SocketListenerTest, TestProcessMessageFilterCompleteSet) {
    std::shared_ptr<LogEventQueue> eventQueue =
            std::make_shared<LogEventQueue>(kEventCount /*buffer limit*/);

    // Setup socket
    std::shared_ptr<LogEventFilter> logEventFilter = std::make_shared<LogEventFilter>();
    StatsSocketListener socketListener(eventQueue, logEventFilter);
    LogEventFilter::AtomIdSet idsList;
    for (int i = 0; i < kEventCount; i++) {
        idsList.insert(kAtomId + i);
    }
    logEventFilter->setAtomIds(idsList, nullptr);

    generateAtomLogging(socketListener, kEventCount, kAtomId);

    // check content of the queue
    EXPECT_EQ(kEventCount, eventQueue->mQueue.size());
    for (int i = 0; i < kEventCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_FALSE(logEvent->isParsedHeaderOnly());
    }
}

TEST(SocketListenerTest, TestProcessMessageFilterPartialSet) {
    std::shared_ptr<LogEventQueue> eventQueue =
            std::make_shared<LogEventQueue>(kEventCount /*buffer limit*/);

    // Setup socket
    std::shared_ptr<LogEventFilter> logEventFilter = std::make_shared<LogEventFilter>();
    StatsSocketListener socketListener(eventQueue, logEventFilter);
    LogEventFilter::AtomIdSet idsList;
    for (int i = 0; i < kEventFilteredCount; i++) {
        idsList.insert(kAtomId + i);
    }
    logEventFilter->setAtomIds(idsList, nullptr);

    generateAtomLogging(socketListener, kEventCount, kAtomId);

    // check content of the queue
    EXPECT_EQ(kEventCount, eventQueue->mQueue.size());
    for (int i = 0; i < kEventFilteredCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_FALSE(logEvent->isParsedHeaderOnly());
    }

    for (int i = kEventFilteredCount; i < kEventCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_TRUE(logEvent->isParsedHeaderOnly());
    }
}

TEST(SocketListenerTest, TestProcessMessageFilterToggle) {
    std::shared_ptr<LogEventQueue> eventQueue =
            std::make_shared<LogEventQueue>(kEventCount * 3 /*buffer limit*/);

    // Setup socket
    std::shared_ptr<LogEventFilter> logEventFilter = std::make_shared<LogEventFilter>();
    StatsSocketListener socketListener(eventQueue, logEventFilter);
    LogEventFilter::AtomIdSet idsList;
    for (int i = 0; i < kEventFilteredCount; i++) {
        idsList.insert(kAtomId + i);
    }
    // events with ids from kAtomId to kAtomId + kEventFilteredCount should not be skipped
    logEventFilter->setAtomIds(idsList, nullptr);

    generateAtomLogging(socketListener, kEventCount, kAtomId);

    logEventFilter->setFilteringEnabled(false);
    // since filtering is disabled - events with any ids should not be skipped
    // will generate events with ids [kAtomId + kEventCount, kAtomId + kEventCount * 2]
    generateAtomLogging(socketListener, kEventCount, kAtomId + kEventCount);

    logEventFilter->setFilteringEnabled(true);
    LogEventFilter::AtomIdSet idsList2;
    for (int i = kEventFilteredCount; i < kEventCount; i++) {
        idsList2.insert(kAtomId + kEventCount * 2 + i);
    }
    // events with idsList2 ids should not be skipped
    logEventFilter->setAtomIds(idsList2, nullptr);

    // will generate events with ids [kAtomId + kEventCount * 2, kAtomId + kEventCount * 3]
    generateAtomLogging(socketListener, kEventCount, kAtomId + kEventCount * 2);

    // check content of the queue
    EXPECT_EQ(kEventCount * 3, eventQueue->mQueue.size());
    // events with ids from kAtomId to kAtomId + kEventFilteredCount should not be skipped
    for (int i = 0; i < kEventFilteredCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_FALSE(logEvent->isParsedHeaderOnly());
    }

    // all events above kAtomId + kEventFilteredCount to kAtomId + kEventCount should be skipped
    for (int i = kEventFilteredCount; i < kEventCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + i, logEvent->GetTagId());
        EXPECT_TRUE(logEvent->isParsedHeaderOnly());
    }

    // events with ids [kAtomId + kEventCount, kAtomId + kEventCount * 2] should not be skipped
    // since wiltering was disabled at that time
    for (int i = 0; i < kEventCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + kEventCount + i, logEvent->GetTagId());
        EXPECT_FALSE(logEvent->isParsedHeaderOnly());
    }

    // first half events with ids [kAtomId + kEventCount * 2, kAtomId + kEventCount * 3]
    // should be skipped
    for (int i = 0; i < kEventFilteredCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + kEventCount * 2 + i, logEvent->GetTagId());
        EXPECT_TRUE(logEvent->isParsedHeaderOnly());
    }

    // second half events with ids [kAtomId + kEventCount * 2, kAtomId + kEventCount * 3]
    // should be processed
    for (int i = kEventFilteredCount; i < kEventCount; i++) {
        auto logEvent = eventQueue->waitPop();
        EXPECT_TRUE(logEvent->isValid());
        EXPECT_EQ(kAtomId + kEventCount * 2 + i, logEvent->GetTagId());
        EXPECT_FALSE(logEvent->isParsedHeaderOnly());
    }
}

// TODO: tests for setAtomIds() with multiple consumers
// TODO: use MockLogEventFilter to test different sets from different consumers

}  // namespace statsd
}  // namespace os
}  // namespace android
#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
