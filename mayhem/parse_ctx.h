#pragma once
// Drives the proxy-side SQL parser through its C entry points (parse_init / parse_sql from
// src/sql/parser/parse_node.h), initialising ParseResult exactly as ObSQLParser::parse_and_gen_sqlid
// (src/sql/parser/ob_sql_parser.cpp) does. That wrapper itself is not linked: it needs the observer's
// ObCharset/ObLogger/diagnostic classes, none of which the parser core touches.
#include <openssl/md5.h>
#include <string.h>

#include "sql/parser/parse_malloc.h"
#include "sql/parser/parse_node.h"
#include "sql/parser/parser_utility.h"
#include "lib/charset/ob_ctype.h"
#include "embed.h"

namespace mayhem {

inline void init_parse_result(ParseResult &r, void *pool, uint64_t sql_mode, bool is_fp,
                              bool gbk_connection = false) {
  memset(static_cast<void *>(&r), 0, sizeof(r));
  r.is_fp_ = is_fp;
  r.is_multi_query_ = false;
  r.malloc_pool_ = pool;
  r.is_ignore_hint_ = false;
  r.need_parameterize_ = true;
  r.pl_parse_info_.is_pl_parse_ = false;
  r.minus_ctx_.has_minus_ = false;
  r.minus_ctx_.pos_ = -1;
  r.minus_ctx_.raw_sql_offset_ = -1;
  r.is_for_trigger_ = false;
  r.is_for_remap_ = false;
  r.is_dynamic_sql_ = false;
  r.is_batched_multi_enabled_split_ = false;
  r.may_bool_value_ = false;
  r.is_external_table_ = false;
  r.sql_mode_ = sql_mode;
  if (gbk_connection) {
    r.connection_collation_ = 28;  // CS_TYPE_GBK_CHINESE_CI
    r.charset_info_ = &ob_charset_gbk_chinese_ci;
    r.is_not_utf8_connection_ = true;
  } else {
    r.connection_collation_ = 45;  // CS_TYPE_UTF8MB4_GENERAL_CI
    r.charset_info_ = &ob_charset_utf8mb4_general_ci;
  }
  r.charset_info_oracle_db_ = nullptr;
  r.realloc_cnt_ = 10;
}

// Parses `sql` into `r` (already initialised); returns parse_sql's code. The caller releases the
// parser's allocations with embed_release_all().
inline int run_parser(ParseResult &r, const char *sql, int64_t len) {
  int64_t n = len + 1;
  r.param_nodes_ = nullptr;
  r.tail_param_node_ = nullptr;
  r.no_param_sql_ = static_cast<char *>(parse_malloc(n, r.malloc_pool_));
  r.no_param_sql_buf_len_ = n;
  int ret = parse_init(&r);
  if (ret == 0) ret = parse_sql(&r, sql, static_cast<size_t>(len));
  return ret;
}

// The sql_id obproxy computes: MD5 of the parameterised statement, as 32 hex digits.
inline int fast_sql_id(const char *sql, int64_t len, uint64_t sql_mode, void *pool, char *sql_id33,
                       bool gbk_connection = false) {
  ParseResult r;
  init_parse_result(r, pool, sql_mode, true, gbk_connection);
  int ret = run_parser(r, sql, len);
  if (ret == 0) {
    unsigned char md5[16];
    if (MD5(reinterpret_cast<const unsigned char *>(r.no_param_sql_), r.no_param_sql_len_, md5) == nullptr) {
      ret = -1;
    } else {
      ret = parser_to_hex_cstr(md5, 16, sql_id33, 33);
    }
  }
  return ret;
}

}  // namespace mayhem
