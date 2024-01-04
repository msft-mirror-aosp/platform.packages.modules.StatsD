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
#include "EventMatcherWizard.h"

namespace android {
namespace os {
namespace statsd {

using std::vector;

MatchingState EventMatcherWizard::matchLogEvent(const LogEvent& event, int matcherIndex) {
    if (matcherIndex < 0 || matcherIndex >= (int)mAllEventMatchers.size()) {
        return MatchingState::kNotComputed;
    }
    std::fill(mMatcherCache.begin(), mMatcherCache.end(), MatchingState::kNotComputed);
    mAllEventMatchers[matcherIndex]->onLogEvent(event, matcherIndex, mAllEventMatchers,
                                                mMatcherCache);
    return mMatcherCache[matcherIndex];
}

}  // namespace statsd
}  // namespace os
}  // namespace android
