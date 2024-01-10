/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "stats_regex.h"

#include <log/log.h>

namespace android {
namespace os {
namespace statsd {

using std::regex;
using std::regex_error;
using std::regex_replace;
using std::string;
using std::unique_ptr;

unique_ptr<regex> createRegex(const string& pattern) {
    try {
        return std::make_unique<regex>(pattern);
    } catch (regex_error& e) {
        ALOGE("regex_error: %s, pattern: %s", e.what(), pattern.c_str());
        return nullptr;
    }
}

string regexReplace(const string& input, const regex& re, const string& format) {
    try {
        return regex_replace(input, re, format);
    } catch (regex_error& e) {
        ALOGE("regex_error: %s, input: %s, format: %s", e.what(), input.c_str(), format.c_str());
        return input;
    }
}

}  // namespace statsd
}  // namespace os
}  // namespace android
