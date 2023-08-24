// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License

#include <gtest/gtest.h>

#include "common/Types.h"
#include "knowhere/comp/index_param.h"
#include "segcore/SegmentGrowing.h"
#include "segcore/SegmentGrowingImpl.h"
#include "pb/schema.pb.h"
#include "test_utils/DataGen.h"

using namespace milvus::segcore;
using namespace milvus;
namespace pb = milvus::proto;

TEST(Growing, DeleteCount) {
    auto schema = std::make_shared<Schema>();
    auto pk = schema->AddDebugField("pk", DataType::INT64);
    schema->set_primary_field_id(pk);
    auto segment = CreateGrowingSegment(schema, empty_index_meta);

    int64_t c = 10;
    auto offset = 0;

    Timestamp begin_ts = 100;
    auto tss = GenTss(c, begin_ts);
    auto pks = GenPKs(c, 0);
    auto status = segment->Delete(offset, c, pks.get(), tss.data());
    ASSERT_TRUE(status.ok());

    auto cnt = segment->get_deleted_count();
    ASSERT_EQ(cnt, c);
}

TEST(Growing, RealCount) {
    auto schema = std::make_shared<Schema>();
    auto pk = schema->AddDebugField("pk", DataType::INT64);
    schema->set_primary_field_id(pk);
    auto segment = CreateGrowingSegment(schema, empty_index_meta);

    int64_t c = 10;
    auto offset = 0;
    auto dataset = DataGen(schema, c);
    auto pks = dataset.get_col<int64_t>(pk);
    segment->Insert(offset,
                    c,
                    dataset.row_ids_.data(),
                    dataset.timestamps_.data(),
                    dataset.raw_);

    // no delete.
    ASSERT_EQ(c, segment->get_real_count());

    // delete half.
    auto half = c / 2;
    auto del_offset1 = 0;
    auto del_ids1 = GenPKs(pks.begin(), pks.begin() + half);
    auto del_tss1 = GenTss(half, c);
    auto status =
        segment->Delete(del_offset1, half, del_ids1.get(), del_tss1.data());
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(c - half, segment->get_real_count());

    // delete duplicate.
    auto del_offset2 = segment->get_deleted_count();
    ASSERT_EQ(del_offset2, half);
    auto del_tss2 = GenTss(half, c + half);
    status =
        segment->Delete(del_offset2, half, del_ids1.get(), del_tss2.data());
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(c - half, segment->get_real_count());

    // delete all.
    auto del_offset3 = segment->get_deleted_count();
    ASSERT_EQ(del_offset3, half * 2);
    auto del_ids3 = GenPKs(pks.begin(), pks.end());
    auto del_tss3 = GenTss(c, c + half * 2);
    status = segment->Delete(del_offset3, c, del_ids3.get(), del_tss3.data());
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(0, segment->get_real_count());
}

TEST(Growing, FillData) {
    auto schema = std::make_shared<Schema>();
    auto metric_type = knowhere::metric::L2;
    auto int8_field = schema->AddDebugField("int8", DataType::INT8);
    auto int16_field = schema->AddDebugField("int16", DataType::INT16);
    auto int32_field = schema->AddDebugField("int32", DataType::INT32);
    auto int64_field = schema->AddDebugField("int64", DataType::INT64);
    auto float_field = schema->AddDebugField("float", DataType::FLOAT);
    auto double_field = schema->AddDebugField("double", DataType::DOUBLE);
    auto varchar_field = schema->AddDebugField("varchar", DataType::VARCHAR);
    auto json_field = schema->AddDebugField("json", DataType::JSON);
    auto vec = schema->AddDebugField(
        "embeddings", DataType::VECTOR_FLOAT, 128, metric_type);
    schema->set_primary_field_id(int64_field);

    std::map<std::string, std::string> index_params = {
        {"index_type", "IVF_FLAT"},
        {"metric_type", metric_type},
        {"nlist", "128"}};
    std::map<std::string, std::string> type_params = {{"dim", "128"}};
    FieldIndexMeta fieldIndexMeta(
        vec, std::move(index_params), std::move(type_params));
    auto& config = SegcoreConfig::default_config();
    config.set_chunk_rows(1024);
    config.set_enable_growing_segment_index(true);
    std::map<FieldId, FieldIndexMeta> filedMap = {{vec, fieldIndexMeta}};
    IndexMetaPtr metaPtr =
        std::make_shared<CollectionIndexMeta>(100000, std::move(filedMap));
    auto segment_growing = CreateGrowingSegment(schema, metaPtr);
    auto segment = dynamic_cast<SegmentGrowingImpl*>(segment_growing.get());

    int64_t per_batch = 1000;
    int64_t n_batch = 3;
    int64_t dim = 128;
    for (int64_t i = 0; i < n_batch; i++) {
        auto dataset = DataGen(schema, per_batch);
        auto int8_values = dataset.get_col<int8_t>(int8_field);
        auto int16_values = dataset.get_col<int16_t>(int16_field);
        auto int32_values = dataset.get_col<int32_t>(int32_field);
        auto int64_values = dataset.get_col<int64_t>(int64_field);
        auto float_values = dataset.get_col<float>(float_field);
        auto double_values = dataset.get_col<double>(double_field);
        auto varchar_values = dataset.get_col<std::string>(varchar_field);
        auto json_values = dataset.get_col<std::string>(json_field);
        auto vector_values = dataset.get_col<float>(vec);

        auto offset = segment->PreInsert(per_batch);
        segment->Insert(offset,
                        per_batch,
                        dataset.row_ids_.data(),
                        dataset.timestamps_.data(),
                        dataset.raw_);
        auto num_inserted = (i + 1) * per_batch;
        auto ids_ds = GenRandomIds(num_inserted);
        auto int8_result =
            segment->bulk_subscript(int8_field, ids_ds->GetIds(), num_inserted);
        auto int16_result = segment->bulk_subscript(
            int16_field, ids_ds->GetIds(), num_inserted);
        auto int32_result = segment->bulk_subscript(
            int32_field, ids_ds->GetIds(), num_inserted);
        auto int64_result = segment->bulk_subscript(
            int64_field, ids_ds->GetIds(), num_inserted);
        auto float_result = segment->bulk_subscript(
            float_field, ids_ds->GetIds(), num_inserted);
        auto double_result = segment->bulk_subscript(
            double_field, ids_ds->GetIds(), num_inserted);
        auto varchar_result = segment->bulk_subscript(
            varchar_field, ids_ds->GetIds(), num_inserted);
        auto json_result =
            segment->bulk_subscript(json_field, ids_ds->GetIds(), num_inserted);
        auto vec_result =
            segment->bulk_subscript(vec, ids_ds->GetIds(), num_inserted);

        EXPECT_EQ(int8_result->scalars().int_data().data_size(), num_inserted);
        EXPECT_EQ(int16_result->scalars().int_data().data_size(), num_inserted);
        EXPECT_EQ(int32_result->scalars().int_data().data_size(), num_inserted);
        EXPECT_EQ(int64_result->scalars().long_data().data_size(), num_inserted);
        EXPECT_EQ(float_result->scalars().float_data().data_size(),
                  num_inserted);
        EXPECT_EQ(double_result->scalars().double_data().data_size(),
                  num_inserted);
        EXPECT_EQ(varchar_result->scalars().string_data().data_size(),
                  num_inserted);
        EXPECT_EQ(json_result->scalars().json_data().data_size(), num_inserted);
        EXPECT_EQ(vec_result->vectors().float_vector().data_size(),
                  num_inserted * dim);
    }
}
