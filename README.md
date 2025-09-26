# Code Structure

```php
grip_ctrl/
├─ CMakeLists.txt
├─ config.yaml
├─ include/
│  └─ grip/
│     ├─ can_bus.hpp
│     ├─ can_defs.hpp
│     ├─ control_interface.hpp
│     ├─ control_policy_native.hpp
│     ├─ control_policy_python.hpp
│     ├─ protocol_codec.hpp
│     └─ settings.hpp
├─ src/
│  ├─ can_bus.cpp            # (kept header-only before; now split for clarity)
│  ├─ control_interface.cpp
│  ├─ control_policy_native.cpp
│  ├─ control_policy_python.cpp
│  ├─ settings.cpp
│  ├─ main_native.cpp
│  └─ main_python.cpp
└─ vendor/
   ├─ zcan.h                 # vendor header you provided
   └─ libusbcanfd.(so|dll)   # vendor library placed next to the binaries at runtime
```

# Build & run
```bash
# from grip_ctrl/
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# ensure vendor lib is next to binaries if needed
cp vendor/libusbcanfd.so build/ 2>/dev/null || true

# run native policy
./build/grip_native ./config.yaml

# run python policy (requires pybind11 + Python)
./build/grip_python ./config.yaml
```