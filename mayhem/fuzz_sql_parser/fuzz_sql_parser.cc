// Fuzzes OceanBase's embeddable MySQL-mode SQL parser (src/sql/parser): the flex lexer, the bison
// grammar and the parse-node builder, via parse_sql.
// The first input byte picks the sql_mode and parser switches; the rest is the SQL text.
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
  switch (flags & 3) {
    case 1: mode |= SMO_ANSI_QUOTES | SMO_PIPES_AS_CONCAT | SMO_IGNORE_SPACE; break;
    case 2: mode |= SMO_NO_BACKSLASH_ESCAPES; break;
    case 3: mode |= SMO_HIGH_NOT_PRECEDENCE; break;
    default: break;
  }

  ParseResult result;
  mayhem::init_parse_result(result, &g_pool_tag, mode, (flags & 4) != 0);
  result.is_ignore_hint_ = (flags & 8) != 0;
  result.is_multi_query_ = (flags & 16) != 0;
  result.need_parameterize_ = (flags & 32) == 0;
  mayhem::run_parser(result, sql, len);

  embed_release_all();
  return 0;
}
