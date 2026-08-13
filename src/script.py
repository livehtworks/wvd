from ppadb.client import Client as AdbClient
from enum import Enum
import os
import subprocess
import re
import ctypes
from utils import *
import random
from threading import Thread,Event
from pathlib import Path
import numpy as np
import copy
import struct

DUNGEON_TARGETS = BuildQuestReflection()
GAME_PACKAGE_NAME = "jp.co.drecom.wizardry.daphne"
CLASH_PACKAGE_CANDIDATES = [
    "com.github.metacubex.clash.meta",
    "com.github.kr328.clash",
    "com.github.kr328.clash.foss",
]
CLASH_EXTERNAL_ACTIVITY = "com.github.kr328.clash.ExternalControlActivity"

##################################################################
        
CONFIG_VAR_LIST = [
            #categor      var_name,                   type,          default_value
            ["GENERAL",   "EMU_PATH",                 tk.StringVar,  None],
            ["GENERAL",   "EMU_INDEX",                tk.IntVar,     0],
            ["GENERAL",   "ADB_ADRESS",               tk.StringVar,  "127.0.0.1:16384"],
            ["GENERAL",   "AUTO_START_CLASH",         tk.BooleanVar, False],
            ["GENERAL",   "LAST_VERSION",             tk.StringVar,  None],
            ["GENERAL",   "LATEST_VERSION",           tk.StringVar,  None],
            ["GENERAL",   "FARM_TARGET_TEXT",         tk.StringVar,  ""],
            ["GENERAL",   "FARM_TARGET",              tk.StringVar,  None],
            ["GENERAL",   "KARMA_ADJUST",             tk.StringVar,  "+0"],
            ["GENERAL",   "TASK_SPECIFIC_CONFIG",     tk.BooleanVar, False],
            ["GENERAL",   "STRATEGY",                 list         , [{
                                                                        "group_name": _("柚子"),
                                                                        "skill_settings": [
                                                                            {
                                                                                "role_var": _("F 柚奈壬姬"),
                                                                                "skill_var": _("左上技能"),
                                                                                "target_var": _("不可用"),
                                                                                "skill_lvl": 1
                                                                            }
                                                                        ]
                                                                    },
                                                                    {
                                                                        "group_name": _("全自动战斗"),
                                                                        "skill_settings": []
                                                                    },]],
            ["GENERAL",   "DEFAULT_OVERALL_STRATEGY", tk.StringVar, _("全自动战斗")],
            ["GENERAL",   "RELOAD_STRATEGY_WHEN",     tk.StringVar, _("不需要")],
            ["GENERAL",   "LANGUAGE",                 tk.StringVar, "zh_CN"],
            ["GENERAL",   "WEBSITE_ORG_TIME",         tk.StringVar, None],
            ["GENERAL",   "AM_REFRESH_TIME",          tk.StringVar, None],

            ["TEMPLATE",   "TASK_POINT_STRATEGY",     dict,          {}],
            ["TEMPLATE",   "QUICK_DISARM_CHEST",      tk.BooleanVar, False],
            ["TEMPLATE",   "WHO_WILL_OPEN_IT",        tk.IntVar,     0],
            ["TEMPLATE",   "SKIP_COMBAT_RECOVER",     tk.BooleanVar, False],
            ["TEMPLATE",   "SKIP_CHEST_RECOVER",      tk.BooleanVar, False],
            ["TEMPLATE",   "RECOVER_WHEN_BEGINNING",  tk.BooleanVar, False],
            ["TEMPLATE",   "ACTIVE_REST",             tk.BooleanVar, True],
            ["TEMPLATE",   "ACTIVE_ROYALSUITE_REST",  tk.BooleanVar, False],
            ["TEMPLATE",   "ACTIVE_TRIUMPH",          tk.BooleanVar, False],
            ["TEMPLATE",   "ACTIVE_BEAUTIFUL_ORE",    tk.BooleanVar, False],
            ["TEMPLATE",   "ACTIVE_BEG_MONEY",        tk.BooleanVar, True],
            ["TEMPLATE",   "MAX_TRY_LIMIT",           tk.IntVar,     25],
            ["TEMPLATE",   "MAX_CRASH_LIMIT",         tk.IntVar,     10],
            ["TEMPLATE",   "REST_INTERVEL",           tk.IntVar,     1],
            ["TEMPLATE",   "ACTIVE_CSC",              tk.BooleanVar, True],
            ["TEMPLATE",   "BYPASS_THE_WALL",         tk.BooleanVar, False],
            ["TEMPLATE",   "RE_ASSEMBLE_PARTY",       tk.BooleanVar, False],
            ]
class FarmConfig:
    for attr_name, var_type, var_config_name, var_default_value in CONFIG_VAR_LIST:
        locals()[var_config_name] = var_default_value
    def __init__(self):
        #### 面板配置其他
        self._FORCESTOPING = None
        self._FINISHINGCALLBACK = None
        self._MSGQUEUE = None
        #### 底层接口
        self._ADBDEVICE = None
    def __getitem__(self, key):
        return getattr(self, key)
    def __getattr__(self, name):
        # 当访问不存在的属性时，抛出AttributeError
        raise AttributeError(_("FarmConfig对象没有属性'%s'") % name)
class RuntimeContext:
    #### 模拟器信息
    _RUNNING_EMU_PID = None # 全局变量
    #### 统计信息
    _LAPTIME = 0
    _TOTALTIME = 0
    _COUNTERDUNG = 0
    _COUNTERCOMBAT = 0
    _COUNTERCHEST = 0
    _TIME_COMBAT= 0
    _TIME_COMBAT_TOTAL = 0
    _TIME_CHEST = 0
    _TIME_CHEST_TOTAL = 0
    #### 其他临时参数
    _MEET_CHEST_OR_COMBAT = False
    _COMBATSPD = False
    _SUICIDE = False # 当有两个人死亡的时候(multipeopledead), 在战斗中尝试自杀.
    _MAXRETRYLIMIT = 20
    _ACTIVESPELLSEQUENCE = None
    _RECOVERAFTERREZ = False
    _ZOOMWORLDMAP = False
    _CRASHCOUNTER = 0
    _IMPORTANTINFO = ""
    _RESUMEAVAILABLE = False
    _BYPASSAFTERRESTART = True
    CURRENT_STRATEGY = {}
    NEED_RECOVER_WHEN_BEGINNING = True
    TASK_STEP_INDEX = 0
    _LAST_BAGCLEAR = 0
    _SKIP_SCREENSHOT_WARNING = False # 截图返回是否包含错误代码.
    _LAST_FOCUS_CHECK = 0
    _PAUSE_CHECK_UNTIL = 0
    _LAST_DEBUG_IMAGE_AT = None
class FarmQuest:
    _TARGETINFOLIST = None
    _EOT = None
    _preEOTcheck = None
    _SPECIALDIALOGOPTION = None
    _SPECIALFORCESTOPINGSYMBOL = None
    _SPELLSEQUENCE = None
    _TYPE = None
    _RTT = None # Return To Town, 回程时执行的流程
    _TIPS = None

    def __getattr__(self, name):
        # 当访问不存在的属性时，抛出AttributeError
        raise AttributeError(_("FarmQuest对象没有属性'%s'") % name)
class TargetInfo:
    def __init__(self, target: str, swipeDir: list = None, roi=None):
        self.target = target
        self.swipeDir = swipeDir
        self.roi = roi
    @property
    def swipeDir(self):
        return self._swipeDir

    @swipeDir.setter
    def swipeDir(self, inputValue):
        value = None
        match inputValue:
            case None:
                value = [None,
                        [100,100,700,1200],
                        [400,1200,400,100],
                        [700,800,100,800],
                        [400,100,400,1200],
                        [100,800,700,800],
                        ]
            case "左上":
                value = [[100,250,700,1200]]
            case "右上":
                value = [[700,250,100,1200]]
            case "右下":
                value = [[700,1200,100,250]]
            case "左下":
                value = [[100,1200,700,250]]
            case _:
                value = inputValue
        
        self._swipeDir = value

    @property
    def roi(self):
        return self._roi

    @roi.setter
    def roi(self, value):
        if value == "default":
            value = [[0,0,900,1600],[0,0,900,208],[0,1265,900,335],[0,636,137,222],[763,636,137,222], [336,208,228,77],[336,1168,228,97]]
        if self.target == "chest":
            if value == None:
                value = [[0,0,900,1600]]
            value += [[0,0,900,208],[0,1265,900,335],[0,636,137,222],[763,636,137,222], [336,208,228,77],[336,1168,228,97]]

        self._roi = value
##################################################################
def LoadQuest(farmtarget):
    logger.debug(f"读取任务{farmtarget}")
    if farmtarget in QUEST_DATA:
        data = QUEST_DATA[farmtarget]
    else:
        logger.error(_("任务列表已更新.请重新手动选择地下城任务."))
        return None
    
    # 创建 Quest 实例并填充属性
    quest = FarmQuest()
    for key, value in data.items():
        if key == "_TARGETINFOLIST":
            setattr(quest, key, [TargetInfo(*args) for args in value])
        elif hasattr(FarmQuest, key):
            setattr(quest, key, value)
        elif key in ["type","questName","questId","questCategory","questName_en_US","questCategory_en_US"]:
            pass
        else:
            logger.info(_("'%s'并不存在于FarmQuest中.") % key)
    
    return quest
##################################################################
def CMDLine(cmd):
    logger.debug(_("执行cmd命令: %s") % cmd)
    result = subprocess.run(cmd,shell=True, capture_output=True, text=True, timeout=10,encoding="utf-8")
    
    stdout = (result.stdout or '').strip()
    stderr = (result.stderr or '').strip()

    if stdout or stderr:
        parts = ["***********"]
        if stdout:
            parts.append(_("cmd命令返回:{}").format(stdout))
            parts.append("***********")
        if stderr:
            parts.append(_("cmd命令错误:{}").format(stderr))
            parts.append("***********")
        parts.append(" ")
        logger.info("\n".join(parts))
    return result
def GetADBPathFromEmuPath(emu_path):
        adb_path = emu_path
        adb_path = adb_path.replace("HD-Player.exe", "HD-Adb.exe") # 蓝叠
        adb_path = adb_path.replace("MuMuPlayer.exe", "adb.exe") # mumu
        adb_path = adb_path.replace("MuMuNxDevice.exe", "adb.exe") # mumu
        if not os.path.exists(adb_path):
            logger.error(_("adb程序序不存在: {a}").format(a=adb_path))
            return None
    
        return adb_path
def DeviceShellOnce(device, cmdStr, timeout=5, log_error=True):
    try:
        return device.shell(cmdStr, timeout=timeout)
    except Exception as e:
        if log_error:
            logger.warning(_("ADB命令执行失败 ({a}): {b}").format(a=cmdStr, b=e))
        return None
def GetCurrentFocusPackage(device):
    focus = DeviceShellOnce(device, "dumpsys window | grep mCurrentFocus", log_error=False) or ""
    if not focus:
        focus = DeviceShellOnce(device, "dumpsys window windows | grep mCurrentFocus", log_error=False) or ""
    return focus
def ResolveMainActivity(device, package_name):
    result = DeviceShellOnce(device, f"cmd package resolve-activity --brief {package_name}", log_error=False)
    if not result:
        return None

    main_activity = result.strip().split("\n")[-1].strip()
    if "/" not in main_activity or "No activity found" in main_activity:
        return None
    return main_activity
def StartAndroidPackage(device, package_name):
    main_activity = ResolveMainActivity(device, package_name)
    if main_activity:
        return DeviceShellOnce(device, f"am start -n {main_activity}") is not None

    result = DeviceShellOnce(device, f"monkey -p {package_name} -c android.intent.category.LAUNCHER 1")
    return result is not None and "No activities found" not in result
def FindInstalledClashPackage(device):
    packages = DeviceShellOnce(device, "pm list packages", log_error=False) or ""
    for package_name in CLASH_PACKAGE_CANDIDATES:
        if f"package:{package_name}" in packages:
            return package_name

    match = re.search(r"package:([^\s]*clash[^\s]*)", packages, re.IGNORECASE)
    if match:
        return match.group(1)
    return None
def IsAndroidVpnConnected(device):
    tun0_info = DeviceShellOnce(device, "ip addr show tun0", log_error=False) or ""
    if tun0_info and "does not exist" not in tun0_info.lower() and "tun0" in tun0_info:
        return True

    connectivity = DeviceShellOnce(device, "dumpsys connectivity", timeout=8, log_error=False) or ""
    for block in connectivity.split("NetworkAgentInfo{")[1:]:
        block = block.split("\n\n", 1)[0]
        if "Transports: VPN" in block:
            return True
    return False
def AcceptVpnPermissionDialogIfPresent(device):
    focus = GetCurrentFocusPackage(device)
    if "com.android.vpndialogs" not in focus:
        return False

    logger.info(_("检测到Android VPN授权弹窗, 尝试点击确认."))
    dump_result = DeviceShellOnce(device, "uiautomator dump /sdcard/window.xml >/dev/null && cat /sdcard/window.xml", timeout=8, log_error=False) or ""
    for text in ["确定", "允许", "OK", "Allow"]:
        match = re.search(rf'text="{re.escape(text)}"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', dump_result)
        if match:
            x1, y1, x2, y2 = [int(v) for v in match.groups()]
            DeviceShellOnce(device, f"input tap {(x1 + x2) // 2} {(y1 + y2) // 2}", log_error=False)
            time.sleep(1)
            return True

    # 兜底坐标按 900x1600 竖屏确认按钮区域估算，仅在已确认是系统VPN弹窗时使用。
    DeviceShellOnce(device, "input tap 690 1045", log_error=False)
    time.sleep(1)
    return True
def EnsureClashVpn(setting: FarmConfig, device):
    if not getattr(setting, "AUTO_START_CLASH", False):
        return True

    package_name = FindInstalledClashPackage(device)
    if not package_name:
        logger.warning(_("已启用Clash自动恢复, 但模拟器内未检测到Clash应用."))
        return False

    focus_before = GetCurrentFocusPackage(device)
    game_was_focused = GAME_PACKAGE_NAME in focus_before

    if IsAndroidVpnConnected(device):
        logger.info(_("Clash/VPN已处于连接状态."))
        return True

    logger.info(_("正在启动Clash并恢复VPN: {a}").format(a=package_name))
    start_action = f"{package_name}.action.START_CLASH"
    start_commands = [
        f"am start -n {package_name}/{CLASH_EXTERNAL_ACTIVITY} -a {start_action}",
        f"am start -a {start_action} -p {package_name}",
    ]

    for cmd in start_commands:
        result = DeviceShellOnce(device, cmd, log_error=False)
        if result is not None and "Error" not in result and "Exception" not in result:
            break

    AcceptVpnPermissionDialogIfPresent(device)

    for check_index in range(8):
        if IsAndroidVpnConnected(device):
            logger.info(_("Clash/VPN已恢复连接."))
            if game_was_focused and GAME_PACKAGE_NAME not in GetCurrentFocusPackage(device):
                logger.info(_("恢复Clash后切回游戏前台."))
                StartAndroidPackage(device, GAME_PACKAGE_NAME)
            return True
        time.sleep(1)
        AcceptVpnPermissionDialogIfPresent(device)

    logger.warning(_("已尝试启动Clash, 但未检测到VPN连接. 请检查Clash配置或首次VPN授权."))
    StartAndroidPackage(device, package_name)
    if game_was_focused:
        time.sleep(1)
        StartAndroidPackage(device, GAME_PACKAGE_NAME)
    return False

class ScreenshotAppKeepAliveError(Exception):
    pass

class AdbScreenshotBackend:
    name = "ADB"

    def capture(self, setting: FarmConfig, runtimeContext: RuntimeContext):
        serial = setting._ADBDEVICE.serial
        if not runtimeContext._SKIP_SCREENSHOT_WARNING:
            cmd = "screencap"
        else:
            cmd = "screencap 2>/dev/null"

        process_result = subprocess.run(
            [GetADBPathFromEmuPath(setting.EMU_PATH), "-s", serial, "exec-out", cmd],
            capture_output=True,
            timeout=5
        )

        if process_result.stderr:
            logger.error(_("截图命令报错: {a}".format(
                a=process_result.stderr.decode('utf-8', errors='ignore'))))
            raise RuntimeError(_("截图命令报错"))

        raw_data = process_result.stdout
        if len(raw_data) < 12:
            logger.error(_("截图数据不足12字节(无头信息)"))
            raise RuntimeError(_("截图数据异常"))

        w, h, fmt = struct.unpack("<III", raw_data[:12])
        if not (0 < w <= 16384 and 0 < h <= 16384):
            if raw_data.startswith(
                b'[Warning] Multiple displays were found, but no display id was specified! '
                b'Defaulting to the first display found, however this default is not guaranteed '
                b'to be consistent across captures. A display id should be specified.\n'
            ):
                raise ScreenshotAppKeepAliveError("你开启了应用保活, 请关闭.")

            if not runtimeContext._SKIP_SCREENSHOT_WARNING:
                logger.error(f"无法识别的截屏数据，头部内容: {raw_data[:400]}")
                logger.error(f"将在之后截图时跳过所有警告信息.")
                runtimeContext._SKIP_SCREENSHOT_WARNING = True
                return self.capture(setting, runtimeContext)

            logger.error(f"再次收到非法数据，头部内容: {raw_data[:400]}")
            raise RuntimeError("截图数据异常，无法修复")

        expected_pixels = w * h * 4
        pixels_data = raw_data[12:]

        if len(pixels_data) == expected_pixels:
            pass
        elif len(pixels_data) > expected_pixels:
            pixels_data = pixels_data[:expected_pixels]
        else:
            logger.error(_("数据长度校验失败: 头部声明 {a}x{b}x4={d}, 实际收到 {c}.").format(
                a=w, b=h, c=len(pixels_data), d=expected_pixels))
            raise RuntimeError(_("截图数据不完整"))

        image = np.frombuffer(pixels_data, dtype=np.uint8)
        image = image.reshape((h, w, 4))
        image = cv2.cvtColor(image, cv2.COLOR_RGBA2BGR)
        return NormalizeScreenshotImage(image)

class MumuIpcScreenshotBackend:
    name = "MuMu IPC"

    def __init__(self, setting: FarmConfig):
        self.setting = setting
        self.handle = 0
        self.display_id = None
        self.width = None
        self.height = None
        self.dll_path, self.mumu_root = self._resolve_paths(setting.EMU_PATH)
        self.dll = self._load_dll(self.dll_path)

    def _resolve_paths(self, emu_path_value):
        emu_path = Path(str(emu_path_value).replace("/", "\\"))
        if not emu_path.exists():
            raise RuntimeError(_("模拟器启动程序不存在: {a}").format(a=emu_path))

        candidates = []
        if emu_path.name == "MuMuNxDevice.exe":
            mumu_root = emu_path.parents[3]
            candidates.append(emu_path.parent / "sdk" / "external_renderer_ipc.dll")
            candidates.extend(mumu_root.glob("nx_device/*/shell/sdk/external_renderer_ipc.dll"))
            candidates.append(mumu_root / "nx_main" / "sdk" / "external_renderer_ipc.dll")
        else:
            raise RuntimeError(_("当前截图后端仅支持MuMu 12增强截图."))

        for dll_path in candidates:
            if dll_path.exists():
                return dll_path, mumu_root
        raise RuntimeError(_("MuMu增强截图DLL不存在."))

    def _load_dll(self, dll_path):
        dll = ctypes.CDLL(str(dll_path))
        dll.nemu_connect.argtypes = [ctypes.c_wchar_p, ctypes.c_int]
        dll.nemu_connect.restype = ctypes.c_int
        dll.nemu_disconnect.argtypes = [ctypes.c_int]
        dll.nemu_disconnect.restype = None
        dll.nemu_get_display_id.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        dll.nemu_get_display_id.restype = ctypes.c_int
        dll.nemu_capture_display.argtypes = [
            ctypes.c_int,
            ctypes.c_uint,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_ubyte),
        ]
        dll.nemu_capture_display.restype = ctypes.c_int
        return dll

    def connect(self):
        if self.handle:
            return

        self.handle = self.dll.nemu_connect(str(self.mumu_root), int(self.setting.EMU_INDEX))
        if self.handle <= 0:
            self.handle = 0
            raise RuntimeError(_("MuMu增强截图连接失败."))

        self.display_id = self.dll.nemu_get_display_id(
            self.handle,
            GAME_PACKAGE_NAME.encode("utf-8"),
            0
        )
        if self.display_id < 0:
            self.disconnect()
            raise RuntimeError(_("MuMu增强截图无法获取display_id."))

        width = ctypes.c_int()
        height = ctypes.c_int()
        ret = self.dll.nemu_capture_display(
            self.handle,
            ctypes.c_uint(self.display_id),
            0,
            ctypes.byref(width),
            ctypes.byref(height),
            None,
        )
        if ret != 0 or width.value <= 0 or height.value <= 0:
            self.disconnect()
            raise RuntimeError(_("MuMu增强截图无法获取分辨率."))

        self.width = width.value
        self.height = height.value
        logger.info(_("MuMu增强截图已启用: display_id={a}, 分辨率={b}x{c}.").format(
            a=self.display_id, b=self.width, c=self.height))

    def disconnect(self):
        if self.handle:
            try:
                self.dll.nemu_disconnect(self.handle)
            except Exception as e:
                logger.debug(_("MuMu增强截图断开异常: {a}").format(a=e))
            finally:
                self.handle = 0
                self.display_id = None
                self.width = None
                self.height = None

    def capture(self, setting: FarmConfig, runtimeContext: RuntimeContext):
        self.connect()
        buffer_size = self.width * self.height * 4
        width = ctypes.c_int()
        height = ctypes.c_int()
        buffer = (ctypes.c_ubyte * buffer_size)()
        ret = self.dll.nemu_capture_display(
            self.handle,
            ctypes.c_uint(self.display_id),
            buffer_size,
            ctypes.byref(width),
            ctypes.byref(height),
            buffer,
        )
        if ret != 0:
            raise RuntimeError(_("MuMu增强截图失败: ret={a}").format(a=ret))
        if width.value != self.width or height.value != self.height:
            self.width = width.value
            self.height = height.value
            raise RuntimeError(_("MuMu增强截图分辨率发生变化."))

        raw = np.ctypeslib.as_array(buffer).reshape((self.height, self.width, 4))
        image = cv2.cvtColor(raw, cv2.COLOR_RGBA2BGR)
        image = cv2.flip(image, 0)
        return NormalizeScreenshotImage(image)

class ScreenshotBackendManager:
    def __init__(self, setting: FarmConfig):
        self.setting = setting
        self.adb = AdbScreenshotBackend()
        self.mumu = None
        self.mumu_disabled_until = 0

    def invalidate(self):
        if self.mumu:
            self.mumu.disconnect()
            self.mumu = None
        self.mumu_disabled_until = 0

    def _get_mumu_backend(self):
        if time.time() < self.mumu_disabled_until:
            return None
        if self.mumu is None:
            try:
                self.mumu = MumuIpcScreenshotBackend(self.setting)
            except Exception as e:
                logger.debug(_("MuMu增强截图不可用, 本次使用ADB截图: {a}").format(a=e))
                self.mumu_disabled_until = time.time() + 30
                return None
        return self.mumu

    def capture(self, setting: FarmConfig, runtimeContext: RuntimeContext):
        mumu = self._get_mumu_backend()
        if mumu:
            try:
                return mumu.capture(setting, runtimeContext)
            except Exception as e:
                logger.warning(_("MuMu增强截图失败, 回退ADB截图: {a}").format(a=e))
                mumu.disconnect()
                self.mumu = None
                self.mumu_disabled_until = time.time() + 30

        return self.adb.capture(setting, runtimeContext)

def NormalizeScreenshotImage(image):
    current_h, current_w = image.shape[:2]
    if (current_h, current_w) != (1600, 900):
        if (current_h, current_w) == (900, 1600):
            logger.debug(_("截图尺寸错误: 当前{a}, 检测为横屏.".format(a=image.shape)))
            image = image.transpose(1, 0, 2)
        else:
            logger.error(_("截图尺寸错误: 期望(1600,900), 实际({a},{b}).".format(
                a=current_h, b=current_w)))
            raise RuntimeError(_("分辨率异常: {a}x{b}".format(a=current_w, b=current_h)))

    return image
def CheckAndRecoverDevice(setting : FarmConfig, runtimeContext: RuntimeContext, FORCE_RESTART_EMU = False, FORCE_RESTART_ADB = False):
    def CheckEmulator():
        result = subprocess.run(
            "tasklist /FO CSV /NH | findstr \"MuMuNxDevice.exe MuMuPlayer.exe\"",
            shell=True,
            capture_output=True, 
            text=True
        )
        result_str = result.stdout.strip()
        logger.debug(result_str)
        split_results_list = result_str.split("\n")
        check_results_list = []
        for task in split_results_list:
            if not task:
                continue
            parts = task.split("\",\"")
            if len(parts) >= 2:
                try:
                    pid = int(parts[1])
                    check_results_list.append(pid)
                except ValueError:
                    pass
        return check_results_list
    def KillAdb():
        adb_path = GetADBPathFromEmuPath(setting.EMU_PATH)
        try:
            logger.info(_("正在检查并关闭adb..."))
            # Windows 系统使用 taskkill 命令
            if os.name == "nt":
                subprocess.run(
                    f"taskkill /f /im adb.exe", 
                    shell=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False  # 不检查命令是否成功（进程可能不存在）
                )
                time.sleep(1)
                subprocess.run(
                    f"taskkill /f /im HD-Adb.exe", 
                    shell=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False  # 不检查命令是否成功（进程可能不存在）
                )
            else:
                subprocess.run(
                    f"pkill -f {adb_path}", 
                    shell=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False
                )
            logger.info(_("已尝试终止adb"))
        except Exception as e:
            logger.error(_("终止模拟器进程时出错: {a}").format(a=str(e)))
    def KillEmulator():
        emulator_name = os.path.basename(setting.EMU_PATH)
        emulator_SVC = "MuMuVMMSVC.exe"
        try:
            logger.info(_("正在检查并关闭已运行的模拟器实例{a}...").format(a=emulator_name))
            # Windows 系统使用 taskkill 命令
            if os.name == "nt":
                if runtimeContext._RUNNING_EMU_PID:
                    logger.info(_("使用已知进程号{a}关闭模拟器...").format(a=runtimeContext._RUNNING_EMU_PID))
                    subprocess.run(
                        f"taskkill /F /PID {runtimeContext._RUNNING_EMU_PID}",
                        shell=True,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        check=False  # 不检查命令是否成功（进程可能不存在）
                    )
                    time.sleep(1)
                    subprocess.run(
                        f"taskkill /F /IM MuMuVMMHeadless.exe", 
                        shell=True,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        check=False  # 不检查命令是否成功（进程可能不存在）
                    )
                    time.sleep(1)
                else:
                    logger.info(_("模拟器uid未知, 全杀了."))
                    subprocess.run(
                        f"taskkill /f /im {emulator_name}", 
                        shell=True,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        check=False  # 不检查命令是否成功（进程可能不存在）
                    )
                    time.sleep(1)
                    subprocess.run(
                        f"taskkill /f /im {emulator_SVC}", 
                        shell=True,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        check=False  # 不检查命令是否成功（进程可能不存在）
                    )
                    time.sleep(1)

            # Unix/Linux 系统使用 pkill 命令
            else:
                subprocess.run(
                    f"pkill -f {emulator_name}", 
                    shell=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False
                )
                subprocess.run(
                    f"pkill -f {emulator_headless}", 
                    shell=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False
                )
            logger.info(_("已尝试终止模拟器进程: {a}".format(a=emulator_name)))
        except Exception as e:
            logger.error(_("终止模拟器进程时出错: {a}".format(a=str(e))))
        finally:
            # 重置进程号
            runtimeContext._RUNNING_EMU_PID = None

    def StartEmulator():
        hd_player_path = setting.EMU_PATH
        if not os.path.exists(hd_player_path):
            logger.error(_("模拟器启动程序不存在: {a}".format(a=hd_player_path)))
            return False
        
        cmd = ("\"{hd}\" control -v {a}").format(hd=hd_player_path, a=setting.EMU_INDEX)
        try:
            logger.info(_("启动模拟器: {a}".format(a=cmd)))

            # 启动前检查进程
            pre_result_list = CheckEmulator()

            subprocess.Popen(
                cmd, 
                shell=True,
                stdout=subprocess.DEVNULL, 
                stderr=subprocess.DEVNULL,
                cwd=os.path.dirname(hd_player_path))

            # 延时
            time.sleep(5)

            # 启动后检查进程
            aft_results_list = CheckEmulator()

            logger.info(aft_results_list)
            new_tasks = [task for task in aft_results_list if task not in pre_result_list]
            if new_tasks:
                # 模拟器启动成功，pid捕获成功
                runtimeContext._RUNNING_EMU_PID = int(new_tasks[0])
                logger.info(_("模拟器启动开始，进程号为{a}".format(a=runtimeContext._RUNNING_EMU_PID)))

        except Exception as e:
            logger.error(_("启动模拟器失败: {a}".format(a=str(e))))
            return False
        
        logger.info(_("等待模拟器启动..."))
        time.sleep(15)

    # 以上是内部函数
    ####################################
    # 功能实现

    if FORCE_RESTART_EMU:
        KillEmulator()
        time.sleep(1)
    
    if FORCE_RESTART_ADB:
        KillAdb()
        time.sleep(1)

    MAXRETRIES = 20

    adb_path = GetADBPathFromEmuPath(setting.EMU_PATH)

    for attempt in range(MAXRETRIES):
        logger.info(_("-----------------------\n开始尝试连接adb. 次数:{a}/{b}...").format(a=attempt + 1, b=MAXRETRIES))

        if attempt == 3:
            logger.info(_("失败次数过多, 尝试关闭adb."))
            KillAdb()

            # 我们不起手就关, 但是如果2次链接还是尝试失败, 那就触发一次强制重启.
        
        try:
            logger.info(_("检查adb服务..."))
            result = CMDLine(f"\"{adb_path}\" devices")
            if ("daemon not running" in result.stderr) or ("offline" in result.stdout):
                time.sleep(2)
                result = CMDLine(f"\"{adb_path}\" devices")
                if ("daemon not running" in result.stderr) or ("offline" in result.stdout):
                    logger.info("adb服务未启动!\n启动adb服务...")
                    CMDLine(f"\"{adb_path}\" kill-server")
                    CMDLine(f"\"{adb_path}\" start-server")
                    time.sleep(2)

            logger.debug(_("尝试连接到adb..."))
            result = CMDLine(f"\"{adb_path}\" connect {setting.ADB_ADRESS}")

            result = CMDLine(f"\"{adb_path}\" devices")
            if ("{a}").format(a=setting.ADB_ADRESS) in result.stdout:
                logger.info(_("成功连接到模拟器!"))
                results_list = CheckEmulator()
                logger.debug("{a}".format(a=results_list))
                if len(results_list)==1:
                    runtimeContext._RUNNING_EMU_PID = int(results_list[0])
                    logger.info(_("模拟器进程号为{a}.".format(a=runtimeContext._RUNNING_EMU_PID)))
                else:
                    logger.info(_("\n\n***********\n有多个模拟器已经启动, 无法识别进程号. 当需要重启模拟器的时候, 会重启所有模拟器.\n为了避免此问题, 请关闭目标模拟器, 并使用本脚本自动启动模拟器.\n\n"))
                break

            if (not runtimeContext._RUNNING_EMU_PID) or (runtimeContext._RUNNING_EMU_PID not in CheckEmulator()):
                logger.info(_("模拟器未运行，尝试启动..."))
                StartEmulator()
                logger.info(_("模拟器(应该)启动完毕.\n 尝试连接到模拟器..."))
                result = CMDLine(f"\"{adb_path}\" connect {setting.ADB_ADRESS}")
                if result.returncode == 0 and ("connected" in result.stdout or "already" in result.stdout):
                    logger.info(_("成功连接到模拟器"))
                    break
                logger.info(_("无法连接. 检查adb端口."))

            logger.info(_("连接失败: {a}".format(a=result.stderr.strip())))
            time.sleep(2)
            if FORCE_RESTART_EMU or attempt >= 5:
                logger.info(_("连接多次失败, 升级为重启模拟器."))
                KillEmulator()
            else:
                logger.info(_("连接失败, 先只重启ADB, 暂不关闭模拟器."))
            KillAdb()
            time.sleep(2)
        except Exception as e:
            logger.error(_("重启ADB服务时出错: {a}".format(a=e)))
            time.sleep(2)
            if FORCE_RESTART_EMU or attempt >= 5:
                logger.info(_("ADB恢复异常且多次失败, 升级为重启模拟器."))
                KillEmulator()
            else:
                logger.info(_("ADB恢复异常, 先只重启ADB, 暂不关闭模拟器."))
            KillAdb()
            time.sleep(2)
    else:
        logger.info(_("达到最大重试次数，连接失败"))
        return None

    try:
        client = AdbClient(host="127.0.0.1", port=5037)
        devices = client.devices()
        
        # 查找匹配的设备
        target_device = "{a}".format(a=setting.ADB_ADRESS)
        for device in devices:
            if device.serial == target_device:
                logger.info(_("成功创建设备对象: {a}".format(a=device.serial)))
                EnsureClashVpn(setting, device)
                return device
    except Exception as e:
        logger.error(_("创建ADB设备时出错: {a}".format(a=e)))
    
    return None
##################################################################
def CutRoI(screenshot, roi):
    """
    screenshot: 输入图像
    roi: 列表，第一个元素是 (x, y, w, h) 作为main_rect，后续元素是需要涂黑的矩形
    返回: 只包含 main_rect 区域的图像，其中与其他 RoI 重叠的部分已被涂黑
    """
    if roi is None or len(roi) == 0:
        return screenshot

    # 第一个是 main_rect，其余是需要涂黑的区域
    main_rect = roi[0]
    other_rects = roi[1:] if len(roi) > 1 else []

    img_h, img_w = screenshot.shape[:2]

    # 在原图上直接涂黑其他 RoI（注意边界裁剪）
    for rect in other_rects:
        x, y, w, h = rect
        x_start = max(0, x)
        y_start = max(0, y)
        x_end = min(img_w, x + w)
        y_end = min(img_h, y + h)

        if x_start < x_end and y_start < y_end:
            screenshot[y_start:y_end, x_start:x_end] = 0

    # 裁剪出 roi1 区域
    x1, y1, w1, h1 = main_rect
    x1_start = max(0, x1)
    y1_start = max(0, y1)
    x1_end = min(img_w, x1 + w1)
    y1_end = min(img_h, y1 + h1)

    if x1_start >= x1_end or y1_start >= y1_end:
        logger.error("错误:roi1范围无效.")
        return screenshot  # 无效 roi1，返回原图

    main_img = screenshot[y1_start:y1_end, x1_start:x1_end].copy()
    return main_img
##################################################################
def Factory():
    setting =  None
    quest = None
    runtimeContext = RuntimeContext()
    runtimeContext = None
    screenshotBackend = None
    matchCacheFrame = None
    matchResultCache = {}
    brightMaskCache = {}
    ##################################################################
    def ResetScreenshotBackend():
        nonlocal screenshotBackend
        if screenshotBackend is not None:
            screenshotBackend.invalidate()
    def CaptureScreen():
        nonlocal matchCacheFrame
        nonlocal screenshotBackend
        if screenshotBackend is None:
            screenshotBackend = ScreenshotBackendManager(setting)
        image = screenshotBackend.capture(setting, runtimeContext)
        matchCacheFrame = image
        matchResultCache.clear()
        return image
    def NormalizeRoiCacheKey(roi):
        if roi is None:
            return None
        return tuple(tuple(rect) for rect in roi)
    def GetMatchCache(screenImage):
        nonlocal matchCacheFrame
        if matchCacheFrame is not screenImage:
            matchCacheFrame = screenImage
            matchResultCache.clear()
        return matchResultCache
    def ResetDevice(force_restart_emu=False, force_restart_adb = False):
        nonlocal setting # 修改device
        nonlocal runtimeContext
        ResetScreenshotBackend()
        if device := CheckAndRecoverDevice(setting, runtimeContext, force_restart_emu, force_restart_adb):
            setting._ADBDEVICE = device
            logger.info(_("ADB服务成功启动，设备已连接."))
    def DeviceShell(cmdStr):
        while True:
            logger.debug(_("DeviceShell {a}".format(a=cmdStr)))
            exception = None
            result = None
            completed = Event()
            
            def adb_command_thread():
                nonlocal exception, result
                try:
                    result = setting._ADBDEVICE.shell(cmdStr, timeout=5)
                except Exception as e:
                    exception = e
                finally:
                    completed.set()
            
            thread = Thread(target=adb_command_thread)
            thread.daemon = True
            thread.start()
            
            try:
                if not completed.wait(timeout=7):
                    # 线程超时未完成
                    logger.warning(_("ADB命令执行超时: {a}".format(a=cmdStr)))
                    raise TimeoutError(_("ADB命令在7秒内未完成"))
                
                if exception is not None:
                    raise exception
                    
                return result
            except ( RuntimeError, ConnectionResetError, cv2.error) as e:
                logger.warning(_("ADB操作失败 ({a}): {b}").format(a=type(e).__name__, b=e))
                logger.info(_("ADB操作失败, 尝试重启ADB或模拟器程序..."))
                ResetDevice()
                time.sleep(1)

                continue
            except TimeoutError as e:
                logger.info(_("ADB超时, 尝试重启ADB或模拟器程序..."))             
                ResetDevice(force_restart_adb=True)
                time.sleep(1)

                continue
            except Exception as e:
                # 非预期异常直接抛出
                logger.error(_("非预期的ADB异常({a}): {b}").format(a=type(e).__name__, b=e))
                raise
    
    def Sleep(t=1):
        time.sleep(t)
    def ScreenShot():
        nonlocal runtimeContext
        now = time.time()
        if now - runtimeContext._LAST_FOCUS_CHECK >= 3:
            runtimeContext._LAST_FOCUS_CHECK = now
            if "wizardry" not in DeviceShell("dumpsys window | grep mCurrentFocus"):
                logger.error("游戏未启动!")
                restartGame(skip_screenshot=True)

        t = time.time()

        while True:
            try:
                return CaptureScreen()

            except subprocess.TimeoutExpired:
                logger.warning(_("截图超时 (Subprocess)"))
                logger.info(_("ADB操作失败, 尝试重启ADB或模拟器程序..."))
                ResetDevice(force_restart_adb=True)

            except Exception as e:
                logger.debug(_("截图发生异常: {a}".format(a=e)))
                if isinstance(e, (AttributeError, RuntimeError, ConnectionResetError, cv2.error)):
                    logger.info(_("ADB操作失败/数据错误, 尝试重启ADB或模拟器程序..."))
                    ResetDevice()
                if isinstance(e, (ScreenshotAppKeepAliveError)):
                    logger.info(_("你开启了应用保活, 请关闭.\n请在\"设备设置-其他-应用运行\"中关闭."))
                    setting._FORCESTOPING.set()
                    return

                Sleep(1)

    def _check(screenImage, template, roi = None, outputMatchResult = False):
        pos = None
        if roi is None or len(roi) == 0:
            search_area = screenImage
        elif len(roi) == 1:
            x, y, w, h = roi[0]
            img_h, img_w = screenImage.shape[:2]
            x_start = max(0, x)
            y_start = max(0, y)
            x_end = min(img_w, x + w)
            y_end = min(img_h, y + h)
            if x_start >= x_end or y_start >= y_end:
                logger.error("错误:roi1范围无效.")
                search_area = screenImage
            else:
                search_area = screenImage[y_start:y_end, x_start:x_end]
        else:
            search_area = CutRoI(screenImage.copy(), roi)
        try:
            result = cv2.matchTemplate(search_area, template, cv2.TM_CCOEFF_NORMED)
        except Exception as e:
                logger.error("{a}".format(a=e))
                logger.info("{a}".format(a=e))
                if isinstance(e, (cv2.error)):
                    logger.info(_("cv2异常."))
                    # SaveImage(screenshot,"cv2异常")
                    return None, 0
                return None, 0

        underscore, max_val, underscore, max_loc = cv2.minMaxLoc(result)

        if outputMatchResult:
            debug_image = search_area.copy()
            SaveImage(debug_image, "origin.png")
            cv2.rectangle(debug_image, max_loc, (max_loc[0] + template.shape[1], max_loc[1] + template.shape[0]), (0, 255, 0), 2)
            SaveImage(debug_image, "matched.png")

        if roi is None or len(roi) == 0:
            pos=[max_loc[0] + template.shape[1]//2,
                 max_loc[1] + template.shape[0]//2]
        else:
            pos=[roi[0][0] + max_loc[0] + template.shape[1]//2,
                 roi[0][1] + max_loc[1] + template.shape[0]//2]
        return pos,max_val
    def _check_bright_mask(screenImage, template, roi = None, min_brightness = 145, outputMatchResult = False):
        if roi is None or len(roi) == 0:
            search_area = screenImage
            offset_x, offset_y = 0, 0
        elif len(roi) == 1:
            x, y, w, h = roi[0]
            img_h, img_w = screenImage.shape[:2]
            x_start = max(0, x)
            y_start = max(0, y)
            x_end = min(img_w, x + w)
            y_end = min(img_h, y + h)
            if x_start >= x_end or y_start >= y_end:
                logger.error("错误:roi1范围无效.")
                return None, 0
            search_area = screenImage[y_start:y_end, x_start:x_end]
            offset_x, offset_y = x_start, y_start
        else:
            logger.error(_("亮色遮罩匹配仅支持单个ROI."))
            return None, 0

        mask_key = (id(template), min_brightness)
        mask = brightMaskCache.get(mask_key)
        if mask is None:
            gray_template = cv2.cvtColor(template, cv2.COLOR_BGR2GRAY)
            mask = cv2.inRange(gray_template, min_brightness, 255)
            mask = cv2.dilate(mask, np.ones((2, 2), np.uint8), iterations=1)
            brightMaskCache[mask_key] = mask
        if not np.any(mask):
            logger.error(_("亮色遮罩匹配失败: 模板没有足够的亮色像素."))
            return None, 0

        try:
            result = cv2.matchTemplate(search_area, template, cv2.TM_CCORR_NORMED, mask=mask)
            result = np.nan_to_num(result, nan=-1, posinf=-1, neginf=-1)
        except Exception as e:
            logger.error("{a}".format(a=e))
            if isinstance(e, (cv2.error)):
                logger.info(_("cv2异常."))
            return None, 0

        underscore, max_val, underscore, max_loc = cv2.minMaxLoc(result)

        if outputMatchResult:
            debug_image = search_area.copy()
            SaveImage(debug_image, "origin.png")
            cv2.rectangle(debug_image, max_loc, (max_loc[0] + template.shape[1], max_loc[1] + template.shape[0]), (0, 255, 0), 2)
            SaveImage(debug_image, "matched.png")

        pos = [
            offset_x + max_loc[0] + template.shape[1] // 2,
            offset_y + max_loc[1] + template.shape[0] // 2,
        ]
        return pos, max_val
    def CheckTemplateInRoi(screenImage, shortPathOfTarget, roi, threshold=0.8, outputMatchResult=False):
        pos, max_val = _check(screenImage, LoadTemplateImage(shortPathOfTarget), roi, outputMatchResult)
        if max_val < threshold:
            logger.debug(_("{a}在指定ROI内匹配失败, 匹配程度为{b:.2f}%, 不足阈值{c:.2f}%.").format(
                a=shortPathOfTarget, b=max_val * 100, c=threshold * 100
            ))
            return None, max_val
        logger.debug(_("{a}在指定ROI内匹配成功, 匹配程度为{b:.2f}%, 位于{c}.").format(
            a=shortPathOfTarget, b=max_val * 100, c=pos
        ))
        return pos, max_val
    DEFAULT_CHECK_ROI = {
        "combatActive": [[0,0,150,80]],
        "combatActive_2": [[0,0,150,80]],
        "combatActive_3": [[0,0,150,80]],
        "combatActive_4": [[0,0,150,80]],
        "flee": [[720,1120,180,130]],
        "next": [[80,220,819,680]],
        "combatTarget": [[80,220,819,680]],
    }
    def CheckIf(screenImage, shortPathOfTarget, roi = None, outputMatchResult = False):
        if roi is None:
            roi = DEFAULT_CHECK_ROI.get(shortPathOfTarget)
        cache_key = None
        if not outputMatchResult:
            cache_key = ("match", shortPathOfTarget, NormalizeRoiCacheKey(roi))
            cached = GetMatchCache(screenImage).get(cache_key)
            if cached is not None:
                pos, max_val = cached
            else:
                pos, max_val = _check(screenImage, LoadTemplateImage(shortPathOfTarget), roi, outputMatchResult)
                GetMatchCache(screenImage)[cache_key] = (pos, max_val)
        else:
            pos, max_val = _check(screenImage, LoadTemplateImage(shortPathOfTarget), roi, outputMatchResult)

        if max_val < 0.8:
            logger.debug(_("匹配失败: {a}的匹配程度为{b:.2f}%, 不足阈值.".format(a=shortPathOfTarget, b=max_val*100)))
            return None
        else:
            logger.debug(_("匹配成功: {a}的匹配程度为{b:.2f}%, 位于{c}.".format(a=shortPathOfTarget, b=max_val*100,c=pos)))
            return pos
    def CheckHow(screenImage, shortPathOfTarget, roi = None, outputMatchResult = False):
        if roi is None:
            roi = DEFAULT_CHECK_ROI.get(shortPathOfTarget)
        if not outputMatchResult:
            cache_key = ("match", shortPathOfTarget, NormalizeRoiCacheKey(roi))
            cached = GetMatchCache(screenImage).get(cache_key)
            if cached is not None:
                pos, max_val = cached
            else:
                pos, max_val = _check(screenImage, LoadTemplateImage(shortPathOfTarget), roi, outputMatchResult)
                GetMatchCache(screenImage)[cache_key] = (pos, max_val)
        else:
            pos, max_val = _check(screenImage, LoadTemplateImage(shortPathOfTarget), roi, outputMatchResult)

        logger.debug(_("匹配检测: {a}的匹配程度为{b:.2f}%, 位于{c}.".format(a=shortPathOfTarget,b=max_val*100, c=pos)))
        return max_val
    def CheckIf_MultiRect(screenImage, shortPathOfTarget):
        template = LoadTemplateImage(shortPathOfTarget)
        screenshot = screenImage
        result = cv2.matchTemplate(screenshot, template, cv2.TM_CCOEFF_NORMED)

        threshold = 0.8
        ys, xs = np.where(result >= threshold)
        h, w = template.shape[:2]
        rectangles = list([])

        for (x, y) in zip(xs, ys):
            rectangles.append([x, y, w, h])
            rectangles.append([x, y, w, h]) # 复制两次, 这样groupRectangles可以保留那些单独的矩形.
        rectangles, underscore = cv2.groupRectangles(rectangles, groupThreshold=1, eps=0.5)
        pos_list = []
        for rect in rectangles:
            x, y, rw, rh = rect
            center_x = x + rw // 2
            center_y = y + rh // 2
            pos_list.append([center_x, center_y])
            # cv2.rectangle(screenshot, (x, y), (x + w, y + h), (0, 255, 0), 2)
        # cv2.imwrite("Matched_Result.png", screenshot)
        return pos_list
    def CheckIf_FocusCursor(screenImage, shortPathOfTarget):
        template = LoadTemplateImage(shortPathOfTarget)
        screenshot = screenImage
        result = cv2.matchTemplate(screenshot, template, cv2.TM_CCOEFF_NORMED)

        threshold = 0.80
        underscore, max_val, underscore, max_loc = cv2.minMaxLoc(result)
        logger.debug(_("搜索到疑似{a}, 匹配程度:{b:.2f}%".format(a=shortPathOfTarget, b=max_val*100)))
        if max_val >= threshold:
            if max_val<=0.9:
                logger.debug(_("警告: {a}的匹配程度超过了80%但不足90%".format(a=shortPathOfTarget)))

            cropped = screenshot[max_loc[1]:max_loc[1]+template.shape[0], max_loc[0]:max_loc[0]+template.shape[1]]
            SIZE = 15 # size of cursor 光标就是这么大
            left = (template.shape[1] - SIZE) // 2
            right =  left+ SIZE
            top = (template.shape[0] - SIZE) // 2
            bottom =  top + SIZE
            midimg_scn = cropped[top:bottom, left:right]
            miding_ptn = template[top:bottom, left:right]
            # SaveImage(midimg_scn,"miding_scn.png", )
            # SaveImage(miding_ptn,"miding_ptn.png")
            gray1 = cv2.cvtColor(midimg_scn, cv2.COLOR_BGR2GRAY)
            gray2 = cv2.cvtColor(miding_ptn, cv2.COLOR_BGR2GRAY)
            mean_diff = cv2.absdiff(gray1, gray2).mean()/255
            logger.debug(_("中心匹配检查:{a:.2f}".format(a=mean_diff)))

            if mean_diff<0.2:
                return True
        return False
    def CheckIf_ReachPosition(screenImage,targetInfo : TargetInfo):
        screenshot = screenImage
        position = targetInfo.roi
        position = [max(33, min(position[0], 866)), max(33, min(position[1], 1566))]
        cropped = screenshot[position[1]-33:position[1]+33, position[0]-33:position[0]+33]

        for i in range(4):
            pos, max_val = _check(cropped, LoadTemplateImage(f"cursor_{i}"))

            logger.debug(_("目标格搜索{a}, 匹配程度:{b:.2f}%".format(a=position, b=max_val*100)))
            if max_val > 0.8:
                logger.debug(_("已达到检测阈值."))
                return None 
        return position
    def CheckIf_throughStair(screenImage,targetInfo : TargetInfo):
        stair_img = ["stair_up","stair_down","stair_teleport"]
        screenshot = screenImage
        position = targetInfo.roi
        cropped = screenshot[position[1]-33:position[1]+33, position[0]-33:position[0]+33]
        threshold = 0.8
        
        if (targetInfo.target not in stair_img):
            # 验证楼层
            pos, max_val = _check(screenshot, LoadTemplateImage(targetInfo.target))

            logger.debug(_("搜索楼层标识{a}, 匹配程度:{b:.2f}%".format(a=targetInfo.target,b=max_val*100)))
            if max_val > threshold:
                logger.info(_("楼层正确, 判定为已通过"))
                return None
            return position
            
        else: #equal: targetInfo.target IN stair_img
            pos, max_val = _check(cropped, LoadTemplateImage(targetInfo.target))

            logger.debug(_("搜索楼梯{a}, 匹配程度:{b:.2f}%".format(a=targetInfo.target, b=max_val*100)))
            if max_val > threshold:
                logger.info(_("判定为楼梯存在, 尚未通过."))
                return position
            return None
    def CheckIf_harkenStair(screenImage,targetInfo: TargetInfo):
        screenshot = screenImage
        target = targetInfo.target
        correctStair = targetInfo.roi
        threshold = 0.80

        isInCorrectStair = True

        if (correctStair != None) and isinstance(correctStair, str) and correctStair.startswith('stair_'):
            pos, max_val = _check(screenshot, LoadTemplateImage(correctStair))

            logger.debug(_("楼层验证中, 目标楼层标识{a}, 匹配程度:{b:.2f}%".format(a=correctStair,b=max_val*100)))
            if max_val > threshold:
                logger.info(_("楼层验证判定通过, 当前处于正确楼层."))
                isInCorrectStair = True
            else:
                isInCorrectStair = False
        
        if not isInCorrectStair:
            logger.info(_("目前处于错误的楼层中, 可能是由于错误点击导致的, 开始全地图搜索哈肯."))
            pos = StateMap_FindSwipeClick(TargetInfo('harken', None, None))
            if pos == None:
                pos = StateMap_FindSwipeClick(TargetInfo('Bharken', None, None))
            return pos
        if isInCorrectStair:
            pos, max_val = _check(screenshot, LoadTemplateImage(target))

            logger.debug(_("哈肯搜索, 目标{a}, 匹配程度:{b:.2f}%".format(a=target,b=max_val*100)))
            if max_val > threshold:
                logger.info(_("哈肯搜索, 已找到哈肯."))
                return pos
            return None
            
    def CheckIf_fastForwardOff(screenImage):
        position = [240,1490]
        template =  LoadTemplateImage(f"fastforward_off")
        screenshot =  screenImage
        cropped = screenshot[position[1]-50:position[1]+50, position[0]-50:position[0]+50]
        
        result = cv2.matchTemplate(cropped, template, cv2.TM_CCOEFF_NORMED)
        underscore, max_val, underscore, max_loc = cv2.minMaxLoc(result)
        threshold = 0.80
        pos=[position[0]+max_loc[0] - cropped.shape[1]//2, position[1]+max_loc[1] -cropped.shape[0]//2]

        if max_val > threshold:
            logger.info(_("快进未开启, 即将开启.{a}".format(a=pos)))
            return pos
        return None
    def Press(pos):
        if pos!=None:
            DeviceShell(f"input tap {pos[0]} {pos[1]}")
            return True
        return False
    def PressBatch(pos_list):
        valid_pos = [pos for pos in pos_list if pos is not None and len(pos) == 2]
        if not valid_pos:
            return False
        commands = [f"input tap {int(pos[0])} {int(pos[1])}" for pos in valid_pos]
        DeviceShell("; ".join(commands))
        return True
    def PressReturn():
        DeviceShell("input keyevent KEYCODE_BACK")
    def WrapImage(image,r,g,b):
        scn_b = image * np.array([b, g, r])
        return np.clip(scn_b, 0, 255).astype(np.uint8)
    def MinusImage(image,r,g,b):
        scn_b = image - np.array([b, g, r])
        return np.clip(scn_b, 0, 255).astype(np.uint8)
    def TryPressRetry(scn):
        if Press(CheckIf(scn,"startdownload")):
            logger.info(_("开始下载……"))
            return True
        if Press(CheckIf(scn,"retry")):
            logger.info(_("发现并点击了\"重试\". 你遇到了网络波动."))
            return True
        if pos:=(CheckIf(scn,"retry_blank")):
            Press([pos[0], pos[1]+103])
            logger.info(_("发现并点击了\"重试\". 你遇到了网络波动."))
            return True
        return False
    def AddImportantInfo(str):
        nonlocal runtimeContext
        if runtimeContext._IMPORTANTINFO == "":
            runtimeContext._IMPORTANTINFO = _("👆向上滑动查看重要信息👆\n")
        time_str = datetime.now().strftime("%Y%m%d-%H%M%S") 
        runtimeContext._IMPORTANTINFO = " {a} {b}\n{c}".format(a = time_str, b=str, c=runtimeContext._IMPORTANTINFO)
    ##################################################################
    def SaveDebugImage(scn, reason):
        reason = re.sub(r"[^0-9A-Za-z_-]+", "_", str(reason)).strip("_") or "debug"
        SaveImage(scn, f"debug_{reason}_{datetime.now().strftime('%H%M%S.%f')[:-3]}")
    def SaveDebugImageThrottled(scn, reason, min_interval=60):
        if scn is None:
            return False
        reason = re.sub(r"[^0-9A-Za-z_-]+", "_", str(reason)).strip("_") or "debug"
        now = time.time()
        last_saved = getattr(runtimeContext, "_LAST_DEBUG_IMAGE_AT", None)
        if not isinstance(last_saved, dict):
            last_saved = {}
            runtimeContext._LAST_DEBUG_IMAGE_AT = last_saved
        if now - last_saved.get(reason, 0) < min_interval:
            return False
        last_saved[reason] = now
        SaveDebugImage(scn, reason)
        return True
    def CollectMatchDiagnostics(scn, probes):
        results = []
        for probe in probes:
            pattern, label = probe[:2]
            roi = probe[2] if len(probe) >= 3 else None
            try:
                score = CheckHow(scn, pattern, roi)
            except Exception as e:
                logger.debug(_("诊断匹配跳过: {a}, 原因={b}.").format(a=pattern, b=e))
                continue
            results.append((score, label, pattern))
        results.sort(key=lambda item: item[0], reverse=True)
        return results
    def LogUnknownScreenDiagnostics(scn, reason, counter=None, save_image=False):
        if scn is None:
            logger.info(_("未知界面诊断: reason={a}, counter={b}, screen=None.").format(
                a=reason, b=counter
            ))
            return
        probes = [
            ("dungFlag", "副本移动"),
            ("chestFlag", "宝箱"),
            ("whowillopenit", "宝箱选择"),
            ("mapFlag", "地图"),
            ("combatActive", "战斗Active"),
            ("combatActive_2", "战斗Active_2"),
            ("combatActive_3", "战斗Active_3"),
            ("combatActive_4", "战斗Active_4"),
            ("retry", "网络重试"),
            ("retry_blank", "空白重试"),
            ("startdownload", "下载确认", [[222, 901, 465, 84]]),
            ("returnText", "返回文本"),
            ("returntoTown", "返回城镇"),
            ("worldmapflag", "世界地图"),
            ("openworldmap", "打开世界地图"),
            ("Inn", "旅店"),
            ("RiseAgain", "复活"),
            ("sandman_recover", "沙男恢复"),
            ("totitle", "返回标题"),
            ("resume", "Resume"),
            ("trait", "角色界面Trait"),
            ("recover", "恢复按钮"),
            ("spellskill/skillDetail", "技能详情"),
            ("close", "底部Close", [[250, 1420, 420, 150]]),
            ("spellskill/CombatAutoDisable", "自动战斗未开启", [[780, 1030, 120, 160]]),
            ("spellskill/CombatAutoEnable", "自动战斗已开启", [[780, 1030, 120, 160]]),
        ]
        top_matches = CollectMatchDiagnostics(scn, probes)[:6]
        summary = ", ".join(
            "{a}={b:.1f}%".format(a=label, b=score * 100)
            for score, label, pattern in top_matches
        )
        logger.info(_("未知界面诊断: reason={a}, counter={b}, top={c}.").format(
            a=reason, b=counter, c=summary
        ))
        if save_image:
            saved = SaveDebugImageThrottled(scn, reason, min_interval=60)
            if not saved:
                logger.debug(_("未知界面诊断截图已限频跳过: reason={a}.").format(a=reason))
    def CheckSkillPopupOpen(scn):
        if CheckIf(scn, "spellskill/skillDetail"):
            return True
        close_pos, close_match = CheckTemplateInRoi(scn, "close", [[250, 1420, 420, 150]], threshold=0.8)
        if close_pos:
            logger.debug(_("检测到技能详情弹窗底部Close按钮: 坐标={a}, 匹配程度={b:.2f}%.").format(
                a=close_pos, b=close_match * 100
            ))
            return True
        return False
    def FindCoordsOrElseExecuteFallbackAndWait(targetPattern, fallback,waitTime):
        # fallback可以是坐标[x,y]或者字符串. 当为字符串的时候, 视为图片地址
        last_scn = None
        last_try_index = 0
        def pressTarget(target):
            if target.lower() == "return":
                PressReturn()
            elif target.startswith("input swipe"):
                DeviceShell(target)
            else:
                Press(CheckIf(scn, target))
                Sleep(0.2)
        def checkPattern(scn, pattern):
            if pattern.startswith("combatActive"):
                return StateCombatCheck(scn)
            else:
                return CheckIf(scn,pattern)

        while True:
            skill_detail_debug_saved = False
            for underscore in range(setting.MAX_TRY_LIMIT):
                if setting._FORCESTOPING.is_set():
                    return None
                scn = ScreenShot()
                last_scn = scn
                last_try_index = underscore + 1
                if isinstance(targetPattern, (list, tuple)):
                    for pattern in targetPattern:
                        if p:=checkPattern(scn, pattern):
                            return p
                else:
                    if p:=checkPattern(scn,targetPattern):
                        return p # FindCoords
                # OrElse
                if TryPressRetry(scn):
                    Sleep(1)
                    continue
                if Press(CheckIf_fastForwardOff(scn)):
                    Sleep(1)
                    continue
                if CheckSkillPopupOpen(scn):
                    logger.warning(_("等待目标{a}时检测到技能详情弹窗仍在屏幕上. 当前尝试次数: {b}/{c}.").format(
                        a=targetPattern, b=underscore + 1, c=setting.MAX_TRY_LIMIT
                    ))
                    if not skill_detail_debug_saved:
                        SaveDebugImage(scn, "wait_target_skill_detail")
                        skill_detail_debug_saved = True

                if fallback: # Execute
                    if isinstance(fallback, (list, tuple)):
                        if (len(fallback) == 2) and all(isinstance(x, (int, float)) for x in fallback):
                            logger.debug(_("等待目标{a}未命中, 执行坐标fallback: {b}. 尝试次数: {c}/{d}.").format(
                                a=targetPattern, b=fallback, c=underscore + 1, d=setting.MAX_TRY_LIMIT
                            ))
                            Press(fallback)
                        else:
                            for p in fallback:
                                if isinstance(p, str):
                                    logger.debug(_("等待目标{a}未命中, 执行图片/命令fallback: {b}. 尝试次数: {c}/{d}.").format(
                                        a=targetPattern, b=p, c=underscore + 1, d=setting.MAX_TRY_LIMIT
                                    ))
                                    pressTarget(p)
                                elif isinstance(p, (list, tuple)) and len(p) == 2:
                                    t = time.time()
                                    logger.debug(_("等待目标{a}未命中, 执行坐标fallback: {b}. 尝试次数: {c}/{d}.").format(
                                        a=targetPattern, b=p, c=underscore + 1, d=setting.MAX_TRY_LIMIT
                                    ))
                                    Press(p)
                                    if (waittime:=(time.time()-t)) < 0.1:
                                        Sleep(0.1-waittime)
                                else:
                                    logger.debug(_("错误: 非法的目标{a}.".format(a=p)))
                                    setting._FORCESTOPING.set()
                                    return None
                    else:
                        if isinstance(fallback, str):
                            logger.debug(_("等待目标{a}未命中, 执行图片/命令fallback: {b}. 尝试次数: {c}/{d}.").format(
                                a=targetPattern, b=fallback, c=underscore + 1, d=setting.MAX_TRY_LIMIT
                            ))
                            pressTarget(fallback)
                        else:
                            logger.debug(_("错误: 非法的目标."))
                            setting._FORCESTOPING.set()
                            return None
                Sleep(waitTime) # and wait

            logger.info(_("{a}次截图依旧没有找到目标{b}, 疑似卡死. 重启游戏.".format(a=setting.MAX_TRY_LIMIT, b=targetPattern)))
            if last_scn is not None:
                logger.info(_("等待目标失败前最后一次截图: 目标={a}, fallback={b}, 最后尝试次数={c}.").format(
                    a=targetPattern, b=fallback, c=last_try_index
                ))
                if CheckSkillPopupOpen(last_scn):
                    logger.warning(_("等待目标失败前最后一帧仍检测到技能详情弹窗."))
                SaveDebugImage(last_scn, "wait_target_failed")
            Sleep()
            restartGame()
            return None # restartGame会抛出异常 所以直接返回none就行了
    def restartGame(skip_screenshot = False, force_restart_EMU = False):
        nonlocal runtimeContext
        runtimeContext._COMBATSPD = False # 重启会重置2倍速, 所以重置标识符以便重新打开.
        runtimeContext._TIME_CHEST = 0
        runtimeContext._TIME_COMBAT = 0 # 因为重启了, 所以清空战斗和宝箱计时器.
        runtimeContext._ZOOMWORLDMAP = False
        runtimeContext._BYPASSAFTERRESTART = False

        # 重新装载战斗策略
        ReloadStrategy()

        # 保存重启前截图作为备份
        if not skip_screenshot:
            logger.info(_("进行重启前截图..."))
            SaveImage(ScreenShot())

        package_name = GAME_PACKAGE_NAME
        def TryDeviceShellOnce(cmdStr):
            logger.debug(_("TryDeviceShellOnce {a}".format(a=cmdStr)))
            try:
                return setting._ADBDEVICE.shell(cmdStr, timeout=5)
            except Exception as e:
                logger.warning(_("ADB命令执行失败, 本次不自动重启模拟器 ({a}): {b}").format(a=cmdStr, b=e))
                return None
        def TryRestartGameApp():
            logger.info(_("尝试仅重启游戏应用..."))
            if TryDeviceShellOnce("logcat -c") is None:
                return False

            mainActResult = TryDeviceShellOnce(f"cmd package resolve-activity --brief {package_name}")
            if mainActResult is None:
                return False
            mainAct = mainActResult.strip().split("\n")[-1]
            if not mainAct:
                logger.warning(_("未能解析游戏启动Activity."))
                return False

            if TryDeviceShellOnce(f"am force-stop {package_name}") is None:
                return False
            Sleep(2)

            focus = TryDeviceShellOnce("dumpsys window | grep mCurrentFocus")
            if focus is None:
                return False
            if package_name in focus:
                logger.warning(_("执行force-stop后游戏仍在前台, 判定为仅重启游戏失败."))
                return False

            logger.info(_("巫术, 启动!"))
            startResult = TryDeviceShellOnce(f"am start -n {mainAct}")
            if startResult is None:
                return False
            logger.debug(startResult)
            Sleep(10)

            logs = TryDeviceShellOnce("logcat -d | grep -i \"unable to initialize.*graphics api\"")
            if logs and logs.strip():
                logger.error(_("检测到崩溃日志, 暂时不重启模拟器.{a}".format(a=logs)))
            runtimeContext._PAUSE_CHECK_UNTIL = time.time() + 120
            return True

        runtimeContext._CRASHCOUNTER +=1
        logger.info(_("崩溃计数: {a}\n崩溃计数超过{b}次后会重启模拟器.".format(a=runtimeContext._CRASHCOUNTER, b=setting.MAX_CRASH_LIMIT)))
        if runtimeContext._CRASHCOUNTER > setting.MAX_CRASH_LIMIT:
            runtimeContext._CRASHCOUNTER = 0
            force_restart_EMU = True

        if force_restart_EMU:
            ResetDevice(force_restart_emu=True)
            Sleep(5)

        if not TryRestartGameApp():
            logger.info(_("仅重启游戏失败, 先尝试恢复ADB后再次重启游戏."))
            ResetDevice(force_restart_adb=True)
            Sleep(2)
            if not TryRestartGameApp():
                logger.info(_("恢复ADB后仍无法仅重启游戏, 升级为重启模拟器."))
                ResetDevice(force_restart_emu=True)
                Sleep(5)
                if not TryRestartGameApp():
                    logger.error(_("重启模拟器后仍无法启动游戏."))

        raise RestartSignal()
    class RestartSignal(Exception):
        pass
    def RestartableSequenceExecution(*operations):
        while True:
            if setting._FORCESTOPING.is_set():
                logger.info(_("任务已停止."))
                return
            try:
                for op in operations:
                    if setting._FORCESTOPING.is_set():
                        logger.info(_("任务已停止."))
                        return
                    op()
                return
            except RestartSignal:
                if setting._FORCESTOPING.is_set():
                    logger.info(_("任务已停止."))
                    return
                logger.info(_("任务进度重置中..."))
                continue
    ##################################################################
    def ReloadStrategy():
        # 会在每次重启游戏和角色死亡后重置策略.
        # 根据面板设置, 可以有额外的重启要求.
        nonlocal runtimeContext, setting

        logger.info(_("重置战斗策略."))

        # 如果不是特定任务配置，使用默认的全局策略
        if not setting.TASK_SPECIFIC_CONFIG:
            strategy_key = setting.DEFAULT_OVERALL_STRATEGY
        else:
            overall = setting.TASK_POINT_STRATEGY.get("overall_strategy", "")
            if overall == _("自定义任务点策略"):
                task_step_idx = getattr(runtimeContext, 'TASK_STEP_INDEX', 0)
                strategy_key = setting.TASK_POINT_STRATEGY.get("task_point", {}).get(str(task_step_idx), "")
            else:
                strategy_key = overall

        # 直接从 setting.STRATEGY 原始列表中查找匹配项并深拷贝
        target_dict = None
        for d in setting.STRATEGY:
            if d.get("group_name") == strategy_key:
                target_dict = d
                break

        if target_dict is not None:
            runtimeContext.CURRENT_STRATEGY = copy.deepcopy(target_dict)
            logger.debug(_("策略已重置为: {a}").format(a=strategy_key))
        else:
            runtimeContext.CURRENT_STRATEGY = {}
            logger.error(_("无法获取当前策略，将降级为全自动战斗。"))
    ##################################################################
    class State(Enum):
        Dungeon = "dungeon"
        Inn = "inn"
        EoT = "edge of Town"
        Quit = "quit"
    class DungeonState(Enum):
        Dungeon = "dungeon"
        Map = "map"
        Chest = "chest"
        Combat = "combat"
        Quit = "quit"

    def BagClear_Item():
        Press([847,1174])
        Sleep(1)
        if not CheckIf(ScreenShot(), "itemList"):
            return
        for i in range(6):
            Press([350+(i%3)*200, 1290+(i//3)*70])
            Sleep(1)
            while 1:
                if not Press(CheckIf(ScreenShot(),"itemIcon", [[67,871,94,288]])):
                    break
                Press(CheckIf(ScreenShot(),"putinstorage"))
                Sleep(1)
        
        while 1:
            if Press(CheckIf(ScreenShot(), "close")):
                Sleep(1)
            else:
                break
    def reunionParty(partyName = None):
        logger.info(_("必须重新集结队伍..."))

        if not partyName:
            RestartableSequenceExecution(
                        lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("Edit",["guild",[1,1]],1)),
                        lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("PartyManagement",["Edit",[1,1]],1)),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("AdventurerGuild",["PartyManagement",[1,1]],1),
                        lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("ok",[[137,290],"AssembleParty"],1)),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn","return",1)
                )
        else:
            RestartableSequenceExecution(
                                    lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("Edit",["guild",[1,1]],1)),
                                    lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("PartyManagement",["Edit",[1,1]],1)),
                                    lambda: FindCoordsOrElseExecuteFallbackAndWait("AdventurerGuild",["PartyManagement",[1,1]],1),
                                    lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("ok",[partyName,"AssembleParty"],1)),
                                    lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn","return",1)
                            )
    def DungeonCompletionCounter():
        nonlocal runtimeContext
        # 如果发生了开箱或者战斗那么+1
        if runtimeContext._MEET_CHEST_OR_COMBAT:
            runtimeContext._MEET_CHEST_OR_COMBAT = False
            runtimeContext._COUNTERDUNG+=1

        if runtimeContext._LAPTIME!= 0:
            runtimeContext._TOTALTIME = runtimeContext._TOTALTIME + time.time() - runtimeContext._LAPTIME
            summary_text = _("已完成{a}次\"{b}\"地下城.\n总计{c}秒.上次用时:{d}秒.\n".format(a=runtimeContext._COUNTERDUNG, b=setting.FARM_TARGET_TEXT, c=round(runtimeContext._TOTALTIME,2), d=round(time.time()-runtimeContext._LAPTIME,2)))
            if runtimeContext._COUNTERCHEST > 0:
                summary_text += f"箱子效率{round(runtimeContext._TOTALTIME/runtimeContext._COUNTERCHEST,2)}秒/箱.\n累计开箱{runtimeContext._COUNTERCHEST}次,开箱平均耗时{round(runtimeContext._TIME_CHEST_TOTAL/runtimeContext._COUNTERCHEST,2)}秒.\n"
            if runtimeContext._COUNTERCOMBAT > 0:
                summary_text += _("累计战斗{a}次.战斗平均用时{b}秒.".format(a=runtimeContext._COUNTERCOMBAT, b=round(runtimeContext._TIME_COMBAT_TOTAL/runtimeContext._COUNTERCOMBAT,2)))
            logger.info("{a}{b}".format(a=runtimeContext._IMPORTANTINFO, b=summary_text),extra={"summary": True})
        # 圈数计时器
        runtimeContext._LAPTIME = time.time()

    def TeleportFromCityToWorldLocation(target, swipe, press_any_key = [550,1]):
        nonlocal runtimeContext
        FindCoordsOrElseExecuteFallbackAndWait(["intoWorldMap","dungFlag","worldmapflag","openworldmap"],["startdownload","closePartyInfo","closePartyInfo_fortress","closePartyInfo_waterway",[1,1]],1)
        
        if CheckIf(scn:=ScreenShot(), "dungflag"):
            # 如果已经在副本里了 直接结束.
            # 因为该函数预设了是从城市开始的.
            return
        
        if CheckIf(scn, "openworldmap"):
            # 如果已经进入了洞窟, 直接结束.
            # 因为这是无战斗无宝箱然后重新尝试的情况.
            return
        
        if Press(CheckIf(scn,"intoWorldMap")):
            # 如果在城市, 尝试进入世界地图
            Sleep(0.5)
            FindCoordsOrElseExecuteFallbackAndWait("worldmapflag","intoWorldMap",1)
        elif CheckIf(scn,"worldmapflag"):
            # 如果在世界地图, 下一步.
            pass

        # 往下都是确保了现在能看见"worldmapflag", 并尝试看见"target"
        Sleep(0.5)
        if not runtimeContext._ZOOMWORLDMAP:
            for underscore in range(3):
                Press([100,1500])
                Sleep(0.5)
            Press([250,1500])
            runtimeContext._ZOOMWORLDMAP = True
        pos = FindCoordsOrElseExecuteFallbackAndWait([target,"openworldmap"],[swipe,press_any_key],1)

        # 现在已经确保了可以看见target, 那么确保可以点击成功
        Sleep(1)
        Press(pos)
        Sleep(1)
        FindCoordsOrElseExecuteFallbackAndWait(["Inn","openworldmap","dungFlag"],[target,press_any_key],1)
    
    def TeleportFromDungeonToCity(target, swipe=None, press_any_key = [550,1]):
        nonlocal runtimeContext
        if Path(target).name.startswith("EVENT_"):
            FindCoordsOrElseExecuteFallbackAndWait("Inn", [[1,1],"worldmapflag","EVENT",target],2)
            return

        FindCoordsOrElseExecuteFallbackAndWait(["dungFlag","worldmapflag","openworldmap","startdownload"],"openworldmap",1)
        scn = ScreenShot()

        if Press(CheckIf(scn, "openworldmap")):
            pass
        elif CheckIf(scn,"worldmapflag"):
            # 如果在世界地图, 下一步.
            pass

        # 往下都是确保了现在能看见"worldmapflag", 并尝试看见"target"
        Sleep(0.5)
        if not runtimeContext._ZOOMWORLDMAP:
            for underscore in range(3):
                Press([100,1500])
                Sleep(0.5)
            Press([250,1500])
            runtimeContext._ZOOMWORLDMAP = True
        pos = FindCoordsOrElseExecuteFallbackAndWait(target,[swipe,press_any_key],1)

        # 现在已经确保了可以看见target, 那么确保可以点击成功
        Sleep(1)
        Press(pos)
        Sleep(1)
        FindCoordsOrElseExecuteFallbackAndWait(["Inn","openworldmap","dungFlag"],[target,press_any_key],1)
        
    def CursedWheelTimeLeap(target="GhostsOfYore", CSC_symbol=None,CSC_setting = None, chapter = "cursedwheel_impregnableFortress"):
        # CSC_symbol: 是否开启因果? 如果开启因果, 将用这个作为是否点开ui的检查标识
        # CSC_setting: 默认会先选择不接所有任务. 这个列表中储存的是想要打开的因果.
        # 其中的RGB用于缩放颜色维度, 以增加识别的可靠性.
        if setting.ACTIVE_CSC == False:
            logger.info(_("因为面板设置, 跳过了调整因果."))
            CSC_symbol = None

        logger.info(_("开始时间跳跃, 本次跳跃目标:{a}".format(a=target)))

        # 调整条目以找到跳跃目标
        FindCoordsOrElseExecuteFallbackAndWait("cursedWheelTitle",["cursedWheel","ruins","startdownload",[1,1]],1)
        Sleep(2)
        if Press(CheckIf(ScreenShot(),target)):
            Sleep(2)
            Press(CheckIf(ScreenShot(),"leap"))
            Sleep(2)
            if not CheckIf(ScreenShot(),"leap"):
                return

        # 翻页
        for underscore in range(10):
            Press([105,230])
            Sleep(0.5)
        Press(FindCoordsOrElseExecuteFallbackAndWait(chapter,["cursedWheelTapRight","cursedWheel",[1,1]],1))
        if not Press(CheckIf(ScreenShot(),target)):
            DeviceShell(f"input swipe 450 1200 450 200")
            Sleep(2)
            DeviceShell(f"input swipe 450 1200 450 200")
            Sleep(2)
            DeviceShell(f"input swipe 450 1200 450 200")
            Sleep(2)
            Press(FindCoordsOrElseExecuteFallbackAndWait(target,"input swipe 50 1200 50 1300",1))
        Sleep(1)

        # 跳跃前尝试调整因果
        while CheckIf(ScreenShot(), "leap"):
            if CSC_symbol != None:
                FindCoordsOrElseExecuteFallbackAndWait(CSC_symbol,"CSC",1)
                last_scn = CutRoI(ScreenShot(), [[77,349,757,1068]])
                # 先关闭所有因果
                while 1:
                    Press(CheckIf(WrapImage(ScreenShot(),2,0,0),"didnottakethequest"))
                    DeviceShell(f"input swipe 150 500 150 400")
                    Sleep(1)
                    scn = CutRoI(ScreenShot(), [[77,349,757,1068]])
                    logger.debug(f"因果: 滑动后的截图误差={cv2.absdiff(scn, last_scn).mean()/255:.6f}")
                    if cv2.absdiff(scn, last_scn).mean()/255 < 0.006:
                        break
                    else:
                        last_scn = scn
                # 然后调整每个因果
                if CSC_setting!=None:
                    last_scn = CutRoI(ScreenShot(), [[77,349,757,1068]])
                    while 1:
                        for option, r, g, b in CSC_setting:
                            Press(CheckIf(WrapImage(ScreenShot(),r,g,b),option))
                            Sleep(1)
                        DeviceShell(f"input swipe 150 400 150 500")
                        Sleep(1)
                        scn = CutRoI(ScreenShot(), [[77,349,757,1068]])
                        logger.debug(f"因果: 滑动后的截图误差={cv2.absdiff(scn, last_scn).mean()/255:.6f}")
                        if cv2.absdiff(scn, last_scn).mean()/255 < 0.006:
                            break
                        else:
                            last_scn = scn
                PressReturn()
                Sleep(0.5)
            Press(CheckIf(ScreenShot(),"leap"))
            Sleep(2)
            Press(CheckIf(ScreenShot(),target))

    def RiseAgainReset(reason):
        nonlocal runtimeContext
        runtimeContext._SUICIDE = False # 死了 自杀成功 设置为false
        runtimeContext._RECOVERAFTERREZ = True
        if reason == "chest":
            runtimeContext._COUNTERCHEST -=1
        else:
            runtimeContext._COUNTERCOMBAT -=1
        logger.info(_("快快请起."))
        AddImportantInfo(_("面具死了, 但是再起."))
        Press([450,750])
        Sleep(10)

        ReloadStrategy()

        return 
    def IdentifyState():
        nonlocal setting # 修改因果
        counter = 0
        while 1:
            screen = ScreenShot()
            logger.info(_("状态检查中...(第{a}次)".format(a=counter+1)))

            if setting._FORCESTOPING.is_set():
                return State.Quit, DungeonState.Quit, screen

            if TryPressRetry(screen):
                Sleep(2)
                counter += 1
                continue

            if Press(CheckIf(screen,"startdownload",[[222,901,465,84]])):
                logger.info(_("确认, 下载, 确认."))
                Sleep(2)
                counter += 1
                continue

            should_check_pause = (counter >= 2) or (time.time() < getattr(runtimeContext, "_PAUSE_CHECK_UNTIL", 0))
            if should_check_pause and TryResumePauseOverlay(screen):
                counter = 0
                return IdentifyState()

            identifyConfig = [
                ("dungFlag",      DungeonState.Dungeon),
                ("chestFlag",     DungeonState.Chest),
                ("whowillopenit", DungeonState.Chest),
                ("mapFlag",       DungeonState.Map),
                ]
            for pattern, state in identifyConfig:
                if CheckIf(screen, pattern):
                    return State.Dungeon, state, screen
                
            if StateCombatCheck(screen):
                return State.Dungeon, DungeonState.Combat, screen

            screen = ScreenShot()

            if CheckIf(screen,"someonedead"):
                AddImportantInfo(_("尝试复活队友..."))
                ReloadStrategy()
                Sleep(1)
                for underscore in range(5):
                    Press([400+random.randint(0,100),750+random.randint(0,100)])
                    Sleep(1)

            if Press(CheckIf(screen, "returnText")):
                Sleep(2)
                return IdentifyState()

            if CheckIf(screen,"returntoTown"):
                if  (
                        setting.ACTIVE_REST and                             # 激活休息
                        runtimeContext._MEET_CHEST_OR_COMBAT and            # 遇到宝箱或战斗
                        ((runtimeContext._COUNTERDUNG - 1) % (max(setting.REST_INTERVEL, 1)) == 0)              # 每隔 REST_INTERVEL 次触发
                    ) or (
                        setting.RE_ASSEMBLE_PARTY and                       # 重新集结队伍
                        (int(runtimeContext._TOTALTIME // (6 * 3600)) != runtimeContext._LAST_BAGCLEAR)         # 每 6 小时清包检查
                    ):
                    FindCoordsOrElseExecuteFallbackAndWait("Inn",["return",[1,1]],1)
                    return State.Inn,DungeonState.Quit, screen
                else:
                    
                    logger.info(_("不满足回城条件, 跳过回城."))
                    return State.EoT,DungeonState.Quit,screen

            if CheckIf(screen, "worldmapflag"):
                for underscore in range(3):
                    Press([100,1500])
                    Sleep(0.5)
                Press([250,1500])
                # 由于定位流程包含了返回键, 有时会退出到大地图, 因此强制执行RTT流程, 不管是否需要住宿.
                # if setting.ACTIVE_REST and runtimeContext._MEET_CHEST_OR_COMBAT and ((runtimeContext._COUNTERDUNG-1) % (max(setting.REST_INTERVEL,1)) == 0):
                if quest._RTT:
                    TeleportFromDungeonToCity(*quest._RTT)
                    return IdentifyState()
                       
            if pos:=(CheckIf(screen,"openworldmap")):
                if setting.ACTIVE_REST and runtimeContext._MEET_CHEST_OR_COMBAT and ((runtimeContext._COUNTERDUNG-1) % (max(setting.REST_INTERVEL,1)) == 0):
                    Press(pos)
                    if quest._RTT:
                        TeleportFromDungeonToCity(*quest._RTT)
                    return IdentifyState()
                else:
                    logger.info(_("不满足回城条件, 跳过回城."))
                    return State.EoT,DungeonState.Quit,screen

            for city in ["City_RoyalCityLuknalia","City_fortress", "City_DHI","City_portTownGrandLegion"]:
                if CheckIf(screen,city):
                    FindCoordsOrElseExecuteFallbackAndWait(["Inn","dungFlag"],[city,[1,1]],1)
                    if CheckIf(scn:=ScreenShot(),"Inn"):
                        return State.Inn,DungeonState.Quit, screen
                    elif CheckIf(scn,"dungFlag"):
                        return State.Dungeon,None, screen

            if (CheckIf(screen,"Inn")):
                return State.Inn, None, screen

            if quest._SPECIALFORCESTOPINGSYMBOL != None:
                for symbol in quest._SPECIALFORCESTOPINGSYMBOL:
                        if CheckIf(screen,symbol):
                            return State.Quit,DungeonState.Quit,screen
                        
            if quest._SPECIALDIALOGOPTION != None:
                for option in quest._SPECIALDIALOGOPTION:
                    if Press(CheckIf(screen,option)):
                        return IdentifyState()

            if counter>=4:
                logger.info(_("看起来遇到了一些不太寻常的情况..."))
                LogUnknownScreenDiagnostics(
                    screen,
                    "identify_unknown",
                    counter=counter + 1,
                    save_image=(counter == 4 or counter % 10 == 0)
                )
                if Press(CheckIf(screen,"RiseAgain")):
                    RiseAgainReset(reason = "combat")
                    return IdentifyState()
                if Press(CheckIf(screen, "sandman_recover")):
                    return IdentifyState()
                if (CheckIf(screen,"cursedWheel_timeLeap")):
                    if (setting.ACTIVE_BEG_MONEY):
                        setting._MSGQUEUE.put(("turn_to_7000G",""))
                        raise SystemExit
                    else:
                        logger.info(_("看起来你没有选择找王女要钱. 那么就等两个小时吧."), extra={"summary": True})
                        Sleep(7300)
                        restartGame()
                if CheckIf(screen,"ambush") or CheckIf(screen,"ignore"):
                    if int(setting.KARMA_ADJUST) == 0:
                        Press(CheckIf(screen,"ambush"))
                        new_str = "+2"
                    elif setting.KARMA_ADJUST.startswith("-"):
                        Press(CheckIf(screen,"ambush"))
                        num = int(setting.KARMA_ADJUST)
                        num = num + 2
                        new_str = "{a}".format(a=num)
                    else:
                        Press(CheckIf(screen,"ignore"))
                        num = int(setting.KARMA_ADJUST)
                        num = num - 1
                        new_str = f"+{num}"

                    logger.info(_("即将进行善恶值调整. 剩余次数:{a}".format(a=new_str)))
                    AddImportantInfo(_("新的善恶:{a}".format(a=new_str)))
                    setting.KARMA_ADJUST = new_str
                    SetOneVarInGeneralConfig("KARMA_ADJUST",setting.KARMA_ADJUST)
                    Sleep(2)

                for op in DIALOG_OPTION_IMAGE_LIST:
                    if Press(CheckIf(screen, "dialogueChoices/"+op)):
                        Sleep(2)
                        if op == "adventurersbones":
                            AddImportantInfo(_("购买了骨头."))
                        if op == "halfBone":
                            AddImportantInfo(_("购买了尸油."))
                        return IdentifyState()
                    
                if pos_b:=CheckIf(screen,"blessing"):
                    if pos:=CheckIf(screen, "combatClose"): # 如果因为某些原因点到了切换哈肯祝福, 进入了二次确认界面
                        Press(pos) # 我们把二次确认的界面关了, 无事发生
                    else:
                        Press(pos_b)
                
                if (CheckIf(screen,"multipeopledead")):
                    runtimeContext._SUICIDE = True # 准备尝试自杀
                    logger.info(_("死了好几个, 惨哦"))
                    # logger.info("Corpses strew the screen")
                    Press(CheckIf(screen,"skull"))
                    Sleep(2)

                if Press(CheckIf(screen,"totitle")):
                    logger.info(_("网络故障警报! 网络故障警报! 返回标题, 重复, 返回标题!"))
                    return IdentifyState()
                PressReturn()
                Sleep(0.5)
                PressReturn()
            if counter>= setting.MAX_TRY_LIMIT:
                logger.info(_("看起来遇到了一些非同寻常的情况...重启游戏."))
                LogUnknownScreenDiagnostics(
                    screen,
                    "identify_restart",
                    counter=counter + 1,
                    save_image=True
                )
                restartGame()
                counter = 0
            if counter>=4:
                Press([1,1])
                Sleep(0.25)
                Press([1,1])
                Sleep(0.25)
                Press([1,1])

            Sleep(1)
            counter += 1
        return None, None, screen
    def GameFrozenCheck(queue, scn,  tick= 10, threshold = 0.15):
        LENGTH = tick
        if scn is None:
            raise ValueError(_("GameFrozenCheck被传入了一个空值."))

        while len(queue) >= LENGTH:
            queue.pop(0)
        queue.append(scn)

        if not hasattr(GameFrozenCheck, "call_counter"):
            GameFrozenCheck.call_counter = 0
        GameFrozenCheck.call_counter += 1

        logger.debug(_("卡死检测截图 counter={a} length={b}".format(a=GameFrozenCheck.call_counter, b=len(queue))))

        if (GameFrozenCheck.call_counter % tick == 0) and (len(queue)==LENGTH):
            totalDiff = 0
            t = time.time()
            for i in range(1,LENGTH):
                grayThis = cv2.cvtColor(queue[i], cv2.COLOR_BGR2GRAY)
                grayLast = cv2.cvtColor(queue[i-1], cv2.COLOR_BGR2GRAY)
                mean_diff = cv2.absdiff(grayThis, grayLast).mean()/255
                totalDiff += mean_diff
            logger.info(f"卡死检测耗时: {time.time()-t:.5f}秒")
            logger.info(f"卡死检测结果: {totalDiff:.5f}")
            if totalDiff<=threshold:
                return queue, True
        return queue, False
    def TryReadPauseTextByOcr(roi):
        # OCR 作为可选增强能力：本地未安装 pytesseract 时直接跳过，不把 OCR 引入强依赖。
        try:
            import pytesseract
            from PIL import Image
        except Exception:
            return None
        try:
            gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
            gray = cv2.resize(gray, None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
            _, binary = cv2.threshold(gray, 110, 255, cv2.THRESH_BINARY)
            text = pytesseract.image_to_string(
                Image.fromarray(binary),
                config="--psm 7 -c tessedit_char_whitelist=Pausepause"
            )
            return re.sub(r"[^A-Za-z]+", "", text).lower()
        except Exception as e:
            logger.debug(_("Pause OCR识别异常: {a}").format(a=e))
            return None
    def CheckPauseTextLayout(roi):
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        mask = cv2.inRange(gray, 120, 255)
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((2, 2), np.uint8), iterations=1)
        component_count, labels, stats, centroids = cv2.connectedComponentsWithStats(mask, 8)
        components = []
        for index in range(1, component_count):
            x, y, w, h, area = stats[index]
            if 6 <= area <= 700 and 2 <= w <= 80 and 8 <= h <= 70:
                components.append((x, y, w, h, area))
        if not components:
            return False, 0, 0, 0
        left = min(item[0] for item in components)
        top = min(item[1] for item in components)
        right = max(item[0] + item[2] for item in components)
        bottom = max(item[1] + item[3] for item in components)
        text_width = right - left
        text_height = bottom - top
        # Pause 通常是中心附近一行短英文；角色面板等误判场景会出现更多组件或更高的文字块。
        layout_ok = (
            3 <= len(components) <= 8
            and 55 <= text_width <= 190
            and 18 <= text_height <= 70
            and 5 <= top <= 80
        )
        return layout_ok, len(components), text_width, text_height
    def GetPauseNegativeEvidence(screen):
        # Pause 没有角色面板和技能详情按钮；这些反证命中时宁可不点，避免把别的界面误当暂停层。
        probes = [
            ("trait", "角色界面Trait"),
            ("recover", "恢复按钮"),
            ("spellskill/skillDetail", "技能详情"),
            ("close", "底部Close", [[250, 1420, 420, 150]]),
        ]
        for score, label, pattern in CollectMatchDiagnostics(screen, probes):
            if score >= 0.8:
                return label, score
        return None
    def CheckPauseOverlay(screen):
        # 游戏应用重启后可能默认停在 Pause 画面。先用中心暗底做候选，再用 OCR/文字布局二次确认，
        # 避免角色面板这类“暗底+白字”界面被当成 Pause。
        if screen is None:
            return False
        x, y, w, h = 330, 740, 240, 110
        roi = screen[y:y + h, x:x + w]
        if roi.size == 0:
            return False
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        dark_ratio = np.count_nonzero(gray < 70) / gray.size
        white_ratio = np.count_nonzero(gray > 120) / gray.size
        max_brightness = int(gray.max())
        candidate = (dark_ratio > 0.65) and (0.015 < white_ratio < 0.09) and (max_brightness > 135)
        if not candidate:
            return False
        negative = GetPauseNegativeEvidence(screen)
        if negative:
            label, score = negative
            logger.info(_("Pause候选被反证排除: evidence={a}, match={b:.2f}%, dark={c:.2f}, white={d:.2f}.").format(
                a=label, b=score * 100, c=dark_ratio, d=white_ratio
            ))
            SaveDebugImageThrottled(screen, "pause_candidate_rejected", min_interval=120)
            return False
        ocr_text = TryReadPauseTextByOcr(roi)
        if ocr_text is not None:
            result = "pause" in ocr_text
            method = "ocr"
            detail = _("text={a}").format(a=ocr_text)
        else:
            result, components, text_width, text_height = CheckPauseTextLayout(roi)
            method = "layout"
            detail = _("components={a}, text_size={b}x{c}").format(
                a=components, b=text_width, c=text_height
            )
        if result:
            logger.info(_("检测到Pause暂停覆盖层: dark={a:.2f}, white={b:.2f}, max={c}, method={d}, {e}.").format(
                a=dark_ratio, b=white_ratio, c=max_brightness, d=method, e=detail
            ))
            SaveDebugImageThrottled(screen, "pause_confirmed", min_interval=120)
        else:
            logger.debug(_("Pause候选被二次确认排除: dark={a:.2f}, white={b:.2f}, max={c}, method={d}, {e}.").format(
                a=dark_ratio, b=white_ratio, c=max_brightness, d=method, e=detail
            ))
        return result
    def TryResumePauseOverlay(screen):
        if not CheckPauseOverlay(screen):
            return False
        logger.info(_("检测到游戏处于Pause状态, 点击画面中部恢复游戏."))
        Press([450, 760])
        Sleep(2)
        return True
    def StateCombatCheck(screen):
        combatActiveFlag = [
            "combatActive",
            "combatActive_2",
            "combatActive_3",
            "combatActive_4",
            ]
        for combat in combatActiveFlag:
            if pos:=CheckIf(screen,combat, [[0,0,150,80]]):
                return pos
        return None
    def StateInn():
        FindCoordsOrElseExecuteFallbackAndWait("Economy", ["Inn","Stay",[1,1]],2)
        if setting.ACTIVE_ROYALSUITE_REST and CheckIf(ScreenShot(),"royalsuite"):
            FindCoordsOrElseExecuteFallbackAndWait("OK",["Inn","Stay","royalsuite",[1,1]],2)
        else:
            FindCoordsOrElseExecuteFallbackAndWait("OK",["Inn","Stay","Economy",[1,1]],2)
            
        FindCoordsOrElseExecuteFallbackAndWait("Stay",["OK",[299,1464]],2)
        PressReturn()
    def StateEoT():
        runtimeContext._RESUMEAVAILABLE = False
        if quest._preEOTcheck:
            Press(CheckIf(ScreenShot(),quest._preEOTcheck))

        def EoTStep(info):
            if info[1]=="intoWorldMap":
                TeleportFromCityToWorldLocation(*info[2])
            elif info[1]=="EVENT":
                FindCoordsOrElseExecuteFallbackAndWait(["openworldmap","dungFlag"], [[1,1],"EVENT",info[2]],2)
            else:
                pos = FindCoordsOrElseExecuteFallbackAndWait(info[1], info[2], info[3])
                if info[0]=="press":
                    Press(pos)
        for info in quest._EOT[:-1]:
            RestartableSequenceExecution(lambda i=info: EoTStep(i))

        last = quest._EOT[-1]
        if last[1] == "intoWorldMap":
            TeleportFromCityToWorldLocation(*last[2])
        else:
            RestartableSequenceExecution(
                lambda: FindCoordsOrElseExecuteFallbackAndWait(
                    ["dungFlag", "GotoDung", last[1]], [last[2], [1, 1]], 1
                )
            )
        Press(CheckIf(ScreenShot(), quest._EOT[-1][1]))
        Sleep(1)
        Press(CheckIf(ScreenShot(), "GotoDung"))
        return
    def StateCombat():
        if runtimeContext._TIME_COMBAT==0:
            runtimeContext._TIME_COMBAT = time.time()
        # 内部函数：复制策略到 runtime.CURRENT_STRATEGY
        def AutoThisChar():
            Press([850,1100])
            Sleep(0.5)
            Press([850,1100])
            Sleep(2)
            return
        def AutoThisCharAfterTargetFailure(reason):
            # 详情弹窗会盖住自动战斗按钮，先退出详情再降级自动战斗。
            logger.warning(_("技能目标选择失败, 改用自动战斗: {a}").format(a=reason))
            for underscore in range(3):
                scn = ScreenShot()
                if not CheckSkillPopupOpen(scn):
                    break
                close_pos, _ = CheckTemplateInRoi(scn, "close", [[250, 1420, 420, 150]], threshold=0.8)
                if close_pos:
                    Press(close_pos)
                else:
                    PressReturn()
                Sleep(0.3)
            AutoThisChar()
            return True
        def ActiveAutoCombat():
            scn = ScreenShot()
            auto_roi = [[780, 1030, 120, 160]]
            disable_pos, disable_match = CheckTemplateInRoi(scn, "spellskill/CombatAutoDisable", auto_roi, threshold=0.8)
            enable_pos, enable_match = CheckTemplateInRoi(scn, "spellskill/CombatAutoEnable", auto_roi, threshold=0.8)
            if disable_pos:
                runtimeContext._AUTO_COMBAT_UNKNOWN_COUNT = 0
                logger.info(_("检测到自动战斗未开启, 点击Auto. 匹配程度={a:.2f}%.").format(a=disable_match * 100))
                Press([850,1100])
            elif enable_pos:
                runtimeContext._AUTO_COMBAT_UNKNOWN_COUNT = 0
                logger.debug(_("自动战斗已开启. 匹配程度={a:.2f}%.").format(a=enable_match * 100))
            else:
                runtimeContext._AUTO_COMBAT_UNKNOWN_COUNT = getattr(runtimeContext, "_AUTO_COMBAT_UNKNOWN_COUNT", 0) + 1
                logger.warning(_("无法确认自动战斗开关状态: disabled={a:.2f}%, enabled={b:.2f}%, count={c}/3.").format(
                    a=disable_match * 100, b=enable_match * 100, c=runtimeContext._AUTO_COMBAT_UNKNOWN_COUNT
                ))
                if runtimeContext._AUTO_COMBAT_UNKNOWN_COUNT >= 3:
                    logger.warning(_("连续无法确认自动战斗状态, 执行保底Auto点击, 避免长时间卡在全自动战斗分支."))
                    SaveDebugImage(scn, "auto_combat_unknown")
                    runtimeContext._AUTO_COMBAT_UNKNOWN_COUNT = 0
                    AutoThisChar()
                    return
            Sleep(5)
            return
        def ClampPoint(pos):
            return [int(max(1, min(898, pos[0]))), int(max(1, min(1598, pos[1])))]
        def ClampCombatTargetPoint(pos):
            return [int(max(1, min(898, pos[0]))), int(max(260, min(900, pos[1])))]
        def PressCombatTargetArea(marker_pos, reason="unknown"):
            # 敌人头顶标记与实际可点击身体区域之间通常有 50~100 像素距离。
            # 这里扫标记下方横向矩形区域，兼容右侧边缘敌人、大体型敌人和前后排高度差。
            offsets = [
                [-80, 80],
                [0, 80],
                [80, 80],
                [-120, 140],
                [-40, 140],
                [40, 140],
                [120, 140],
                [-110, 210],
                [-35, 210],
                [35, 210],
                [110, 210],
                [-70, 275],
                [0, 275],
                [70, 275],
            ]
            points = [
                ClampCombatTargetPoint([marker_pos[0] + offset[0], marker_pos[1] + offset[1]])
                for offset in offsets
            ]
            logger.info(_("战斗目标点击诊断: 来源={a}, 标记坐标={b}, 点击点={c}.").format(
                a=reason, b=marker_pos, c=points
            ))
            PressBatch(points)
        def PressEnemyTargetFallback():
            points = [
                [x, y]
                for y in [430, 560, 700, 830]
                for x in [140, 300, 460, 620, 780, 860]
            ]
            logger.info(_("战斗目标兜底点击诊断: 未识别到NEXT或目标标记, 点击敌方候选区域={a}.").format(a=points))
            PressBatch(points)
        def CheckCombatTargetByTemplate(scn, target, threshold=0.86):
            pos, max_val = CheckTemplateInRoi(scn, target, DEFAULT_CHECK_ROI.get(target), threshold=threshold)
            logger.info(_("战斗目标识别诊断: 模板={a}, ROI={b}, 匹配方式=普通相关, 阈值={c:.2f}%, 匹配程度={d:.2f}%, 坐标={e}.").format(
                a=target, b=DEFAULT_CHECK_ROI.get(target), c=threshold * 100, d=max_val * 100, e=pos
            ))
            return pos
        def CheckCombatNextTarget(scn):
            # 亮色遮罩会忽略背景，洞穴纹理或特效偶尔能拿到很高分，导致锚点跑到空地。
            # 这里用完整 NEXT 小图做普通相关匹配；若 NEXT 背景变化导致分数不足，再交给倒三角兜底。
            return CheckCombatTargetByTemplate(scn, "next", threshold=0.86)
        def CheckCombatNextTargetLowConfidence(scn):
            # 远距离怪物会让 NEXT 渲染得更小更淡，强阈值失败时允许低阈值兜底。
            # 低阈值只在单体技能目标选择阶段使用，仍限制在敌方活动 ROI 内。
            return CheckCombatTargetByTemplate(scn, "next", threshold=0.60)
        def CheckCombatTargetMarker(scn):
            # 当 NEXT 字样不可用时，使用敌人头顶的倒三角作为兜底锚点。
            # 倒三角背景比 NEXT 更干净，但仍限制在敌方活动区内，避开行动条和技能UI。
            return CheckCombatTargetByTemplate(scn, "combatTarget", threshold=0.86)
        def CheckRolePortraitMatch(screenImage, shortPathOfTarget, active_pos=None):
            template = LoadTemplateImage(shortPathOfTarget)
            template_h, template_w = template.shape[:2]
            base_roi_list = [
                [_("旧版头像位"), [87, 55, template_w, template_h]],
                [_("新版头像位"), [24, 55, template_w, template_h]],
                [_("新版头像位偏下"), [24, 63, template_w, template_h]],
                [_("新版头像位偏右"), [32, 55, template_w, template_h]],
            ]
            if active_pos is not None:
                base_roi_list.append([
                    _("Active动态头像位"),
                    [int(active_pos[0] - template_w * 0.35), int(active_pos[1] + 35), template_w, template_h],
                ])

            crop_list = [
                [_("整图"), [0, 0, template_w, template_h]],
                [_("右半"), [template_w * 40 // 100, 0, template_w - template_w * 40 // 100, template_h]],
                [_("右上"), [template_w * 33 // 100, 0, template_w - template_w * 33 // 100, template_h * 80 // 100]],
                [_("上半"), [0, 0, template_w, template_h * 70 // 100]],
            ]

            highest_match_rate = 0
            highest_match_desc = ""
            checked_roi = set()
            for base_name, base_roi in base_roi_list:
                base_key = tuple(base_roi)
                if base_key in checked_roi:
                    continue
                checked_roi.add(base_key)
                for crop_name, crop_roi in crop_list:
                    crop_x, crop_y, crop_w, crop_h = crop_roi
                    if crop_w <= 0 or crop_h <= 0:
                        continue
                    template_crop = template[crop_y:crop_y + crop_h, crop_x:crop_x + crop_w]
                    screen_roi = [base_roi[0] + crop_x, base_roi[1] + crop_y, crop_w, crop_h]
                    match_pos, match_rate = _check(screenImage, template_crop, [screen_roi])
                    logger.debug(_("角色匹配检测: {a}/{b}/{c} 匹配程度为{d:.2f}%.".format(
                        a=shortPathOfTarget, b=base_name, c=crop_name, d=match_rate * 100
                    )))
                    if match_rate > highest_match_rate:
                        highest_match_rate = match_rate
                        highest_match_desc = _("{a}/{b}".format(a=base_name, b=crop_name))
            return highest_match_rate, highest_match_desc
        def SkillLvlSelectAndDoubleCheck(skillPos,skilllvl, supportTarget):
            skillPosDict = { _("左上技能"):[266,965],_("右上技能"):[640,965],_("左下技能"):[266,1054],_("右下技能"):[640,1054]}
            supportTargetDict = {"左上角色": [200,1200], "中上角色": [450,1200], "右上角色": [700,1200], "左下角色":[200,1400], "中下角色":[450,1400], "右下角色":[700,1400]}
            
            # 打开详情界面
            logger.info(_("技能释放诊断: 准备释放技能={a}, 坐标={b}, 目标等级={c}, 辅助目标={d}.").format(
                a=skillPos, b=skillPosDict[skillPos], c=skilllvl, d=supportTarget
            ))
            into_detail = False
            for underscore in range(3):
                Press(skillPosDict[skillPos])
                Sleep(1)
                detail_scn = ScreenShot()
                if CheckIf(detail_scn,"spellskill/skillDetail"):
                    logger.info(_("技能释放诊断: 第{a}次点击后检测到技能详情界面.").format(a=underscore + 1))
                    into_detail = True
                    break
                logger.debug(_("技能释放诊断: 第{a}次点击后未检测到技能详情界面.").format(a=underscore + 1))
            if not into_detail:
                logger.info(_("没有检测到任务详情界面. 疑似法力不足, 使用自动战斗."))
                SaveDebugImage(detail_scn, "skill_detail_not_found")
                for underscore in range(3):
                    PressReturn()
                    Sleep(0.2)

                AutoThisChar()
                return True

            # 设置等级
            Sleep(1)
            scn = ScreenShot()
            has_lv_1 = (CheckIf(scn,f"spellskill/skillLvl/lv1")) or (CheckIf(scn,f"spellskill/skillLvl/s_lv1"))
            logger.info(_("技能等级诊断: 是否检测到1级按钮={a}, 目标等级={b}.").format(a=bool(has_lv_1), b=skilllvl))
            if (not has_lv_1):
                if (skilllvl>=2):
                    logger.error(_("错误: 设定了高于1级的技能, 但并未检测到技能等级.\n 使用默认技能."))
                    SaveDebugImage(scn, "skill_level_missing")
            else:
                if skilllvl!=1:
                    has_lv_x = (CheckIf(scn,f"spellskill/skillLvl/lv{skilllvl}")) or (CheckIf(scn,f"spellskill/skillLvl/s_lv{skilllvl}"))
                else:
                    has_lv_x = has_lv_1
                logger.info(_("技能等级诊断: 目标等级按钮检测结果={a}, 目标等级={b}.").format(a=bool(has_lv_x), b=skilllvl))

                if not has_lv_x:
                    skilllvl = 1
                    logger.error(_("错误: 未检测到目标等级\n 使用1级技能."))
                    SaveDebugImage(scn, "skill_target_level_missing")
                if not Press(CheckIf(scn,f"spellskill/skillLvl/lv{skilllvl}")):
                    if not Press(CheckIf(scn,f"spellskill/skillLvl/s_lv{skilllvl}")):
                        logger.error(_("错误: 我认为不可能发生这种情况. 请务必告诉我."))
                        SaveDebugImage(scn, "skill_level_press_failed")

            # 辅助技能
            if CheckIf(ScreenShot(),"supportSkillCheck",[[677,1475,189,80]]):
                if supportTarget in supportTargetDict.keys():
                    Press(supportTargetDict[supportTarget])
                    logger.info(_("释放了位于\"{a}\"的辅助技能, 技能等级为{b}, 释放对象为{c}".format(a=skillPos, b=skilllvl, c=supportTarget)))

            # 确认
            scn = ScreenShot()
            ok_pos = CheckIf(scn,"OK")
            logger.info(_("技能确认诊断: OK坐标={a}.").format(a=ok_pos))
            if Press(ok_pos):
                logger.info(_("释放了位于\"{a}\"的全体技能, 技能等级为{b}.".format(a=skillPos, b=skilllvl)))
                Sleep(2)
            else:
                if pos:= CheckCombatNextTarget(scn):
                    logger.info(_("释放了位于\"{a}\"的单体技能, 技能等级为{b}. 选择next作为敌方目标.".format(a=skillPos, b=skilllvl)))
                    PressCombatTargetArea(pos, "next")
                elif pos:= CheckCombatTargetMarker(scn):
                    logger.info(_("释放了位于\"{a}\"的单体技能, 技能等级为{b}. 未检测到next, 选择目标标记作为敌方目标.".format(a=skillPos, b=skilllvl)))
                    PressCombatTargetArea(pos, "combatTarget")
                elif pos:= CheckCombatNextTargetLowConfidence(scn):
                    logger.warning(_("释放了位于\"{a}\"的单体技能, 技能等级为{b}. NEXT强识别失败, 使用60%低阈值兜底选择敌方目标.".format(a=skillPos, b=skilllvl)))
                    PressCombatTargetArea(pos, "next_low_confidence")
                else:
                    logger.warning(_("释放了位于\"{a}\"的单体技能, 技能等级为{b}. 未检测到next和目标标记, 改用自动战斗避免误判卡死.".format(a=skillPos, b=skilllvl)))
                    SaveDebugImage(scn, "combat_target_before_fallback")
                    return AutoThisCharAfterTargetFailure(_("未检测到NEXT或目标标记"))
                Sleep(2)

            # 资源不足
            Sleep(1)
            scn = ScreenShot()
            if CheckSkillPopupOpen(scn):
                logger.warning(_("技能目标点击后仍检测到技能详情弹窗: 技能={a}, 技能等级={b}.").format(
                    a=skillPos, b=skilllvl
                ))
                SaveDebugImage(scn, "combat_target_after_press_still_detail")
                return AutoThisCharAfterTargetFailure(_("点击目标后技能详情弹窗仍未关闭"))
            if CheckIf(scn,"notenoughsp") or CheckIf(scn,"notenoughmp"):
                for underscore in range(3):
                    PressReturn()
                    Sleep(0.2)

                return SkillLvlSelectAndDoubleCheck(skillPos,1,supportTarget)
            return True

        ###################################################################################
        # 主逻辑开始
        # 0. 开启二倍速
        screen = ScreenShot()
        if Press(CheckIf(screen,"combatSpd")) or Press(CheckIf(screen,"combatSpd_DHI")):
            runtimeContext._COMBATSPD = True
            Sleep(1)
  
        # 1. 获取当前策略中的技能设置列表
        skill_settings = runtimeContext.CURRENT_STRATEGY.get("skill_settings", [])
        aac = False
        if runtimeContext.CURRENT_STRATEGY == {}:
            logger.error(_("错误: 当前战斗策略内容为空. 使用全自动战斗."))
            aac = True
        elif runtimeContext.CURRENT_STRATEGY.get("group_name","") ==_("全自动战斗"):
            logger.info(_("当前战斗为\"全自动战斗\", 因此使用全自动战斗."))
            aac = True
        elif skill_settings == []:
            logger.info(_("当前战斗技能列表内容为空, 因此使用全自动战斗."))
            aac = True
        
        if aac:
            ActiveAutoCombat()
            return

        # 2. 非全自动模式：点击任意键直到出现“flee”图片
        [pos_x, pos_y] = FindCoordsOrElseExecuteFallbackAndWait(["flee","chestFlag","dungFlag", "someonedead","multipeopledead","RiseAgain"],[1,1],1)
        if (pos_x>=735)and(pos_x<=735+126)and(pos_y>=1158)and(pos_y<=1158+68):
            pass
        else:
            logger.debug(_("战斗已结束."))
            return

        # 3. 进行匹配
        highest_match_rate = 0
        target_skill = None
        scn = ScreenShot()
        active_pos = StateCombatCheck(scn)
        t = time.time()
        for skill in skill_settings:
            role_var = skill.get("role_var")
            if not role_var:  # 如果 role_var 为空则跳过
                continue
            for candidate in [role_var, role_var + "_sp", role_var + "_alt"]:
                # 构造图片完整路径并检查是否存在
                img_path = os.path.join(IMAGE_FOLDER, "spellskill", "char", f"{candidate}.png")
                full_path = ResourcePath(img_path)
                if os.path.exists(full_path):
                    match_rate, match_desc = CheckRolePortraitMatch(scn, f"spellskill/char/{candidate}", active_pos)
                    if match_rate > highest_match_rate:
                        highest_match_rate = match_rate
                        target_skill = skill
                        logger.debug(_("最佳 {a}, {b}, {c}".format(a=candidate, b=highest_match_rate, c=match_desc)))
        logger.debug(f"匹配时间 {time.time() - t}")

        # 4. 判断匹配率是否达标
        if highest_match_rate < 0.80:
            logger.info(_("并未设定该角色的行为, 使用自动战斗."))
            AutoThisChar()
            return

        # 5. 按照技能等级释放技能
        if target_skill.get("skill_var") == _("防御"):
            Press([513,1200])
            Sleep(0.1)
            Press([513,1200])
            Sleep(0.1)
        else:
            skill_released = SkillLvlSelectAndDoubleCheck(target_skill.get("skill_var"), target_skill.get("skill_lvl"), target_skill.get("target_var"))
            if not skill_released:
                logger.warning(_("技能目标选择未完成, 保留当前策略条目以便下次重试."))
                return

        # 6. 释放技能后删除条目
        if target_skill in runtimeContext.CURRENT_STRATEGY.get("skill_settings", []):
            runtimeContext.CURRENT_STRATEGY["skill_settings"].remove(target_skill)
            logger.debug(_("技能已释放，已从当前策略队列中移除。"))

        return
    def StateMap_FindSwipeClick(targetInfo : TargetInfo):
        ### return = None: 视为没找到, 大约等于目标点结束.
        ### return = [x,y]: 视为找到, [x,y]是坐标.
        target = targetInfo.target
        roi = targetInfo.roi
        for i in range(len(targetInfo.swipeDir)):
            scn = ScreenShot()
            if not CheckIf(scn,"mapFlag"):
                raise KeyError(_("地图不可用."))

            swipeDir = targetInfo.swipeDir[i]
            if swipeDir!=None:
                logger.debug(f"拖动地图:{swipeDir[0]} {swipeDir[1]} {swipeDir[2]} {swipeDir[3]}")
                DeviceShell(f"input swipe {swipeDir[0]} {swipeDir[1]} {swipeDir[2]} {swipeDir[3]}")
                Sleep(2)
                scn = ScreenShot()
            
            targetPos = None
            if target == "position":
                logger.info(_("当前目标: 地点{a}".format(a=roi)))
                targetPos = CheckIf_ReachPosition(scn,targetInfo)
            elif target in ['harken','Bharken']:
                targetPos = CheckIf_harkenStair(scn,targetInfo)
            elif target.startswith("stair"):
                logger.info(_("当前目标: 楼梯{a}".format(a=target)))
                targetPos = CheckIf_throughStair(scn,targetInfo)
            else:
                logger.info(_("搜索{a}...".format(a=target)))
                if targetPos:=CheckIf(scn,target,roi):
                    logger.info(_("找到了 {a}! {b}".format(a=target, b=targetPos)))
                    if (target == "chest") and (swipeDir!= None):
                        logger.debug(_("宝箱热力图: 地图:{a} 方向:{b} 位置:{c}".format(a=setting.FARM_TARGET, b=swipeDir, c=targetPos)))
                    Sleep(1)
                    break
            if targetPos!=None:
                return targetPos
        return targetPos
    def StateMoving_CheckStop():
        runtimeContext._RESUMEAVAILABLE = True
        lastscreen = None
        dungState = None
        logger.info(_("面具男, 移动."))
        while 1:
            Sleep(3)
            underscore, dungState,screen = IdentifyState()
            if dungState == DungeonState.Map:
                logger.info(_("开始移动失败. 不要停下来啊面具男!"))
                FindCoordsOrElseExecuteFallbackAndWait("dungFlag",[[280,1433],[1,1]],1)
                dungState = dungState.Dungeon
                break
            if dungState != DungeonState.Dungeon:
                logger.info(_("已退出移动状态. 当前状态: {a}.".format(a=dungState)))
                break
            if lastscreen is not None:
                gray1 = cv2.cvtColor(CutRoI(screen,[[650,25,225,225]]), cv2.COLOR_BGR2GRAY)
                gray2 = cv2.cvtColor(CutRoI(lastscreen,[[650,25,225,225]]), cv2.COLOR_BGR2GRAY)
                mean_diff = cv2.absdiff(gray1, gray2).mean()/255
                logger.debug(f"移动停止检查:{mean_diff:.2f}")
                if mean_diff < 0.1:
                    dungState = None
                    logger.info(_("已退出移动状态. 进行状态检查..."))
                    break
            lastscreen = screen
        return dungState
    def StateSearch(waitTimer, targetInfo):
        normalPlace = ["harken","chest","leaveDung","position","Bharken"]
        target = targetInfo.target
        # 地图已经打开.
        map = ScreenShot()

        if CheckIf(map,"tooPoorToReadTheMap"):
            logger.info(_("在暴风雪中."))
            Press(CheckIf(map,"dungFlag"))
            return StateMoving_CheckStop(),False
    
        if not CheckIf(map,"mapFlag"):
            logger.info(_("没有检测到地图."))
            return None,False # 发生了其他错误

        try:
            searchResult = StateMap_FindSwipeClick(targetInfo)
        except KeyError as e:
            logger.info(_("错误: {a}".format(a=e))) # 一般来说这里只会返回"地图不可用
            return None, False
    
        if not CheckIf(map,"mapFlag"):
                return None, False # 发生了错误, 应该是进战斗了

        if searchResult == None:
            if target == "chest":
                logger.info(_("没有找到宝箱.\n停止检索宝箱."))
                return DungeonState.Map,  True
            elif (target == "position" or target.startswith("stair")):
                logger.info(_("已经抵达目标地点或目标楼层."))
                return DungeonState.Map,  True
            else:
                # 这种时候我们认为真正失败了. 所以不弹出.
                # 当然, 更好的做法时传递finish标识()
                logger.info(_("未找到目标{a}.".format(a=target)))
                return DungeonState.Map,  False
        else:
            if target in normalPlace or target.endswith("_quit") or target.startswith("stair"):
                Press(searchResult)
                Press([136,1431]) # automove
                return StateMoving_CheckStop(),False
            else:
                if (CheckIf_FocusCursor(ScreenShot(),target)): #注意 这里通过二次确认 我们可以看到目标地点 而且是未选中的状态
                    logger.info(_("经过对比中心区域, 确认没有抵达."))
                    Press(searchResult)
                    Press([136,1431]) # automove
                    return StateMoving_CheckStop(), False
                else:
                    # if setting._DUNGWAITTIMEOUT == 0:
                        logger.info(_("经过对比中心区域, 判断为抵达目标地点."))
                        logger.info(_("无需等待, 当前目标已完成."))
                        return DungeonState.Map, True
                    # else:
                    #     logger.info(_("经过对比中心区域, 判断为抵达目标地点."))
                    #     logger.info(_("开始等待...等待..."))
                    #     PressReturn()
                    #     Sleep(0.5)
                    #     PressReturn()
                    #     while 1:
                    #         if setting._DUNGWAITTIMEOUT-time.time()+waitTimer<0:
                    #             logger.info(_("等得够久了. 目标地点完成."))
                    #             Sleep(1)
                    #             Press([777,150])
                    #             return None, True
                    #         logger.info(_("还需要等待{a}秒.".foramt(a=setting._DUNGWAITTIMEOUT-time.time()+waitTimer)))
                    #         if StateCombatCheck(ScreenShot()):
                    #             return DungeonState.Combat, False
        return DungeonState.Map, False
    def StateChest():
        nonlocal runtimeContext
        availableChar = [0, 1, 2, 3, 4, 5]
        disarm = [515,934]  # 527,920会按到接受死亡 450 1000会按到技能 445,1050还是会按到技能
        haveBeenTried = False

        if runtimeContext._TIME_CHEST==0:
            runtimeContext._TIME_CHEST = time.time()
        
        if setting.QUICK_DISARM_CHEST:
            if Press(CheckIf(ScreenShot(),"chestFlag")):
                Sleep(1)
                whowillopenit = setting.WHO_WILL_OPEN_IT - 1
                pos = [258+(whowillopenit%3)*258, 1161+((whowillopenit)//3)%2*184]
                Press(pos)
                Sleep(0.2)
                Press(pos)
                Sleep(0.2)
                Press(pos)
                Sleep(1)
                for underscore in range(30):
                    Press(disarm)
                    Sleep(0.2)
                for underscore in range(3):
                    Press([1,1])
                    Press(disarm)

        while 1:
            FindCoordsOrElseExecuteFallbackAndWait(
                ["dungFlag","combatActive","chestOpening","whowillopenit","RiseAgain", "ambush"],
                [[1,1],[1,1],"chestFlag"],
                1)
            scn = ScreenShot()

            if CheckIf(scn,"whowillopenit"):
                while 1:
                    pointSomeone = setting.WHO_WILL_OPEN_IT - 1
                    if (pointSomeone != -1) and (pointSomeone in availableChar) and (not haveBeenTried):
                        whowillopenit = pointSomeone # 如果指定了一个角色并且该角色可用并且没尝试过, 使用它
                    else:
                        whowillopenit = random.choice(availableChar) # 否则从列表里随机选一个
                    pos = [258+(whowillopenit%3)*258, 1161+((whowillopenit)//3)%2*184]
                    # logger.info(f"{availableChar},{pos}")
                    if CheckIf(scn,"chestfear",[[pos[0]-125,pos[1]-82,250,164]]):
                        if whowillopenit in availableChar:
                            availableChar.remove(whowillopenit) # 如果发现了恐惧, 删除这个角色.
                    else:
                        Press(pos)
                        Sleep(1.5)
                        # if not setting._SMARTDISARMCHEST:
                        for underscore in range(8):
                            t = time.time()
                            Press(disarm)
                            if time.time()-t<0.3:
                                Sleep(0.3-(time.time()-t))
                                
                        break
                if not haveBeenTried:
                    haveBeenTried = True

            if CheckIf(scn,"chestOpening"):
                Sleep(1)
                # if setting._SMARTDISARMCHEST:
                #     ChestOpen()
                FindCoordsOrElseExecuteFallbackAndWait(
                    ["dungFlag","combatActive","chestFlag","RiseAgain"], # 如果这个fallback重启了, 战斗箱子会直接消失, 固有箱子会是chestFlag
                    [disarm,disarm,disarm,disarm,disarm,disarm,disarm,disarm],
                    1)
            
            if CheckIf(scn,"RiseAgain"):
                RiseAgainReset(reason = "chest")
                return None
            
            # 在图像识别的时候保持截图是最新的
            scn = ScreenShot()
            if CheckIf(scn,"dungFlag"):
                return DungeonState.Dungeon
            if CheckIf(scn, "ambush"):
                logger.info("开箱子然后遇到怪物还是善恶, 你这什么运气啊.")
                return None
            if StateCombatCheck(scn):
                return DungeonState.Combat
            
            TryPressRetry(scn)
    def StateDungeon(targetInfoList : list[TargetInfo]):
        gameFrozen_StateNoneScreenHistory = []
        gameFrozen_StateMapCounter = 0
        GAMEFROZEN_STATEMAPLIMIT = 20+20*len(targetInfoList)
        dungState = None
        shouldRecover = False
        waitTimer = time.time()
        needRecoverBecauseCombat = False
        needRecoverBecauseChest = False

        nonlocal runtimeContext
        runtimeContext.TASK_STEP_INDEX = 0
        def TargetPointComplete():
            logger.info(f"任务点完成: {targetInfoList[0].target} {targetInfoList[0].roi}")
            targetInfoList.pop(0)
            runtimeContext.TASK_STEP_INDEX += 1
            return
        
        runtimeContext.NEED_RECOVER_WHEN_BEGINNING = True

        if setting.RELOAD_STRATEGY_WHEN == _("每次副本开始"):
            ReloadStrategy()
        
        ##############################################
        while 1:
            logger.info("----------------------")
            if setting._FORCESTOPING.is_set():
                logger.info(_("即将停止脚本..."))
                dungState = DungeonState.Quit
            logger.info(_("当前状态(地下城): {a}".format(a=dungState)))

            match dungState:
                case None:
                    s, dungState,scn = IdentifyState()
                    logger.info(_("state:None诊断: IdentifyState返回 state={a}, dungState={b}.").format(
                        a=s, b=dungState
                    ))
                    if (s == State.Inn) or (dungState == DungeonState.Quit):
                        logger.debug(_(f"本次地下城打开地图次数{gameFrozen_StateMapCounter}"))
                        break

                    if dungState is None:
                        gameFrozen_StateNoneScreenHistory, result = GameFrozenCheck(gameFrozen_StateNoneScreenHistory,scn)
                        if result:
                            logger.info(_("由于画面卡死, 在state:None中重启."))
                            LogUnknownScreenDiagnostics(
                                scn,
                                "state_none_frozen_restart",
                                save_image=True
                            )
                            restartGame()
                        MAXTIMEOUT = 400
                        if (runtimeContext._TIME_CHEST != 0 ) and (time.time()-runtimeContext._TIME_CHEST > MAXTIMEOUT):
                            logger.info(_("由于宝箱用时过久, 在state:None中重启."))
                            LogUnknownScreenDiagnostics(
                                scn,
                                "state_none_chest_timeout_restart",
                                save_image=True
                            )
                            restartGame()
                        if (runtimeContext._TIME_COMBAT != 0) and (time.time()-runtimeContext._TIME_COMBAT > MAXTIMEOUT):
                            logger.info(_("由于战斗用时过久, 在state:None中重启."))
                            LogUnknownScreenDiagnostics(
                                scn,
                                "state_none_combat_timeout_restart",
                                save_image=True
                            )
                            restartGame()
                    else:
                        gameFrozen_StateNoneScreenHistory.clear()
                        logger.debug(_("state:None诊断: 已识别到dungState={a}, 跳过None态卡死/超时检测.").format(a=dungState))
                case DungeonState.Quit:
                    logger.debug(_(f"本次地下城打开地图次数{gameFrozen_StateMapCounter}"))
                    break
                case DungeonState.Dungeon:
                    Press([1,1])
                    ########### 重置战斗策略
                    if (runtimeContext._TIME_COMBAT !=0) and (setting.RELOAD_STRATEGY_WHEN == _("每场战斗前")):
                        ReloadStrategy()
                    ########### TIMER
                    if (runtimeContext._TIME_CHEST !=0) or (runtimeContext._TIME_COMBAT!=0):
                        spend_on_chest = 0
                        if runtimeContext._TIME_CHEST !=0:
                            spend_on_chest = time.time()-runtimeContext._TIME_CHEST
                            runtimeContext._TIME_CHEST = 0
                        spend_on_combat = 0
                        if runtimeContext._TIME_COMBAT !=0:
                            spend_on_combat = time.time()-runtimeContext._TIME_COMBAT
                            runtimeContext._TIME_COMBAT = 0
                        logger.info(f"粗略统计: 宝箱{spend_on_chest:.2f}秒, 战斗{spend_on_combat:.2f}秒.")
                        if (spend_on_chest!=0) and (spend_on_combat!=0):
                            if spend_on_combat>spend_on_chest:
                                runtimeContext._TIME_COMBAT_TOTAL = runtimeContext._TIME_COMBAT_TOTAL + spend_on_combat-spend_on_chest
                                runtimeContext._TIME_CHEST_TOTAL = runtimeContext._TIME_CHEST_TOTAL + spend_on_chest
                            else:
                                runtimeContext._TIME_CHEST_TOTAL = runtimeContext._TIME_CHEST_TOTAL + spend_on_chest-spend_on_combat
                                runtimeContext._TIME_COMBAT_TOTAL = runtimeContext._TIME_COMBAT_TOTAL + spend_on_combat
                        else:
                            runtimeContext._TIME_COMBAT_TOTAL = runtimeContext._TIME_COMBAT_TOTAL + spend_on_combat
                            runtimeContext._TIME_CHEST_TOTAL = runtimeContext._TIME_CHEST_TOTAL + spend_on_chest
                    ########### RECOVER
                    if needRecoverBecauseChest:
                        logger.info(_("进行开启宝箱后的恢复."))
                        runtimeContext._COUNTERCHEST+=1
                        needRecoverBecauseChest = False
                        runtimeContext._MEET_CHEST_OR_COMBAT = True
                        if not setting.SKIP_CHEST_RECOVER:
                            logger.info(_("由于面板配置, 进行开启宝箱后恢复."))
                            shouldRecover = True
                        else:
                            logger.info(_("由于面板配置, 跳过了开启宝箱后恢复."))
                    if needRecoverBecauseCombat:
                        runtimeContext._COUNTERCOMBAT+=1
                        needRecoverBecauseCombat = False
                        runtimeContext._MEET_CHEST_OR_COMBAT = True
                        if (not setting.SKIP_COMBAT_RECOVER):
                            logger.info(_("由于面板配置, 进行战后恢复."))
                            shouldRecover = True
                        else:
                            logger.info(_("由于面板配置, 跳过了战后恢复."))
                    if setting.RECOVER_WHEN_BEGINNING and runtimeContext.NEED_RECOVER_WHEN_BEGINNING:
                        shouldRecover = True
                        runtimeContext.NEED_RECOVER_WHEN_BEGINNING = False
                        logger.info(_("由于面板配置, 在刚进入地下城时进行恢复."))
                    if runtimeContext._RECOVERAFTERREZ == True:
                        shouldRecover = True
                        runtimeContext._RECOVERAFTERREZ = False
                    if shouldRecover:
                        for undrscore in range(3):
                            Press([1,1])
                            Sleep(0.1)
                        counter_trychar = -1
                        while 1:
                            counter_trychar += 1
                            scn=ScreenShot()
                            if (CheckIf(scn,"dungflag") and not CheckIf(scn,"mapFlag")) and (counter_trychar <=30):
                                Press([36+(counter_trychar%3)*286,1425])
                                Sleep(2)
                                continue
                            elif CheckIf(scn,"trait"):
                                if CheckIf(scn,"story", [[676,800,220,108]]):
                                    Press([725,850])
                                else:
                                    Press([830,850])
                                Sleep(1)
                                FindCoordsOrElseExecuteFallbackAndWait(["recover","combatActive",],[833,843],1)
                                if CheckIf(ScreenShot(),"recover"):
                                    Sleep(1.5)
                                    Press([600,1200])
                                    Sleep(1)
                                    for underscore in range(5):
                                        t = time.time()
                                        PressReturn()
                                        if time.time()-t<0.3:
                                            Sleep(0.3-(time.time()-t))
                                    shouldRecover = False
                                    break
                            else:
                                logger.info(_("自动回复异常, 中止本次回复."))
                                break
                    ########### 防止卡空气墙
                    if setting.BYPASS_THE_WALL:
                        if (not runtimeContext._BYPASSAFTERRESTART) and (quest._TYPE == "dungeon"): # 加入类别判断以避免干扰任务流程
                            logger.info("防止卡空气墙, 右转后左右走.")
                            DeviceShell(f"input swipe 300 950 600 950")
                            Sleep(1)
                            Press([27,950])
                            Sleep(1)
                            Press([853,950])

                            runtimeContext._BYPASSAFTERRESTART = True
                    ########### 尝试resume
                    not_moving = False
                    if runtimeContext._RESUMEAVAILABLE and Press(CheckIf(ScreenShot(),"resume")):
                        logger.info(_("resume可用. 使用resume."))
                        lastscreen = ScreenShot()
                        for counter in range(30):
                            Sleep(3)
                            underscore, dungState,screen = IdentifyState()
                            if dungState != DungeonState.Dungeon:
                                logger.info(_("已退出移动状态. 当前状态为{a}.".format(a=dungState)))
                                not_moving = True
                            elif lastscreen is not None:
                                gray1 = cv2.cvtColor(screen, cv2.COLOR_BGR2GRAY)
                                gray2 = cv2.cvtColor(lastscreen, cv2.COLOR_BGR2GRAY)
                                mean_diff = cv2.absdiff(gray1, gray2).mean()/255
                                logger.debug(f"移动停止检查:{mean_diff:.2f}")
                                if mean_diff < 0.1:
                                    runtimeContext._RESUMEAVAILABLE = False
                                    logger.info(_("已退出移动状态. 当前状态为{a}.".format(a=dungState)))
                                    not_moving = True
                                lastscreen = screen
                            if counter == 29:
                                # 转圈可能 重启.
                                restartGame()
                            if not_moving:
                                break
                    ########### 如果resume失败且为退出:
                    if dungState == DungeonState.Quit:
                        break
                    ########### 如果resume失败且为地下城
                    if dungState == DungeonState.Dungeon:
                        dungState = DungeonState.Map
                case DungeonState.Map:
                    ########### 卡死检测
                    gameFrozen_StateMapCounter +=1
                    if gameFrozen_StateMapCounter > GAMEFROZEN_STATEMAPLIMIT:
                        logger.info(_("多次访问地图, 认为游戏卡死. 重启游戏."))
                        gameFrozen_StateMapCounter = 0
                        restartGame()
                    ########### 不打开地图, 执行自动任务
                    def startAuto():
                        if targetInfoList[0] and (targetInfoList[0].target =="stay"):
                            Sleep(2)
                            return None
                        for tar in ["chest_auto","mark_auto"]:
                            if targetInfoList[0] and (targetInfoList[0].target == tar):                        
                                lastscreen = ScreenShot()
                                if not Press(CheckIf(lastscreen,tar,[[720,250,150,180]])):
                                    Press(CheckIf(lastscreen,"mapflag"))
                                    Press([762,346]) # 认为是没有展开菜单
                                    Sleep(1)
                                    lastscreen = ScreenShot()
                                    if not Press(CheckIf(lastscreen,tar,[[720,250,150,180]])):
                                        return None # 如果我们两次检测失败, 认为发生了异常
                                
                                if tar == "chest_auto":
                                    if not CheckIf(MinusImage(lastscreen,90,90,90),"chest_auto_minus",[[811,340, 41, 30]]): # 精确匹配按钮是否可用
                                        logger.info('宝箱按钮不可用.')
                                        return DungeonState.Dungeon
                                    
                                lastscreen = ScreenShot()
                                if CheckIf(lastscreen,"NoChestCanBeFound") or CheckIf(lastscreen,"theRouteToTheDestinationCannotBeFound"):
                                    TargetPointComplete()
                                    logger.info(_("退出自动搜索."))
                                    return DungeonState.Dungeon
                                lastscreen = ScreenShot()
                                if CheckIf(lastscreen,"NoChestCanBeFound") or CheckIf(lastscreen,"theRouteToTheDestinationCannotBeFound"):
                                    TargetPointComplete()
                                    logger.info(_("退出自动搜索."))
                                    return DungeonState.Dungeon


                                Sleep(1)
                                Press(CheckIf(lastscreen,"resume")) # 立刻按一次resume 以兼容暴风雪场景.
                                return StateMoving_CheckStop()
                            
                        return DungeonState.Dungeon

                    dungState = startAuto()
                    if dungState == None:
                        # 如果状态无效, 直接进入下一轮.
                        continue
                    ########### 不是自动任务, 开始搜索

                    Sleep(1)
                    Press([777,150])
                    Sleep(1)

                    dungState, ifTargetPointComplete = StateSearch(waitTimer,targetInfoList[0])

                    if ifTargetPointComplete:
                        TargetPointComplete()

                    if (targetInfoList==None) or (targetInfoList == []):
                        logger.info(_("地下城目标完成. 地下城状态结束.(仅限任务模式.)"))
                        break
                case DungeonState.Chest:
                    needRecoverBecauseChest = True
                    dungState = StateChest()
                case DungeonState.Combat:
                    needRecoverBecauseCombat =True
                    StateCombat()
                    dungState = None
    def StateAcceptRequest(request: str, pressbias:list = [0,0]):
        FindCoordsOrElseExecuteFallbackAndWait("Inn",[1,1],1)
        StateInn()
        Press(FindCoordsOrElseExecuteFallbackAndWait("guildRequest",["guild",[1,1]],1))
        Press(FindCoordsOrElseExecuteFallbackAndWait("guildFeatured",["guildRequest",[1,1]],1))
        for underscore in range(3):
            Sleep(1)
            DeviceShell(f"input swipe 150 1000 150 200")
        Sleep(2)
        pos = FindCoordsOrElseExecuteFallbackAndWait(request,["input swipe 150 200 150 250",[1,1]],1)
        if not CheckIf(ScreenShot(),"request_accepted",[[0,pos[1]-200,900,pos[1]+200]]):
            FindCoordsOrElseExecuteFallbackAndWait(["Inn","guildRequest"],[[pos[0]+pressbias[0],pos[1]+pressbias[1]],"return",[1,1]],1)
            FindCoordsOrElseExecuteFallbackAndWait("Inn",["return",[1,1]],1)
        else:
            logger.info(_("奇怪, 任务怎么已经接了."))
            FindCoordsOrElseExecuteFallbackAndWait("Inn",["return",[1,1]],1)

    def DungeonFarm():
        nonlocal runtimeContext
        state = None
        while 1:
            logger.info("======================")
            Sleep(1)
            if setting._FORCESTOPING.is_set():
                logger.info(_("即将停止脚本..."))
                break
            logger.info(_("当前状态: {a}".format(a=state)))
            match state:
                case None:
                    def _identifyState():
                        nonlocal state
                        state=IdentifyState()[0]
                    RestartableSequenceExecution(
                        lambda: _identifyState()
                        )
                    logger.info(_("下一状态: {a}".format(a=state)))
                    if state ==State.Quit:
                        logger.info(_("即将停止脚本..."))
                        break
                case State.Inn:
                    if setting.RE_ASSEMBLE_PARTY:
                        period = int(runtimeContext._TOTALTIME // (6 * 3600))
                        if runtimeContext._LAST_BAGCLEAR != period:
                            reunionParty()
                            runtimeContext._LAST_BAGCLEAR = period
                    if not runtimeContext._MEET_CHEST_OR_COMBAT:
                        logger.info(_("因为没有遇到战斗或宝箱, 跳过住宿."))
                    elif not setting.ACTIVE_REST:
                        logger.info(_("因为面板设置, 跳过住宿."))
                    elif ((runtimeContext._COUNTERDUNG-1) % (max(setting.REST_INTERVEL,1)) != 0):
                        logger.info(_("还有许多地下城要刷. 面具男, 现在还不能休息哦."))
                    else:
                        logger.info(_("休息时间到!"))
                        RestartableSequenceExecution(
                        lambda:StateInn()
                        )
                    state = State.EoT
                case State.EoT:
                    DungeonCompletionCounter()
                    RestartableSequenceExecution(
                        lambda:StateEoT()
                        )
                    state = State.Dungeon
                case State.Dungeon:
                    # 首次进入地下城
                    targetInfoList = quest._TARGETINFOLIST.copy()
                    RestartableSequenceExecution(
                        lambda: StateDungeon(targetInfoList)
                        )
                    state = None
        setting._FINISHINGCALLBACK()
    def QuestFarm():
        nonlocal setting # 强制自动战斗 等等.
        nonlocal runtimeContext
        match setting.FARM_TARGET:
            case "7000G":
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break

                    starttime = time.time()
                    runtimeContext._COUNTERDUNG += 1

                    logger.info(_("第一步: 时间跳跃..."))
                    RestartableSequenceExecution(    
                        lambda: CursedWheelTimeLeap(target="FortressArrival",chapter = "cursedwheel_impregnableFortress")
                        )

                    Sleep(10)
                    logger.info(_("第二步: 返回要塞..."))
                    RestartableSequenceExecution(
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn",["returntotown","returnText","leaveDung","blessing",[1,1]],2)
                        )

                    logger.info(_("第三步: 前往王城..."))
                    RestartableSequenceExecution(
                        lambda:TeleportFromCityToWorldLocation("City_RoyalCityLuknalia", "input swipe 450 150 500 150"),
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("guild",["City_RoyalCityLuknalia",[1,1]],1),
                        )

                    logger.info(_("第四步: 给我!(伸手)"))
                    stepMark = -1
                    def stepMain():
                        nonlocal stepMark
                        if stepMark == -1:
                            Press(FindCoordsOrElseExecuteFallbackAndWait("guild",[1,1],1))
                            Press(FindCoordsOrElseExecuteFallbackAndWait("7000G/illgonow",[1,1],1))
                            Sleep(15)
                            FindCoordsOrElseExecuteFallbackAndWait(["7000G/olddist","7000G/iminhungry"],[1,1],2)
                            if pos:=CheckIf(scn:=ScreenShot(),"7000G/olddist"):
                                Press(pos)
                            else:
                                Press(CheckIf(scn,"7000G/iminhungry"))
                                Press(FindCoordsOrElseExecuteFallbackAndWait("7000G/olddist",[1,1],2))
                            stepMark = 0
                        if stepMark == 0:
                            Sleep(4)
                            Press([1,1])
                            Press([1,1])
                            Sleep(8)
                            Press(FindCoordsOrElseExecuteFallbackAndWait("7000G/royalcapital",[1,1],2))
                            FindCoordsOrElseExecuteFallbackAndWait("intoWorldMap",[1,1],2)
                            stepMark = 1
                        if stepMark == 1:
                            FindCoordsOrElseExecuteFallbackAndWait("fastforward",[450,1111],0)
                            FindCoordsOrElseExecuteFallbackAndWait("intoWorldMap",["7000G/why",[1,1]],2)
                            stepMark = 2
                        if stepMark == 2:
                            FindCoordsOrElseExecuteFallbackAndWait("fastforward",[200,1180],0)
                            FindCoordsOrElseExecuteFallbackAndWait("intoWorldMap",["7000G/why",[1,1]],2)
                            stepMark = 3
                        if stepMark == 3:
                            FindCoordsOrElseExecuteFallbackAndWait("fastforward",[680,1200],0)
                            Press(FindCoordsOrElseExecuteFallbackAndWait("7000G/leavethechild",["7000G/why",[1,1]],2))
                            stepMark = 4
                        if stepMark == 4:
                            Press(FindCoordsOrElseExecuteFallbackAndWait("7000G/icantagreewithU",[1,1],1))
                            stepMark = 5
                        if stepMark == 5:
                            Press(FindCoordsOrElseExecuteFallbackAndWait("7000G/illgo",[[1,1],"7000G/olddist"],1))
                            Press(FindCoordsOrElseExecuteFallbackAndWait("7000G/noeasytask",[1,1],1))
                            FindCoordsOrElseExecuteFallbackAndWait("ruins",[1,1],1)
                    RestartableSequenceExecution(
                        lambda: stepMain()
                        )
                    costtime = time.time()-starttime
                    logger.info(_("第{a}次\"7000G\"完成. 该次花费时间{b:.2f}, 每秒收益:{c:.2f}Gps.".format(a=runtimeContext._COUNTERDUNG, b=costtime, c=7000/costtime)),
                                extra={"summary": True})
            case "fordraig":
                quest._SPECIALDIALOGOPTION = ["fordraig/thedagger","fordraig/InsertTheDagger"]
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break
                    runtimeContext._COUNTERDUNG += 1
                    setting._SYSTEMAUTOCOMBAT = True
                    starttime = time.time()
                    logger.info(_("第一步: 诅咒之旅..."))
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("cursedWheel",["ruins",[1,1]],1)),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("Fordraig/Leap",["specialRequest",[1,1]],1)),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("OK","leap",1)),
                        )
                    Sleep(15)

                    RestartableSequenceExecution(
                        lambda: logger.info(_("第二步: 领取任务.")),
                        lambda: StateAcceptRequest("fordraig/RequestAccept",[350,180])
                        )

                    logger.info(_("第三步: 进入地下城."))
                    TeleportFromCityToWorldLocation("fordraig/labyrinthOfFordraig","input swipe 450 150 500 150")
                    Press(FindCoordsOrElseExecuteFallbackAndWait("fordraig/Entrance",["fordraig/labyrinthOfFordraig",[1,1]],1))
                    FindCoordsOrElseExecuteFallbackAndWait("dungFlag",["fordraig/Entrance","GotoDung",[1,1]],1)

                    logger.info(_("第四步: 陷阱."))
                    RestartableSequenceExecution(
                        lambda:StateDungeon([
                            TargetInfo("position","左上",[721,448]),
                            TargetInfo("position","左上",[720,608])]), # 前往第一个陷阱
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("dungFlag","return",1), # 关闭地图
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("fordraig/TryPushingIt",["input swipe 100 250 800 250",[400,800],[400,800],[400,800]],1)), # 转向来开启机关
                        )
                    logger.info("已完成第一个陷阱.")

                    RestartableSequenceExecution(
                        lambda:StateDungeon([
                            TargetInfo("stair_down","左上",[721,236]),
                            TargetInfo("position","左下", [240,921])]), #前往第二个陷阱
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("dungFlag","return",1), # 关闭地图
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("fordraig/TryPushingIt",["input swipe 100 250 800 250",[400,800],[400,800],[400,800]],1)), # 转向来开启机关
                        )
                    logger.info(_("已完成第二个陷阱."))

                    RestartableSequenceExecution(
                        lambda:StateDungeon([
                            TargetInfo("position","左下",[33,1238]),
                            TargetInfo("stair_down","左下",[453,1027]),
                            TargetInfo("position","左下",[187,1027]),
                            TargetInfo("stair_teleport","左下",[80,1026])
                            ]), #前往第三个陷阱
                        )
                    logger.info(_("已完成第三个陷阱."))

                    StateDungeon([TargetInfo("position","左下",[508,1025])]) # 前往boss战门前
                    setting._SYSTEMAUTOCOMBAT = False
                    StateDungeon([TargetInfo("position","左下",[720,1025])]) # 前往boss战斗
                    setting._SYSTEMAUTOCOMBAT = True
                    StateDungeon([TargetInfo("stair_teleport","左上",[665,395])]) # 第四层出口
                    FindCoordsOrElseExecuteFallbackAndWait("dungFlag","return",1)
                    Press(FindCoordsOrElseExecuteFallbackAndWait("ReturnText",["leaveDung",[455,1200]],3.75)) # 回城
                    # 3.75什么意思 正常循环是3秒 有4次尝试机会 因此3.75秒按一次刚刚好.
                    Press(FindCoordsOrElseExecuteFallbackAndWait("City_RoyalCityLuknalia",["return",[1,1]],1)) # 回城
                    FindCoordsOrElseExecuteFallbackAndWait("Inn",[1,1],1)

                    costtime = time.time()-starttime
                    logger.info(_("第{a}次\"鸟剑\"完成. 该次花费时间{b:.2f}.".format(a=runtimeContext._COUNTERDUNG, b=costtime)),
                            extra={"summary": True})
            case "repelEnemyForces":
                if not setting.ACTIVE_REST:
                    logger.info(_("注意, \"休息间隔\"控制连续战斗多少次后回城. 当前未启用休息, 强制设置为1."))
                    setting.REST_INTERVEL = 1
                if setting.REST_INTERVEL == 0:
                    logger.info(_("注意, \"休息间隔\"控制连续战斗多少次后回城. 当前值0为无效值, 最低为1."))
                    setting.REST_INTERVEL = 1
                logger.info(_("注意, 该流程不包括时间跳跃和接取任务, 请确保接取任务后再开启!"))
                counter = 0
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break
                    t = time.time()
                    RestartableSequenceExecution(
                        lambda : StateInn()
                    )
                    RestartableSequenceExecution(
                        lambda : Press(FindCoordsOrElseExecuteFallbackAndWait("TradeWaterway","EdgeOfTown",1)),
                        lambda : FindCoordsOrElseExecuteFallbackAndWait("7thDist",[1,1],1),
                        lambda : FindCoordsOrElseExecuteFallbackAndWait("dungFlag",["7thDist","GotoDung",[1,1]],1),
                    )
                    RestartableSequenceExecution(
                        lambda : StateDungeon([TargetInfo("position","左下",[559,599]),
                                               TargetInfo("position","左下",[186,813])])
                    )
                    logger.info(_("已抵达目标地点, 开始战斗."))
                    FindCoordsOrElseExecuteFallbackAndWait("dungFlag",["return",[1,1]],1)
                    for i in range(setting.REST_INTERVEL):
                        logger.info(_("第{a}轮开始.".format(a=i+1)))
                        secondcombat = False
                        while 1:
                            Press(FindCoordsOrElseExecuteFallbackAndWait(["icanstillgo","combatActive"],["input swipe 400 400 400 100",[1,1]],1))
                            Sleep(1)
                            ReloadStrategy()
                            while 1:
                                scn=ScreenShot()
                                if TryPressRetry(scn):
                                    continue
                                if CheckIf(scn,"icanstillgo"):
                                    break
                                if StateCombatCheck(scn):
                                    StateCombat()
                                else:
                                    Press([1,1])
                            if not secondcombat:
                                logger.info(_("第1场战斗结束."))
                                secondcombat = True
                                Press(CheckIf(ScreenShot(),"icanstillgo"))
                            else:
                                logger.info(_("第2场战斗结束."))
                                Press(CheckIf(ScreenShot(),"letswithdraw"))
                                Sleep(1)
                                break
                        logger.info(_("第{a}轮结束.".format(a=i+1)))
                    RestartableSequenceExecution(
                        lambda:StateDungeon([TargetInfo("position","左上",[612,448])])
                    )
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("returnText",[[1,1],"leaveDung","return"],3))
                    )
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("Inn",["return",[1,1]],1)
                    )
                    counter+=1
                    logger.info(_("第{a}x{b}轮\"击退敌势力\"完成, 共计{c}场战斗. 该次花费时间{c:.2f}秒.".format(a=counter, b=setting.REST_INTERVEL, c=counter*setting.REST_INTERVEL*2), c=(time.time()-t)),
                                    extra={"summary": True})
            case "darkLight":
                gameFrozen_StateNoneScreenHistory = []
                dungState = None
                shouldRecover = False
                needRecoverBecauseCombat = False
                needRecoverBecauseChest = False
                while 1:
                    underscore, dungState,underscore = IdentifyState()
                    logger.info(dungState)
                    match dungState:
                        case None:
                            s, dungState,scn = IdentifyState()
                            logger.info(_("state:None诊断: IdentifyState返回 state={a}, dungState={b}.").format(
                                a=s, b=dungState
                            ))
                            if (s == State.Inn) or (dungState == DungeonState.Quit):
                                break
                            if dungState is None:
                                gameFrozen_StateNoneScreenHistory, result = GameFrozenCheck(gameFrozen_StateNoneScreenHistory,scn)
                                if result:
                                    logger.info(_("由于画面卡死, 在state:None中重启."))
                                    LogUnknownScreenDiagnostics(
                                        scn,
                                        "state_none_frozen_restart",
                                        save_image=True
                                    )
                                    restartGame()
                                MAXTIMEOUT = 400
                                if (runtimeContext._TIME_CHEST != 0 ) and (time.time()-runtimeContext._TIME_CHEST > MAXTIMEOUT):
                                    logger.info(_("由于宝箱用时过久, 在state:None中重启."))
                                    LogUnknownScreenDiagnostics(
                                        scn,
                                        "state_none_chest_timeout_restart",
                                        save_image=True
                                    )
                                    restartGame()
                                if (runtimeContext._TIME_COMBAT != 0) and (time.time()-runtimeContext._TIME_COMBAT > MAXTIMEOUT):
                                    logger.info(_("由于战斗用时过久, 在state:None中重启."))
                                    LogUnknownScreenDiagnostics(
                                        scn,
                                        "state_none_combat_timeout_restart",
                                        save_image=True
                                    )
                                    restartGame()
                            else:
                                gameFrozen_StateNoneScreenHistory.clear()
                                logger.debug(_("state:None诊断: 已识别到dungState={a}, 跳过None态卡死/超时检测.").format(a=dungState))
                        case DungeonState.Dungeon:
                            Press([1,1])
                            ########### TIMER
                            if (runtimeContext._TIME_CHEST !=0) or (runtimeContext._TIME_COMBAT!=0):
                                spend_on_chest = 0
                                if runtimeContext._TIME_CHEST !=0:
                                    spend_on_chest = time.time()-runtimeContext._TIME_CHEST
                                    runtimeContext._TIME_CHEST = 0
                                spend_on_combat = 0
                                if runtimeContext._TIME_COMBAT !=0:
                                    spend_on_combat = time.time()-runtimeContext._TIME_COMBAT
                                    runtimeContext._TIME_COMBAT = 0
                                logger.info(f"粗略统计: 宝箱{spend_on_chest:.2f}秒, 战斗{spend_on_combat:.2f}秒.")
                                if (spend_on_chest!=0) and (spend_on_combat!=0):
                                    if spend_on_combat>spend_on_chest:
                                        runtimeContext._TIME_COMBAT_TOTAL = runtimeContext._TIME_COMBAT_TOTAL + spend_on_combat-spend_on_chest
                                        runtimeContext._TIME_CHEST_TOTAL = runtimeContext._TIME_CHEST_TOTAL + spend_on_chest
                                    else:
                                        runtimeContext._TIME_CHEST_TOTAL = runtimeContext._TIME_CHEST_TOTAL + spend_on_chest-spend_on_combat
                                        runtimeContext._TIME_COMBAT_TOTAL = runtimeContext._TIME_COMBAT_TOTAL + spend_on_combat
                                else:
                                    runtimeContext._TIME_COMBAT_TOTAL = runtimeContext._TIME_COMBAT_TOTAL + spend_on_combat
                                    runtimeContext._TIME_CHEST_TOTAL = runtimeContext._TIME_CHEST_TOTAL + spend_on_chest
                            ########### RECOVER
                            if needRecoverBecauseChest:
                                logger.info(_("进行开启宝箱后的恢复."))
                                runtimeContext._COUNTERCHEST+=1
                                needRecoverBecauseChest = False
                                runtimeContext._MEET_CHEST_OR_COMBAT = True
                                if not setting.SKIP_CHEST_RECOVER:
                                    logger.info(_("由于面板配置, 进行开启宝箱后恢复."))
                                    shouldRecover = True
                                else:
                                    logger.info(_("由于面板配置, 跳过了开启宝箱后恢复."))
                            if needRecoverBecauseCombat:
                                runtimeContext._COUNTERCOMBAT+=1
                                needRecoverBecauseCombat = False
                                runtimeContext._MEET_CHEST_OR_COMBAT = True
                                if (not setting.SKIP_COMBAT_RECOVER):
                                    logger.info(_("由于面板配置, 进行战后恢复."))
                                    shouldRecover = True
                                else:
                                    logger.info(_("由于面板配置, 跳过了战后恢复."))
                            if shouldRecover:
                                Press([1,1])
                                FindCoordsOrElseExecuteFallbackAndWait( # 点击打开人物面板有可能会被战斗打断
                                    ["trait","combatActive","chestFlag","combatClose"],
                                    [[36,1425],[322,1425],[606,1425]],
                                    1
                                    )
                                if CheckIf(ScreenShot(),"trait"):
                                    Press([833,843])
                                    Sleep(1)
                                    FindCoordsOrElseExecuteFallbackAndWait(
                                        ["recover","combatActive"],
                                        [833,843],
                                        1
                                        )
                                    if CheckIf(ScreenShot(),"recover"):
                                        Sleep(1)
                                        Press([600,1200])
                                        for underscore in range(5):
                                            t = time.time()
                                            PressReturn()
                                            if time.time()-t<0.3:
                                                Sleep(0.3-(time.time()-t))
                                        shouldRecover = False
                            ########### light the dark light
                            Press(FindCoordsOrElseExecuteFallbackAndWait("darklight_lightIt","darkLight",1))
                        case DungeonState.Chest:
                            needRecoverBecauseChest = True
                            dungState = StateChest()
                        case DungeonState.Combat:
                            needRecoverBecauseCombat =True
                            StateCombat()
                            dungState = None
            case "manualSepDemon":
                RestartableSequenceExecution(
                    lambda: StateDungeon([TargetInfo("stair_2","左下",[827,547]),
                                         TargetInfo("harken","左下",None)]))
                
                PressReturn()
                PressReturn()
                StateInn()

                RestartableSequenceExecution(
                    lambda: CursedWheelTimeLeap(chapter="cursedwheel_dhi", target="BeautifulOre"))

                quest._EOT = [
                    ["press","COS/COS",["EdgeOfTown",[1,1]],1],
                    ["press","COS/COSB2F",[1,1],1]
                ]
                RestartableSequenceExecution(
                    lambda: StateEoT()
                    )
                
                RestartableSequenceExecution(
                    lambda: StateDungeon([TargetInfo("stair_3","左上",[720,822]),
                                         TargetInfo("position","左上",[79,447])]))

            case "LBC-oneGorgon":
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break
                    if runtimeContext._LAPTIME!= 0:
                        runtimeContext._TOTALTIME = runtimeContext._TOTALTIME + time.time() - runtimeContext._LAPTIME
                        logger.info(_("第{a}次三牛完成. 本次用时:{b}秒. 累计开箱子{c}, 累计战斗{d}, 累计用时{e}秒.".format(a=runtimeContext._COUNTERDUNG, b=round(time.time()-runtimeContext._LAPTIME,2),c=runtimeContext._COUNTERCHEST, d=runtimeContext._COUNTERCOMBAT, e=round(runtimeContext._TOTALTIME,2))),
                                    extra={"summary": True})
                    runtimeContext._LAPTIME = time.time()
                    runtimeContext._COUNTERDUNG+=1

                    RestartableSequenceExecution(
                        lambda: logger.info(_("第一步: 重置因果")),
                        lambda: CursedWheelTimeLeap("GhostsOfYore","LBC/symbolofalliance",[["LBC/EnaWasSaved",2,1,0]])
                        )
                    Sleep(10)
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第二步: 返回要塞")),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn",["returntotown","returnText","leaveDung","blessing",[1,1]],2)
                        )
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第三步: 前往王城")),
                        lambda: TeleportFromCityToWorldLocation("City_RoyalCityLuknalia","input swipe 450 150 500 150"),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("guild",["City_RoyalCityLuknalia",[1,1]],1),
                        )
               
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第四步: 领取任务")),
                        lambda: StateAcceptRequest("LBC/Request",[266,257]),
                    )
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第五步: 进入牛洞")),
                        lambda: TeleportFromCityToWorldLocation("LBC/LBC","input swipe 400 400 400 500")
                        )

                    Gorgon1 = TargetInfo("position","左上",[134,342])
                    Gorgon2 = TargetInfo("position","右上",[500,395])
                    Gorgon3 = TargetInfo("position","右下",[340,1027])
                    LBC_quit = TargetInfo("LBC/LBC_quit")
                    if setting.ACTIVE_REST:
                        RestartableSequenceExecution(
                            lambda: logger.info(_("第六步: 击杀一牛")),
                            lambda: StateDungeon([Gorgon1,LBC_quit])
                            )
                        RestartableSequenceExecution(
                            lambda: logger.info(_("第七步: 回去睡觉")),
                            lambda: StateInn()
                            )
                        RestartableSequenceExecution(
                            lambda: logger.info(_("第八步: 再入牛洞")),
                            lambda: TeleportFromCityToWorldLocation("LBC/LBC","input swipe 400 400 400 500")
                            )
                        RestartableSequenceExecution(
                            lambda: logger.info(_("第九步: 击杀二牛")),
                            lambda: StateDungeon([Gorgon2,Gorgon3,LBC_quit])
                            )
                    else:
                        logger.info("跳过回城休息.")
                        RestartableSequenceExecution(
                            lambda: logger.info(_("第六步: 连杀三牛")),
                            lambda: StateDungeon([Gorgon1,Gorgon2,Gorgon3,LBC_quit])
                            )
            case "SSC-goldenchest":
                while 1:
                    quest._SPECIALDIALOGOPTION = ["SSC/dotdotdot","SSC/shadow"]
                    if setting._FORCESTOPING.is_set():
                        break
                    if runtimeContext._LAPTIME!= 0:
                        runtimeContext._TOTALTIME = runtimeContext._TOTALTIME + time.time() - runtimeContext._LAPTIME
                        logger.info(_("第{a}次忍洞完成. 本次用时:{b}秒. 累计开箱子{c}, 累计战斗{d}, 累计用时{e}秒.".format(a=runtimeContext._COUNTERDUNG, b=round(time.time()-runtimeContext._LAPTIME,2),c=runtimeContext._COUNTERCHEST, d=runtimeContext._COUNTERCOMBAT, e=round(runtimeContext._TOTALTIME,2))),
                                    extra={"summary": True})
                    runtimeContext._LAPTIME = time.time()
                    runtimeContext._COUNTERDUNG+=1
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第一步: 重置因果")),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("cursedWheel",["ruins",[1,1]],1)),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("SSC/Leap",["specialRequest",[1,1]],1)),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("OK","leap",1)),
                        )
                    Sleep(10)
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第二步: 前往王城")),
                        lambda: TeleportFromCityToWorldLocation("City_RoyalCityLuknalia","input swipe 450 150 500 150"),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("guild",["City_RoyalCityLuknalia",[1,1]],1),
                        )
                    def stepThree():
                        FindCoordsOrElseExecuteFallbackAndWait("Inn",[1,1],1)
                        StateInn()
                        Press(FindCoordsOrElseExecuteFallbackAndWait("guildRequest",["guild",[1,1]],1))
                        Press(FindCoordsOrElseExecuteFallbackAndWait("guildFeatured",["guildRequest",[1,1]],1))
                        Sleep(1)
                        DeviceShell(f"input swipe 150 1300 150 200")
                        Sleep(2)
                        while 1:
                            pos = CheckIf(ScreenShot(),"SSC/Request")
                            if not pos:
                                DeviceShell(f"input swipe 150 200 150 250")
                                Sleep(1)
                            else:
                                Press([pos[0]+300,pos[1]+150])
                                break
                        FindCoordsOrElseExecuteFallbackAndWait("guildRequest",[1,1],1)
                        PressReturn()
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第三步: 领取任务")),
                        lambda: stepThree()
                        )

                    RestartableSequenceExecution(
                        lambda: logger.info(_("第四步: 进入忍洞")),
                        lambda: TeleportFromCityToWorldLocation("SSC/SSC","input swipe 700 500 600 600")
                        )
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第五步: 关闭陷阱")),
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("SSC/trapdeactived",["input swipe 450 1050 450 850",[445,721]],4),
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("dungFlag",[1,1],1)
                    )
                    quest._SPECIALDIALOGOPTION = ["SSC/dotdotdot","SSC/shadow"]
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第六步: 第一个箱子")),
                        lambda: StateDungeon([
                                TargetInfo("position",     "左上", [719,1088]),
                                TargetInfo("position",     "左上", [346,874]),
                                TargetInfo("chest",        "左上", [[0,0,900,1600],[640,0,260,1600],[506,0,200,700]]),
                                TargetInfo("chest",        "右上", [[0,0,900,1600],[0,0,407,1600]]),
                                TargetInfo("chest",        "右下", [[0,0,900,1600],[0,0,900,800]]),
                                TargetInfo("chest",        "左下", [[0,0,900,1600],[650,0,250,811],[507,166,179,165]]),
                                TargetInfo("SSC/SSC_quit", "右下", None)
                            ])
                        )
            case "CaveOfSeperation":
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break
                    if runtimeContext._LAPTIME!= 0:
                        runtimeContext._TOTALTIME = runtimeContext._TOTALTIME + time.time() - runtimeContext._LAPTIME
                        logger.info(_("第{a}次约定之剑完成. 本次用时:{b}秒. 累计开箱子{c}, 累计战斗{d}, 累计用时{e}秒.".format(a=runtimeContext._COUNTERDUNG, b=round(time.time()-runtimeContext._LAPTIME,2),c=runtimeContext._COUNTERCHEST, d=runtimeContext._COUNTERCOMBAT, e=round(runtimeContext._TOTALTIME,2))),
                                    extra={"summary": True})
                    runtimeContext._LAPTIME = time.time()
                    runtimeContext._COUNTERDUNG+=1
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第一步: 重置因果")),
                        lambda: CursedWheelTimeLeap("GhostsOfYore","COS/ArnasPast")
                        )
                    Sleep(10)
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第二步: 返回要塞")),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn",["returntotown","returnText","leaveDung","blessing",[1,1]],2)
                        )
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第三步: 前往王城)")),
                        lambda: TeleportFromCityToWorldLocation("City_RoyalCityLuknalia","input swipe 450 150 500 150"),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("guild",["City_RoyalCityLuknalia",[1,1]],1),
                        )
                    
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第四步: 领取任务")),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait(["COS/Okay","guildRequest"],["guild",[1,1]],1),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn",["COS/Okay","return",[1,1]],1),
                        lambda: StateInn(),
                        )
                    
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第五步: 进入洞窟")),
                        lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("COS/COS",["EdgeOfTown",[1,1]],1)),
                        lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("COS/COSENT",[1,1],1))
                        )
                    quest._SPECIALDIALOGOPTION = ["COS/takehimwithyou"]
                    cosb1f = [TargetInfo("position","右下",[286-54,440]),
                              TargetInfo("position","右下",[819,653+54]),
                              TargetInfo("position","右上",[659-54,501]),
                              TargetInfo("stair_2","右上",[126-54,342]),
                        ]
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第六步: 1层找人")),
                        lambda: StateDungeon(cosb1f)
                        )

                    quest._SPECIALFORCESTOPINGSYMBOL = ["COS/EnaTheAdventurer"]
                    cosb2f = [TargetInfo("position","右上",[340+54,448]),
                              TargetInfo("position","右上",[500-54,1088]),
                              TargetInfo("position","左上",[398+54,766]),
                        ]
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第七步: 2层找人")),
                        lambda: StateDungeon(cosb2f)
                        )

                    quest._SPECIALFORCESTOPINGSYMBOL = ["COS/requestwasfor"] 
                    cosb3f = [TargetInfo("stair_3","左上",[720,822]),
                              TargetInfo("position","左下",[239,600]),
                              TargetInfo("position","左下",[185,1185]),
                              TargetInfo("position","左下",[560,652]),
                              ]
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第八步: 3层找人")),
                        lambda: StateDungeon(cosb3f)
                        )

                    quest._SPECIALFORCESTOPINGSYMBOL = None
                    quest._SPECIALDIALOGOPTION = ["COS/requestwasfor"] 
                    cosback2f = [
                                 TargetInfo("stair_2","左下",[827,547]),
                                 TargetInfo("position","右上",[340+54,448]),
                                 TargetInfo("position","右上",[500-54,1088]),
                                 TargetInfo("position","左上",[398+54,766]),
                                 TargetInfo("position","左上",[559,1087]),
                                 TargetInfo("stair_1","左上",[666,448]),
                                 TargetInfo("position", "右下",[660,919])
                        ]
                    RestartableSequenceExecution(
                        lambda: logger.info(_("第九步: 离开洞穴")),
                        lambda: StateDungeon(cosback2f)
                        )
                    Press(FindCoordsOrElseExecuteFallbackAndWait("guild",["return",[1,1]],1)) # 回城
                    FindCoordsOrElseExecuteFallbackAndWait("Inn",["return",[1,1]],1)
                    
                pass
            case "gaintKiller":
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break
                    if runtimeContext._LAPTIME!= 0:
                        runtimeContext._TOTALTIME = runtimeContext._TOTALTIME + time.time() - runtimeContext._LAPTIME
                        logger.info(_("第{a}次巨人完成. 本次用时:{b}秒. 累计开箱子{c}, 累计战斗{d}, 累计用时{e}秒.".format(a=runtimeContext._COUNTERDUNG, b=round(time.time()-runtimeContext._LAPTIME,2),c=runtimeContext._COUNTERCHEST, d=runtimeContext._COUNTERCOMBAT, e=round(runtimeContext._TOTALTIME,2))),
                                    extra={"summary": True})
                    runtimeContext._LAPTIME = time.time()
                    runtimeContext._COUNTERDUNG+=1

                    quest._EOT = [
                        ["press","impregnableFortress",["EdgeOfTown",[1,1]],1],
                        ["press","fortressb7f",[1,1],1]]
                    RestartableSequenceExecution(
                        lambda: StateEoT()
                        )
                    
                    logger.info(_("跳过了巨人检测环节. 现在默认总是击杀灯怪."))
                    RestartableSequenceExecution(
                        lambda: StateDungeon([TargetInfo("position","左上",[560,928+54]),
                                              TargetInfo("harken2","左上")]),
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn",["returntotown","returnText","leaveDung","blessing",[1,1]],2)
                    )

                    if ((runtimeContext._COUNTERDUNG-1) % (setting.REST_INTERVEL+1) == 0):
                        RestartableSequenceExecution(
                            lambda: StateInn()
                        )
            case "Scorpionesses":
                total_time = 0
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break

                    starttime = time.time()
                    runtimeContext._COUNTERDUNG += 1

                    if not setting.ACTIVE_BEAUTIFUL_ORE:
                        if not setting.ACTIVE_TRIUMPH:                            
                            logger.info(_("第一步: 时空跳跃..."))
                            RestartableSequenceExecution(
                                lambda: CursedWheelTimeLeap()
                            )
                            Sleep(10)
                            logger.info(_("第二步: 返回要塞..."))
                            RestartableSequenceExecution(
                                lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn",["returntotown","returnText","leaveDung","blessing",[1,1]],2)
                                )
                                
                            logger.info(_("第三步: 前往王城..."))
                            RestartableSequenceExecution(
                                lambda:TeleportFromCityToWorldLocation("City_RoyalCityLuknalia","input swipe 450 150 500 150"),
                                )
                        else:
                            logger.info(_("第一步: 时空跳跃..."))
                            RestartableSequenceExecution(
                                lambda: CursedWheelTimeLeap(chapter="cursedwheel_impregnableFortress", target="Triumph")
                            )
                            Sleep(10)
                                
                            logger.info(_("第三步: 前往王城..."))
                            RestartableSequenceExecution(
                                lambda:TeleportFromCityToWorldLocation("City_RoyalCityLuknalia","input swipe 450 150 500 150"),
                                )

                    elif setting.ACTIVE_BEAUTIFUL_ORE:
                        logger.info(_("第一步: 时空跳跃..."))
                        RestartableSequenceExecution(
                            lambda: CursedWheelTimeLeap(chapter="cursedwheel_dhi", target="BeautifulOre")
                        )
                        Sleep(10)

                    logger.info(_("第四步: 悬赏揭榜"))
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("guildRequest",["guild",[1,1]],1)),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("Bounties",["guild","guildRequest","input swipe 600 1400 300 1400",[1,1]],1)),
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("EdgeOfTown",["return",[1,1]],1)
                        )

                    logger.info(_("第五步: 击杀蝎女"))
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("dungFlag",["EdgeOfTown","beginningAbyss","B2FTemple","GotoDung",[1,1]],1),
                    )
                    RestartableSequenceExecution(
                        lambda:StateDungeon([TargetInfo("position","左下",[505,760]),
                                             TargetInfo("position","左上",[506,821])]),
                        )
                    
                    logger.info(_("第六步: 提交悬赏"))
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("guild",["return",[1,1]],1),
                    )
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("CompletionReported",["guild","guildRequest","input swipe 600 1400 300 1400","Bounties",[1,1]],1))
                        )
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("EdgeOfTown",["return",[1,1]],1)
                        )
                    
                    logger.info(_("第七步: 休息"))
                    if ((runtimeContext._COUNTERDUNG-1) % (setting.REST_INTERVEL+1) == 0):
                        RestartableSequenceExecution(
                            lambda:StateInn()
                            )
                        
                    costtime = time.time()-starttime
                    total_time = total_time + costtime
                    logger.info(_("第{a}次\"悬赏:蝎女\"完成. \n该次花费时间{b:.2f}s.\n总计用时{c:.2f}s.\n平均用时{d:.2f}".format(a=runtimeContext._COUNTERDUNG,b=costtime, c=total_time, d=total_time/runtimeContext._COUNTERDUNG)),
                            extra={"summary": True})
            case "Scorpionesses_plus_6_hands":
                total_time = 0
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break

                    starttime = time.time()
                    runtimeContext._COUNTERDUNG += 1

                    if not setting.ACTIVE_BEAUTIFUL_ORE:
                        if not setting.ACTIVE_TRIUMPH:                            
                            logger.info(_("第一步: 时空跳跃..."))
                            RestartableSequenceExecution(
                                lambda: CursedWheelTimeLeap()
                            )
                            Sleep(10)
                            logger.info(_("第二步: 返回要塞..."))
                            RestartableSequenceExecution(
                                lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn",["returntotown","returnText","leaveDung","blessing",[1,1]],2)
                                )
                                
                            logger.info(_("第三步: 前往王城..."))
                            RestartableSequenceExecution(
                                lambda:TeleportFromCityToWorldLocation("City_RoyalCityLuknalia","input swipe 450 150 500 150"),
                                )
                        else:
                            logger.info(_("第一步: 时空跳跃..."))
                            RestartableSequenceExecution(
                                lambda: CursedWheelTimeLeap(chapter="cursedwheel_impregnableFortress", target="Triumph")
                            )
                            Sleep(10)
                                
                            logger.info(_("第三步: 前往王城..."))
                            RestartableSequenceExecution(
                                lambda:TeleportFromCityToWorldLocation("City_RoyalCityLuknalia","input swipe 450 150 500 150"),
                                )

                    elif setting.ACTIVE_BEAUTIFUL_ORE:
                        logger.info(_("第一步: 时空跳跃..."))
                        RestartableSequenceExecution(
                            lambda: CursedWheelTimeLeap(chapter="cursedwheel_dhi", target="BeautifulOre")
                        )
                        Sleep(10)

                    logger.info(_("第四步: 悬赏揭榜"))
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("guildRequest",["guild",[1,1]],1)),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("Bounties",["guild","guildRequest","input swipe 600 1400 300 1400",[1,1]],1)),
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("EdgeOfTown",["return",[1,1]],1)
                        )

                    logger.info(_("第五步: 击杀蝎女"))
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("dungFlag",["EdgeOfTown","beginningAbyss","B2FTemple","GotoDung",[1,1]],1),
                    )
                    RestartableSequenceExecution(
                        lambda:StateDungeon([TargetInfo("position","左下",[505,760]),
                                             TargetInfo("position","左上",[506,821])]),
                        )
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("guild",["return",[1,1]],1),
                    )
                    logger.info(_("第5.5步: 击杀风暴六手"))
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("dungFlag",["EdgeOfTown","beginningAbyss","B5FWarpedOnesNest","GotoDung",[1,1]],1),
                    )
                    RestartableSequenceExecution(
                        lambda:StateDungeon([TargetInfo("position","左上",[454,662]),
                                             TargetInfo("position","左上",[135,714])]),
                        )
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("guild",["return",[1,1]],1),
                    )
                    
                    logger.info(_("第六步: 提交悬赏"))
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("CompletionReported",["guild","guildRequest","input swipe 600 1400 300 1400","Bounties",[1,1]],1))
                        )
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("EdgeOfTown",["return",[1,1]],1)
                        )
                    
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("CompletionReported",["guild","guildRequest","input swipe 600 1400 300 1400","Bounties",[1,1]],1))
                        )
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("EdgeOfTown",["return",[1,1]],1)
                        )
                    
                    logger.info(_("第七步: 休息"))
                    if ((runtimeContext._COUNTERDUNG-1) % (setting.REST_INTERVEL+1) == 0):
                        RestartableSequenceExecution(
                            lambda:StateInn()
                            )
                        
                    costtime = time.time()-starttime
                    total_time = total_time + costtime
                    logger.info(_("第{a}次\"悬赏:蝎女+六手\"完成. \n该次花费时间{b:.2f}s.\n总计用时{c:.2f}s.\n平均用时{d:.2f}".format(a=runtimeContext._COUNTERDUNG,b=costtime, c=total_time, d=total_time/runtimeContext._COUNTERDUNG)),
                            extra={"summary": True})
            case "steeltrail":
                total_time = 0
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break

                    starttime = time.time()
                    runtimeContext._COUNTERDUNG += 1

                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("guildRequest",["guild",[1,1]],1)),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("gradeexam",["guild","guildRequest","input swipe 600 1400 300 1400",[1,1]],1)),
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("Steel","gradeexam",1)
                    )

                    pos = CheckIf(ScreenShot(),"Steel")
                    Press([pos[0]+306,pos[1]+258])
                    
                    quest._SPECIALDIALOGOPTION = ["ready","noneed", "quit"]
                    RestartableSequenceExecution(
                        lambda:StateDungeon([TargetInfo("position","左上",[131,769]),
                                    TargetInfo("position","左上",[827,447]),
                                    TargetInfo("position","左上",[131,769]),
                                    TargetInfo("position","左下",[719,1080]),
                                    ])
                                  )
                    
                    if ((runtimeContext._COUNTERDUNG-1) % (setting.REST_INTERVEL+1) == 0):
                        RestartableSequenceExecution(
                            lambda:StateInn()
                            )
                    costtime = time.time()-starttime
                    total_time = total_time + costtime
                    logger.info(_("第{a}次\"钢试炼\"完成. \n该次花费时间{b:.2f}s.\n总计用时{c:.2f}s.\n平均用时{d:.2f}".format(a=runtimeContext._COUNTERDUNG,b=costtime, c=total_time, d=total_time/runtimeContext._COUNTERDUNG)),
                            extra={"summary": True})
            case "jier":
                total_time = 0
                while 1:
                    quest._SPECIALDIALOGOPTION = ["bounty/cuthimdown"]

                    if setting._FORCESTOPING.is_set():
                        break

                    starttime = time.time()
                    runtimeContext._COUNTERDUNG += 1

                    RestartableSequenceExecution(
                        lambda: CursedWheelTimeLeap("requestToRescueTheDuke")
                        )

                    Sleep(10)
                    logger.info(_("第二步: 返回要塞..."))
                    RestartableSequenceExecution(
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("Inn",["returntotown","returnText","leaveDung","blessing",[1,1]],2)
                        )

                    logger.info(_("第三步: 前往王城..."))
                    RestartableSequenceExecution(
                        lambda:TeleportFromCityToWorldLocation("City_RoyalCityLuknalia","input swipe 450 150 500 150"),
                        )

                    logger.info(_("第四步: 悬赏揭榜"))
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("guildRequest",["guild",[1,1]],1)),
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("Bounties",["guild","guildRequest","input swipe 600 1400 300 1400",[1,1]],1)),
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("EdgeOfTown",["return",[1,1]],1)
                        )

                    logger.info(_("第五步: 和吉尔说再见吧"))
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("dungFlag",["EdgeOfTown","beginningAbyss","B4FLabyrinth","GotoDung",[1,1]],1)
                        )
                    RestartableSequenceExecution( 
                        lambda:StateDungeon([TargetInfo("position","左下",[452,545]),
                                             TargetInfo("position","左下",[452,1026]),
                                             TargetInfo("harken","左上",None)]),
                        )
                    
                    logger.info(_("第六步: 提交悬赏"))
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("guild",["return",[1,1]],1),
                    )
                    RestartableSequenceExecution(
                        lambda:Press(FindCoordsOrElseExecuteFallbackAndWait("CompletionReported",["guild","guildRequest","input swipe 600 1400 300 1400","Bounties",[1,1]],1))
                        )
                    RestartableSequenceExecution(
                        lambda:FindCoordsOrElseExecuteFallbackAndWait("EdgeOfTown",["return",[1,1]],1)
                        )
                    
                    logger.info(_("第七步: 休息"))
                    if ((runtimeContext._COUNTERDUNG-1) % (setting.REST_INTERVEL+1) == 0):
                        RestartableSequenceExecution(
                            lambda:StateInn()
                            )
                        
                    costtime = time.time()-starttime
                    total_time = total_time + costtime
                    logger.info(_("第{a}次\"悬赏:吉尔\"完成. \n该次花费时间{b:.2f}s.\n总计用时{c:.2f}s.\n平均用时{d:.2f}".format(a=runtimeContext._COUNTERDUNG,b=costtime, c=total_time, d=total_time/runtimeContext._COUNTERDUNG)),
                            extra={"summary": True})
            case "lovesleep":
                logger.info(_("开始睡觉."))
                t = time.time()
                for counter in range(9999):
                    if setting._FORCESTOPING.is_set():
                        break
                    RestartableSequenceExecution(
                            lambda:StateInn()
                            )
                    if setting._FORCESTOPING.is_set():
                        break
                    logger.info(_("完成了{a}次旅店休息.\n总计用时{c:.2f}s.\n平均用时{d:.2f}s.").format(a=counter+1, c=time.time()-t, d=(time.time()-t)/(counter+1)),extra={"summary": True})
            case "fortress-B8F_trap":
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break
                    if runtimeContext._LAPTIME!= 0:
                        runtimeContext._TOTALTIME = runtimeContext._TOTALTIME + time.time() - runtimeContext._LAPTIME
                        logger.info(_("第{a}次要塞小精灵完成. 本次用时:{b}秒. 累计开箱子{c}, 累计战斗{d}, 累计用时{e}秒.".format(a=runtimeContext._COUNTERDUNG, b=round(time.time()-runtimeContext._LAPTIME,2),c=runtimeContext._COUNTERCHEST, d=runtimeContext._COUNTERCOMBAT, e=round(runtimeContext._TOTALTIME,2))),
                                    extra={"summary": True})
                    runtimeContext._LAPTIME = time.time()
                    runtimeContext._COUNTERDUNG+=1

                    RestartableSequenceExecution(
                        lambda: StateDungeon([TargetInfo("stair_fortress1f","左上",[720,395]),
                                              TargetInfo("mark_auto"),
                                              TargetInfo("position","右下",[712,972]),
                                              TargetInfo("position","右下",[500,1080]),
                                              TargetInfo("position","右下",[765,918]),
                                              TargetInfo("position","右下",[606,1026]),
                                              TargetInfo("stair_fortressGate","左下", [720,1027]),])
                        )
            case "FFXI-Org":
                counter = {
                    "全改" : 0,
                    "下改" : 0,
                    "衔尾蛇": 0,
                    "精炼石": 0,
                    "改造石": 0,
                    "银矿石" : 0,
                    "特级矿石": 0,
                    "上级矿石": 0,
                    "中级矿石": 0,
                    "下级矿石": 0,
                    "某些无法判断的东西":0,

                    "其他": 0
                    }
                start_time = time.time()
                # reunionParty("FFXI/FFXIStone")
                resetBag = False
                while 1:
                    if setting._FORCESTOPING.is_set():
                        break
                    logger.info("进本!")
                    RestartableSequenceExecution(
                        lambda: StateEoT()
                        )

                    logger.info("前往矿点...")
                    RestartableSequenceExecution(
                        lambda: FindCoordsOrElseExecuteFallbackAndWait("theRouteToTheDestinationCannotBeFound",[[1,1],"mark_auto","donothing"],0.5)
                    )

                    while 1:
                        if setting._FORCESTOPING.is_set():
                            break
                        scn = ScreenShot()
                        Press([450,600])

                        if TryPressRetry(scn):
                            Sleep(1)
                            continue

                        if CheckIf(scn, "FFXI/receive",[[4,664,890,283]]):
                            vals = {
                                "特级矿石": CheckHow(scn,"FFXI/org_fine", [[4,664,890,283]]),
                                "上级矿石": CheckHow(scn,"FFXI/org_high", [[4,664,890,283]]),
                                "中级矿石": CheckHow(scn,"FFXI/org_mid", [[4,664,890,283]]),
                                "下级矿石": CheckHow(scn,"FFXI/org_low", [[4,664,890,283]]),
                                "精炼石": CheckHow(scn,"FFXI/org_refine", [[4,664,890,283]]),
                                "改造石": CheckHow(scn,"FFXI/org_alter", [[4,664,890,283]]),
                                "银矿石": CheckHow(scn,"FFXI/org_sliver", [[4,664,890,283]]),
                                "衔尾蛇": CheckHow(scn,"FFXI/org_ouro", [[4,664,890,283]]),
                                "下改": CheckHow(scn,"FFXI/org_lesser_full", [[4,664,890,283]]),
                                "全改": CheckHow(scn,"FFXI/org_full", [[4,664,890,283]]),

                            }
                            
                            if vals[best := max(vals, key=vals.get)] > 0.9:
                                logger.info(f"获得了{best}!")
                                counter[best]+=1
                            else:
                                logger.info(f"遇到了一些状况之外的情况.")
                                SaveImage(scn)

                                logger.info(f"某些无法判断的东西...")
                                counter["某些无法判断的东西"]+=1

                        if CheckIf(scn, "FFXI/nothingToDig",[[320,667,423,474]]) or CheckIf(scn, "FFXI/nothingToDig2",[[320,667,423,474]]):
                            logger.info("没东西了, 撤退。")
                            RestartableSequenceExecution(
                                lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("ReturnText",[[1,1],"leaveDung","donothing"],1))
                            )
                            break

                        if CheckIf(scn,"FFXI/needpickaxe",[[4,664,890,283]]):
                            resetBag = True
                            logger.info("镐子用完了，回去拿。")
                            RestartableSequenceExecution(
                                lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("ReturnText",[[1,1],"leaveDung","donothing"],1))
                            )
                            break

                        Sleep(1.5)

                    if resetBag:
                        RestartableSequenceExecution(
                            lambda: Press(FindCoordsOrElseExecuteFallbackAndWait("OpenWorldMap",[[1,1],"leaveDung","donothing"],1))
                        )
                        RestartableSequenceExecution(
                            lambda: TeleportFromDungeonToCity(*quest._RTT)
                        )

                        reunionParty("FFXI/FFXIStone")
                        resetBag = False

                    output_str = ""
                    for k,v in counter.items():
                        if v>0:
                            output_str += f"{k}:{v}个,"
                    output_str += f"\n累计用时:{(time.time()-start_time):.2f}."
                    logger.info(output_str,extra={"summary": True})
        ##########################
        setting._FINISHINGCALLBACK()
        return
    def Farm(set:FarmConfig):
        nonlocal quest
        nonlocal setting # 初始化
        nonlocal runtimeContext
        

        setting = set
        runtimeContext = RuntimeContext()
        runtimeContext._LAST_DEBUG_IMAGE_AT = {}

        Sleep(1)

        ReloadStrategy()
        
        ResetDevice()

        quest = LoadQuest(setting.FARM_TARGET)
        if quest:
            if quest._TYPE =="dungeon":
                DungeonFarm()
            else:
                QuestFarm()
        else:
            setting._FINISHINGCALLBACK()
    return Farm
