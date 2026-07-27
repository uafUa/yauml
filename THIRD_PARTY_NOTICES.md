# Third-party runtime notices

The Windows package contains dynamically linked runtime components produced by
other projects. Their inclusion does not change the license of yauml source
code.

## Qt

The application uses Qt 6 libraries and QML modules. The Windows setup program
is generated with Qt Installer Framework. Qt open-source licensing and
obligations are described at:

- https://www.qt.io/licensing/open-source-lgpl-obligations
- https://doc.qt.io/qt-6/licensing.html

Qt source packages are available from:

- https://download.qt.io/archive/qt/

## LLVM and Clang

The C++ import feature uses LLVM's `libclang`. LLVM is distributed under the
Apache License 2.0 with LLVM Exceptions:

- https://llvm.org/LICENSE.txt
- https://github.com/llvm/llvm-project

## Microsoft Visual C++ runtime

Windows packages include Microsoft Visual C++ runtime files from the compiler
toolchain used to build yauml. Their redistribution is governed by the
Microsoft Visual Studio licensing terms:

- https://visualstudio.microsoft.com/license-terms/

This notice is informational. Before distributing the application outside the
project's current private audience, review the complete application and
third-party licensing policy.
