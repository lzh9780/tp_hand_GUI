# TP hand Graphical Control Interface

---

## Pre-requests

### Install necessary requests
```bash
sudo apt update

sudo apt install -y \
	  build-essential \
	  cmake \
	  git \
	  pkg-config \
	  qtbase5-dev \
	  qtbase5-dev-tools \
	  libqt5charts5-dev \
	  libyaml-cpp-dev \
	  libogre-1.12-dev \
	  libassimp-dev \
	  libtinyxml2-dev \
	  libopencv-dev \
	  python3 \
	  python3-dev \
	  python3-pip \
	  python3-numpy \
	  pybind11-dev
```

### Hand Detect
```bash
python3 -m pip install \
	  opencv-python \
	  mediapipe \
	  pyrealsense2
```

### UDE Glove Teleoperation (Optional)

Follow [Offical Document](https://hs7ghlauag.feishu.cn/docx/BlqKdFOSeoFQG1xzbV9ccNMMnzc) to install drive and SDK.

### Manus Glove Teleoperation (Optional)

Install Manus SDK follows [Manus Offical Guide](https://docs.manus-meta.com/latest/Plugins/SDK/Linux/)

The linux SDK `SDKClient_Linux` should be copied into `third_party` folder for compile. 

This program tested on Manus SDK `3.1.1`.

---

## CAN Device Install & Permissions

ZLG official documentation: [ZLG Linux Drive](https://manual.zlg.cn/web/#/42/1710)

### Simple Install Steps:
- Download drive from [https://manual.zlg.cn/web/#/146](https://manual.zlg.cn/web/#/146)
- Install libusb-1.0
  ```bash
  sudo apt-get install libusb-1.0-0
  ```
- Unzip the downloaded drive, enter unzipped folder and run
  ```bash
  sudo cp libusbcanfd.so /lib
  ```

### Permissions

If the current user is a regular user, performing device operations requires using `sudo` to invoke root privileges, or you can modify the device's access permissions using the following command:

```bash
sudo chmod 666 /dev/bus/usb/xxx/yyy
```
Where `xxx` corresponds to the Bus serial number of the USB device in the `lsusb` output, and `yyy` corresponds to the Device serial number. For example, the Bus serial number of USBCANFD is 004, and the Device serial number is 057.

To permanently modify user permissions, you need to modify the udev configuration by adding the file: `/etc/udev/rules.d/50-usbcanfd.rules`, with the following content:

```text
SUBSYSTEMS=="usb", ATTRS{idVendor}=="04cc", ATTRS{idProduct}=="1240", GROUP="users", MODE="0666"
```

Reload the udev rules using the following command:

```bash
udevadm control --reload
```

Finally, plugging and unplugging the device will apply the new permissions.

---

## Build & run
```bash
# Only GUI
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
# GUI + Manus + UDE
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release -D BUILD_UDE_GLOVE=ON -D BUILD_MANUS_GLOVE=ON

cmake --build build --config Release

# Run
./build/grip_gui
```

---

## Usage

Upon program startup, the main interface is displayed as shown in the image below. 

![main_page](./images/main_page.png)

The left side comprises the **Action Panel** (upper) and the **Control Panel** (lower), while the right side contains two tabbed widgets. These tabs support drag-and-drop functionality, allowing users to reorder them within the same window or detach them into separate floating windows. By default, the **Connector Panel** and **Log Panel** are open.

### Connector Panel

From this panel, users can select a hand model and establish a connection to the CanFD device.

![connector](./images/connector.png)

The available options are as follows:

1. Select a model – Choose the hand model, which determines the configuration loaded by the software.

2. No device connection – Load all software functionalities without connecting to the CanFD device.

3. Connect / Disconnect – Initiate or terminate the connection to the CanFD device; this action also loads or clears the corresponding control widgets.

The main page after connect to hand will be

![main_page_loaded](./images/main_page_loaded)

### Control Panel

This is the primary interface for sending control commands to the hand, including mode selection, position targeting, and real-time sensor feedback.

![control_panel](./images/control_panel.png)

1. Mode: Follow / Setup – Determines how commands are issued. In Follow mode, the target position is sent immediately whenever a joint value changes. In Setup mode, the new target is applied to all joints only when the Execute button is clicked.

2. Execute – Applies the new target positions for all joints when in Setup mode. This button is disabled in Follow mode.

3. Reset – Sets all joint target values to zero.

4. Control modes: Finger / Wrist – Toggles the control mode for either the fingers or the wrist. Due to structural differences between the finger and wrist mechanisms, the applicable control modes differ for each.

5. Advanced – Opens the advanced control panel on the right side. For more details, please refer to the [Advance Control Panel](#advance-control-panel) section.

6. Input boxes / Sliders – Allow user to adjust joint target values. These two controls are synchronized, so editing a value in either one will automatically update the other.

7. Red / Green indicator lights – Indicate the current status of each joint. A red light means the joint is not ready for control, while a green light means it is ready and can be commanded.

8. Numbers enclosed - Realtime joint angle feedback. 

### Advance Control Panel

---

## License

This software is provided under the [Apache License 2.0](LICENSE).
