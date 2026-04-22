#!/usr/bin/env bash
set -euo pipefail

LIBEVENT_VERSION="${LIBEVENT_VERSION:-libevent-2.2.1-alpha-dev}"
LIBEVENT_RELEASE_TAG="${LIBEVENT_RELEASE_TAG:-release-2.2.1-alpha}"
LIBEVENT_URL="${LIBEVENT_URL:-https://github.com/libevent/libevent/releases/download/${LIBEVENT_RELEASE_TAG}/${LIBEVENT_VERSION}.tar.gz}"

PREFIX_DIR="${1:-$PWD/.deps/libevent}"
SOURCE_OVERRIDE="${LIBEVENT_SOURCE_DIR:-}"

ARCHIVE_DIR="${PREFIX_DIR}/archive"
SOURCE_DIR="${PREFIX_DIR}/source"
BUILD_DIR="${PREFIX_DIR}/build"
INSTALL_DIR="${PREFIX_DIR}/install"
ARCHIVE_PATH="${ARCHIVE_DIR}/${LIBEVENT_VERSION}.tar.gz"

mkdir -p "${ARCHIVE_DIR}" "${SOURCE_DIR}" "${BUILD_DIR}" "${INSTALL_DIR}"

if [ -n "${SOURCE_OVERRIDE}" ]; then
  rm -rf "${SOURCE_DIR}"
  mkdir -p "${SOURCE_DIR}"
  cp -a "${SOURCE_OVERRIDE}/." "${SOURCE_DIR}/"
else
  if [ ! -f "${ARCHIVE_PATH}" ]; then
    curl --fail --location --retry 3 --output "${ARCHIVE_PATH}" "${LIBEVENT_URL}"
  fi
  rm -rf "${SOURCE_DIR}"
  mkdir -p "${SOURCE_DIR}"
  tar -xf "${ARCHIVE_PATH}" --strip-components=1 -C "${SOURCE_DIR}"
fi

cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
  -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON \
  -DEVENT__DISABLE_BENCHMARK=ON \
  -DEVENT__LIBRARY_TYPE=SHARED \
  -DEVENT__DISABLE_MBEDTLS=ON \
  -DEVENT__DISABLE_OPENSSL=OFF \
  -DEVENT__DISABLE_REGRESS=ON \
  -DEVENT__DISABLE_SAMPLES=ON \
  -DEVENT__DISABLE_TESTS=ON

cmake --build "${BUILD_DIR}" --parallel
cmake --install "${BUILD_DIR}"
