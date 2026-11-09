#!/usr/bin/env bash
#
# mayhem/build.sh — builds OceanBase's embeddable SQL parser (src/sql/parser, the proxy-parser
# library `ob_sql_proxy_parser_objects`) three ways and links the harnesses against it:
#   1. sanitized + DWARF-3, into mayhem/build/obj-san        -> the fuzz and -standalone binaries
#   2. normal flags, into mayhem/build/obj-plain             -> mayhem/selftest (run by test.sh)
# The observer itself (millions of lines, a bespoke CMake/dep_create world) is not buildable air-gapped
# in a fuzz image, and the parser is the part of OceanBase that eats untrusted SQL text.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
R="$SRC"
B="$R/mayhem/build"
D=/opt/toolchains/deps
P="$R/src/sql/parser"
G="$B/gen/src/sql/parser"
OBINC="$D/obcharset-1.0.0/include"

# ---- 1. generate the bison/flex sources (upstream's gen_parser.sh, out of tree) -------------------
# Only OceanBase's own bison 2.4.1 / flex 2.5.35 produce a working parser from these grammars
# (bison 3.x rejects the actions and calls yylex with a different arity), so use the prebuilt ones.
export BISON_PKGDATADIR="$D/bison-2.4.1/share/bison" M4=/usr/bin/m4
BISON="$D/bison-2.4.1/bin/bison"
FLEX="$D/flex-2.5.35/bin/flex"
# MAYHEM_REUSE=1 (dev only, default off): keep generated sources and up-to-date objects of a previous run.
if [ "${MAYHEM_REUSE:-0}" != 1 ] || [ ! -f "$G/type_name.c" ]; then
rm -rf "$B/gen"
mkdir -p "$G"
(
  cd "$P"
  "$BISON" -d ftsparser.y -o "$G/ftsparser_tab.c"
  "$BISON" -d sql_parser_mysql_mode.y -o "$G/sql_parser_mysql_mode_tab.c"
  cd "$G"
  "$FLEX" -Cfa -B -8 -o ftsblex_lex.c "$P/ftsblex.l" ftsparser_tab.h
  "$FLEX" -Cfa -B -8 -o sql_parser_mysql_mode_lex.c "$P/sql_parser_mysql_mode.l" sql_parser_mysql_mode_tab.h
  for f in ftsblex_lex.c sql_parser_mysql_mode_lex.c; do
    sed '/This var may be unused depending upon options./d' -i "$f"
    sed "/Setup the input buffer state to scan the given bytes/,/}/{/int i/d}" -i "$f"
    sed "/Setup the input buffer state to scan the given bytes/,/}/{/for ( i = 0; i < _yybytes_len; ++i )/d}" -i "$f"
    sed "/Setup the input buffer state to scan the given bytes/,/}/{s/\tbuf\[i\] = yybytes\[i\]/memcpy(buf, yybytes, _yybytes_len)/g}" -i "$f"
  done
  # flex 2.5.35 omits the yyg declaration from yy_try_NUL_trans (fixed in 2.5.36)
  sed '/static yy_state_type yy_try_NUL_trans  (/{n;s/^{$/{\n    struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;/}' -i sql_parser_mysql_mode_lex.c
  sed "/obsql_mysql_yylex_init is special because it creates the scanner itself/,/Initialization is the same as for the non-reentrant scanner/{s/return 1/return errno/g}" -i sql_parser_mysql_mode_lex.c
)
"$P/gen_type_name.sh" "$R/src/objit/include/objit/common/ob_item_type.h" > "$G/type_name.c"
fi
[ "${MAYHEM_REUSE:-0}" = 1 ] || rm -rf "$B/obj-san" "$B/obj-plain"
mkdir -p "$B/obj-san" "$B/obj-plain"

# ---- 2. compile the parser library ----------------------------------------------------------------
# Plain strings (no spaces in any path) so the flags survive `export` into the xargs workers.
INC="-I$B/gen/src -I$G -I$R -I$R/src -I$R/src/plugin/legacy/include -I$R/deps/easy/src
     -I$R/deps/easy/src/include -I$R/deps/oblib/src -I$R/deps/oblib/src/common -I$R/deps/ussl-hook
     -I$R/deps/ussl-hook/loop -I$R/src/objit/include -I$R/src/objit/src -I$P
     -I$R/deps/oblib/src/common/sql_mode -I$R/src/objit/include/objit/common
     -I$R/deps/oblib/src/lib/charset -I$OBINC -I$D/rapidjson-1.0.0/include -I$D/fast-float-6.1.3/include"
CXXFLAGS="-std=gnu++17 -fdelayed-template-parsing -Wno-register -DSQL_PARSER_COMPILATION -w"
CFLAGS="-DSQL_PARSER_COMPILATION -w"
export INC CXXFLAGS CFLAGS

PARSER_SRCS="$G/ftsblex_lex.c $G/ftsparser_tab.c $G/sql_parser_mysql_mode_lex.c
  $G/sql_parser_mysql_mode_tab.c $G/type_name.c
  $P/fts_base.c $P/fts_parse.c $P/non_reserved_keywords_mysql_mode.c $P/ob_non_reserved_keywords.c
  $P/parse_malloc.cpp $P/parse_node.c $P/parse_node_hash.cpp
  $P/sql_parser_base.c $P/parser_utility.cpp
  $R/deps/oblib/src/lib/hash_func/murmur_hash.cpp $R/deps/easy/src/util/easy_string.c
  $R/mayhem/embed.cc"
for c in ob_ctype_bin ob_ctype ob_ctype_gbk ob_ctype_latin1 ob_ctype_gb2312 ob_ctype_ujis ob_ctype_euckr \
         ob_ctype_eucjpms ob_ctype_cp932 ob_ctype_ascii ob_ctype_tis620 ob_ctype_mb ob_ctype_simple \
         ob_ctype_utf8 ob_dtoa ob_ctype_utf16 ob_ctype_uca ob_ctype_gb18030; do
  PARSER_SRCS="$PARSER_SRCS $R/deps/oblib/src/lib/charset/$c.cc"
done

# compile_one <obj-dir> <flags> <source>
compile_one() {
  local odir="$1" fl="$2" src="$3"
  local obj="$odir/$(basename "$src").o"
  [ "${MAYHEM_REUSE:-0}" = 1 ] && [ "$obj" -nt "$src" ] && return 0
  case "$src" in
    *.cpp|*.cc) $CXX $fl $CXXFLAGS $INC -c "$src" -o "$obj" ;;
    # non_reserved_keywords_mysql_mode.c uses ParseResult without including its header
    *non_reserved_keywords_mysql_mode.c) $CC $fl $CFLAGS $INC -include sql/parser/parse_node.h -c "$src" -o "$obj" ;;
    *) $CC $fl $CFLAGS $INC -c "$src" -o "$obj" ;;
  esac
}
export -f compile_one

build_objs() {   # build_objs <obj-dir> <flags>
  printf '%s\n' $PARSER_SRCS | xargs -P "$MAYHEM_JOBS" -I{} bash -c 'compile_one "$@"' _ "$1" "$2" {}
}

# -fsanitize=fuzzer-no-link puts SanCov on the parser itself (the harness TU alone would record 0 edges)
SANF="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS -O1"
# the selftest's copy of the parser gets the project's normal flags (no sanitizer, no -gdwarf-3); the two
# builds are independent, and each is bounded by the one 7.5MB generated grammar TU, so run them together
PLAINF="-O1 $COVERAGE_FLAGS"
build_objs "$B/obj-san" "$SANF" &
san_pid=$!
build_objs "$B/obj-plain" "$PLAINF" &
plain_pid=$!
wait $san_pid || exit 1
wait $plain_pid || exit 1

# ---- 3. harnesses: fuzz binary + run-once standalone reproducer, each linked with lsan_off ----------
CHARSET_TABLES="$D/obcharset-1.0.0/lib/obcharset/libcharset_tables.a"
TARGETS="fuzz_sql_parser fuzz_sql_fast_parser fuzz_fts_parser"
# an archive, so objects nothing references (the observer-side fullwidth converter) stay out of the link
rm -f "$B/libparser-san.a"
ar rcs "$B/libparser-san.a" "$B"/obj-san/*.o
SAN_OBJS="$B/libparser-san.a"
$CXX $SANF -c "$R/mayhem/lsan_off.cc" -o "$B/lsan_off.o"
$CC $SANF -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o"
for t in $TARGETS; do
  $CXX $SANF $CXXFLAGS $INC -I"$R/mayhem" -c "$R/mayhem/$t/$t.cc" -o "$B/$t.o"
  $CXX $SANF $LIB_FUZZING_ENGINE "$B/$t.o" $SAN_OBJS "$B/lsan_off.o" "$CHARSET_TABLES" -lcrypto -o "$R/$t"
  $CXX $SANF "$B/$t.o" "$B/standalone_main.o" $SAN_OBJS "$B/lsan_off.o" "$CHARSET_TABLES" -lcrypto -o "$R/$t-standalone"
done

# ---- 4. the test suite, with the normal flags: mayhem/test.sh only runs it ----
rm -f "$B/libparser-plain.a"
ar rcs "$B/libparser-plain.a" "$B"/obj-plain/*.o
$CXX $PLAINF $CXXFLAGS $INC -I"$R/mayhem" "$R/mayhem/selftest.cc" "$B/libparser-plain.a" "$CHARSET_TABLES" \
  -lcrypto -o "$R/selftest"
