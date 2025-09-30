# Code Structure

```php
grip_ctrl/
├─ CMakeLists.txt                              # same flag: -DBUILD_GUI=ON to enable Qt
├─ config.yaml
├─ include/grip/                               # (unchanged from previous message)
│  ├─ can_bus.hpp
│  ├─ can_defs.hpp
│  ├─ control_interface.hpp
│  ├─ control_policy_native.hpp
│  ├─ protocol_codec.hpp
│  └─ settings.hpp
├─ src/                                        # (unchanged from previous message)
│  ├─ can_bus.cpp
│  ├─ control_interface.cpp
│  ├─ control_policy_native.cpp
│  ├─ main.cpp                                  # reused by CLI and GUI builds
│  └─ settings.cpp
├─ src_gui/
│  ├─ MainWindow.h
│  ├─ MainWindow.cpp
│  ├─ widgets/
│  │  ├─ SidePanel.h
│  │  └─ SidePanel.cpp
│  └─ models/
│     ├─ ActionStore.hpp                     # JSON load/save of actions & sets
│     └─ ActionStore.cpp
└─ vendor/
   ├─ zcan.h
   └─ libusbcanfd.(so|dll|dylib)

```

# Build & run
```bash
# CLI only (no GUI)
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cp vendor/libusbcanfd.so build/ 2>/dev/null || true
./build/grip_cli ./config.yaml

# GUI (same policy/threads underneath)
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release -D BUILD_GUI=ON
cmake --build build --config Release
./build/grip_gui ./config.yaml
```
