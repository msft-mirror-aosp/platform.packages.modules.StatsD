#define STATSD_DEBUG true
#include "Log.h"

#include "RestrictedEventMetricProducer.h"

#include "utils/DbUtils.h"

using std::lock_guard;
using std::vector;

namespace android {
namespace os {
namespace statsd {

#define NS_PER_DAY (24 * 3600 * NS_PER_SEC)

RestrictedEventMetricProducer::RestrictedEventMetricProducer(
        const ConfigKey& key, const EventMetric& metric, const int conditionIndex,
        const vector<ConditionState>& initialConditionCache, const sp<ConditionWizard>& wizard,
        const uint64_t protoHash, const int64_t startTimeNs,
        const unordered_map<int, shared_ptr<Activation>>& eventActivationMap,
        const unordered_map<int, vector<shared_ptr<Activation>>>& eventDeactivationMap,
        const vector<int>& slicedStateAtoms,
        const unordered_map<int, unordered_map<int, int64_t>>& stateGroupMap,
        int restrictedDataTtlInDays)
    : EventMetricProducer(key, metric, conditionIndex, initialConditionCache, wizard, protoHash,
                          startTimeNs, eventActivationMap, eventDeactivationMap, slicedStateAtoms,
                          stateGroupMap),
      mRestrictedDataTtlInDays(restrictedDataTtlInDays) {
}

void RestrictedEventMetricProducer::onMatchedLogEventInternalLocked(
        const size_t matcherIndex, const MetricDimensionKey& eventKey,
        const ConditionKey& conditionKey, bool condition, const LogEvent& event,
        const std::map<int, HashableDimensionKey>& statePrimaryKeys) {
    if (!condition) {
        return;
    }
    mLogEvents.push_back(event);
    mTotalSize += getSize(event.getValues()) + sizeof(event);
}

void RestrictedEventMetricProducer::onDumpReportLocked(
        const int64_t dumpTimeNs, const bool include_current_partial_bucket, const bool erase_data,
        const DumpLatency dumpLatency, std::set<string>* str_set,
        android::util::ProtoOutputStream* protoOutput) {
    // TODO(b/268150038): report error to statsdstats
    VLOG("Unexpected call to onDumpReportLocked() in RestrictedEventMetricProducer");
}

void RestrictedEventMetricProducer::onMetricRemove() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsMetricTableCreated) {
        return;
    }
    if (!dbutils::deleteTable(mConfigKey, mMetricId)) {
        // TODO(b/268150038): report error to statsdstats
        VLOG("Failed to delete table for metric %lld", (long long)mMetricId);
    }
}

void RestrictedEventMetricProducer::enforceRestrictedDataTtl(sqlite3* db,
                                                             const int64_t wallClockNs) {
    int64_t ttlTime = wallClockNs - mRestrictedDataTtlInDays * NS_PER_DAY;
    dbutils::flushTtl(db, mMetricId, ttlTime);
}

void RestrictedEventMetricProducer::clearPastBucketsLocked(const int64_t dumpTimeNs) {
    VLOG("Unexpected call to clearPastBucketsLocked in RestrictedEventMetricProducer");
}

void RestrictedEventMetricProducer::dropDataLocked(const int64_t dropTimeNs) {
    mLogEvents.clear();
    mTotalSize = 0;
    StatsdStats::getInstance().noteBucketDropped(mMetricId);
}

void RestrictedEventMetricProducer::flushRestrictedData() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mLogEvents.empty()) {
        return;
    }
    if (!mIsMetricTableCreated) {
        // TODO(b/271481944): add retry.
        if (!dbutils::createTableIfNeeded(mConfigKey, mMetricId, mLogEvents[0])) {
            VLOG("Failed to create table for metric %lld", (long long)mMetricId);
            // TODO(b/268150038): report error to statsdstats
            return;
        }
        mIsMetricTableCreated = true;
    }
    if (!dbutils::insert(mConfigKey, mMetricId, mLogEvents)) {
        // TODO(b/268150038): report error to statsdstats
        VLOG("Failed to insert logEvent to table for metric %lld", (long long)mMetricId);
    }
    mLogEvents.clear();
    mTotalSize = 0;
}

}  // namespace statsd
}  // namespace os
}  // namespace android
