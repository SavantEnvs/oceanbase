// The embedder side of OceanBase's standalone SQL parser (what obproxy supplies): the parser
// allocates through parser_alloc_buffer and calls back into a handful of host functions.
#include <stdint.h>
#include <stdlib.h>
#include <sys/resource.h>

#include "embed.h"
#include "sql/parser/parse_node.h"

namespace {
struct Block {
  Block *prev;
  Block *next;
};
Block *g_head = nullptr;

const uintptr_t kHeader = (sizeof(Block) + 15) & ~uintptr_t(15);

uintptr_t g_stack_top = 0;
uintptr_t g_stack_budget = 0;
}  // namespace

void embed_release_all() {
  while (g_head != nullptr) {
    Block *b = g_head;
    g_head = b->next;
    free(b);
  }
}

void *parser_alloc_buffer(void *malloc_pool, const int64_t buff_size) {
  if (malloc_pool == nullptr || buff_size <= 0) return nullptr;
  Block *b = static_cast<Block *>(malloc(kHeader + static_cast<size_t>(buff_size)));
  if (b == nullptr) return nullptr;
  b->prev = nullptr;
  b->next = g_head;
  if (g_head != nullptr) g_head->prev = b;
  g_head = b;
  return reinterpret_cast<char *>(b) + kHeader;
}

void parser_free_buffer(void *malloc_pool, void *buffer) {
  (void)malloc_pool;
  if (buffer == nullptr) return;
  Block *b = reinterpret_cast<Block *>(static_cast<char *>(buffer) - kHeader);
  if (b->prev != nullptr) b->prev->next = b->next; else g_head = b->next;
  if (b->next != nullptr) b->next->prev = b->prev;
  free(b);
}

// A real depth check (as the host server does), so deeply nested input yields the parser's
// "stack overflow" error instead of killing the process.
static bool stack_nearly_exhausted() {
  char probe;
  uintptr_t here = reinterpret_cast<uintptr_t>(&probe);
  if (g_stack_top == 0) {
    struct rlimit rl;
    uintptr_t limit = 8u << 20;
    if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < limit)
      limit = rl.rlim_cur;
    g_stack_top = here;
    g_stack_budget = limit / 2;
  }
  return g_stack_top > here && g_stack_top - here > g_stack_budget;
}

extern "C" {
// Optional AVX accelerator for ob_parse_binary (src/sql/parser/ob_parse_simd.cpp, which drags in the
// observer headers); parse_node.c finishes the remaining bytes with its scalar loop.
void ob_parse_binary_simd(const char **src, const char *end, char **dest) {
  (void)src;
  (void)end;
  (void)dest;
}

// Oracle-mode fullwidth symbol mapping (ob_parser_fullwidth_converter*.cpp) needs the observer's charset
// and logger; the MySQL-mode parser fuzzed here never reaches it, so it is a pass-through.
int sql_parser_preprocess_fullwidth_symbols(ParseResult *parse_result, const char *input_sql, int32_t input_len,
                                            const char **processed_sql, int32_t *processed_len,
                                            void **full_width_sym_converter) {
  (void)parse_result;
  *processed_sql = input_sql;
  *processed_len = input_len;
  *full_width_sym_converter = nullptr;
  return 0;
}

void sql_parser_postprocess_fullwidth_symbols(ParseResult *parse_result, void *full_width_sym_converter,
                                              int32_t processed_len) {
  (void)parse_result;
  (void)full_width_sym_converter;
  (void)processed_len;
}

int check_stack_overflow_in_c(int *check_overflow) {
  *check_overflow = stack_nearly_exhausted() ? 1 : 0;
  return 0;
}

bool check_stack_overflow_c() { return stack_nearly_exhausted(); }

void right_to_die_or_duty_to_live_c() { abort(); }

int ob_backtrace_c(void **buffer, int size) {
  (void)buffer;
  (void)size;
  return 0;
}

char *parray_c(char *buf, int64_t len, int64_t *array, int size) {
  (void)buf;
  (void)len;
  (void)array;
  (void)size;
  return nullptr;
}
}
