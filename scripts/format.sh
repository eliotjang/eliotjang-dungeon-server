#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
git ls-files '*.cc' '*.h' | xargs clang-format -i
