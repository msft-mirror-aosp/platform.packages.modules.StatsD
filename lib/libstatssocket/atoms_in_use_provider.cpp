
/*
 * Copyright (C) 2025 The Android Open Source Project
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

#define STATSSOCKET_DEBUG false  // STOPSHIP if true
#include "Log.h"

#include "atoms_in_use_provider.h"

#include <StatsdLoggingControl.h>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <com_android_os_statsd_flags.h>

#include <cerrno>
#include <cinttypes>
#include <filesystem>
#include <string>
#include <vector>

using namespace android::os::statsd;

namespace flags = com::android::os::statsd::flags;

template <typename Clock>
AtomsInUseProvider<Clock>::AtomsInUseProvider(std::string fileName, std::string versionPropertyName,
                                              int64_t cacheTtlNanos)
    : mFileName(std::move(fileName)),
      mVersionPropertyName(std::move(versionPropertyName)),
      mCacheCooldownTimer(cacheTtlNanos) {
}

template <typename Clock>
AtomsInUseProvider<Clock>::~AtomsInUseProvider() {
    if (mSyncThread.joinable()) {
        mSyncThread.join();
    }
}

template <typename Clock>
bool AtomsInUseProvider<Clock>::isAtomInUse(int32_t atomId) {
    const int64_t nowNs = Clock::getTimeNs();
    // TODO (b/407064406): should be fast but thread safe - without mutex on each access
    // use atomics to see if there was update to enabled list
    // only then take mutex to sync/otherwise return cached value
    // consider that isAtomEnabled can be invoked from different threads
    // also locking should not be held during atom in use cache update - so
    // atoms can be logged by other threads except the one which triggered the
    // cache update
    if (updateCacheIfNeeded(nowNs)) {
        // cache update is in progress or was invalidated
        // all atoms are allowed in this case
        return true;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    // if cache is empty it usually means config was not set or did not read properly
    // or list sync is in progress, and by default all atoms are enabled in this case
    if (mAtomsInUseCached.size() == 0) {
        return true;
    }

    const bool atomEnabled = mAtomsInUseCached.find(atomId) != mAtomsInUseCached.end();
    VLOG("AtomsInUseProvider::isAtomEnabled %d == %d from %d", atomId, atomEnabled,
         (int)mAtomsInUseCached.size());
    return atomEnabled;
}

template <typename Clock>
bool AtomsInUseProvider<Clock>::updateCacheIfNeeded(int64_t nowNs) {
    int64_t newVersion = 0;
    // determine if cache needs to be updated
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mCacheCooldownTimer.isExpired(nowNs)) {
            VLOG("updateCacheIfNeeded: cooldown timer is not expired yet");
            return false;
        }
        // whatever will go wrong below - keep delay before retry
        mCacheCooldownTimer.start(nowNs);

        if (!isSyncNeededLocked(newVersion)) {
            VLOG("updateCacheIfNeeded: no sync needed");
            return false;
        }
        // list sync is done asynchronously - clear cache while it is in progress
        mListVersion = 0;
        mAtomsInUseCached.clear();
    }

    if (newVersion > 0) {
        // populate with new version in async way allowing all atoms to be logged during update
        // if something will go wrong - by default all atoms are in use
        // cache version will be updated once async operation finished
        updateCache(newVersion);
    }

    return true;
}

template <typename Clock>
void AtomsInUseProvider<Clock>::updateCache(int64_t newVersion) {
    if (flags::logging_control_sync_in_background()) {
        // Only spawn one thread to manage requests
        // mMutex must not be held at this point by the calling thread
        if (mSyncThreadAlive.exchange(true)) {
            return;
        }
        if (mSyncThread.joinable()) {
            mSyncThread.join();
        }
        mSyncThread = std::thread(&AtomsInUseProvider::syncAtomsList, this, newVersion);
    } else {
        syncAtomsList(newVersion);
    }
}

template <typename Clock>
bool AtomsInUseProvider<Clock>::isSyncNeededLocked(int64_t& newVersion) {
    // check if there is a new list published
    const std::string value = android::base::GetProperty(mVersionPropertyName, "");
    if (value.empty()) {
        VLOG("isSyncNeededLocked: list was removed or not defined");
        return mListVersion > 0;
    }

    newVersion = atoll(value.c_str());
    VLOG("isSyncNeededLocked: newVersion %" PRId64 " vs mListVersion %" PRId64, newVersion,
         mListVersion);
    // test if new version is available
    return newVersion != mListVersion;
}

template <typename Clock>
void AtomsInUseProvider<Clock>::syncAtomsList(int64_t newVersion) {
    VLOG("syncAtomsList: start");
    std::string buffer;
    if (!android::base::ReadFileToString(mFileName.c_str(), &buffer)) {
        VLOG("syncAtomsList: Error reading %s: %s", mFileName.c_str(), std::strerror(errno));
        mSyncThreadAlive = false;
        return;
    }

    if (buffer.size() < sizeof(FileHeader) + sizeof(BlockHeader) + sizeof(int32_t)) {
        VLOG("syncAtomsList: invalid file size");
        mSyncThreadAlive = false;
        return;
    }

    const char* ptr = buffer.data();
    const FileHeader* fileHeader = reinterpret_cast<const FileHeader*>(ptr);
    if (fileHeader->magic_number != kMagicNumber) {
        VLOG("syncAtomsList: invalid file header magic number");
        mSyncThreadAlive = false;
        return;
    }

    if (fileHeader->version != kFormatVersion1) {
        VLOG("syncAtomsList: invalid file header version");
        mSyncThreadAlive = false;
        return;
    }

    ptr += sizeof(FileHeader);
    const BlockHeader* blockHeader = reinterpret_cast<const BlockHeader*>(ptr);
    const int32_t atomIdsCount = blockHeader->atomIdsCount;

    if (atomIdsCount < 1) {
        VLOG("syncAtomsList: invalid file content");
        mSyncThreadAlive = false;
        return;
    }

    if (buffer.size() !=
        sizeof(FileHeader) + sizeof(BlockHeader) + sizeof(int32_t) * atomIdsCount) {
        VLOG("syncAtomsList: invalid file size");
        mSyncThreadAlive = false;
        return;
    }

    ptr += sizeof(BlockHeader);

    const int32_t* atomIdsArray = reinterpret_cast<const int32_t*>(ptr);

    std::lock_guard<std::mutex> lock(mMutex);
    mAtomsInUseCached = {atomIdsArray, atomIdsArray + atomIdsCount};
    mListVersion = newVersion;
    mSyncThreadAlive = false;
    VLOG("syncAtomsList: done");
}

template class AtomsInUseProvider<RealTimeClock>;
