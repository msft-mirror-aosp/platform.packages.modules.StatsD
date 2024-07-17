/*
 * Copyright (C) 2024, The Android Open Source Project
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

#include <memory>
#include <optional>

#include "src/StatsLogProcessor.h"
#include "src/logd/LogEvent.h"
#include "src/stats_log.pb.h"
#include "src/stats_log_util.h"
#include "src/statsd_config.pb.h"
#include "tests/metrics/parsing_utils/parsing_test_utils.h"
#include "tests/statsd_test_util.h"

#ifdef __ANDROID__

using namespace std;
using namespace testing;

namespace android {
namespace os {
namespace statsd {
namespace {

class ValueMetricHistogramE2eTest : public Test {
protected:
    void createProcessor(const StatsdConfig& config) {
        processor = CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);
    }
    optional<ConfigMetricsReportList> getReports() {
        ConfigMetricsReportList reports;
        vector<uint8_t> buffer;
        processor->onDumpReport(cfgKey, bucketStartTimeNs + bucketSizeNs,
                                /*include_current_bucket*/ false, true, ADB_DUMP, FAST, &buffer);
        if (reports.ParseFromArray(&buffer[0], buffer.size())) {
            backfillDimensionPath(&reports);
            backfillStringInReport(&reports);
            backfillStartEndTimestamp(&reports);
            return reports;
        }
        return nullopt;
    }

    void logEvents(const vector<shared_ptr<LogEvent>>& events) {
        for (const shared_ptr<LogEvent> event : events) {
            processor->OnLogEvent(event.get());
        }
    }

    void validateHistogram(const ConfigMetricsReportList& reports, int valueIndex,
                           const vector<int>& binCounts) {
        ASSERT_EQ(reports.reports_size(), 1);
        ConfigMetricsReport report = reports.reports(0);
        ASSERT_EQ(report.metrics_size(), 1);
        StatsLogReport metricReport = report.metrics(0);
        ASSERT_TRUE(metricReport.has_value_metrics());
        ASSERT_EQ(metricReport.value_metrics().skipped_size(), 0);
        ValueMetricData data = metricReport.value_metrics().data(0);
        ASSERT_EQ(data.bucket_info_size(), 1);
        ValueBucketInfo bucket = data.bucket_info(0);
        ASSERT_GE(bucket.values_size(), valueIndex + 1);
        ASSERT_THAT(bucket.values(valueIndex),
                    Property(&ValueBucketInfo::Value::has_histogram, IsTrue()));
        ASSERT_THAT(bucket.values(valueIndex).histogram().count(), ElementsAreArray(binCounts));
    }

    const uint64_t bucketStartTimeNs = 10000000000;  // 0:10
    const uint64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(TEN_MINUTES) * 1000000LL;
    ConfigKey cfgKey;
    sp<StatsLogProcessor> processor;
};

class ValueMetricHistogramE2eTestPushedExplicitBins : public ValueMetricHistogramE2eTest {
protected:
    void SetUp() override {
        StatsdConfig config = createExplicitHistogramStatsdConfig(/* bins */ {1, 7, 10, 20});
        createProcessor(config);
    }
};

TEST_F(ValueMetricHistogramE2eTestPushedExplicitBins, TestNoEvents) {
    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    ASSERT_EQ(reports->reports_size(), 1);
    ConfigMetricsReport report = reports->reports(0);
    ASSERT_EQ(report.metrics_size(), 1);
    StatsLogReport metricReport = report.metrics(0);
    EXPECT_TRUE(metricReport.has_value_metrics());
    ASSERT_EQ(metricReport.value_metrics().skipped_size(), 1);
}

TEST_F(ValueMetricHistogramE2eTestPushedExplicitBins, TestOneEventInFirstBinAfterUnderflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 5,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {0, 1, -3});
}

TEST_F(ValueMetricHistogramE2eTestPushedExplicitBins, TestOneEventInOverflowAndUnderflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 0,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 20, /* value1 */ 20,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {1, -3, 1});
}

TEST_F(ValueMetricHistogramE2eTestPushedExplicitBins, TestOneEventInUnderflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ -1,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {1, -4});
}

TEST_F(ValueMetricHistogramE2eTestPushedExplicitBins, TestOneEventInOverflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 100,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {-4, 1});
}

TEST_F(ValueMetricHistogramE2eTestPushedExplicitBins, TestOneEventInFirstBinBeforeOverflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 15,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {-3, 1, 0});
}

TEST_F(ValueMetricHistogramE2eTestPushedExplicitBins, TestOneEventInMiddleBin) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 7,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {-2, 1, -2});
}

TEST_F(ValueMetricHistogramE2eTestPushedExplicitBins, TestMultipleEvents) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 8,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 20, /* value1 */ 15,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 30, /* value1 */ 19,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 40, /* value1 */ 3,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 50, /* value1 */ 9,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 60, /* value1 */ 3,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {0, 2, 2, 2, 0});
}

class ValueMetricHistogramE2eTestPushedLinearBins : public ValueMetricHistogramE2eTest {
protected:
    void SetUp() override {
        // Bin starts: [UNDERFLOW, -10, -6, -2, 2, 6, 10]
        StatsdConfig config =
                createGeneratedHistogramStatsdConfig(/* min */ -10, /* max */ 10, /* count */ 5,
                                                     HistogramBinConfig::GeneratedBins::LINEAR);
        createProcessor(config);
    }
};

TEST_F(ValueMetricHistogramE2eTestPushedLinearBins, TestNoEvents) {
    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    ASSERT_EQ(reports->reports_size(), 1);
    ConfigMetricsReport report = reports->reports(0);
    ASSERT_EQ(report.metrics_size(), 1);
    StatsLogReport metricReport = report.metrics(0);
    EXPECT_TRUE(metricReport.has_value_metrics());
    ASSERT_EQ(metricReport.value_metrics().skipped_size(), 1);
}

TEST_F(ValueMetricHistogramE2eTestPushedLinearBins, TestOneEventInFirstBinAfterUnderflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ -10,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {0, 1, -5});
}

TEST_F(ValueMetricHistogramE2eTestPushedLinearBins, TestOneEventInOverflowAndUnderflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ -11,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 20, /* value1 */ 10,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {1, -5, 1});
}

TEST_F(ValueMetricHistogramE2eTestPushedLinearBins, TestOneEventInUnderflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ -15,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {1, -6});
}

TEST_F(ValueMetricHistogramE2eTestPushedLinearBins, TestOneEventInOverflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 100,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {-6, 1});
}

TEST_F(ValueMetricHistogramE2eTestPushedLinearBins, TestOneEventInFirstBinBeforeOverflow) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 6,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {-5, 1, 0});
}

TEST_F(ValueMetricHistogramE2eTestPushedLinearBins, TestOneEventInMiddleBin) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 0,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {-3, 1, -3});
}

TEST_F(ValueMetricHistogramE2eTestPushedLinearBins, TestMultipleEvents) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 1,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 20, /* value1 */ 4,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 30, /* value1 */ -9,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 40, /* value1 */ 3,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 50, /* value1 */ 8,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 60, /* value1 */ -11,
                                      /* value2 */ 0)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {1, 1, 0, 1, 2, 1, 0});
}

class ValueMetricHistogramE2eTestMultiplePushedHistograms : public ValueMetricHistogramE2eTest {
protected:
    void SetUp() override {
        StatsdConfig config;
        *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
        *config.add_value_metric() =
                createValueMetric("ValueMetric", config.atom_matcher(0), /* valueFields */ {1, 2},
                                  {ValueMetric::HISTOGRAM, ValueMetric::HISTOGRAM},
                                  /* condition */ nullopt, /* states */ {});

        // Bin starts: [UNDERFLOW, 5, 10, 20, 40, 80, 160]
        *config.mutable_value_metric(0)->add_histogram_bin_configs() =
                createGeneratedBinConfig(/* id */ 1, /* min */ 5, /* max */ 160, /* count */ 5,
                                         HistogramBinConfig::GeneratedBins::EXPONENTIAL);

        // Bin starts: [UNDERFLOW, -10, -6, -2, 2, 6, 10]
        *config.mutable_value_metric(0)->add_histogram_bin_configs() =
                createGeneratedBinConfig(/* id */ 2, /* min */ -10, /* max */ 10, /* count */ 5,
                                         HistogramBinConfig::GeneratedBins::LINEAR);

        createProcessor(config);
    }
};

TEST_F(ValueMetricHistogramE2eTestMultiplePushedHistograms, TestNoEvents) {
    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    ASSERT_EQ(reports->reports_size(), 1);
    ConfigMetricsReport report = reports->reports(0);
    ASSERT_EQ(report.metrics_size(), 1);
    StatsLogReport metricReport = report.metrics(0);
    EXPECT_TRUE(metricReport.has_value_metrics());
    ASSERT_EQ(metricReport.value_metrics().skipped_size(), 1);
}

TEST_F(ValueMetricHistogramE2eTestMultiplePushedHistograms, TestMultipleEvents) {
    logEvents({CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 90,
                                      /* value2 */ 0),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 20, /* value1 */ 6,
                                      /* value2 */ 12),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 30, /* value1 */ 50,
                                      /* value2 */ -1),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 40, /* value1 */ 30,
                                      /* value2 */ 5),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 50, /* value1 */ 15,
                                      /* value2 */ 2),
               CreateTwoValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 60, /* value1 */ 160,
                                      /* value2 */ 9)});

    optional<ConfigMetricsReportList> reports = getReports();
    ASSERT_NE(reports, nullopt);

    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 0, {0, 1, 1, 1, 1, 1, 1});
    TRACE_CALL(validateHistogram, *reports, /* valueIndex */ 1, {-3, 2, 2, 1, 1});
}

TEST_F(ValueMetricHistogramE2eTest, TestDimensionConditionAndMultipleAggregationTypes) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_atom_matcher() = CreateScreenTurnedOnAtomMatcher();
    *config.add_atom_matcher() = CreateScreenTurnedOffAtomMatcher();
    *config.add_predicate() = CreateScreenIsOnPredicate();
    config.mutable_predicate(0)->mutable_simple_predicate()->set_initial_value(
            SimplePredicate::FALSE);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueFields */ {1, 2, 2},
                              {ValueMetric::HISTOGRAM, ValueMetric::SUM, ValueMetric::MIN},
                              /* condition */ config.predicate(0).id(), /* states */ {});
    *config.mutable_value_metric(0)->mutable_dimensions_in_what() =
            CreateDimensions(/* atomId */ 1, {3 /* value3 */});

    // Bin starts: [UNDERFLOW, 5, 10, 20, 40, 80, 160]
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createGeneratedBinConfig(/* id */ 1, /* min */ 5, /* max */ 160, /* count */ 5,
                                     HistogramBinConfig::GeneratedBins::EXPONENTIAL);

    createProcessor(config);

    logEvents(
            {CreateThreeValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 10, /* value1 */ 90,
                                      /* value2 */ 0, /* value3 */ 1),
             CreateScreenStateChangedEvent(bucketStartTimeNs + 15, android::view::DISPLAY_STATE_ON),
             CreateThreeValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 20, /* value1 */ 6,
                                      /* value2 */ 12, /* value3 */ 1),
             CreateThreeValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 30, /* value1 */ 50,
                                      /* value2 */ -1, /* value3 */ 1),
             CreateThreeValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 40, /* value1 */ 30,
                                      /* value2 */ 5, /* value3 */ 2),
             CreateThreeValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 50, /* value1 */ 15,
                                      /* value2 */ 2, /* value3 */ 1),
             CreateThreeValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 60, /* value1 */ 5,
                                      /* value2 */ 3, /* value3 */ 2),
             CreateScreenStateChangedEvent(bucketStartTimeNs + 65,
                                           android::view::DISPLAY_STATE_OFF),
             CreateThreeValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 70, /* value1 */ 160,
                                      /* value2 */ 9, /* value3 */ 1),
             CreateThreeValueLogEvent(/* atomId */ 1, bucketStartTimeNs + 80, /* value1 */ 70,
                                      /* value2 */ 20, /* value3 */ 2)});

    optional<ConfigMetricsReportList> reports = getReports();

    ASSERT_NE(reports, nullopt);
    ASSERT_EQ(reports->reports_size(), 1);
    ConfigMetricsReport report = reports->reports(0);
    ASSERT_EQ(report.metrics_size(), 1);
    StatsLogReport metricReport = report.metrics(0);
    ASSERT_TRUE(metricReport.has_value_metrics());
    ASSERT_EQ(metricReport.value_metrics().skipped_size(), 0);
    ASSERT_EQ(metricReport.value_metrics().data_size(), 2);

    // Dimension 1
    {
        ValueMetricData data = metricReport.value_metrics().data(0);
        ASSERT_EQ(data.bucket_info_size(), 1);
        ValueBucketInfo bucket = data.bucket_info(0);
        ASSERT_EQ(bucket.values_size(), 3);
        ASSERT_THAT(bucket.values(0), Property(&ValueBucketInfo::Value::has_histogram, IsTrue()));
        EXPECT_THAT(bucket.values(0).histogram().count(), ElementsAreArray({0, 1, 1, 0, 1, -2}));
        ASSERT_THAT(bucket.values(1), Property(&ValueBucketInfo::Value::has_value_long, IsTrue()));
        EXPECT_EQ(bucket.values(1).value_long(), 13);
        ASSERT_THAT(bucket.values(2), Property(&ValueBucketInfo::Value::has_value_long, IsTrue()));
        EXPECT_EQ(bucket.values(2).value_long(), -1);
    }

    // Dimension 2
    {
        ValueMetricData data = metricReport.value_metrics().data(1);
        ASSERT_EQ(data.bucket_info_size(), 1);
        ValueBucketInfo bucket = data.bucket_info(0);
        ASSERT_EQ(bucket.values_size(), 3);
        ASSERT_THAT(bucket.values(0), Property(&ValueBucketInfo::Value::has_histogram, IsTrue()));
        EXPECT_THAT(bucket.values(0).histogram().count(), ElementsAreArray({0, 1, 0, 1, -3}));
        ASSERT_THAT(bucket.values(1), Property(&ValueBucketInfo::Value::has_value_long, IsTrue()));
        EXPECT_EQ(bucket.values(1).value_long(), 8);
        ASSERT_THAT(bucket.values(2), Property(&ValueBucketInfo::Value::has_value_long, IsTrue()));
        EXPECT_EQ(bucket.values(2).value_long(), 3);
    }
}

}  // anonymous namespace
}  // namespace statsd
}  // namespace os
}  // namespace android
#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
