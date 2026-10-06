# Applestia.Glass

Qt 6 C++/QML module: the client side of `../plugin/protocols/applestia-glass-shapes-v1.xml`. It reports the exact
geometry of glass controls (`LiquidGlass` items) to the compositor, which renders a native lens for each.
It uses private Qt Quick/Wayland APIs, so rebuild it after a Qt upgrade (`./install.sh` does this).

Built and installed by the top-level `install.sh`. By hand:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DGLASS_QML_INSTALL_DIR:STRING=lib/qt6/qml
cmake --build build
cmake --install build
```

Tests: configure with `-DBUILD_TESTING=ON` and run `ctest --test-dir build`.
