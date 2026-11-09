// Fuzzes the lexer/grammar in obproxy fast-parse mode (is_fp_): parameterises a SQL statement and derives
// its sql_id (MD5 of the parameterised text), as ObSQLParser::parse_and_gen_sqlid does.
// The first input byte picks the sql_mode (MySQL mode only) and a GBK connection; the rest is the SQL text.
#include <stddef.h>
#include <stdint.h>

#include "../parse_ctx.h"

static int g_pool_tag;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1 || size > (1u << 16)) return 0;
  const uint8_t flags = data[0];
  const char *sql = reinterpret_cast<const char *>(data + 1);
  const int64_t len = static_cast<int64_t>(size - 1);

  uint64_t mode = DEFAULT_MYSQL_MODE;
  if (flags & 1) mode |= SMO_ANSI_QUOTES | SMO_PIPES_AS_CONCAT;
  if (flags & 2) mode |= SMO_NO_BACKSLASH_ESCAPES;

  char sql_id[33];
  mayhem::fast_sql_id(sql, len, mode, &g_pool_tag, sql_id, (flags & 4) != 0);

  embed_release_all();
  return 0;
}
