// Fuzzes the full-text boolean-mode query parser (src/sql/parser/ftsparser.y + ftsblex.l),
// which parses the AGAINST('...' IN BOOLEAN MODE) operand.
#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "sql/parser/fts_base.h"
#include "sql/parser/fts_parse.h"
}
#include "../embed.h"

static int g_pool_tag;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > (1u << 16)) return 0;
  FtsParserResult result;
  fts_parse_docment(reinterpret_cast<const char *>(data), static_cast<int>(size), &g_pool_tag,
                    &result);
  embed_release_all();
  return 0;
}
