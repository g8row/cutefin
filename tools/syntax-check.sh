#!/bin/sh
# Compile-only check for a single translation unit, without configuring the
# whole CMake project. Useful while parts of the tree are still missing (and
# for parallel work where sharing one build/ directory would race).
#
# It does not run moc, so Q_OBJECT vtable/link errors are invisible here --
# a clean run means "this file parses and type-checks", not "this links".
#
#   tools/syntax-check.sh src/ui/HomePage.cpp
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
qtlib=${QT_LIB_DIR:-/opt/homebrew/opt/qt/lib}

exec clang++ -std=c++17 -fsyntax-only -fPIC \
    -I "$root/src" -I "$root/src/ui" \
    -iframework "$qtlib" \
    -I "$qtlib/QtCore.framework/Headers" \
    -I "$qtlib/QtGui.framework/Headers" \
    -I "$qtlib/QtWidgets.framework/Headers" \
    -I "$qtlib/QtNetwork.framework/Headers" \
    -I "$qtlib/QtConcurrent.framework/Headers" \
    "$@"
