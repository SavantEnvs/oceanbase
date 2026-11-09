# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form, PREFERRED) — builds ON TOP of the org C/C++ base
# image (ghcr.io/savantenvs/base). The base already provides debian + clang/llvm +
# libFuzzer/afl++ + the identities + the build-contract ENV, so this only adds the
# per-commit build (built on top of the prebuilt base, rather than building the toolchain in-image).
#
# Build (from the repo root):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t mytag .

# The org base, pinned to a fixed DATE-tagged base (YYYYMMDD; shared by all integrations
# minted against it). Use the current dated tag; pin by digest in production: ...base@sha256:<digest>.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); do NOT bake it as an ENV — an
# empty value breaks clang ("must be a non-negative decimal integer") both here and for
# any in-image rebuild. build.sh unsets it when empty.
ARG SOURCE_DATE_EPOCH

# Sanitizer override: the base exports SANITIZER_FLAGS (ASan+UBSan, halting) as the default.
# To change it for a build (e.g. ASan-only, or add MSan): --build-arg SANITIZER_FLAGS="...".
# Declare it WITHOUT a default (no '='): an undefaulted ARG lets the base ENV flow through to
# build.sh when not passed, and an explicit --build-arg overrides it. (An empty default — `=` —
# would instead shadow the base ENV and silently wipe the sanitizers.)
ARG SANITIZER_FLAGS
# Coverage (optional, off by default): instrument the TEST build to measure how much of the project
# the suite exercises — --build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping".
# Empty by default (no effect on the normal oracle build); build.sh appends it to the test build only.
ARG COVERAGE_FLAGS=
# Build parallelism (optional). build.sh falls back to nproc when unset: --build-arg MAYHEM_JOBS=8
ARG MAYHEM_JOBS=

# If mayhem/build.sh needs EXTRA apt packages, install them as root before the build
# (re-pin to SOURCE_DATE_EPOCH first for reproducibility — repro-sources-list.sh ships
# in the base):
#   USER root
#   RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
#       apt-get update && apt-get install -yq --no-install-recommends <pkgs> \
#    && rm -rf /var/lib/apt/lists/*
#   USER mayhem

# Anything the upstream build would download, or a library apt lacks or ships at the wrong version, is
# built once from pinned source into a static prefix under /opt/toolchains/deps/<name>-<version>/
# (root-owned, world-readable, no .so); build.sh just points at it, e.g. via PKG_CONFIG_PATH
# (SPEC §6.2 item 19). The source stays in $d/src and the build runs out of tree with -gdwarf-3, so
# backtraces show /opt/toolchains/deps/... paths and the harness binary stays DWARF <= 3. Example:
#   USER root
#   ARG JANSSON_VER=2.14.1
#   ARG JANSSON_SHA256=<sha256 of the tarball>
#   RUN d=/opt/toolchains/deps/jansson-${JANSSON_VER} && mkdir -p "$d/src" /tmp/dep-build \
#    && curl -fsSL -o /tmp/dep.tgz \
#         https://github.com/akheron/jansson/releases/download/v${JANSSON_VER}/jansson-${JANSSON_VER}.tar.gz \
#    && echo "${JANSSON_SHA256}  /tmp/dep.tgz" | sha256sum -c - \
#    && tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d/src" && cd /tmp/dep-build \
#    && "$d/src/configure" --prefix="$d" --disable-shared --enable-static CFLAGS="-O2 -g -gdwarf-3" \
#    && make -j"$(nproc)" install \
#    && cd / && rm -rf /tmp/dep-build /tmp/dep.tgz && chmod -R a+rX "$d"
#   USER mayhem

USER root
ARG OB_REPO=http://mirrors.aliyun.com/oceanbase/development-kit/el/8/x86_64
# OceanBase's own prebuilt development-kit RPMs (the same ones deps/init/oceanbase.el8.x86_64.deps
# installs): header/static-lib deps of the SQL parser, plus bison 2.4.1 and flex 2.5.35, the only
# generators that produce a working parser from upstream's .y/.l (bison 3.x breaks the actions).
RUN set -eux; \
    if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then repro-sources-list.sh || true; fi; \
    apt-get update && apt-get install -yq --no-install-recommends rpm2cpio cpio m4 \
 && rm -rf /var/lib/apt/lists/*; \
    fetch() { \
      name="$1"; rpm="$2"; sha="$3"; sub="$4"; d=/opt/toolchains/deps/$name; \
      mkdir -p "$d/src" /tmp/rpmx; \
      curl -fsSL -o /tmp/dep.rpm "$OB_REPO/$rpm"; \
      echo "$sha  /tmp/dep.rpm" | sha256sum -c -; \
      cp /tmp/dep.rpm "$d/src/$rpm"; \
      (cd /tmp/rpmx && rpm2cpio /tmp/dep.rpm | cpio -idm --quiet); \
      cp -a "/tmp/rpmx/usr/local/oceanbase/$sub/." "$d/"; \
      rm -rf /tmp/rpmx /tmp/dep.rpm; chmod -R a+rX "$d"; \
    }; \
    fetch obcharset-1.0.0 devdeps-obcharset-1.0.0-32025111215.el8.x86_64.rpm 9b0057660c69eba5c04fbfa5feba0714cfbfbdad4f96267396b5b850e7e2bd9f deps/devel; \
    fetch rapidjson-1.0.0 devdeps-relaxed-rapidjson-1.0.0-52023011719.el8.x86_64.rpm cde7c2f75d504721a1b977432f017fd37ab535f829a164093a01f3aea5a47304 deps/devel; \
    fetch fast-float-6.1.3 devdeps-fast-float-6.1.3-42024112122.el8.x86_64.rpm 3d8b6f5ec54dc663fe2411022374d237161478d0e175c88f221de0b972da268e deps/devel; \
    fetch bison-2.4.1 obdevtools-bison-2.4.1-12022100413.el8.x86_64.rpm 3a3bdbb952249c6ed9d77504ee10cb8ade2ab99a36b0b107d9e1bf4e830c9890 devtools; \
    fetch flex-2.5.35 obdevtools-flex-2.5.35-12022100417.el8.x86_64.rpm 8bf8a88e1ce62bd61643e0f52a78a1d2cc3d15e2ef2a7bb26f1ba7729dd68730 devtools
USER mayhem

# Source enters from the build CONTEXT (the CI checkout): the FULL repo (incl. .git
# history) is copied to /mayhem as `mayhem`; build.sh builds in place.
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

# Anything a Dockerfile step materializes into the tree (downloaded fixtures, generated headers,
# vendored deps) MUST be committed or (re)created by build.sh: rlenv's patch grader runs
# `git clean -ffdX` before every graded build, so a gitignored, untracked file is gone by then and
# every honest patch scores zero (SPEC §6.2 item 16; verify-repo.sh checks it).
#
# OPTIONAL compile-cache pre-fill (SPEC §6.2 item 18). Bases minted with /opt/mayhem/ccache/prefill
# run the build under rlenv's pinned ccache, so the patch grader's first (otherwise cold) rebuild is
# warm — this is what keeps large C/C++ targets inside the build time limit. Older bases fall back to
# the plain build; a missing or stale pre-fill only costs a cold cache. Never enable ccache in
# build.sh or via ENV/PATH, and keep the compiler resolving by name (CC=clang) or nothing is cached.
RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

# Run the project's functional tests at build time IF the repo ships them (mayhem/test.sh is
# optional — omit it only when the repo has no tests meaningful for RL). A failing suite fails the
# image build, so the commit image is guaranteed to build AND pass its own tests.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
