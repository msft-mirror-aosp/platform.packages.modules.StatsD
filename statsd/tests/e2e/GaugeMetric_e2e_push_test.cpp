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

#include <gtest/gtest.h>

#include <vector>

#include "src/StatsLogProcessor.h"
#include "src/stats_log_util.h"
#include "stats_event.h"
#include "tests/statsd_test_util.h"

namespace android {
namespace os {
namespace statsd {

#ifdef __ANDROID__

namespace {

StatsdConfig CreateStatsdConfigForPushedEvent(const GaugeMetric::SamplingType sampling_type) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateMoveToBackgroundAtomMatcher();
    *config.add_atom_matcher() = CreateMoveToForegroundAtomMatcher();

    auto atomMatcher = CreateSimpleAtomMatcher("", util::APP_START_OCCURRED);
    *config.add_atom_matcher() = atomMatcher;

    auto isInBackgroundPredicate = CreateIsInBackgroundPredicate();
    *isInBackgroundPredicate.mutable_simple_predicate()->mutable_dimensions() =
        CreateDimensions(util::ACTIVITY_FOREGROUND_STATE_CHANGED, {1 /* uid field */ });
    *config.add_predicate() = isInBackgroundPredicate;

    auto gaugeMetric = config.add_gauge_metric();
    gaugeMetric->set_id(123456);
    gaugeMetric->set_what(atomMatcher.id());
    gaugeMetric->set_condition(isInBackgroundPredicate.id());
    gaugeMetric->mutable_gauge_fields_filter()->set_include_all(false);
    gaugeMetric->set_sampling_type(sampling_type);
    auto fieldMatcher = gaugeMetric->mutable_gauge_fields_filter()->mutable_fields();
    fieldMatcher->set_field(util::APP_START_OCCURRED);
    fieldMatcher->add_child()->set_field(3);  // type (enum)
    fieldMatcher->add_child()->set_field(4);  // activity_name(str)
    fieldMatcher->add_child()->set_field(7);  // activity_start_msec(int64)
    *gaugeMetric->mutable_dimensions_in_what() =
        CreateDimensions(util::APP_START_OCCURRED, {1 /* uid field */ });
    gaugeMetric->set_bucket(FIVE_MINUTES);

    auto links = gaugeMetric->add_links();
    links->set_condition(isInBackgroundPredicate.id());
    auto dimensionWhat = links->mutable_fields_in_what();
    dimensionWhat->set_field(util::APP_START_OCCURRED);
    dimensionWhat->add_child()->set_field(1);  // uid field.
    auto dimensionCondition = links->mutable_fields_in_condition();
    dimensionCondition->set_field(util::ACTIVITY_FOREGROUND_STATE_CHANGED);
    dimensionCondition->add_child()->set_field(1);  // uid field.
    return config;
}

StatsdConfig CreateStatsdConfigForRepeatedFieldsPushedEvent(
        const GaugeMetric::SamplingType sampling_type) {
    StatsdConfig config;

    AtomMatcher testAtomReportedAtomMatcher =
            CreateSimpleAtomMatcher("TestAtomReportedMatcher", util::TEST_ATOM_REPORTED);
    *config.add_atom_matcher() = testAtomReportedAtomMatcher;

    GaugeMetric* gaugeMetric = config.add_gauge_metric();
    gaugeMetric->set_id(123456);
    gaugeMetric->set_what(testAtomReportedAtomMatcher.id());
    gaugeMetric->set_sampling_type(sampling_type);
    FieldMatcher* fieldMatcher = gaugeMetric->mutable_gauge_fields_filter()->mutable_fields();
    fieldMatcher->set_field(util::TEST_ATOM_REPORTED);

    FieldMatcher* childFieldMatcher = fieldMatcher->add_child();
    childFieldMatcher->set_field(9);  // repeated_int_field
    childFieldMatcher->set_position(Position::FIRST);

    childFieldMatcher = fieldMatcher->add_child();
    childFieldMatcher->set_field(10);  // repeated_long_field
    childFieldMatcher->set_position(Position::LAST);

    childFieldMatcher = fieldMatcher->add_child();
    childFieldMatcher->set_field(11);  // repeated_float_field
    childFieldMatcher->set_position(Position::ALL);

    childFieldMatcher = fieldMatcher->add_child();
    childFieldMatcher->set_field(12);  // repeated_string_field
    childFieldMatcher->set_position(Position::FIRST);

    childFieldMatcher = fieldMatcher->add_child();
    childFieldMatcher->set_field(13);  // repeated_boolean_field
    childFieldMatcher->set_position(Position::LAST);

    childFieldMatcher = fieldMatcher->add_child();
    childFieldMatcher->set_field(14);  // repeated_enum_field
    childFieldMatcher->set_position(Position::ALL);

    gaugeMetric->set_bucket(FIVE_MINUTES);
    return config;
}

}  // namespace

// Setup for test fixture.
class GaugeMetricE2ePushedTest : public ::testing::Test {
    void SetUp() override {
        FlagProvider::getInstance().overrideFuncs(&isAtLeastSFuncTrue);
    }

    void TearDown() override {
        FlagProvider::getInstance().resetOverrides();
    }

public:
    void doTestRepeatedFieldsForPushedEvent();
};

TEST_F(GaugeMetricE2ePushedTest, TestMultipleFieldsForPushedEvent) {
    for (const auto& sampling_type :
         {GaugeMetric::FIRST_N_SAMPLES, GaugeMetric::RANDOM_ONE_SAMPLE}) {
        auto config = CreateStatsdConfigForPushedEvent(sampling_type);
        int64_t bucketStartTimeNs = 10000000000;
        int64_t bucketSizeNs =
                TimeUnitToBucketSizeInMillis(config.gauge_metric(0).bucket()) * 1000000;

        ConfigKey cfgKey;
        auto processor =
                CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);
        ASSERT_EQ(processor->mMetricsManagers.size(), 1u);
        EXPECT_TRUE(processor->mMetricsManagers.begin()->second->isConfigValid());

        int appUid1 = 123;
        int appUid2 = 456;
        std::vector<std::unique_ptr<LogEvent>> events;
        events.push_back(CreateMoveToBackgroundEvent(bucketStartTimeNs + 15, appUid1));
        events.push_back(
                CreateMoveToForegroundEvent(bucketStartTimeNs + bucketSizeNs + 250, appUid1));
        events.push_back(
                CreateMoveToBackgroundEvent(bucketStartTimeNs + bucketSizeNs + 350, appUid1));
        events.push_back(
                CreateMoveToForegroundEvent(bucketStartTimeNs + 2 * bucketSizeNs + 100, appUid1));

        events.push_back(CreateAppStartOccurredEvent(
                bucketStartTimeNs + 10, appUid1, "app1", AppStartOccurred::WARM, "activity_name1",
                "calling_pkg_name1", true /*is_instant_app*/, 101 /*activity_start_msec*/));
        events.push_back(CreateAppStartOccurredEvent(
                bucketStartTimeNs + 20, appUid1, "app1", AppStartOccurred::HOT, "activity_name2",
                "calling_pkg_name2", true /*is_instant_app*/, 102 /*activity_start_msec*/));
        events.push_back(CreateAppStartOccurredEvent(
                bucketStartTimeNs + 30, appUid1, "app1", AppStartOccurred::COLD, "activity_name3",
                "calling_pkg_name3", true /*is_instant_app*/, 103 /*activity_start_msec*/));
        events.push_back(CreateAppStartOccurredEvent(
                bucketStartTimeNs + bucketSizeNs + 30, appUid1, "app1", AppStartOccurred::WARM,
                "activity_name4", "calling_pkg_name4", true /*is_instant_app*/,
                104 /*activity_start_msec*/));
        events.push_back(CreateAppStartOccurredEvent(
                bucketStartTimeNs + 2 * bucketSizeNs, appUid1, "app1", AppStartOccurred::COLD,
                "activity_name5", "calling_pkg_name5", true /*is_instant_app*/,
                105 /*activity_start_msec*/));
        events.push_back(CreateAppStartOccurredEvent(
                bucketStartTimeNs + 2 * bucketSizeNs + 10, appUid1, "app1", AppStartOccurred::HOT,
                "activity_name6", "calling_pkg_name6", false /*is_instant_app*/,
                106 /*activity_start_msec*/));

        events.push_back(
                CreateMoveToBackgroundEvent(bucketStartTimeNs + bucketSizeNs + 10, appUid2));
        events.push_back(CreateAppStartOccurredEvent(
                bucketStartTimeNs + 2 * bucketSizeNs + 10, appUid2, "app2", AppStartOccurred::COLD,
                "activity_name7", "calling_pkg_name7", true /*is_instant_app*/,
                201 /*activity_start_msec*/));

        sortLogEventsByTimestamp(&events);

        for (const auto& event : events) {
            processor->OnLogEvent(event.get());
        }
        ConfigMetricsReportList reports;
        vector<uint8_t> buffer;
        processor->onDumpReport(cfgKey, bucketStartTimeNs + 3 * bucketSizeNs, false, true, ADB_DUMP,
                                FAST, &buffer);
        EXPECT_TRUE(buffer.size() > 0);
        EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
        backfillDimensionPath(&reports);
        backfillStringInReport(&reports);
        backfillStartEndTimestamp(&reports);
        backfillAggregatedAtoms(&reports);
        ASSERT_EQ(1, reports.reports_size());
        ASSERT_EQ(1, reports.reports(0).metrics_size());
        EXPECT_TRUE(reports.reports(0).metrics(0).has_estimated_data_bytes());
        StatsLogReport::GaugeMetricDataWrapper gaugeMetrics;
        sortMetricDataByDimensionsValue(reports.reports(0).metrics(0).gauge_metrics(),
                                        &gaugeMetrics);
        ASSERT_EQ(2, gaugeMetrics.data_size());

        auto data = gaugeMetrics.data(0);
        EXPECT_EQ(util::APP_START_OCCURRED, data.dimensions_in_what().field());
        ASSERT_EQ(1, data.dimensions_in_what().value_tuple().dimensions_value_size());
        EXPECT_EQ(1 /* uid field */,
                  data.dimensions_in_what().value_tuple().dimensions_value(0).field());
        EXPECT_EQ(appUid1, data.dimensions_in_what().value_tuple().dimensions_value(0).value_int());
        ASSERT_EQ(3, data.bucket_info_size());
        if (sampling_type == GaugeMetric::FIRST_N_SAMPLES) {
            ASSERT_EQ(2, data.bucket_info(0).atom_size());
            ASSERT_EQ(2, data.bucket_info(0).elapsed_timestamp_nanos_size());
            ASSERT_EQ(0, data.bucket_info(0).wall_clock_timestamp_nanos_size());
            EXPECT_EQ(bucketStartTimeNs, data.bucket_info(0).start_bucket_elapsed_nanos());
            EXPECT_EQ(bucketStartTimeNs + bucketSizeNs,
                      data.bucket_info(0).end_bucket_elapsed_nanos());
            EXPECT_EQ(AppStartOccurred::HOT,
                      data.bucket_info(0).atom(0).app_start_occurred().type());
            EXPECT_EQ("activity_name2",
                      data.bucket_info(0).atom(0).app_start_occurred().activity_name());
            EXPECT_EQ(102L,
                      data.bucket_info(0).atom(0).app_start_occurred().activity_start_millis());
            EXPECT_EQ(AppStartOccurred::COLD,
                      data.bucket_info(0).atom(1).app_start_occurred().type());
            EXPECT_EQ("activity_name3",
                      data.bucket_info(0).atom(1).app_start_occurred().activity_name());
            EXPECT_EQ(103L,
                      data.bucket_info(0).atom(1).app_start_occurred().activity_start_millis());

            ASSERT_EQ(1, data.bucket_info(1).atom_size());
            ASSERT_EQ(1, data.bucket_info(1).elapsed_timestamp_nanos_size());
            EXPECT_EQ(bucketStartTimeNs + bucketSizeNs,
                      data.bucket_info(1).start_bucket_elapsed_nanos());
            EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs,
                      data.bucket_info(1).end_bucket_elapsed_nanos());
            EXPECT_EQ(AppStartOccurred::WARM,
                      data.bucket_info(1).atom(0).app_start_occurred().type());
            EXPECT_EQ("activity_name4",
                      data.bucket_info(1).atom(0).app_start_occurred().activity_name());
            EXPECT_EQ(104L,
                      data.bucket_info(1).atom(0).app_start_occurred().activity_start_millis());

            ASSERT_EQ(2, data.bucket_info(2).atom_size());
            ASSERT_EQ(2, data.bucket_info(2).elapsed_timestamp_nanos_size());
            EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs,
                      data.bucket_info(2).start_bucket_elapsed_nanos());
            EXPECT_EQ(bucketStartTimeNs + 3 * bucketSizeNs,
                      data.bucket_info(2).end_bucket_elapsed_nanos());
            EXPECT_EQ(AppStartOccurred::COLD,
                      data.bucket_info(2).atom(0).app_start_occurred().type());
            EXPECT_EQ("activity_name5",
                      data.bucket_info(2).atom(0).app_start_occurred().activity_name());
            EXPECT_EQ(105L,
                      data.bucket_info(2).atom(0).app_start_occurred().activity_start_millis());
            EXPECT_EQ(AppStartOccurred::HOT,
                      data.bucket_info(2).atom(1).app_start_occurred().type());
            EXPECT_EQ("activity_name6",
                      data.bucket_info(2).atom(1).app_start_occurred().activity_name());
            EXPECT_EQ(106L,
                      data.bucket_info(2).atom(1).app_start_occurred().activity_start_millis());
        } else {
            ASSERT_EQ(1, data.bucket_info(0).atom_size());
            ASSERT_EQ(1, data.bucket_info(0).elapsed_timestamp_nanos_size());
            EXPECT_EQ(bucketStartTimeNs, data.bucket_info(0).start_bucket_elapsed_nanos());
            EXPECT_EQ(bucketStartTimeNs + bucketSizeNs,
                      data.bucket_info(0).end_bucket_elapsed_nanos());
            EXPECT_EQ(AppStartOccurred::HOT,
                      data.bucket_info(0).atom(0).app_start_occurred().type());
            EXPECT_EQ("activity_name2",
                      data.bucket_info(0).atom(0).app_start_occurred().activity_name());
            EXPECT_EQ(102L,
                      data.bucket_info(0).atom(0).app_start_occurred().activity_start_millis());

            ASSERT_EQ(1, data.bucket_info(1).atom_size());
            ASSERT_EQ(1, data.bucket_info(1).elapsed_timestamp_nanos_size());
            EXPECT_EQ(bucketStartTimeNs + bucketSizeNs,
                      data.bucket_info(1).start_bucket_elapsed_nanos());
            EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs,
                      data.bucket_info(1).end_bucket_elapsed_nanos());
            EXPECT_EQ(AppStartOccurred::WARM,
                      data.bucket_info(1).atom(0).app_start_occurred().type());
            EXPECT_EQ("activity_name4",
                      data.bucket_info(1).atom(0).app_start_occurred().activity_name());
            EXPECT_EQ(104L,
                      data.bucket_info(1).atom(0).app_start_occurred().activity_start_millis());

            ASSERT_EQ(1, data.bucket_info(2).atom_size());
            ASSERT_EQ(1, data.bucket_info(2).elapsed_timestamp_nanos_size());
            EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs,
                      data.bucket_info(2).start_bucket_elapsed_nanos());
            EXPECT_EQ(bucketStartTimeNs + 3 * bucketSizeNs,
                      data.bucket_info(2).end_bucket_elapsed_nanos());
            EXPECT_EQ(AppStartOccurred::COLD,
                      data.bucket_info(2).atom(0).app_start_occurred().type());
            EXPECT_EQ("activity_name5",
                      data.bucket_info(2).atom(0).app_start_occurred().activity_name());
            EXPECT_EQ(105L,
                      data.bucket_info(2).atom(0).app_start_occurred().activity_start_millis());
        }

        data = gaugeMetrics.data(1);

        EXPECT_EQ(data.dimensions_in_what().field(), util::APP_START_OCCURRED);
        ASSERT_EQ(data.dimensions_in_what().value_tuple().dimensions_value_size(), 1);
        EXPECT_EQ(1 /* uid field */,
                  data.dimensions_in_what().value_tuple().dimensions_value(0).field());
        EXPECT_EQ(appUid2, data.dimensions_in_what().value_tuple().dimensions_value(0).value_int());
        ASSERT_EQ(1, data.bucket_info_size());
        ASSERT_EQ(1, data.bucket_info(0).atom_size());
        ASSERT_EQ(1, data.bucket_info(0).elapsed_timestamp_nanos_size());
        EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs,
                  data.bucket_info(0).start_bucket_elapsed_nanos());
        EXPECT_EQ(bucketStartTimeNs + 3 * bucketSizeNs,
                  data.bucket_info(0).end_bucket_elapsed_nanos());
        EXPECT_EQ(AppStartOccurred::COLD, data.bucket_info(0).atom(0).app_start_occurred().type());
        EXPECT_EQ("activity_name7",
                  data.bucket_info(0).atom(0).app_start_occurred().activity_name());
        EXPECT_EQ(201L, data.bucket_info(0).atom(0).app_start_occurred().activity_start_millis());
    }
}

TEST_F_GUARDED(GaugeMetricE2ePushedTest, TestRepeatedFieldsForPushedEvent, __ANDROID_API_T__) {
    for (const auto& sampling_type :
         {GaugeMetric::FIRST_N_SAMPLES, GaugeMetric::RANDOM_ONE_SAMPLE}) {
        StatsdConfig config = CreateStatsdConfigForRepeatedFieldsPushedEvent(sampling_type);
        int64_t bucketStartTimeNs = 10000000000;
        int64_t bucketSizeNs =
                TimeUnitToBucketSizeInMillis(config.gauge_metric(0).bucket()) * 1000000;

        ConfigKey cfgKey;
        sp<StatsLogProcessor> processor =
                CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);

        std::vector<std::unique_ptr<LogEvent>> events;

        vector<int> intArray = {3, 6};
        vector<int64_t> longArray = {1000L, 10002L};
        vector<float> floatArray = {0.3f, 0.09f};
        vector<string> stringArray = {"str1", "str2"};
        int boolArrayLength = 2;
        bool boolArray[boolArrayLength];
        boolArray[0] = 1;
        boolArray[1] = 0;
        vector<uint8_t> boolArrayVector = {1, 0};
        vector<int> enumArray = {TestAtomReported::ON, TestAtomReported::OFF};

        events.push_back(CreateTestAtomReportedEventVariableRepeatedFields(
                bucketStartTimeNs + 10 * NS_PER_SEC, intArray, longArray, floatArray, stringArray,
                boolArray, boolArrayLength, enumArray));
        events.push_back(CreateTestAtomReportedEventVariableRepeatedFields(
                bucketStartTimeNs + 20 * NS_PER_SEC, {}, {}, {}, {}, {}, 0, {}));

        for (const auto& event : events) {
            processor->OnLogEvent(event.get());
        }

        ConfigMetricsReportList reports;
        vector<uint8_t> buffer;
        processor->onDumpReport(cfgKey, bucketStartTimeNs + 3 * bucketSizeNs, false, true, ADB_DUMP,
                                FAST, &buffer);
        EXPECT_TRUE(buffer.size() > 0);
        EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
        backfillDimensionPath(&reports);
        backfillStringInReport(&reports);
        backfillStartEndTimestamp(&reports);
        backfillAggregatedAtoms(&reports);

        ASSERT_EQ(1, reports.reports_size());
        ASSERT_EQ(1, reports.reports(0).metrics_size());
        StatsLogReport::GaugeMetricDataWrapper gaugeMetrics;
        sortMetricDataByDimensionsValue(reports.reports(0).metrics(0).gauge_metrics(),
                                        &gaugeMetrics);
        ASSERT_EQ(1, gaugeMetrics.data_size());

        GaugeMetricData data = gaugeMetrics.data(0);
        ASSERT_EQ(1, data.bucket_info_size());
        if (sampling_type == GaugeMetric::FIRST_N_SAMPLES) {
            EXPECT_EQ(bucketStartTimeNs, data.bucket_info(0).start_bucket_elapsed_nanos());
            EXPECT_EQ(bucketStartTimeNs + bucketSizeNs,
                      data.bucket_info(0).end_bucket_elapsed_nanos());
            ASSERT_EQ(2, data.bucket_info(0).atom_size());

            TestAtomReported atom = data.bucket_info(0).atom(0).test_atom_reported();
            EXPECT_THAT(atom.repeated_int_field(), ElementsAreArray({3}));
            EXPECT_THAT(atom.repeated_long_field(), ElementsAreArray({10002L}));
            EXPECT_THAT(atom.repeated_float_field(), ElementsAreArray(floatArray));
            EXPECT_THAT(atom.repeated_string_field(), ElementsAreArray({"str1"}));
            EXPECT_THAT(atom.repeated_boolean_field(), ElementsAreArray({0}));
            EXPECT_THAT(atom.repeated_enum_field(), ElementsAreArray(enumArray));

            atom = data.bucket_info(0).atom(1).test_atom_reported();
            EXPECT_EQ(atom.repeated_int_field_size(), 0);
            EXPECT_EQ(atom.repeated_long_field_size(), 0);
            EXPECT_EQ(atom.repeated_float_field_size(), 0);
            EXPECT_EQ(atom.repeated_string_field_size(), 0);
            EXPECT_EQ(atom.repeated_boolean_field_size(), 0);
            EXPECT_EQ(atom.repeated_enum_field_size(), 0);
        } else {
            EXPECT_EQ(bucketStartTimeNs, data.bucket_info(0).start_bucket_elapsed_nanos());
            EXPECT_EQ(bucketStartTimeNs + bucketSizeNs,
                      data.bucket_info(0).end_bucket_elapsed_nanos());
            ASSERT_EQ(1, data.bucket_info(0).atom_size());

            TestAtomReported atom = data.bucket_info(0).atom(0).test_atom_reported();
            EXPECT_THAT(atom.repeated_int_field(), ElementsAreArray({3}));
            EXPECT_THAT(atom.repeated_long_field(), ElementsAreArray({10002L}));
            EXPECT_THAT(atom.repeated_float_field(), ElementsAreArray(floatArray));
            EXPECT_THAT(atom.repeated_string_field(), ElementsAreArray({"str1"}));
            EXPECT_THAT(atom.repeated_boolean_field(), ElementsAreArray({0}));
            EXPECT_THAT(atom.repeated_enum_field(), ElementsAreArray(enumArray));
        }
    }
}

TEST_F(GaugeMetricE2ePushedTest, TestDimensionalSampling) {
    ShardOffsetProvider::getInstance().setShardOffset(5);

    StatsdConfig config;

    AtomMatcher appCrashMatcher =
            CreateSimpleAtomMatcher("APP_CRASH_OCCURRED", util::APP_CRASH_OCCURRED);
    *config.add_atom_matcher() = appCrashMatcher;

    GaugeMetric sampledGaugeMetric =
            createGaugeMetric("GaugeSampledAppCrashesPerUid", appCrashMatcher.id(),
                              GaugeMetric::FIRST_N_SAMPLES, nullopt, nullopt);
    *sampledGaugeMetric.mutable_dimensions_in_what() =
            CreateDimensions(util::APP_CRASH_OCCURRED, {1 /* uid */});
    *sampledGaugeMetric.mutable_dimensional_sampling_info()->mutable_sampled_what_field() =
            CreateDimensions(util::APP_CRASH_OCCURRED, {1 /*uid*/});
    sampledGaugeMetric.mutable_dimensional_sampling_info()->set_shard_count(2);
    *config.add_gauge_metric() = sampledGaugeMetric;

    const int64_t configAddedTimeNs = 10 * NS_PER_SEC;  // 0:10
    const int64_t bucketSizeNs =
            TimeUnitToBucketSizeInMillis(config.gauge_metric(0).bucket()) * 1000LL * 1000LL;

    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);

    sp<StatsLogProcessor> processor = CreateStatsLogProcessor(
            configAddedTimeNs, configAddedTimeNs, config, cfgKey, nullptr, 0, new UidMap());

    int appUid1 = 1001;  // odd hash value
    int appUid2 = 1002;  // even hash value
    int appUid3 = 1003;  // odd hash value

    const int64_t gaugeEventTimeNs1 = configAddedTimeNs + 20 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs2 = configAddedTimeNs + 40 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs3 = configAddedTimeNs + 60 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs4 = configAddedTimeNs + 100 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs5 = configAddedTimeNs + 110 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs6 = configAddedTimeNs + 150 * NS_PER_SEC;

    std::vector<std::unique_ptr<LogEvent>> events;
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs1, appUid1));  // 0:30
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs2, appUid2));  // 0:50
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs3, appUid3));  // 1:10
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs4, appUid1));  // 1:50
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs5, appUid2));  // 2:00
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs6, appUid3));  // 2:40

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    ConfigMetricsReportList reports;
    vector<uint8_t> buffer;
    processor->onDumpReport(cfgKey, configAddedTimeNs + bucketSizeNs + 1, false, true, ADB_DUMP,
                            FAST, &buffer);
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillDimensionPath(&reports);
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    backfillAggregatedAtoms(&reports);

    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    EXPECT_TRUE(reports.reports(0).metrics(0).has_gauge_metrics());
    StatsLogReport::GaugeMetricDataWrapper gaugeMetrics;
    sortMetricDataByDimensionsValue(reports.reports(0).metrics(0).gauge_metrics(), &gaugeMetrics);
    ASSERT_EQ(2, gaugeMetrics.data_size());

    // Only Uid 1 and 3 are logged. (odd hash value) + (offset of 5) % (shard count of 2) = 0
    GaugeMetricData data = gaugeMetrics.data(0);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, appUid1);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {gaugeEventTimeNs1, gaugeEventTimeNs4});

    data = gaugeMetrics.data(1);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, appUid3);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {gaugeEventTimeNs3, gaugeEventTimeNs6});
}

TEST_F(GaugeMetricE2ePushedTest, TestPushedGaugeMetricSampling) {
    // Initiating StatsdStats at the start of this test, so it doesn't call rand() during the test.
    StatsdStats::getInstance();
    // Set srand seed to make rand deterministic for testing.
    srand(0);

    StatsdConfig config;
    config.add_allowed_log_source("AID_ROOT");  // LogEvent defaults to UID of root.

    AtomMatcher appCrashMatcher =
            CreateSimpleAtomMatcher("APP_CRASH_OCCURRED", util::APP_CRASH_OCCURRED);
    *config.add_atom_matcher() = appCrashMatcher;

    GaugeMetric sampledGaugeMetric =
            createGaugeMetric("GaugeSampledAppCrashesPerUid", appCrashMatcher.id(),
                              GaugeMetric::FIRST_N_SAMPLES, nullopt, nullopt);
    *sampledGaugeMetric.mutable_dimensions_in_what() =
            CreateDimensions(util::APP_CRASH_OCCURRED, {1 /* uid */});
    sampledGaugeMetric.set_sampling_percentage(50);
    *config.add_gauge_metric() = sampledGaugeMetric;

    const int64_t configAddedTimeNs = 10 * NS_PER_SEC;  // 0:10
    const int64_t bucketSizeNs =
            TimeUnitToBucketSizeInMillis(config.gauge_metric(0).bucket()) * 1000LL * 1000LL;

    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);

    sp<StatsLogProcessor> processor = CreateStatsLogProcessor(
            configAddedTimeNs, configAddedTimeNs, config, cfgKey, nullptr, 0, new UidMap());

    std::vector<std::unique_ptr<LogEvent>> events;
    for (int i = 0; i < 10; i++) {
        events.push_back(
                CreateAppCrashOccurredEvent(configAddedTimeNs + (10 * i * NS_PER_SEC), 1000 + i));
    }

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    ConfigMetricsReportList reports;
    vector<uint8_t> buffer;
    processor->onDumpReport(cfgKey, configAddedTimeNs + bucketSizeNs + 1, false, true, ADB_DUMP,
                            FAST, &buffer);
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillDimensionPath(&reports);
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    backfillAggregatedAtoms(&reports);

    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    EXPECT_TRUE(reports.reports(0).metrics(0).has_gauge_metrics());
    StatsLogReport::GaugeMetricDataWrapper gaugeMetrics;
    sortMetricDataByDimensionsValue(reports.reports(0).metrics(0).gauge_metrics(), &gaugeMetrics);
    ASSERT_EQ(5, gaugeMetrics.data_size());

    GaugeMetricData data = gaugeMetrics.data(0);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1000);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs, {configAddedTimeNs});

    data = gaugeMetrics.data(1);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1002);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {configAddedTimeNs + (10 * 2 * NS_PER_SEC)});

    data = gaugeMetrics.data(2);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1003);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {configAddedTimeNs + (10 * 3 * NS_PER_SEC)});

    data = gaugeMetrics.data(3);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1007);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {configAddedTimeNs + (10 * 7 * NS_PER_SEC)});

    data = gaugeMetrics.data(4);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1009);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {configAddedTimeNs + (10 * 9 * NS_PER_SEC)});
}

TEST_F(GaugeMetricE2ePushedTest, TestPushedGaugeMetricSamplingWithDimensionalSampling) {
    ShardOffsetProvider::getInstance().setShardOffset(5);
    // Initiating StatsdStats at the start of this test, so it doesn't call rand() during the test.
    StatsdStats::getInstance();
    // Set srand seed to make rand deterministic for testing.
    srand(0);

    StatsdConfig config;
    config.add_allowed_log_source("AID_ROOT");  // LogEvent defaults to UID of root.

    AtomMatcher appCrashMatcher =
            CreateSimpleAtomMatcher("APP_CRASH_OCCURRED", util::APP_CRASH_OCCURRED);
    *config.add_atom_matcher() = appCrashMatcher;

    GaugeMetric sampledGaugeMetric =
            createGaugeMetric("GaugeSampledAppCrashesPerUid", appCrashMatcher.id(),
                              GaugeMetric::FIRST_N_SAMPLES, nullopt, nullopt);
    *sampledGaugeMetric.mutable_dimensions_in_what() =
            CreateDimensions(util::APP_CRASH_OCCURRED, {1 /* uid */});
    *sampledGaugeMetric.mutable_dimensional_sampling_info()->mutable_sampled_what_field() =
            CreateDimensions(util::APP_CRASH_OCCURRED, {1 /*uid*/});
    sampledGaugeMetric.mutable_dimensional_sampling_info()->set_shard_count(2);
    sampledGaugeMetric.set_sampling_percentage(50);
    *config.add_gauge_metric() = sampledGaugeMetric;

    const int64_t configAddedTimeNs = 10 * NS_PER_SEC;  // 0:10
    const int64_t bucketSizeNs =
            TimeUnitToBucketSizeInMillis(config.gauge_metric(0).bucket()) * 1000LL * 1000LL;

    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);

    sp<StatsLogProcessor> processor = CreateStatsLogProcessor(
            configAddedTimeNs, configAddedTimeNs, config, cfgKey, nullptr, 0, new UidMap());

    std::vector<std::unique_ptr<LogEvent>> events;
    for (int i = 0; i < 30; i++) {
        // Generate events with three different app uids: 1001, 1002, 1003.
        events.push_back(CreateAppCrashOccurredEvent(configAddedTimeNs + (10 * i * NS_PER_SEC),
                                                     1001 + (i % 3)));
    }

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    ConfigMetricsReportList reports;
    vector<uint8_t> buffer;
    processor->onDumpReport(cfgKey, configAddedTimeNs + bucketSizeNs + 1, false, true, ADB_DUMP,
                            FAST, &buffer);
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillDimensionPath(&reports);
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    backfillAggregatedAtoms(&reports);

    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    EXPECT_TRUE(reports.reports(0).metrics(0).has_gauge_metrics());
    StatsLogReport::GaugeMetricDataWrapper gaugeMetrics;
    sortMetricDataByDimensionsValue(reports.reports(0).metrics(0).gauge_metrics(), &gaugeMetrics);
    ASSERT_EQ(2, gaugeMetrics.data_size());

    // Only Uid 1 and 3 are logged. (odd hash value) + (offset of 5) % (shard count of 2) = 0
    GaugeMetricData data = gaugeMetrics.data(0);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1001);
    ValidateGaugeBucketTimes(
            data.bucket_info(0), configAddedTimeNs, configAddedTimeNs + bucketSizeNs,
            {10 * NS_PER_SEC, 40 * NS_PER_SEC, 220 * NS_PER_SEC, 280 * NS_PER_SEC});

    data = gaugeMetrics.data(1);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1003);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {60 * NS_PER_SEC, 120 * NS_PER_SEC, 150 * NS_PER_SEC, 180 * NS_PER_SEC,
                              210 * NS_PER_SEC, 300 * NS_PER_SEC});
}

TEST_F(GaugeMetricE2ePushedTest, TestPushedGaugeMetricSliceByStates) {
    StatsdConfig config;
    config.add_allowed_log_source("AID_ROOT");  // LogEvent defaults to UID of root.

    auto syncStartMatcher = CreateSyncStartAtomMatcher();
    *config.add_atom_matcher() = syncStartMatcher;

    auto state = CreateScreenState();
    *config.add_state() = state;

    // Create gauge metric that slices by screen state.
    GaugeMetric syncStateGaugeMetric =
            createGaugeMetric("GaugeSyncState", syncStartMatcher.id(), GaugeMetric::FIRST_N_SAMPLES,
                              nullopt, nullopt, {state.id()});
    syncStateGaugeMetric.set_max_num_gauge_atoms_per_bucket(2);
    *config.add_gauge_metric() = syncStateGaugeMetric;

    const int64_t configAddedTimeNs = 10 * NS_PER_SEC;  // 0:10
    const int64_t bucketSizeNs =
            TimeUnitToBucketSizeInMillis(config.gauge_metric(0).bucket()) * 1000LL * 1000LL;

    // Initialize StatsLogProcessor.
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);

    sp<StatsLogProcessor> processor = CreateStatsLogProcessor(
            configAddedTimeNs, configAddedTimeNs, config, cfgKey, nullptr, 0, new UidMap());

    // Initialize log events.
    std::vector<int> attributionUids1 = {123};
    std::vector<string> attributionTags1 = {"App1"};

    const int64_t gaugeEventTimeNs1 = configAddedTimeNs + 50 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs2 = configAddedTimeNs + 75 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs3 = configAddedTimeNs + 150 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs4 = configAddedTimeNs + 180 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs5 = configAddedTimeNs + 200 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs6 = configAddedTimeNs + 250 * NS_PER_SEC;

    std::vector<std::unique_ptr<LogEvent>> events;
    events.push_back(CreateScreenStateChangedEvent(
            gaugeEventTimeNs1,
            android::view::DisplayStateEnum::DISPLAY_STATE_ON));  // 1:00
    events.push_back(CreateSyncStartEvent(gaugeEventTimeNs2, attributionUids1, attributionTags1,
                                          "sync_name"));  // 1:25
    events.push_back(CreateSyncStartEvent(gaugeEventTimeNs3, attributionUids1, attributionTags1,
                                          "sync_name"));  // 2:40
    // Not logged since max gauge atoms have been reached
    events.push_back(CreateSyncStartEvent(gaugeEventTimeNs4, attributionUids1, attributionTags1,
                                          "sync_name"));  // 3:10
    events.push_back(CreateScreenStateChangedEvent(
            gaugeEventTimeNs5,
            android::view::DisplayStateEnum::DISPLAY_STATE_OFF));  // 3:30
    events.push_back(CreateSyncStartEvent(gaugeEventTimeNs6, attributionUids1, attributionTags1,
                                          "sync_name"));  // 4:20

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    // Check dump report.
    uint64_t dumpTimeNs = configAddedTimeNs + bucketSizeNs;
    ConfigMetricsReportList reports;
    vector<uint8_t> buffer;
    processor->onDumpReport(cfgKey, dumpTimeNs, true, true, ADB_DUMP, FAST, &buffer);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    backfillAggregatedAtoms(&reports);
    ASSERT_EQ(reports.reports_size(), 1);

    ConfigMetricsReport report = reports.reports(0);
    ASSERT_EQ(report.metrics_size(), 1);
    StatsLogReport metricReport = report.metrics(0);
    EXPECT_EQ(metricReport.metric_id(), syncStateGaugeMetric.id());
    EXPECT_TRUE(metricReport.has_gauge_metrics());
    ASSERT_EQ(metricReport.gauge_metrics().data_size(), 2);

    // For each GaugeMetricData, check StateValue info is correct
    GaugeMetricData data = metricReport.gauge_metrics().data(0);

    ASSERT_EQ(data.slice_by_state_size(), 1);
    EXPECT_EQ(data.slice_by_state(0).atom_id(), SCREEN_STATE_ATOM_ID);
    EXPECT_EQ(data.slice_by_state(0).value(), android::view::DisplayStateEnum::DISPLAY_STATE_ON);
    ASSERT_EQ(data.bucket_info(0).atom_size(), 2);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {gaugeEventTimeNs2, gaugeEventTimeNs3});

    data = metricReport.gauge_metrics().data(1);
    ASSERT_EQ(data.slice_by_state_size(), 1);
    EXPECT_EQ(data.slice_by_state(0).atom_id(), SCREEN_STATE_ATOM_ID);
    EXPECT_EQ(data.slice_by_state(0).value(), android::view::DisplayStateEnum::DISPLAY_STATE_OFF);
    ASSERT_EQ(data.bucket_info(0).atom_size(), 1);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs, {gaugeEventTimeNs6});
}

TEST_F(GaugeMetricE2ePushedTest, TestSlicedStateWithMap) {
    StatsdConfig config;

    auto syncStartMatcher = CreateSyncStartAtomMatcher();
    *config.add_atom_matcher() = syncStartMatcher;

    int64_t screenOnId = 4444;
    int64_t screenOffId = 9876;
    auto state = CreateScreenStateWithOnOffMap(screenOnId, screenOffId);
    *config.add_state() = state;

    // Create gauge metric that slices by screen state with on/off map.
    GaugeMetric syncStateGaugeMetric =
            createGaugeMetric("GaugeSyncStart", syncStartMatcher.id(), GaugeMetric::FIRST_N_SAMPLES,
                              nullopt, nullopt, {state.id()});
    *config.add_gauge_metric() = syncStateGaugeMetric;

    const int64_t configAddedTimeNs = 10 * NS_PER_SEC;  // 0:10
    const int64_t bucketSizeNs =
            TimeUnitToBucketSizeInMillis(config.gauge_metric(0).bucket()) * 1000LL * 1000LL;

    // Initialize StatsLogProcessor.
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);

    sp<StatsLogProcessor> processor = CreateStatsLogProcessor(
            configAddedTimeNs, configAddedTimeNs, config, cfgKey, nullptr, 0, new UidMap());

    /*
    |     1     2     3     4(minutes)
    |-----------------------|-
      x   x     x       x     (syncStartEvents)
     -------------------------SCREEN_OFF events
             |                (ScreenStateOffEvent = 1)
         |                    (ScreenStateDozeEvent = 3)
     -------------------------SCREEN_ON events
       |                      (ScreenStateOnEvent = 2)
                      |       (ScreenStateVrEvent = 5)

    Based on the diagram above, a Sync Start Event querying for Screen State would return:
    - Event 0: StateTracker::kStateUnknown
    - Event 1: Off
    - Event 2: Off
    - Event 3: On
    */

    const int64_t gaugeEventTimeNs1 = configAddedTimeNs + 20 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs2 = configAddedTimeNs + 30 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs3 = configAddedTimeNs + 50 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs4 = configAddedTimeNs + 60 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs5 = configAddedTimeNs + 90 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs6 = configAddedTimeNs + 120 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs7 = configAddedTimeNs + 180 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs8 = configAddedTimeNs + 200 * NS_PER_SEC;
    // Initialize log events
    std::vector<int> attributionUids1 = {123};
    std::vector<string> attributionTags1 = {"App1"};

    std::vector<std::unique_ptr<LogEvent>> events;
    events.push_back(CreateSyncStartEvent(gaugeEventTimeNs1, attributionUids1, attributionTags1,
                                          "sync_name"));  // 0:30
    // Event 0 Occurred
    events.push_back(CreateScreenStateChangedEvent(
            gaugeEventTimeNs2,
            android::view::DisplayStateEnum::DISPLAY_STATE_ON));  // 0:40
    events.push_back(CreateScreenStateChangedEvent(
            gaugeEventTimeNs3,
            android::view::DisplayStateEnum::DISPLAY_STATE_DOZE));  // 1:00
    events.push_back(CreateSyncStartEvent(gaugeEventTimeNs4, attributionUids1, attributionTags1,
                                          "sync_name"));  // 1:10
    // Event 1 Occurred
    events.push_back(CreateScreenStateChangedEvent(
            gaugeEventTimeNs5,
            android::view::DisplayStateEnum::DISPLAY_STATE_OFF));  // 1:40
    events.push_back(CreateSyncStartEvent(gaugeEventTimeNs6, attributionUids1, attributionTags1,
                                          "sync_name"));  // 2:10
    // Event 2 Occurred
    events.push_back(CreateScreenStateChangedEvent(
            gaugeEventTimeNs7,
            android::view::DisplayStateEnum::DISPLAY_STATE_VR));  // 3:10
    events.push_back(CreateSyncStartEvent(gaugeEventTimeNs8, attributionUids1, attributionTags1,
                                          "sync_name"));  // 3:30
    // Event 3 Occurred

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    // Check dump report.
    uint64_t dumpTimeNs = configAddedTimeNs + bucketSizeNs;
    vector<uint8_t> buffer;
    ConfigMetricsReportList reports;
    processor->onDumpReport(cfgKey, dumpTimeNs, false, true, ADB_DUMP, FAST, &buffer);
    ASSERT_GT(buffer.size(), 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    backfillAggregatedAtoms(&reports);
    ASSERT_EQ(reports.reports_size(), 1);

    ConfigMetricsReport report = reports.reports(0);
    ASSERT_EQ(report.metrics_size(), 1);
    StatsLogReport metricReport = report.metrics(0);
    EXPECT_EQ(metricReport.metric_id(), syncStateGaugeMetric.id());
    EXPECT_TRUE(metricReport.has_gauge_metrics());
    ASSERT_EQ(metricReport.gauge_metrics().data_size(), 3);

    // For each GaugeMetricData, check StateValue info is correct
    GaugeMetricData data = metricReport.gauge_metrics().data(0);

    // StateTracker::kStateUnknown
    ASSERT_EQ(data.slice_by_state_size(), 1);
    EXPECT_EQ(data.slice_by_state(0).atom_id(), SCREEN_STATE_ATOM_ID);
    EXPECT_EQ(data.slice_by_state(0).value(), -1 /* StateTracker::kStateUnknown */);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs, {gaugeEventTimeNs1});

    // Off
    data = metricReport.gauge_metrics().data(1);
    ASSERT_EQ(data.slice_by_state_size(), 1);
    EXPECT_EQ(data.slice_by_state(0).atom_id(), SCREEN_STATE_ATOM_ID);
    EXPECT_EQ(data.slice_by_state(0).group_id(), screenOffId);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {gaugeEventTimeNs4, gaugeEventTimeNs6});

    // On
    data = metricReport.gauge_metrics().data(2);
    ASSERT_EQ(data.slice_by_state_size(), 1);
    EXPECT_EQ(data.slice_by_state(0).atom_id(), SCREEN_STATE_ATOM_ID);
    EXPECT_EQ(data.slice_by_state(0).group_id(), screenOnId);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs, {gaugeEventTimeNs8});
}

TEST_F(GaugeMetricE2ePushedTest, TestSlicedStateWithPrimaryFields) {
    StatsdConfig config;

    auto appCrashMatcher = CreateSimpleAtomMatcher("APP_CRASH_OCCURRED", util::APP_CRASH_OCCURRED);
    *config.add_atom_matcher() = appCrashMatcher;

    auto state = CreateUidProcessState();
    *config.add_state() = state;

    // Create gauge metric that slices by uid process state.
    GaugeMetric appCrashGaugeMetric =
            createGaugeMetric("AppCrashReported", appCrashMatcher.id(),
                              GaugeMetric::FIRST_N_SAMPLES, nullopt, nullopt, {state.id()});
    *appCrashGaugeMetric.mutable_dimensions_in_what() =
            CreateDimensions(util::APP_CRASH_OCCURRED, {1 /* uid */});
    MetricStateLink* stateLink = appCrashGaugeMetric.add_state_link();
    stateLink->set_state_atom_id(UID_PROCESS_STATE_ATOM_ID);
    auto fieldsInWhat = stateLink->mutable_fields_in_what();
    *fieldsInWhat = CreateDimensions(util::APP_CRASH_OCCURRED, {1 /*uid*/});
    auto fieldsInState = stateLink->mutable_fields_in_state();
    *fieldsInState = CreateDimensions(UID_PROCESS_STATE_ATOM_ID, {1 /*uid*/});
    *config.add_gauge_metric() = appCrashGaugeMetric;

    const int64_t configAddedTimeNs = 10 * NS_PER_SEC;  // 0:10
    const int64_t bucketSizeNs =
            TimeUnitToBucketSizeInMillis(config.gauge_metric(0).bucket()) * 1000LL * 1000LL;

    // Initialize StatsLogProcessor.
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);

    sp<StatsLogProcessor> processor = CreateStatsLogProcessor(
            configAddedTimeNs, configAddedTimeNs, config, cfgKey, nullptr, 0, new UidMap());

    /*
    NOTE: "1" or "2" represents the uid associated with the state/app crash event
    |    1    2    3
    |--------------|-
      1   1       2 1(AppCrashEvents)
     ----------------PROCESS STATE events
            2        (TopEvent = 1002)
       1             (ImportantForegroundEvent = 1005)

    Based on the diagram above, an AppCrashEvent querying for process state value would return:
    - Event 0: StateTracker::kStateUnknown
    - Event 1: Important Foreground
    - Event 2: Top
    - Event 3: Important Foreground
    */

    const int64_t gaugeEventTimeNs1 = configAddedTimeNs + 20 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs2 = configAddedTimeNs + 30 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs3 = configAddedTimeNs + 60 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs4 = configAddedTimeNs + 90 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs5 = configAddedTimeNs + 160 * NS_PER_SEC;
    const int64_t gaugeEventTimeNs6 = configAddedTimeNs + 180 * NS_PER_SEC;
    // Initialize log events
    std::vector<std::unique_ptr<LogEvent>> events;
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs1, 1 /*uid*/));  // 0:30
    // Event 0 Occurred
    events.push_back(CreateUidProcessStateChangedEvent(
            gaugeEventTimeNs2, 1 /*uid*/,
            android::app::ProcessStateEnum::PROCESS_STATE_IMPORTANT_FOREGROUND));  // 0:40
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs3, 1 /*uid*/));   // 1:10
    // Event 1 Occurred
    events.push_back(CreateUidProcessStateChangedEvent(
            gaugeEventTimeNs4, 2 /*uid*/,
            android::app::ProcessStateEnum::PROCESS_STATE_TOP));                  // 1:40
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs5, 2 /*uid*/));  // 2:50
    // Event 2 Occurred
    events.push_back(CreateAppCrashOccurredEvent(gaugeEventTimeNs6, 1 /*uid*/));  // 3:10
    // Event 3 Occurred

    // Send log events to StatsLogProcessor.
    for (auto& event : events) {
        processor->OnLogEvent(event.get());
    }

    // Check dump report.
    uint64_t dumpTimeNs = configAddedTimeNs + bucketSizeNs;
    vector<uint8_t> buffer;
    ConfigMetricsReportList reports;
    processor->onDumpReport(cfgKey, dumpTimeNs, false, true, ADB_DUMP, FAST, &buffer);
    ASSERT_GT(buffer.size(), 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillDimensionPath(&reports);
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    backfillAggregatedAtoms(&reports);
    ASSERT_EQ(reports.reports_size(), 1);

    ConfigMetricsReport report = reports.reports(0);
    ASSERT_EQ(report.metrics_size(), 1);
    StatsLogReport metricReport = report.metrics(0);
    EXPECT_EQ(metricReport.metric_id(), appCrashGaugeMetric.id());
    EXPECT_TRUE(metricReport.has_gauge_metrics());
    ASSERT_EQ(metricReport.gauge_metrics().data_size(), 3);

    // For each GaugeMetricData, check StateValue info is correct
    GaugeMetricData data = metricReport.gauge_metrics().data(0);

    // StateTracker::kStateUnknown
    ASSERT_EQ(data.slice_by_state_size(), 1);
    EXPECT_EQ(data.slice_by_state(0).atom_id(), UID_PROCESS_STATE_ATOM_ID);
    EXPECT_EQ(data.slice_by_state(0).value(), -1 /* StateTracker::kStateUnknown */);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1 /* uid */);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs, {gaugeEventTimeNs1});

    // Important Foreground
    data = metricReport.gauge_metrics().data(1);
    ASSERT_EQ(data.slice_by_state_size(), 1);
    EXPECT_EQ(data.slice_by_state(0).atom_id(), UID_PROCESS_STATE_ATOM_ID);
    EXPECT_EQ(data.slice_by_state(0).value(), android::app::PROCESS_STATE_IMPORTANT_FOREGROUND);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 1 /* uid */);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs,
                             {gaugeEventTimeNs3, gaugeEventTimeNs6});

    // Top
    data = metricReport.gauge_metrics().data(2);
    ASSERT_EQ(data.slice_by_state_size(), 1);
    EXPECT_EQ(data.slice_by_state(0).atom_id(), UID_PROCESS_STATE_ATOM_ID);
    EXPECT_EQ(data.slice_by_state(0).value(), android::app::PROCESS_STATE_TOP);
    ValidateUidDimension(data.dimensions_in_what(), util::APP_CRASH_OCCURRED, 2 /* uid */);
    ValidateGaugeBucketTimes(data.bucket_info(0), configAddedTimeNs,
                             configAddedTimeNs + bucketSizeNs, {gaugeEventTimeNs5});
}

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif

}  // namespace statsd
}  // namespace os
}  // namespace android
