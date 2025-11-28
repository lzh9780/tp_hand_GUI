import cv2
import numpy as np
import mediapipe as mp
import math
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
from collections import deque
import time
from threading import Thread, Lock, Event
from copy import deepcopy
# Intel RealSense
import pyrealsense2 as rs


# -------------------------- Config --------------------------
MODEL_PATH = "hand_landmarker.task"  # Put the .task model file in the same folder as this script
COLOR_W, COLOR_H, FPS = 1280, 720, 30
CLIPPING_DISTANCE_M = 0.6  # meters
MASK_MEDIAN_KSIZE = 5      # median blur to denoise the mask (set 0/1 to disable)
HALO_PIX = 12  # pixels; width of the allowed RGB "halo" around detected near objects (set 0 to disable)
DRAW_CONNECTIONS = True
HAND_CONNECTIONS = mp.solutions.hands.HAND_CONNECTIONS

# 定义手部关键点索引（MediaPipe 手部模型的拓扑结构索引）
WRIST = 0
THUMB_CMC, THUMB_MCP, THUMB_IP, THUMB_TIP = 1, 2, 3, 4
INDEX_MCP, INDEX_PIP, INDEX_DIP, INDEX_TIP = 5, 6, 7, 8
MIDDLE_MCP, MIDDLE_PIP, MIDDLE_DIP, MIDDLE_TIP = 9, 10, 11, 12
RING_MCP, RING_PIP, RING_DIP, RING_TIP = 13, 14, 15, 16
PINKY_MCP, PINKY_PIP, PINKY_DIP, PINKY_TIP = 17, 18, 19, 20

import math

class LowPassFilter: # Level 1
    def __init__(self, fs, fc, init=0.0):
        self.fs = fs
        self.fc = fc
        self.alpha = (2*math.pi*fc) / (2*math.pi*fc + fs)
        self.y = init
    
    def update(self, x):
        self.y = self.y + self.alpha * (x - self.y)
        return self.y

class MovingAverageFilter:
    def __init__(self, window_size):
        self.window_size = window_size
        self.buffer = deque(maxlen=window_size)
    
    def update(self, x):
        self.buffer.append(x)
        return np.mean(self.buffer)

class HandDetector():
    def __init__(self):
        
        # -------------------- Helper for drawing --------------------
        self.mp_drawing = mp.solutions.drawing_utils
        self.mp_styles = mp.solutions.drawing_styles
        self.frame_buffer = deque(maxlen=1)
        
        self.hand_detected = False
        
        self.BaseOptions = mp.tasks.BaseOptions
        self.HandLandmarker = vision.HandLandmarker
        self.HandLandmarkerOptions = vision.HandLandmarkerOptions
        self.VisionRunningMode = vision.RunningMode
        
        self.stop_flag = False
        self.lock = Lock()
        self.thr:Thread = None
        
        # Low Pass Filter
        self.low_pass_filter_size = 5
        
        self.finger_joints = {"thumb": {"cmc_rot": 0.0, "cmc_pull": 0.0, "mcp": 0.0, "ip": 0.0}, 
                           "index": {"mcp": 0.0, "pip": 0.0, "dip": 0.0, "sway": 0.0}, 
                           "middle":{"mcp": 0.0, "pip": 0.0, "dip": 0.0, "sway": 0.0}, 
                           "ring": {"mcp": 0.0, "pip": 0.0, "dip": 0.0, "sway": 0.0}, 
                           "pinky": {"mcp": 0.0, "pip": 0.0, "dip": 0.0, "sway": 0.0}}
        
        self.previous_finger_joints = {"thumb": {"cmc_rot": MovingAverageFilter(self.low_pass_filter_size), "cmc_pull":  MovingAverageFilter(self.low_pass_filter_size), "mcp":  MovingAverageFilter(self.low_pass_filter_size), "ip":  MovingAverageFilter(self.low_pass_filter_size)}, 
                                        "index": {"mcp":  MovingAverageFilter(self.low_pass_filter_size), "pip":  MovingAverageFilter(self.low_pass_filter_size), "dip":  MovingAverageFilter(self.low_pass_filter_size), "sway":  MovingAverageFilter(self.low_pass_filter_size)}, 
                                        "middle":{"mcp":  MovingAverageFilter(self.low_pass_filter_size), "pip":  MovingAverageFilter(self.low_pass_filter_size), "dip":  MovingAverageFilter(self.low_pass_filter_size), "sway":  MovingAverageFilter(self.low_pass_filter_size)}, 
                                        "ring": {"mcp":  MovingAverageFilter(self.low_pass_filter_size), "pip":  MovingAverageFilter(self.low_pass_filter_size), "dip":  MovingAverageFilter(self.low_pass_filter_size), "sway":  MovingAverageFilter(self.low_pass_filter_size)}, 
                                        "pinky": {"mcp":  MovingAverageFilter(self.low_pass_filter_size), "pip":  MovingAverageFilter(self.low_pass_filter_size), "dip":  MovingAverageFilter(self.low_pass_filter_size), "sway":  MovingAverageFilter(self.low_pass_filter_size)}}
        
        self.previous_finger_joints_2 = {"thumb": {"cmc_rot": LowPassFilter(30, 6), "cmc_pull":  LowPassFilter(30, 6), "mcp":  LowPassFilter(30, 6), "ip":  LowPassFilter(30, 6)}, 
                                        "index": {"mcp":  LowPassFilter(30, 6), "pip":  LowPassFilter(30, 6), "dip":  LowPassFilter(30, 6), "sway":  LowPassFilter(30, 6)}, 
                                        "middle":{"mcp":  LowPassFilter(30, 6), "pip":  LowPassFilter(30, 6), "dip":  LowPassFilter(30, 6), "sway":  LowPassFilter(30, 6)}, 
                                        "ring": {"mcp":  LowPassFilter(30, 6), "pip":  LowPassFilter(30, 6), "dip":  LowPassFilter(30, 6), "sway":  LowPassFilter(30, 6)}, 
                                        "pinky": {"mcp":  LowPassFilter(30, 6), "pip":  LowPassFilter(30, 6), "dip":  LowPassFilter(30, 6), "sway":  LowPassFilter(30, 6)}}

        self.wrist_joints = {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}

        self.previous_wrist_joints = {"pitch": MovingAverageFilter(20), "yaw": MovingAverageFilter(10), "roll": MovingAverageFilter(10)}
        
        self.previous_wrist_joints_2 = {"pitch": LowPassFilter(30, 1.5), "yaw": LowPassFilter(30, 3), "roll": LowPassFilter(30, 3)}

        self.curr_img = None
        # self.hands = {"left": None, "right": None} # init
        # self.hands["left"] = self.finger_joints # left hand value
        # self.hands["right"] = deepcopy(self.finger_joints) # right hand value (need copy)
        
    def setup_realsense(self):
        ctx = rs.context()
        device = ctx.query_devices()
        if len(device) == 0:
            raise SystemExit
        
        pipeline = rs.pipeline()
        config = rs.config()
        config.enable_stream(rs.stream.depth, COLOR_W, COLOR_H, rs.format.z16, FPS)
        config.enable_stream(rs.stream.color, COLOR_W, COLOR_H, rs.format.bgr8, FPS)
        profile = pipeline.start(config)

        align_to = rs.stream.color
        align = rs.align(align_to)

        # depth scale (meters per depth unit)
        depth_sensor = profile.get_device().first_depth_sensor()
        depth_scale = depth_sensor.get_depth_scale()

        clipping_distance_depth_units = CLIPPING_DISTANCE_M / depth_scale
        return pipeline, align, clipping_distance_depth_units, depth_scale
    
    def calc_angle(self, p1, p2, p3):
        """以p2为顶点, 计算p1-p2与p3-p2的夹角(返回角度值)"""
        v1 = p1 - p2  # 向量 v1：p2->p1
        v2 = p3 - p2  # 向量 v2：p2->p3
        # 计算向量的长度
        norm1 = np.linalg.norm(v1)
        norm2 = np.linalg.norm(v2)
        if norm1 < 1e-6 or norm2 < 1e-6:
            return None  # 如果向量过短（重合），无法计算角度
        # 计算夹角的余弦值
        cos_angle = np.dot(v1, v2) / (norm1 * norm2)
        # 防止数值误差导致cos_angle超出[-1,1]
        cos_angle = min(1.0, max(-1.0, cos_angle))
        # 反余弦得到弧度，再转换为角度
        angle = math.degrees(math.acos(cos_angle))
        return angle

    def calc_sway_angle(self, vec_base, vec_finger, normal_unit):
        """计算vec_finger相对vec_base在平面内的夹角(有正负方向)"""
        if vec_base is None or normal_unit is None:
            return None
        if np.linalg.norm(vec_base) < 1e-6 or np.linalg.norm(vec_finger) < 1e-6:
            return None
        # 将手指向量投影到掌平面
        finger_proj = vec_finger - np.dot(vec_finger, normal_unit) * normal_unit
        # 计算vec_base到finger_proj的有向夹角
        dot_val = np.dot(vec_base, finger_proj)
        # 利用叉积和法向量确定旋转方向（左/右）
        cross_val = np.dot(np.cross(vec_base, finger_proj), normal_unit)
        angle_rad = math.atan2(cross_val, dot_val)
        angle_deg = math.degrees(angle_rad)
        return angle_deg
    
    def hand_landmarker_callback(self, result: vision.HandLandmarkerResult, output_image: mp.Image, timestamp_ms: int):
        """处理 HandLandmarker 的结果，包括绘制关键点和计算手指关节角度"""
        # 将输出图像转换为 NumPy 数组（RGB通道顺序）
        image_rgb = output_image.numpy_view()
        # 转换为 OpenCV 的 BGR 通道顺序用于可视化
        image_bgr = cv2.cvtColor(image_rgb, cv2.COLOR_RGB2BGR)

        # A flag for success of detection
        self.hand_detected = False 
        # 如果检测到手部关键点，则绘制关键点和连接，并计算角度
        if result.hand_landmarks:
            self.hand_detected = True  # 设置全局标志，表示检测到手部关键点
            # 获取图像尺寸
            h, w = image_bgr.shape[:2]
            # 使用 MediaPipe 提供的手部连接点索引对（用于绘制骨架）
            hand_connections = mp.solutions.hands.HAND_CONNECTIONS

            # 遍历每只检测到的手
            for hand_idx, landmarks in enumerate(result.hand_landmarks):
                # 获取这一只手的 handedness 标签
                hand_label = result.handedness[hand_idx][0].category_name  # e.g. "Left" / "Right"
                
                for i in range(hand_idx):
                    if result.handedness[i][0].category_name == hand_label:
                        self.hand_detected = False
                        print("Duplicate hand detected, skipping...")
                        return

                # 在图像上标注这个手是 Left 还是 Right
                # 在该手关键点的某个位置旁写字
                # e.g.
                x0 = int(landmarks[0].x * w)  # WRIST 的 x 像素坐标
                y0 = int(landmarks[0].y * h)
                cv2.putText(image_bgr, hand_label, (x0 + 20, y0 + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0,255,0), 2)
                
                
                # 将归一化坐标转换为像素坐标并绘制关节点和骨架
                points = []  # 存储当前手的像素坐标列表
                for lm in landmarks:
                    px = int(lm.x * w)
                    py = int(lm.y * h)
                    points.append((px, py))
                    cv2.circle(image_bgr, (px, py), 5, (0, 255, 0), -1)  # 用绿色圆点标记关键点
                for connection in hand_connections:
                    i, j = connection
                    if i < len(points) and j < len(points):
                        cv2.line(image_bgr, points[i], points[j], (0, 255, 0), 2)

                # 计算当前手的各关节角度（利用三维世界坐标）
                # 提取当前手的21个世界坐标点 (单位：米)
                world_landmarks = result.hand_world_landmarks[hand_idx]
                # 将每个关键点的世界坐标转换为 NumPy 数组，方便计算向量
                pts = [np.array([lm.x, lm.y, lm.z]) for lm in world_landmarks]

                # 计算拇指 CMC 关节的两个角度（两自由度）：
                #   CMC_rot（大拇指根部绕旋）：由三角形 WRIST-THUMB_MCP-INDEX_MCP 和 WRIST-INDEX_MCP-PINKY_MCP 两平面的法向量计算有符号夹角（外翻为负）(pts[INDEX_MCP][0])
                #   CMC_pull（大拇指根部合拢）：以 WRIST 为顶点，计算 THUMB_MCP、WRIST、INDEX_MCP 三点间的夹角
                v_w_index = pts[INDEX_MCP] - pts[WRIST]
                v_w_thumb = pts[THUMB_MCP] - pts[WRIST]
                v_w_pinky = pts[PINKY_MCP] - pts[WRIST]
                normal_thumb_plane = np.cross(v_w_index, v_w_thumb)
                normal_palm_plane  = np.cross(v_w_index, v_w_pinky)

                # 判定法向量或轴向量有效性
                if (np.linalg.norm(normal_thumb_plane) < 1e-6 or 
                    np.linalg.norm(normal_palm_plane) < 1e-6 or 
                    np.linalg.norm(v_w_index) < 1e-6):
                    thumb_cmc_rot = None
                else:
                    # 计算掌平面(normal_palm_plane)与拇指平面(normal_thumb_plane)法向量的有向夹角
                    n1 = normal_thumb_plane / np.linalg.norm(normal_thumb_plane)
                    n2 = normal_palm_plane  / np.linalg.norm(normal_palm_plane)
                    axis_vec = v_w_index / np.linalg.norm(v_w_index)
                    dot_val = np.dot(n1, n2)
                    # 数值稳定性处理
                    dot_val = max(-1.0, min(1.0, dot_val))
                    # 利用叉积和参考轴计算符号（外翻为负）
                    cross_val = np.dot(np.cross(n1, n2), axis_vec)
                    thumb_cmc_rot_rad = math.atan2(cross_val, dot_val)
                    thumb_cmc_rot = math.degrees(thumb_cmc_rot_rad)

                # 计算拇指合拢角（使用 WRIST 为顶点的三点角度）
                thumb_cmc_pull = self.calc_angle(pts[THUMB_MCP], pts[WRIST], pts[INDEX_MCP])


                # 计算拇指各关节角度：CMC、MCP、IP（Tip 不计算，因为末端只有两个关节夹角）
                #thumb_cmc = self.calc_angle(pts[WRIST], pts[THUMB_CMC], pts[THUMB_MCP])
                thumb_mcp = self.calc_angle(pts[THUMB_CMC], pts[THUMB_MCP], pts[THUMB_IP])
                thumb_ip  = self.calc_angle(pts[THUMB_MCP], pts[THUMB_IP], pts[THUMB_TIP])

                # 计算食指、中指、无名指、小指的 MCP、PIP、DIP 关节角
                index_mcp  = self.calc_angle(pts[WRIST], pts[INDEX_MCP], pts[INDEX_PIP])
                index_pip  = self.calc_angle(pts[INDEX_MCP], pts[INDEX_PIP], pts[INDEX_DIP])
                index_dip  = self.calc_angle(pts[INDEX_PIP], pts[INDEX_DIP], pts[INDEX_TIP])
                middle_mcp = self.calc_angle(pts[WRIST], pts[MIDDLE_MCP], pts[MIDDLE_PIP])
                middle_pip = self.calc_angle(pts[MIDDLE_MCP], pts[MIDDLE_PIP], pts[MIDDLE_DIP])
                middle_dip = self.calc_angle(pts[MIDDLE_PIP], pts[MIDDLE_DIP], pts[MIDDLE_TIP])
                ring_mcp   = self.calc_angle(pts[WRIST], pts[RING_MCP], pts[RING_PIP])
                ring_pip   = self.calc_angle(pts[RING_MCP], pts[RING_PIP], pts[RING_DIP])
                ring_dip   = self.calc_angle(pts[RING_PIP], pts[RING_DIP], pts[RING_TIP])
                pinky_mcp  = self.calc_angle(pts[WRIST], pts[PINKY_MCP], pts[PINKY_PIP])
                pinky_pip  = self.calc_angle(pts[PINKY_MCP], pts[PINKY_PIP], pts[PINKY_DIP])
                pinky_dip  = self.calc_angle(pts[PINKY_PIP], pts[PINKY_DIP], pts[PINKY_TIP])

                # 计算手腕的pitch roll yaw（可选）
                # pitch 向前俯为正角度
                pitch_z = pts[MIDDLE_MCP][2] - pts[WRIST][2]  # Z 轴差值 深度
                pitch_y = pts[MIDDLE_MCP][1] - pts[WRIST][1]  # Y 轴差值 垂直
                pitch_y = -pitch_y  # 图像坐标系 Y 轴向下为正，转换为数学坐标系向上为正
                pitch = - math.degrees(math.atan2(pitch_z, pitch_y)) # 右手坐标系，向前为正

                # yaw 向左拐为正角度 （右手坐标系）
                yaw_x = pts[MIDDLE_MCP][0] - pts[WRIST][0]  # X 轴差值 左右
                yaw_y = pitch_y  # Y 轴差值 垂直
                yaw = math.degrees(math.atan2(yaw_x, yaw_y)) # 右手坐标系，向左为正
                
                # 计算每根手指的侧摆角（相对于中指基准方向的偏斜角度）
                # 手掌平面法向量：由手腕->食指MCP 和 手腕->小指MCP 两向量的叉积确定
                v_index = pts[INDEX_MCP] - pts[WRIST]
                v_pinky = pts[PINKY_MCP] - pts[WRIST]
                palm_normal = np.cross(v_index, v_pinky)  # 手掌平面的法向量
                if np.linalg.norm(palm_normal) < 1e-6:
                    palm_normal_unit = None
                else:
                    palm_normal_unit = palm_normal / np.linalg.norm(palm_normal)
                # 计算手腕->中指MCP的基准方向在掌平面内的投影
                if palm_normal_unit is not None:
                    baseline_vec = pts[MIDDLE_MCP] - pts[WRIST]
                    # 从baseline_vec移除法向量分量，得到其在掌平面内的投影baseline_proj
                    baseline_proj = baseline_vec - np.dot(baseline_vec, palm_normal_unit) * palm_normal_unit
                else:
                    baseline_proj = None

                # （可选）简单节流：每 0.2s 打印一次
                # now = time.time()
                # if not hasattr(self.hand_landmarker_callback, "_last_print") or (now - self.hand_landmarker_callback._last_print) > 0.2:
                #     self.hand_landmarker_callback._last_print = now
                # else:
                #     # 不打印，直接将图像入缓冲区并返回
                #     self.frame_buffer.append(image_bgr)
                #     return

                # 计算各手指的侧摆角（以中指方向为参考，向内为正，向外为负）
                index_sway = self.calc_sway_angle(baseline_proj, pts[INDEX_PIP] - pts[INDEX_MCP], palm_normal_unit)
                middle_sway = self.calc_sway_angle(baseline_proj, pts[MIDDLE_PIP] - pts[MIDDLE_MCP], palm_normal_unit)
                ring_sway = self.calc_sway_angle(baseline_proj, pts[RING_PIP] - pts[RING_MCP], palm_normal_unit)
                pinky_sway = self.calc_sway_angle(baseline_proj, pts[PINKY_PIP] - pts[PINKY_MCP], palm_normal_unit)

                # TODO: multi-hand data process
                # 打印关节角度信息到控制台
                # if len(result.hand_world_landmarks) > 1:
                #     print(f"Hand {hand_idx+1}:")  # 多手时标识手的编号
                
                with self.lock:
                    # 拇指关节角
                    if thumb_cmc_rot is not None:
                        # print(f"Thumb : CMC_rot={thumb_cmc_rot:.1f}°, CMC_pull={thumb_cmc_pull:.1f}°, MCP={thumb_mcp:.1f}°, IP={thumb_ip:.1f}°")
                        
                        self.finger_joints["thumb"]["cmc_rot"] = self.previous_finger_joints["thumb"]["cmc_rot"].update(thumb_cmc_rot) # low pass filter

                
                    self.finger_joints["thumb"]["cmc_pull"] = self.previous_finger_joints["thumb"]["cmc_pull"].update(thumb_cmc_pull) # low pass filter

                    
                    self.finger_joints["thumb"]["mcp"] = self.previous_finger_joints["thumb"]["mcp"].update(thumb_mcp) # low pass filter

                    
                    self.finger_joints["thumb"]["ip"] = self.previous_finger_joints["thumb"]["ip"].update(thumb_ip) # low pass filter

                    
                    self.finger_joints["index"]["mcp"] = self.previous_finger_joints_2["index"]["mcp"].update(index_mcp) # low pass filter

                    
                    self.finger_joints["index"]["pip"] = self.previous_finger_joints_2["index"]["pip"].update(index_pip) # low pass filter

                    
                    self.finger_joints["index"]["dip"] = self.previous_finger_joints_2["index"]["dip"].update(index_dip) # low pass filter

                    
                    self.finger_joints["middle"]["mcp"] = self.previous_finger_joints["middle"]["mcp"].update(middle_mcp) # low pass filter

                    
                    self.finger_joints["middle"]["pip"] = self.previous_finger_joints["middle"]["pip"].update(middle_pip) # low pass filter

                    self.finger_joints["middle"]["dip"] = self.previous_finger_joints["middle"]["dip"].update(middle_dip) # low pass filter

                    self.finger_joints["ring"]["mcp"] = self.previous_finger_joints["ring"]["mcp"].update(ring_mcp) # low pass filter
                    

                    
                    self.finger_joints["ring"]["pip"] = self.previous_finger_joints["ring"]["pip"].update(ring_pip) # low pass filter

                    self.finger_joints["ring"]["dip"] = self.previous_finger_joints["ring"]["dip"].update(ring_dip) # low pass filter
                    

                    self.finger_joints["pinky"]["mcp"] = self.previous_finger_joints["pinky"]["mcp"].update(pinky_mcp) # low pass filter

                    self.finger_joints["pinky"]["pip"] = self.previous_finger_joints["pinky"]["pip"].update(pinky_pip) # low pass filter

                    self.finger_joints["pinky"]["dip"] = self.previous_finger_joints["pinky"]["dip"].update(pinky_dip) # low pass filter
                    
                    
                    self.wrist_joints["pitch"] = self.previous_wrist_joints["pitch"].update(pitch)

                    self.wrist_joints["yaw"] = self.previous_wrist_joints["yaw"].update(yaw)
                    
                    # self.wrist_joints["roll"] = self.previous_wrist_joints["roll"].update(roll)

                    # self.finger_joints["thumb"]["cmc_pull"] = thumb_cmc_pull
                    # self.finger_joints["thumb"]["mcp"] = thumb_mcp
                    # self.finger_joints["thumb"]["ip"]  = float(thumb_ip)
                    # self.finger_joints["index"]["mcp"]  = index_mcp
                    # self.finger_joints["index"]["pip"]  = index_pip
                    # self.finger_joints["index"]["dip"]  = index_dip
                    # self.finger_joints["middle"]["mcp"] = middle_mcp
                    # self.finger_joints["middle"]["pip"] = middle_pip
                    # self.finger_joints["middle"]["dip"] = middle_dip
                    # self.finger_joints["ring"]["mcp"]   = ring_mcp
                    # self.finger_joints["ring"]["pip"]   = ring_pip
                    # self.finger_joints["ring"]["dip"]   = ring_dip
                    # self.finger_joints["pinky"]["mcp"]  = pinky_mcp
                    # self.finger_joints["pinky"]["pip"]  = pinky_pip
                    # self.finger_joints["pinky"]["dip"]  = pinky_dip
                    
                    self.finger_joints["index"]["sway"] = index_sway-14.9
                    self.finger_joints["middle"]["sway"] = middle_sway-8.5
                    self.finger_joints["ring"]["sway"] = ring_sway-2.5
                    self.finger_joints["pinky"]["sway"] = pinky_sway-16.5
                    
                    print("pitch: ", self.wrist_joints["pitch"])
                    print("yaw: ", self.wrist_joints["yaw"], "\n")
                    
                    # print(self.finger_joints)

        # 将绘制完成的图像放入缓冲区，由主线程负责显示（macOS 上避免在回调线程调用 OpenCV UI）
        self.frame_buffer.append(image_bgr)
        
    def run(self):
        # 配置 HandLandmarker 选项并创建实例（LIVE_STREAM 模式）
        options = self.HandLandmarkerOptions(
            base_options=self.BaseOptions(model_asset_path=MODEL_PATH),
            running_mode=self.VisionRunningMode.LIVE_STREAM,
            num_hands=1,
            min_hand_detection_confidence=0.5,
            min_hand_presence_confidence=0.5,
            min_tracking_confidence=0.5,
            result_callback=self.hand_landmarker_callback
        )
        # print("python program")
        # landmarker = self.HandLandmarker.create_from_options(options)
        with self.HandLandmarker.create_from_options(options) as landmarker:
            pipeline, align, clip_depth_units, depth_scale = self.setup_realsense()
            try:
                # 读取摄像头帧并执行实时手部关键点检测
                while not self.stop_flag:
                    # print("start python loop")
                    # 转换帧为 RGB 并封装为 MediaPipe Image
                    #frame_rgb = cv2.cvtColor(align, cv2.COLOR_BGR2RGB)
                    #mp_frame = mp.Image(image_format=mp.ImageFormat.SRGB, data=frame_rgb)
                    # Get frameset of color and depth
                    frames = pipeline.wait_for_frames()
                    aligned_frames = align.process(frames)
                    depth_frame = aligned_frames.get_depth_frame()
                    color_frame = aligned_frames.get_color_frame()
                    if not depth_frame or not color_frame:
                        continue


                    # Convert to numpy
                    depth_image = np.asanyarray(depth_frame.get_data())
                    color_bgr = np.asanyarray(color_frame.get_data())

                    # Save for futher use
                    original_background = color_bgr

                    # Build depth mask: keep pixels with 0 < depth < clipping
                    depth_mask = np.where(
                        (depth_image > 0) & (depth_image < clip_depth_units),
                        255, 0
                    ).astype(np.uint8)

                    if MASK_MEDIAN_KSIZE and MASK_MEDIAN_KSIZE >= 3:
                        depth_mask = cv2.medianBlur(depth_mask, MASK_MEDIAN_KSIZE)

                    # Expand mask with a controllable "halo" (ring) around near objects
                    if HALO_PIX and HALO_PIX > 0:
                        k = int(HALO_PIX)
                        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2*k + 1, 2*k + 1))
                        # Dilate the foreground; the additional band beyond the original mask becomes the halo
                        dilated = cv2.dilate(depth_mask, kernel, iterations=1)
                        halo_band = cv2.subtract(dilated, depth_mask)  # ring-only region
                        combined_mask = cv2.bitwise_or(depth_mask, halo_band)  # keep foreground + ring
                    else:
                        combined_mask = depth_mask

                    # Apply mask to color (foreground + halo ring are visible; far background stays black)
                    masked_bgr = cv2.bitwise_and(color_bgr, color_bgr, mask=combined_mask)

                    # Send to MediaPipe (LIVE_STREAM)
                    # Use RealSense timestamp in ms for strict monotonic increase
                    timestamp = int(color_frame.get_timestamp())  # already in ms
                    image_rgb = cv2.cvtColor(masked_bgr, cv2.COLOR_BGR2RGB)


                    # 获取当前时间戳（毫秒），用于同步输出结果
                    timestamp = int(time.time() * 1000)
                    # 转换帧为 RGB 并封装为 MediaPipe Image
                    mp_frame = mp.Image(image_format=mp.ImageFormat.SRGB, data=image_rgb)
                    # 将当前帧发送到 HandLandmarker 模型进行异步检测
                    landmarker.detect_async(mp_frame, timestamp)

                    # 从缓冲区取出最新一帧进行显示（避免在回调线程里调用 imshow）
                    with self.lock:
                        if self.frame_buffer:
                            to_show = self.frame_buffer[-1]
                            if self.hand_detected == False:
                                cv2.rectangle(original_background, pt1=(50, 300), pt2=(1210, 355), color=(128, 128, 128), thickness=-1)
                                cv2.rectangle(original_background, pt1=(850, 630), pt2=(1210, 685), color=(128, 128, 128), thickness=-1)
                                image_blur = cv2.blur(original_background, (21, 21))

                                #在图片上添加文字信息
                                cv2.putText(image_blur, ">>>>> Please move closer <<<<<", (50, 350), cv2.FONT_HERSHEY_SIMPLEX, 2.0, (255, 255, 255), 2, cv2.LINE_AA)
                                cv2.putText(image_blur, "~by Xingjian Qin", (890, 670), cv2.FONT_HERSHEY_SCRIPT_SIMPLEX, 1.0, (255, 255, 255), 2, cv2.LINE_AA)
                                # cv2.imshow('MediaPipe Hand Angles', image_blur)
                                self.curr_img = image_blur

                            else:
                                # cv2.imshow('MediaPipe Hand Angles', to_show)
                                self.curr_img = to_show

                    # 检查退出按键（按 ESC 键退出）
                    # if cv2.waitKey(1) & 0xFF == 27:
                    #     break
                    
                    # time.sleep(0.01)

            except Exception as e:
                print(f"发生错误: {e}")

            finally:
                # landmarker.close()
                pipeline.stop()
                # cv2.destroyAllWindows()
    
    def start(self):
        if self.thr is None or not self.thr.is_alive():
            self.thr = Thread(target=self.run, daemon=True)
            self.thr.start()
            
    def stop(self):
        self.stop_flag = True
        if self.thr is not None:
            self.thr.join()
    
    def get_frame(self):
        with self.lock:
            return self.curr_img
    
    def get_joints(self):
        with self.lock:
            return self.finger_joints
    
    def get_wrist_joints(self):
        with self.lock:
            return self.wrist_joints
        