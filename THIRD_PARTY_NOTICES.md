# Third-party notices

The application is built from the versions pinned in `scripts/dependencies-macos.json`.

| Component | License / source |
| --- | --- |
| Qt 6.11.2 | LGPL-3.0 / GPL-3.0; https://code.qt.io/cgit/qt/qtbase.git/ |
| QScintilla 2.14.1 | GPL-3.0; https://riverbankcomputing.com/software/qscintilla/ |
| MongoDB C Driver and libbson 2.5.3 | Apache-2.0 and bundled notices; https://github.com/mongodb/mongo-c-driver |
| mongosh libraries | Apache-2.0 and dependency-specific notices; https://github.com/mongodb-js/mongosh |
| Node.js 26.8.2 | MIT and bundled third-party notices; https://nodejs.org/ |
| OpenSSL 4.0.2 | Apache-2.0; https://www.openssl.org/ |
| libssh2 1.11.1 | BSD-style; https://libssh2.org/ |
| GoogleTest 1.18.0 (build/test only) | BSD-3-Clause; https://github.com/google/googletest |

License texts for native dependencies are in `licenses/` and included in the app.
JavaScript packages retain their license files inside `Resources/runtime/node_modules`.
Qt is dynamically linked; its frameworks can be replaced with compatible builds.
Dependency source archives, including Qt source, are available from the upstream
projects above. The complete application build scripts are included in this repository.
