/*
 * Copyright 2017 MapD Technologies, Inc.
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

/*
 * File:   ParserWrapper.cpp
 * Author: michael
 *
 * Created on Feb 23, 2016, 9:33 AM
 */

#include "ParserWrapper.h"
#include "Shared/measure.h"

#include <boost/algorithm/string.hpp>

using namespace std;

const std::vector<std::string> ParserWrapper::ddl_cmd = {"ARCHIVE",
                                                         "ALTER",
                                                         "COPY",
                                                         "CREATE",
                                                         "DROP",
                                                         "DUMP",
                                                         "GRANT",
                                                         "KILL",
                                                         "OPTIMIZE",
                                                         "REFRESH",
                                                         "RENAME",
                                                         "RESTORE",
                                                         "REVOKE",
                                                         "SHOW",
                                                         "TRUNCATE",
                                                         "REASSIGN",
                                                         "VALIDATE",
                                                         "CLEAR"};

const std::vector<std::string> ParserWrapper::update_dml_cmd = {
    "INSERT",
    "DELETE",
    "UPDATE",
    "UPSERT",
};

const std::string ParserWrapper::explain_str = {"explain"};
const std::string ParserWrapper::calcite_explain_str = {"explain calcite"};
const std::string ParserWrapper::optimized_explain_str = {"explain optimized"};
const std::string ParserWrapper::plan_explain_str = {"explain plan"};
const std::string ParserWrapper::optimize_str = {"optimize"};
const std::string ParserWrapper::validate_str = {"validate"};

extern bool g_enable_fsi;

namespace {
void validate_no_leading_comments(const std::string& query_str) {
  if (boost::starts_with(query_str, "--") || boost::starts_with(query_str, "//") ||
      boost::starts_with(query_str, "/*")) {
    throw std::runtime_error(
        "SQL statements starting with comments are currently not allowed.");
  }
}
}  // namespace
ParserWrapper::ParserWrapper(std::string query_string) {
  validate_no_leading_comments(query_string);
  query_type_ = QueryType::SchemaRead;
  if (boost::istarts_with(query_string, calcite_explain_str)) {
    actual_query = boost::trim_copy(query_string.substr(calcite_explain_str.size()));
    ParserWrapper inner{actual_query};
    if (inner.isDdl() || inner.is_update_dml) {
      explain_type_ = ExplainType::Other;
      return;
    } else {
      explain_type_ = ExplainType::Calcite;
      return;
    }
  }

  if (boost::istarts_with(query_string, optimized_explain_str)) {
    actual_query = boost::trim_copy(query_string.substr(optimized_explain_str.size()));
    ParserWrapper inner{actual_query};
    if (inner.isDdl() || inner.is_update_dml) {
      explain_type_ = ExplainType::Other;
      return;
    } else {
      explain_type_ = ExplainType::OptimizedIR;
      return;
    }
  }

  if (boost::istarts_with(query_string, plan_explain_str)) {
    actual_query = boost::trim_copy(query_string.substr(plan_explain_str.size()));
    ParserWrapper inner{actual_query};
    if (inner.isDdl() || inner.is_update_dml) {
      explain_type_ = ExplainType::Other;
      return;
    } else {
      explain_type_ = ExplainType::ExecutionPlan;
      return;
    }
  }

  if (boost::istarts_with(query_string, explain_str)) {
    actual_query = boost::trim_copy(query_string.substr(explain_str.size()));
    ParserWrapper inner{actual_query};
    if (inner.isDdl() || inner.is_update_dml) {
      explain_type_ = ExplainType::Other;
      return;
    } else {
      explain_type_ = ExplainType::IR;
      return;
    }
  }

  query_type_ = QueryType::Read;
  for (std::string ddl : ddl_cmd) {
    if (boost::istarts_with(query_string, ddl)) {
      query_type_ = QueryType::SchemaWrite;
      is_ddl_ = true;

      if (ddl == "CREATE") {
        boost::regex ctas_regex{
            R"(CREATE\s+(TEMPORARY\s+|\s*)+TABLE.*(\"|\s)AS(\(|\s)+(SELECT|WITH).*)",
            boost::regex::extended | boost::regex::icase};
        if (boost::regex_match(query_string, ctas_regex)) {
          is_ctas = true;
        }
      } else if (ddl == "COPY") {
        is_copy = true;
        // now check if it is COPY TO
        boost::regex copy_to{R"(COPY\s*\(([^#])(.+)\)\s+TO\s+.*)",
                             boost::regex::extended | boost::regex::icase};
        if (boost::regex_match(query_string, copy_to)) {
          query_type_ = QueryType::Read;
          is_copy_to = true;
        } else {
          query_type_ = QueryType::Write;
        }
      } else if (ddl == "SHOW") {
        query_type_ = QueryType::SchemaRead;

      } else if (ddl == "KILL") {
        query_type_ = QueryType::Unknown;
      } else if (ddl == "VALIDATE") {
        query_type_ = QueryType::Unknown;
        // needs to execute in a different context from other DDL
        is_validate = true;
      } else if (ddl == "ALTER") {
        boost::regex alter_system_regex{R"(ALTER\s+(SYSTEM).*)",
                                        boost::regex::extended | boost::regex::icase};
        if (boost::regex_match(query_string, alter_system_regex)) {
          query_type_ = QueryType::Unknown;
        }
      } else if (ddl == "ARCHIVE" || ddl == "DUMP") {
        query_type_ = QueryType::SchemaRead;
      }
      return;
    }
  }

  for (int i = 0; i < update_dml_cmd.size(); i++) {
    is_update_dml = boost::istarts_with(query_string, ParserWrapper::update_dml_cmd[i]);
    if (is_update_dml) {
      query_type_ = QueryType::Write;
      dml_type_ = (DMLType)(i);
      break;
    }
  }

  if (dml_type_ == DMLType::Insert) {
    boost::regex itas_regex{R"(INSERT\s+INTO\s+.*(\s+|\(|\")SELECT(\s|\(|\").*)",
                            boost::regex::extended | boost::regex::icase};
    if (boost::regex_match(query_string, itas_regex)) {
      is_itas = true;
      return;
    }
  }
}

ParserWrapper::~ParserWrapper() {}

ExplainInfo ParserWrapper::getExplainInfo() const {
  return {explain_type_ == ExplainType::IR,
          explain_type_ == ExplainType::OptimizedIR,
          explain_type_ == ExplainType::ExecutionPlan,
          explain_type_ == ExplainType::Calcite};
}
