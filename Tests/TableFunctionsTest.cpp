/*
 * Copyright 2019 OmniSci, Inc.
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

#include "TestHelpers.h"

#include <gtest/gtest.h>

#include "QueryEngine/ResultSet.h"
#include "QueryEngine/TableFunctions/TableFunctionManager.h"
#include "QueryRunner/QueryRunner.h"

#ifndef BASE_PATH
#define BASE_PATH "./tmp"
#endif

using QR = QueryRunner::QueryRunner;

extern bool g_enable_table_functions;
namespace {

inline void run_ddl_statement(const std::string& stmt) {
  QR::get()->runDDLStatement(stmt);
}

std::shared_ptr<ResultSet> run_multiple_agg(const std::string& query_str,
                                            const ExecutorDeviceType device_type) {
  // Following line left but commented out for easy debugging
  // std::cout << std::endl << "Query: " << query_str << std::endl;
  return QR::get()->runSQL(query_str, device_type, false, false);
}

}  // namespace

bool skip_tests(const ExecutorDeviceType device_type) {
#ifdef HAVE_CUDA
  return device_type == ExecutorDeviceType::GPU && !(QR::get()->gpusPresent());
#else
  return device_type == ExecutorDeviceType::GPU;
#endif
}

#define SKIP_NO_GPU()                                        \
  if (skip_tests(dt)) {                                      \
    CHECK(dt == ExecutorDeviceType::GPU);                    \
    LOG(WARNING) << "GPU not available, skipping GPU tests"; \
    continue;                                                \
  }

class TableFunctions : public ::testing::Test {
  void SetUp() override {
    {
      run_ddl_statement("DROP TABLE IF EXISTS tf_test;");
      run_ddl_statement(
          "CREATE TABLE tf_test (x INT, x2 INT, f FLOAT, d DOUBLE, d2 DOUBLE) WITH "
          "(FRAGMENT_SIZE=2);");

      TestHelpers::ValuesGenerator gen("tf_test");

      for (int i = 0; i < 5; i++) {
        const auto insert_query = gen(i, 5 - i, i * 1.1, i * 1.1, 1.0 - i * 2.2);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS tf_test2;");
      run_ddl_statement(
          "CREATE TABLE tf_test2 (x2 INT, d2 INT) WITH "
          "(FRAGMENT_SIZE=2);");

      TestHelpers::ValuesGenerator gen("tf_test2");

      for (int i = 0; i < 5; i++) {
        const auto insert_query = gen(i, i * i);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS sd_test;");
      run_ddl_statement(
          "CREATE TABLE sd_test ("
          "   base TEXT ENCODING DICT(32),"
          "   derived TEXT,"
          "   SHARED DICTIONARY (derived) REFERENCES sd_test(base)"
          ");");

      TestHelpers::ValuesGenerator gen("sd_test");
      std::vector<std::pair<std::string, std::string>> v = {{"'hello'", "'world'"},
                                                            {"'foo'", "'bar'"},
                                                            {"'bar'", "'baz'"},
                                                            {"'world'", "'foo'"},
                                                            {"'baz'", "'hello'"}};

      for (const auto& p : v) {
        const auto insert_query = gen(p.first, p.second);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS err_test;");
      run_ddl_statement(
          "CREATE TABLE err_test (x INT, y BIGINT, f FLOAT, d DOUBLE, x2 INT) WITH "
          "(FRAGMENT_SIZE=2);");

      TestHelpers::ValuesGenerator gen("err_test");

      for (int i = 0; i < 5; i++) {
        const auto insert_query = gen(std::numeric_limits<int32_t>::max() - 1,
                                      std::numeric_limits<int64_t>::max() - 1,
                                      std::numeric_limits<float>::max() - 1.0,
                                      std::numeric_limits<double>::max() - 1.0,
                                      i);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
  }

  void TearDown() override {
    run_ddl_statement("DROP TABLE IF EXISTS tf_test;");
    run_ddl_statement("DROP TABLE IF EXISTS sd_test;");
    run_ddl_statement("DROP TABLE IF EXISTS err_test");
  }
};

TEST_F(TableFunctions, BasicProjection) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), 0)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(0));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), 1)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), 2)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), 3)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(15));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), 4)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(20));
    }
    if (dt == ExecutorDeviceType::CPU) {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d FROM tf_test), 0)) ORDER "
          "BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(0));
    }
    if (dt == ExecutorDeviceType::CPU) {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d FROM tf_test), 1)) ORDER "
          "BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_adder(1, cursor(SELECT d, d2 FROM tf_test)));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_adder(4, cursor(SELECT d, d2 FROM tf_test)));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(20));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, out1 FROM TABLE(row_addsub(1, cursor(SELECT d, d2 FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Omit sizer (kRowMultiplier)
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_adder(cursor(SELECT d, d2 FROM tf_test)));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test))) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Constant (kConstant) size tests with get_max_with_row_offset
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(get_max_with_row_offset(cursor(SELECT x FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]),
                static_cast<int64_t>(4));  // max value of x
    }
    {
      // swap output column order
      const auto rows = run_multiple_agg(
          "SELECT out1, out0 FROM TABLE(get_max_with_row_offset(cursor(SELECT x FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]),
                static_cast<int64_t>(4));  // row offset of max x
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]),
                static_cast<int64_t>(4));  // max value of x
    }
    // Table Function specified sizer test
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(column_list_row_sum(cursor(SELECT x, x FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
    }
    // TextEncodingDict specific tests
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier_text(cursor(SELECT base FROM sd_test),"
          "1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"hello", "foo", "bar", "world", "baz"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
    {
      const auto rows = run_multiple_agg("SELECT base FROM sd_test;", dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"hello", "foo", "bar", "world", "baz"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier_text(cursor(SELECT derived FROM sd_test),"
          "1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"world", "bar", "baz", "foo", "hello"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }

    // Test boolean scalars AND return of less rows than allocated in table function
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(sort_column_limit(CURSOR(SELECT x FROM tf_test), 2, "
          "true, "
          "true)) ORDER by out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      std::vector<int64_t> expected_result_set{0, 1};
      for (size_t i = 0; i < 2; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(sort_column_limit(CURSOR(SELECT x FROM tf_test), 3, "
          "false, "
          "true)) ORDER by out0 DESC;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(3));
      std::vector<int64_t> expected_result_set{4, 3, 2};
      for (size_t i = 0; i < 3; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require(cursor(SELECT x"
          " FROM tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 3);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_templating(cursor(SELECT x"
          " FROM tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 5);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_templating(cursor(SELECT d"
          " FROM tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 6.0);
    }
    {
      if (dt == ExecutorDeviceType::GPU) {
        const auto rows = run_multiple_agg(
            "SELECT * FROM TABLE(ct_require_device_cuda(cursor(SELECT x"
            " FROM tf_test), 2));",
            dt);
        ASSERT_EQ(rows->rowCount(), size_t(1));
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, 12345);
      }
    }

    // Test for columns containing null values (QE-163)
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_test_nullable(cursor(SELECT x from tf_test), 1)) "
          "where out0 is not null;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      std::vector<int64_t> expected_result_set{1, 3};
      for (size_t i = 0; i < 2; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    // Tests various invalid returns from a table function:
    if (dt == ExecutorDeviceType::CPU) {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d FROM tf_test), -1));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(0));
    }

    if (dt == ExecutorDeviceType::CPU) {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d FROM tf_test), -2));",
              dt),
          std::runtime_error);
    }

    // TODO: enable the following tests after QE-50 is resolved:
    if (false && dt == ExecutorDeviceType::CPU) {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d FROM tf_test), -3));",
              dt),
          UserTableFunctionError);
    }

    if (false && dt == ExecutorDeviceType::CPU) {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d FROM tf_test), -4));",
              dt),
          UserTableFunctionError);
    }

    if (false && dt == ExecutorDeviceType::CPU) {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d FROM tf_test), -5));",
              dt),
          TableFunctionError);
    }
  }
}

TEST_F(TableFunctions, GroupByIn) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test GROUP BY d), "
          "1)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test GROUP BY d), "
          "2)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test GROUP BY d), "
          "3)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(15));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d FROM tf_test GROUP BY d), "
          "4)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(20));
    }
  }
}

TEST_F(TableFunctions, GroupByInAndOut) {
  auto check_result = [](const auto rows, const size_t copies) {
    ASSERT_EQ(rows->rowCount(), size_t(5));
    for (size_t i = 0; i < 5; i++) {
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), static_cast<int64_t>(copies));
    }
  };

  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, count(*) FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), "
          "1)) "
          "GROUP BY out0 ORDER BY out0;",
          dt);
      check_result(rows, 1);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, count(*) FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), "
          "2)) "
          "GROUP BY out0 ORDER BY out0;",
          dt);
      check_result(rows, 2);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, count(*) FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), "
          "3)) "
          "GROUP BY out0 ORDER BY out0;",
          dt);
      check_result(rows, 3);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, count(*) FROM TABLE(row_copier(cursor(SELECT d FROM tf_test), "
          "4)) "
          "GROUP BY out0 ORDER BY out0;",
          dt);
      check_result(rows, 4);
    }
    // TextEncodingDict specific tests
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier_text(cursor(SELECT base FROM sd_test),"
          "1)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"bar", "baz", "foo", "hello", "world"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier_text(cursor(SELECT derived FROM sd_test),"
          "1)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"bar", "baz", "foo", "hello", "world"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
  }
}

TEST_F(TableFunctions, ConstantCasts) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Numeric constant to float
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT f FROM "
          "tf_test), 2.2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Numeric constant to double
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT d FROM "
          "tf_test), 2.2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Integer constant to double
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT d FROM "
          "tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Numeric (integer) constant to double
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT d FROM "
          "tf_test), 2.));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Integer constant
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT x FROM "
          "tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Should throw: Numeric constant to integer
    {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT x FROM "
              "tf_test), 2.2));",
              dt),
          std::exception);
    }
    // Should throw: boolean constant to integer
    {
      EXPECT_THROW(run_multiple_agg(
                       "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT "
                       "x FROM tf_test), true));",
                       dt),
                   std::invalid_argument);
    }
  }
}

TEST_F(TableFunctions, Template) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT x FROM tf_test), "
          "cursor(SELECT d from tf_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(10));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT d FROM tf_test), "
          "cursor(SELECT d2 from tf_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(20));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT x FROM tf_test), "
          "cursor(SELECT x from tf_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(30));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT d FROM tf_test), "
          "cursor(SELECT x from tf_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(40));
    }
    // TextEncodingDict
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT base FROM sd_test),"
          "cursor(SELECT derived from sd_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"hello", "foo", "bar", "world", "baz"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
  }
}

TEST_F(TableFunctions, Unsupported) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();

    EXPECT_THROW(run_multiple_agg("select * from table(row_copier(cursor(SELECT d, "
                                  "cast(x as double) FROM tf_test), 2));",
                                  dt),
                 std::runtime_error);
  }
}

TEST_F(TableFunctions, CallFailure) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();

    EXPECT_THROW(run_multiple_agg("SELECT out0 FROM TABLE(row_copier(cursor("
                                  "SELECT d FROM tf_test),101));",
                                  dt),
                 UserTableFunctionError);

    // Skip this test for GPU. TODO: row_copier return value is ignored.
    break;
  }
}

TEST_F(TableFunctions, NamedOutput) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_output(cursor(SELECT d FROM tf_test)));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<double>(crt_row[0]), static_cast<double>(11));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_const_output(cursor(SELECT x FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(6));
      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(4));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_user_const_output(cursor(SELECT x FROM "
          "tf_test), 1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(10));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_user_const_output(cursor(SELECT x FROM "
          "tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(6));
      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(4));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_rowmul_output(cursor(SELECT x FROM "
          "tf_test), 1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_rowmul_output(cursor(SELECT x FROM "
          "tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));
    }
  }
}

TEST_F(TableFunctions, CursorlessInputs) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT answer FROM TABLE(ct_no_arg_constant_sizing()) ORDER BY answer;", dt);
      ASSERT_EQ(rows->rowCount(), size_t(42));
      for (size_t i = 0; i < 42; i++) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(42 * i));
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT answer / 882 AS g, COUNT(*) AS n FROM "
          "TABLE(ct_no_arg_constant_sizing()) GROUP BY g ORDER BY g;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));

      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(0));
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), static_cast<int64_t>(21));

      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(1));
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), static_cast<int64_t>(21));
    }

    {
      const auto rows =
          run_multiple_agg("SELECT answer FROM TABLE(ct_no_arg_runtime_sizing());", dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(42));
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT answer FROM TABLE(ct_scalar_1_arg_runtime_sizing(123));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(3));

      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(123));
      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(12));
      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(1));
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT answer1, answer2 FROM TABLE(ct_scalar_2_args_constant_sizing(100, "
          "5));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));

      for (size_t r = 0; r < 5; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(100 + r * 5));
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), static_cast<int64_t>(100 - r * 5));
      }
    }

    // Tests for user-defined constant parameter sizing, which were separately broken
    // from the above
    {
      const auto rows = run_multiple_agg(
          "SELECT output FROM TABLE(ct_no_cursor_user_constant_sizer(8, 10));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));

      for (size_t r = 0; r < 10; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(8));
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT output FROM TABLE(ct_templated_no_cursor_user_constant_sizer(7, 4));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(4));

      for (size_t r = 0; r < 4; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(7));
      }
    }
  }
}

TEST_F(TableFunctions, TextEncodedNoneLiteralArgs) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Following tests ability to transform to std::string running on CPU (runs on CPU
    // only)
    {
      const std::string test_string{"this is only a test"};
      const size_t test_string_len{test_string.size()};
      const std::string test_query(
          "SELECT char_idx, char_bytes FROM TABLE(ct_string_to_chars('" + test_string +
          "')) ORDER BY char_idx;");
      const auto rows = run_multiple_agg(test_query, dt);
      ASSERT_EQ(rows->rowCount(), test_string_len);
      for (size_t r = 0; r < test_string_len; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(r));
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]),
                  static_cast<int64_t>(test_string[r]));
      }
    }
    // Following tests two text encoding none input, plus running on GPU + CPU
    {
      const std::string test_string1{"theater"};
      const std::string test_string2{"theatre"};
      const std::string test_query(
          "SELECT hamming_distance FROM TABLE(ct_hamming_distance('" + test_string1 +
          "','" + test_string2 + "'));");
      const auto rows = run_multiple_agg(test_query, dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(2));
    }

    // Following tests varchar element accessors and that TextEncodedNone literal inputs
    // play nicely with column inputs + RowMultiplier
    {
      const std::string test_string{"theater"};
      const std::string test_query(
          "SELECT idx, char_bytes FROM TABLE(ct_get_string_chars(CURSOR(SELECT x FROM "
          "tf_test), '" +
          test_string + "', 1)) ORDER BY idx;");
      const auto rows = run_multiple_agg(test_query, dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));  // size of tf_test
      for (size_t r = 0; r < 5; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(r));
        ASSERT_EQ(
            TestHelpers::v<int64_t>(crt_row[1]),
            static_cast<int64_t>(test_string[r]));  // x in tf_test is {1, 2, 3, 4, 5}
      }
    }
  }
}

TEST_F(TableFunctions, ThrowingTests) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(column_list_safe_row_sum(cursor(SELECT x FROM "
              "err_test)));",
              dt),
          UserTableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(column_list_safe_row_sum(cursor(SELECT y FROM "
              "err_test)));",
              dt),
          UserTableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(column_list_safe_row_sum(cursor(SELECT f FROM "
              "err_test)));",
              dt),
          UserTableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg(
              "SELECT out0 FROM TABLE(column_list_safe_row_sum(cursor(SELECT d FROM "
              "err_test)));",
              dt),
          UserTableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require(cursor(SELECT x"
                                    " FROM tf_test), -2));",
                                    dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_templating(cursor(SELECT x"
                           " FROM tf_test), -2));",
                           dt),
          TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_templating(cursor(SELECT d"
                           " FROM tf_test), -2));",
                           dt),
          TableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require_and(cursor(SELECT x"
                                    " FROM tf_test), -2));",
                                    dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_or_str(cursor(SELECT x"
                           " FROM tf_test), 'string'));",
                           dt),
          TableFunctionError);
    }
    {
      if (dt == ExecutorDeviceType::GPU) {
        EXPECT_THROW(
            run_multiple_agg("SELECT * FROM TABLE(ct_require_device_cuda(cursor(SELECT x"
                             " FROM tf_test), -2));",
                             dt),
            TableFunctionError);
      }
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require_mgr(cursor(SELECT x"
                                    " FROM tf_test), -2));",
                                    dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require_mgr(cursor(SELECT x"
                                    " FROM tf_test), 6));",
                                    dt),
                   TableFunctionError);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(column_list_safe_row_sum(cursor(SELECT x2 FROM "
          "err_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]),
                static_cast<int32_t>(10));  // 0+1+2+3+4=10
    }

    // Ensure TableFunctionMgr and error throwing works properly for templated CPU TFs
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_throw_if_gt_100(CURSOR(SELECT "
                           "CAST(f AS FLOAT) AS "
                           "f FROM (VALUES (0.0), (1.0), (2.0), (110.0)) AS t(f))));",
                           dt),
          UserTableFunctionError);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT CAST(val AS INT) AS val FROM TABLE(ct_throw_if_gt_100(CURSOR(SELECT "
          "CAST(f AS DOUBLE) AS f FROM (VALUES (0.0), (1.0), (2.0), (3.0)) AS t(f)))) "
          "ORDER BY val;",
          dt);
      const size_t num_rows = rows->rowCount();
      ASSERT_EQ(num_rows, size_t(4));
      for (size_t r = 0; r < num_rows; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int32_t>(r));
      }
    }
  }
}

std::string gen_grid_values(const int32_t num_x_bins,
                            const int32_t num_y_bins,
                            const bool add_w_val,
                            const bool merge_with_w_null,
                            const std::string& aliased_table = "") {
  const std::string project_w_sql = add_w_val ? ", CAST(w AS INT) as w" : "";
  std::string values_sql =
      "SELECT CAST(id AS INT) as id, CAST(x AS INT) AS x, CAST(y AS INT) AS y, CAST(z AS "
      "INT) AS z" +
      project_w_sql + " FROM (VALUES ";
  auto gen_values = [](const int32_t num_x_bins,
                       const int32_t num_y_bins,
                       const int32_t start_id,
                       const int32_t start_x_bin,
                       const int32_t start_y_bin,
                       const bool add_w_val,
                       const bool w_is_null) {
    std::string values_sql;
    int32_t out_id = start_id;
    for (int32_t y_bin = start_y_bin; y_bin < start_y_bin + num_y_bins; ++y_bin) {
      for (int32_t x_bin = start_x_bin; x_bin < start_x_bin + num_x_bins; ++x_bin) {
        const std::string id_str = std::to_string(out_id);
        const std::string x_val_str = std::to_string(x_bin);
        const std::string y_val_str = std::to_string(y_bin);
        const int32_t z_val = x_bin * y_bin;
        const std::string z_val_str = std::to_string(z_val);
        const int32_t w_val = x_bin;
        const std::string w_val_str = w_is_null ? "null" : std::to_string(w_val);
        if (out_id++ > start_id) {
          values_sql += ", ";
        }
        values_sql +=
            "(" + id_str + ", " + x_val_str + ", " + y_val_str + ", " + z_val_str;
        if (add_w_val) {
          values_sql += ", " + w_val_str;
        }
        values_sql += ")";
      }
    }
    return values_sql;
  };
  if (add_w_val) {
    if (merge_with_w_null) {
      values_sql += gen_values(num_x_bins, num_y_bins, 0, 0, 0, true, true) + ", ";
      values_sql += gen_values(num_x_bins,
                               num_y_bins,
                               num_x_bins * num_y_bins,
                               num_x_bins,
                               num_y_bins,
                               true,
                               false);
    } else {
      values_sql += gen_values(num_x_bins,
                               num_y_bins,
                               num_x_bins * num_y_bins,
                               num_x_bins,
                               num_y_bins,
                               true,
                               false);
    }
  } else {
    values_sql += gen_values(num_x_bins, num_y_bins, 0, 0, 0, false, false);
  }
  if (aliased_table.empty()) {
    values_sql += ") AS t(id, x, y, z";
    if (add_w_val) {
      values_sql += ", w";
    }
    values_sql += ")";
  } else {
    values_sql += ") AS " + aliased_table;
  }
  return values_sql;
}

void check_result_set_equality(const ResultSetPtr rows_1, const ResultSetPtr rows_2) {
  const size_t num_result_rows_1 = rows_1->rowCount();
  const size_t num_result_rows_2 = rows_2->rowCount();
  ASSERT_EQ(num_result_rows_1, num_result_rows_2);
  const size_t num_result_cols_1 = rows_1->colCount();
  const size_t num_result_cols_2 = rows_2->colCount();
  ASSERT_EQ(num_result_cols_1, num_result_cols_2);
  for (size_t r = 0; r < num_result_rows_1; ++r) {
    const auto& row_1 = rows_1->getNextRow(false, false);
    const auto& row_2 = rows_2->getNextRow(false, false);
    for (size_t c = 0; c < num_result_cols_1; ++c) {
      ASSERT_EQ(TestHelpers::v<int64_t>(row_1[c]), TestHelpers::v<int64_t>(row_2[c]));
    }
  }
}

void check_result_against_expected_result(
    const ResultSetPtr rows,
    const std::vector<std::vector<int64_t>>& expected_result) {
  const size_t num_result_rows = rows->rowCount();
  ASSERT_EQ(num_result_rows, expected_result.size());
  const size_t num_result_cols = rows->colCount();
  for (size_t r = 0; r < num_result_rows; ++r) {
    const auto& expected_result_row = expected_result[r];
    const auto& row = rows->getNextRow(false, false);
    ASSERT_EQ(num_result_cols, expected_result_row.size());
    ASSERT_EQ(num_result_cols, row.size());
    for (size_t c = 0; c < num_result_cols; ++c) {
      ASSERT_EQ(TestHelpers::v<int64_t>(row[c]), expected_result_row[c]);
    }
  }
}

void print_result(const ResultSetPtr rows) {
  const size_t num_result_rows = rows->rowCount();
  const size_t num_result_cols = rows->colCount();
  for (size_t r = 0; r < num_result_rows; ++r) {
    std::cout << std::endl << "Row: " << r << std::endl;
    const auto& row = rows->getNextRow(false, false);
    for (size_t c = 0; c < num_result_cols; ++c) {
      std::cout << "Col: " << c << " Result: " << TestHelpers::v<int64_t>(row[c])
                << std::endl;
    }
  }
}

TEST_F(TableFunctions, FilterTransposeRuleOneCursor) {
  // Test FILTER_TABLE_FUNCTION_TRANSPOSE optimization on single cursor table functions

  enum StatType { MIN, MAX };

  auto compare_tf_pushdown_with_values_rollup =
      [&](const std::string& values_sql,
          const std::string& filter_sql,
          const std::string& non_pushdown_filter_sql,
          const StatType stat_type,
          const ExecutorDeviceType dt) {
        std::string tf_filter = filter_sql;
        if (non_pushdown_filter_sql.size()) {
          tf_filter += " AND " + non_pushdown_filter_sql;
        }
        const std::string stat_type_agg = stat_type == StatType::MIN ? "MIN" : "MAX";
        const std::string tf_query = "SELECT * FROM TABLE(ct_pushdown_stats('" +
                                     stat_type_agg + "', CURSOR(" + values_sql +
                                     "))) WHERE " + tf_filter + ";";
        std::string values_rollup_query =
            "SELECT COUNT(*) AS row_count, " + stat_type_agg + "(id) AS id, " +
            stat_type_agg + "(x) AS x, " + stat_type_agg + "(y) AS y, " + stat_type_agg +
            "(z) AS z FROM (" + values_sql + " WHERE " + filter_sql + ")";
        if (!non_pushdown_filter_sql.empty()) {
          values_rollup_query = "SELECT * FROM (" + values_rollup_query + ") WHERE " +
                                non_pushdown_filter_sql + ";";
        } else {
          values_rollup_query += ";";
        }
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        const auto values_rollup_rows = run_multiple_agg(values_rollup_query, dt);
        check_result_set_equality(tf_rows, values_rollup_rows);
      };

  auto compare_tf_pushdown_with_values_projection =
      [&](const std::string& values_sql,
          const std::string& filter_sql,
          const std::string& non_pushdown_filter_sql,
          const ExecutorDeviceType dt) {
        std::string tf_filter = filter_sql;
        if (non_pushdown_filter_sql.size()) {
          tf_filter += " AND " + non_pushdown_filter_sql;
        }
        const std::string tf_query =
            "SELECT * FROM TABLE(ct_pushdown_projection(CURSOR(" + values_sql +
            "))) WHERE " + tf_filter + " ORDER BY id ASC;";

        std::string values_projection_query =
            "SELECT * FROM (" + values_sql + " WHERE " + filter_sql + ")";
        if (!non_pushdown_filter_sql.empty()) {
          values_projection_query = "SELECT * FROM (" + values_projection_query +
                                    ") WHERE " + non_pushdown_filter_sql;
        }
        values_projection_query += " ORDER BY id ASC;";
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        const auto values_projection_rows = run_multiple_agg(values_projection_query, dt);
        check_result_set_equality(tf_rows, values_projection_rows);
      };

  auto run_tests_for_filter = [&](const std::string& values_sql,
                                  const std::string& filter_sql,
                                  const std::string& non_pushdown_filter_sql,
                                  const ExecutorDeviceType dt) {
    compare_tf_pushdown_with_values_rollup(
        values_sql, filter_sql, non_pushdown_filter_sql, StatType::MIN, dt);
    compare_tf_pushdown_with_values_rollup(
        values_sql, filter_sql, non_pushdown_filter_sql, StatType::MAX, dt);
    compare_tf_pushdown_with_values_projection(
        values_sql, filter_sql, non_pushdown_filter_sql, dt);
  };

  const std::string grid_values =
      gen_grid_values(8, 8, false /* add_w_val */, false /* merge_with_w_null */);
  std::cout << grid_values << std::endl;
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Single cursor arguments

    {
      // no filter
      const std::string pushdown_filter{"TRUE"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // single filter
      const std::string pushdown_filter{"x <= 4"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // two filters
      const std::string pushdown_filter{"x < 4 AND y < 3"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // two filters - with betweens and equality
      const std::string pushdown_filter{"x BETWEEN 2 AND 4 AND y = 4"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // three filters - with inequality
      const std::string pushdown_filter{"z <> 6 AND x <= 3 AND y between -5 and 2"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // filter that filters out all rows
      // compare_tf_pushdown_with_values_rollup(grid_values, "z <> 3 AND x <= 0 AND y
      // between 1 and 2", "", StatType::MIN, dt);
      const std::string pushdown_filter{"z <> 3 AND x <= 0 AND y between 1 and 2"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // four filters
      const std::string pushdown_filter{
          "z <> 3 AND x > 1 AND y between 1 and 4 AND id < 15"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // four pushdown filters + one filter that cannot be pushed down
      const std::string pushdown_filter{
          "z <> 3 AND x > 1 AND y between 1 and 8 AND id < 28"};
      const std::string non_pushdown_filter{"row_count > 0"};
      compare_tf_pushdown_with_values_rollup(
          grid_values, pushdown_filter, non_pushdown_filter, StatType::MIN, dt);
      compare_tf_pushdown_with_values_rollup(
          grid_values, pushdown_filter, non_pushdown_filter, StatType::MAX, dt);
    }

    {
      // disjunctive pushdown filter
      const std::string pushdown_filter{"x >= 2 OR y >= 3"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // conjunctive pushdown filter with disjunctive sub-predicate
      const std::string pushdown_filter{"x >= 1 OR y < 3 AND z < 4"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // More complexity...
      const std::string pushdown_filter{
          "x >= 1 AND x + y + z < 20 AND x * y < y + 6 OR z > 12"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }
  }
}

TEST_F(TableFunctions, FilterTransposeRuleMultipleCursors) {
  enum StatType { MIN, MAX };

  auto compare_tf_pushdown_with_values_rollup =
      [&](const std::string& values1_sql,
          const std::string& values2_sql,
          const std::string& values_merged_sql,
          const std::string& filter_sql,
          const std::string& non_pushdown_filter_sql,
          const StatType stat_type,
          const ExecutorDeviceType dt) {
        std::string tf_filter = filter_sql;
        if (non_pushdown_filter_sql.size()) {
          tf_filter += " AND " + non_pushdown_filter_sql;
        }
        const std::string stat_type_agg = stat_type == StatType::MIN ? "MIN" : "MAX";
        const std::string tf_query = "SELECT * FROM TABLE(ct_union_pushdown_stats('" +
                                     stat_type_agg + "', CURSOR(" + values1_sql +
                                     "), CURSOR(" + values2_sql + "))) WHERE " +
                                     tf_filter + ";";
        // std::cout << "Query plan" << std::endl;
        // const std::string tf_explain = "EXPLAIN PLAN " << tf_query << std::endl;
        // const auto tf_explain = run_multiple_agg(tf_query, dt);
        // std::cout << tf_explain << std::endl;

        std::string values_rollup_query =
            "SELECT COUNT(*) AS row_count, " + stat_type_agg + "(id) AS id, " +
            stat_type_agg + "(x) AS x, " + stat_type_agg + "(y) AS y, " + stat_type_agg +
            "(z) AS z, " + stat_type_agg + "(w) AS w FROM (" + values_merged_sql +
            " WHERE " + filter_sql + ")";
        if (!non_pushdown_filter_sql.empty()) {
          values_rollup_query = "SELECT * FROM (" + values_rollup_query + ") WHERE " +
                                non_pushdown_filter_sql + ";";
        } else {
          values_rollup_query += ";";
        }
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        const auto values_rollup_rows = run_multiple_agg(values_rollup_query, dt);
        check_result_set_equality(tf_rows, values_rollup_rows);
      };

  auto compare_tf_pushdown_with_values_projection =
      [&](const std::string& values1_sql,
          const std::string& values2_sql,
          const std::string& values_merged_sql,
          const std::string& filter_sql,
          const std::string& non_pushdown_filter_sql,
          const ExecutorDeviceType dt) {
        std::string tf_filter = filter_sql;
        if (non_pushdown_filter_sql.size()) {
          tf_filter += " AND " + non_pushdown_filter_sql;
        }
        const std::string tf_query =
            "SELECT * FROM TABLE(ct_union_pushdown_projection(CURSOR(" + values1_sql +
            "), CURSOR(" + values2_sql + "))) WHERE " + tf_filter + " ORDER BY id;";

        std::string values_projection_query =
            "SELECT * FROM (" + values_merged_sql + " WHERE " + filter_sql + ")";
        if (!non_pushdown_filter_sql.empty()) {
          values_projection_query = "SELECT * FROM (" + values_projection_query +
                                    ") WHERE " + non_pushdown_filter_sql;
        }
        values_projection_query += " ORDER BY id ASC;";
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        const auto values_projection_rows = run_multiple_agg(values_projection_query, dt);
        check_result_set_equality(tf_rows, values_projection_rows);
      };

  // TM: Commenting out the following function to eliminate an unused variable warning
  // but leaving as its quite useful for debugging

  // auto print_tf_pushdown_projection = [&](const std::string& values1_sql,
  //                                        const std::string& values2_sql,
  //                                        const std::string& filter_sql,
  //                                        const std::string& non_pushdown_filter_sql,
  //                                        const ExecutorDeviceType dt) {
  //  std::string tf_filter = filter_sql;
  //  if (non_pushdown_filter_sql.size()) {
  //    tf_filter += " AND " + non_pushdown_filter_sql;
  //  }
  //  const std::string tf_query =
  //      "SELECT * FROM TABLE(ct_union_pushdown_projection(CURSOR(" + values1_sql +
  //      "), CURSOR(" + values2_sql + "))) WHERE " + tf_filter + " ORDER BY id;";
  //  const auto tf_rows = run_multiple_agg(tf_query, dt);
  //  print_result(tf_rows);
  //};

  auto compare_tf_pushdown_with_expected_result =
      [&](const std::string& values1_sql,
          const std::string& values2_sql,
          const std::string& filter_sql,
          const std::vector<std::vector<int64_t>>& expected_result,
          const StatType stat_type,
          const ExecutorDeviceType dt) {
        const std::string stat_type_agg = stat_type == StatType::MIN ? "MIN" : "MAX";
        const std::string tf_query = "SELECT * FROM TABLE(ct_union_pushdown_stats('" +
                                     stat_type_agg + "', CURSOR(" + values1_sql +
                                     "), CURSOR(" + values2_sql + "))) WHERE " +
                                     filter_sql + ";";
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        check_result_against_expected_result(tf_rows, expected_result);
      };

  auto run_tests_for_filter = [&](const std::string& values1_sql,
                                  const std::string& values2_sql,
                                  const std::string& values_merged_sql,
                                  const std::string& filter_sql,
                                  const std::string& non_pushdown_filter_sql,
                                  const ExecutorDeviceType dt) {
    compare_tf_pushdown_with_values_rollup(values1_sql,
                                           values2_sql,
                                           values_merged_sql,
                                           filter_sql,
                                           non_pushdown_filter_sql,
                                           StatType::MIN,
                                           dt);
    compare_tf_pushdown_with_values_rollup(values1_sql,
                                           values2_sql,
                                           values_merged_sql,
                                           filter_sql,
                                           non_pushdown_filter_sql,
                                           StatType::MAX,
                                           dt);
    compare_tf_pushdown_with_values_projection(values1_sql,
                                               values2_sql,
                                               values_merged_sql,
                                               filter_sql,
                                               non_pushdown_filter_sql,
                                               dt);
  };

  const std::string grid_values_1 = gen_grid_values(8, 8, false, false);
  const std::string grid_values_2 = gen_grid_values(8, 8, true, false);
  const std::string grid_values_merged = gen_grid_values(8, 8, true, true);
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      // No filter
      const std::string filter_sql{"TRUE"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // One filter
      const std::string filter_sql{"x > 1"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // One range filter
      const std::string filter_sql{"x BETWEEN 1 AND 10"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // Two filters
      const std::string filter_sql{"x BETWEEN 4 AND 10 AND y < 9"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // Two filters - order swap
      const std::string filter_sql{"y < 9 AND x BETWEEN 4 AND 10"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // Three filters
      const std::string filter_sql{"x < 10 AND y > 4 AND z BETWEEN 4 AND 20"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // One filter - push down only to one input (w)
      compare_tf_pushdown_with_expected_result(grid_values_1,
                                               grid_values_2,
                                               "w >= 12",
                                               {{96, 0, 0, 0, 0, 12}},
                                               StatType::MIN,
                                               dt);
      compare_tf_pushdown_with_expected_result(grid_values_1,
                                               grid_values_2,
                                               "w >= 12",
                                               {{96, 127, 15, 15, 225, 15}},
                                               StatType::MAX,
                                               dt);
      compare_tf_pushdown_with_values_projection(grid_values_1,
                                                 grid_values_2,
                                                 grid_values_merged,
                                                 "w >= 12 OR w IS null",
                                                 "",
                                                 dt);
    }

    {
      // Three filters - one only pushes down to one input (w)
      compare_tf_pushdown_with_expected_result(
          grid_values_1,
          grid_values_2,
          "z <= 72 AND w BETWEEN 7 AND 10 AND x >= 7",
          {{11, 7, 7, 0, 0, 8}},
          StatType::MIN,
          dt);
      compare_tf_pushdown_with_expected_result(
          grid_values_1,
          grid_values_2,
          "z <= 72 AND w BETWEEN 7 AND 10 AND x >= 7",
          {{11, 72, 9, 9, 72, 9}},
          StatType::MAX,
          dt);
      compare_tf_pushdown_with_values_projection(grid_values_1,
                                                 grid_values_2,
                                                 grid_values_merged,
                                                 "w BETWEEN 7 AND 10 OR w IS NULL",
                                                 "",
                                                 dt);
    }

    {
      // Three filters - one only pushes down to one input (w)
      compare_tf_pushdown_with_expected_result(
          grid_values_1,
          grid_values_2,
          "z <= 72 AND w BETWEEN 7 AND 10 AND x >= 7",
          {{11, 7, 7, 0, 0, 8}},
          StatType::MIN,
          dt);
      compare_tf_pushdown_with_expected_result(
          grid_values_1,
          grid_values_2,
          "z <= 72 AND w BETWEEN 7 AND 10 AND x >= 7",
          {{11, 72, 9, 9, 72, 9}},
          StatType::MAX,
          dt);
      compare_tf_pushdown_with_values_projection(grid_values_1,
                                                 grid_values_2,
                                                 grid_values_merged,
                                                 "w BETWEEN 7 AND 10 OR w IS NULL",
                                                 "",
                                                 dt);
    }
  }
}

TEST_F(TableFunctions, FilterTransposeRuleMisc) {
  // Test FILTER_TABLE_FUNCTION_TRANSPOSE optimization.

  auto check_result = [](const auto rows, std::vector<int64_t> result) {
    ASSERT_EQ(rows->rowCount(), result.size());
    for (size_t i = 0; i < result.size(); i++) {
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), result[i]);
    }
  };

  auto check_result2 =
      [](const auto rows, std::vector<int64_t> result1, std::vector<int64_t> result2) {
        ASSERT_EQ(rows->rowCount(), result1.size());
        ASSERT_EQ(rows->rowCount(), result2.size());
        for (size_t i = 0; i < result1.size(); i++) {
          auto crt_row = rows->getNextRow(false, false);
          ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), result1[i]);
          ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), result2[i]);
        }
      };

  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_copy_and_add_size(cursor(SELECT x FROM tf_test WHERE "
          "x>1)));",
          dt);
      check_result(rows, {2 + 3, 3 + 3, 4 + 3});
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_copy_and_add_size(cursor(SELECT x FROM tf_test)))"
          "WHERE x>1;",
          dt);
      check_result(rows, {2 + 3, 3 + 3, 4 + 3});
    }
    {
      run_ddl_statement("DROP VIEW IF EXISTS view_ct_copy_and_add_size");
      run_ddl_statement(
          "CREATE VIEW view_ct_copy_and_add_size AS SELECT * FROM "
          "TABLE(ct_copy_and_add_size(cursor(SELECT x FROM tf_test)));");
      const auto rows1 =
          run_multiple_agg("SELECT * FROM view_ct_copy_and_add_size WHERE x>1;", dt);
      check_result(rows1, {2 + 3, 3 + 3, 4 + 3});
      const auto rows2 = run_multiple_agg("SELECT * FROM view_ct_copy_and_add_size;", dt);
      check_result(rows2, {0 + 5, 1 + 5, 2 + 5, 3 + 5, 4 + 5});
    }
    {
      // x=0,1,2,3,4
      // x2=5,4,3,2,1
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_add_size_and_mul_alpha(cursor(SELECT x, x2 FROM "
          "tf_test WHERE "
          "x>1 and x2>1), 4));",
          dt);
      check_result2(rows, {2 + 2, 3 + 2}, {3 * 4, 2 * 4});
    }
    {
      // x =0,1,2,3,4
      // x2=5,4,3,2,1
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_add_size_and_mul_alpha(cursor(SELECT x, x2 FROM "
          "tf_test), 4)) WHERE x>1 and x2>1;",
          dt);
      check_result2(rows, {2 + 2, 3 + 2}, {3 * 4, 2 * 4});
    }
    // Multiple cursor arguments
    {
      const auto rows = run_multiple_agg(
          "SELECT x, d FROM TABLE(ct_sparse_add(cursor(SELECT x, x FROM tf_test), 0"
          ", cursor(SELECT x, x FROM tf_test), 0));",
          dt);
      check_result2(
          rows, {0, 1, 2, 3, 4}, {0, (1 + 1) * 5, (2 + 2) * 5, (3 + 3) * 5, (4 + 4) * 5});
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT x, d FROM TABLE(ct_sparse_add(cursor(SELECT x, x FROM tf_test), 0"
          ", cursor(SELECT x, x + 1 FROM tf_test WHERE x > 2), 15)) WHERE (x > 1 AND x < "
          "4);",
          dt);
      check_result2(rows, {2, 3}, {(2 + 15) * 2, (3 + 4) * 2});
    }
  }
}

int main(int argc, char** argv) {
  TestHelpers::init_logger_stderr_only(argc, argv);
  testing::InitGoogleTest(&argc, argv);

  // Table function support must be enabled before initialized the query runner
  // environment
  g_enable_table_functions = true;
  QR::init(BASE_PATH);

  int err{0};
  try {
    err = RUN_ALL_TESTS();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }
  QR::reset();
  return err;
}
