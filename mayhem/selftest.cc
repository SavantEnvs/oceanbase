// Known-answer checks for the standalone SQL parser and full-text query parser, built with the
// project's normal flags (no sanitizer) by mayhem/build.sh and run by mayhem/test.sh.
#include <stdio.h>
#include <string.h>

extern "C" {
#include "sql/parser/fts_base.h"
#include "sql/parser/fts_parse.h"
}
#include "parse_ctx.h"

static int g_pool_tag;
static int g_pass = 0, g_fail = 0;

static void check(const char *name, bool ok) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  (ok ? g_pass : g_fail)++;
}

static const uint64_t kMode = DEFAULT_MYSQL_MODE;

static void sql_id_is(const char *sql, const char *want) {
  char id[33] = "";
  int ret = mayhem::fast_sql_id(sql, strlen(sql), kMode, &g_pool_tag, id);
  embed_release_all();
  check(sql, ret == 0 && strcmp(id, want) == 0);
}

static void parameterized_is(const char *sql, const char *want) {
  ParseResult r;
  mayhem::init_parse_result(r, &g_pool_tag, kMode, true);
  int ret = mayhem::run_parser(r, sql, strlen(sql));
  bool ok = ret == 0 && r.no_param_sql_ != nullptr &&
            strncmp(r.no_param_sql_, want, r.no_param_sql_len_) == 0 && strlen(want) == (size_t)r.no_param_sql_len_;
  if (!ok) printf("  got ret=%d '%.*s'\n", ret, (int)r.no_param_sql_len_, r.no_param_sql_ ? r.no_param_sql_ : "");
  embed_release_all();
  check(sql, ok);
}

static void tree_is(const char *sql, ObItemType root_type, int num_stmts) {
  ParseResult r;
  mayhem::init_parse_result(r, &g_pool_tag, kMode, false);
  int ret = mayhem::run_parser(r, sql, strlen(sql));
  bool ok = ret == 0 && r.result_tree_ != nullptr && r.result_tree_->type_ == root_type &&
            r.result_tree_->num_child_ == num_stmts;
  if (!ok) printf("  got ret=%d type=%d children=%d\n", ret, r.result_tree_ ? (int)r.result_tree_->type_ : -1,
                  r.result_tree_ ? (int)r.result_tree_->num_child_ : -1);
  embed_release_all();
  check(sql, ok);
}

static void stmt_type_is(const char *sql, ObItemType want) {
  ParseResult r;
  mayhem::init_parse_result(r, &g_pool_tag, kMode, false);
  int ret = mayhem::run_parser(r, sql, strlen(sql));
  bool ok = ret == 0 && r.result_tree_ && r.result_tree_->num_child_ >= 1 && r.result_tree_->children_[0] &&
            r.result_tree_->children_[0]->type_ == want;
  if (!ok) printf("  got ret=%d first=%d\n", ret,
                  (r.result_tree_ && r.result_tree_->children_ && r.result_tree_->children_[0])
                      ? (int)r.result_tree_->children_[0]->type_ : -1);
  embed_release_all();
  check(sql, ok);
}

static void sql_rejected(const char *sql) {
  ParseResult r;
  mayhem::init_parse_result(r, &g_pool_tag, kMode, false);
  int ret = mayhem::run_parser(r, sql, strlen(sql));
  embed_release_all();
  check(sql, ret != 0);
}

static void fts_is(const char *q, int want) {
  FtsParserResult r;
  fts_parse_docment(q, (int)strlen(q), &g_pool_tag, &r);
  int got = r.ret_;
  embed_release_all();
  check(q, got == want);
}

int main() {
  sql_id_is("select 1", "1FE1379FE2A31B8D16219655761820A2");
  sql_id_is("select 2.2345;", "99B8023929C1482A458CB071ADE822AC");
  sql_id_is("select a from t where a = 'hello' and b = 3;", "ADFDC18C98D0A001EF337A9EAB18CAD8");
  sql_id_is("select * from t;", "99C43C457B0C8E5AA6666E89C80DC5CE");
  parameterized_is("select a from t where a = 'hello' and b = 3;", "select a from t where a = ? and b = ?;");
  parameterized_is("insert into t values (1, 'x'), (2, 'y')", "insert into t values (?, ?), (?, ?)");
  tree_is("select 1", T_STMT_LIST, 1);
  stmt_type_is("select a from t", T_SELECT);
  stmt_type_is("insert into t values (1)", T_INSERT);
  stmt_type_is("update t set a = 1", T_UPDATE);
  stmt_type_is("delete from t", T_DELETE);
  sql_rejected("select from where )");
  sql_rejected("selec 1");
  fts_is("you me", FTS_OK);
  fts_is("+you +(-me2 +(A +C))", FTS_OK);
  fts_is("you ++me", FTS_ERROR_SYNTAX);
  fts_is("you (me2", FTS_ERROR_SYNTAX);
  printf("SELFTEST passed=%d failed=%d\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
