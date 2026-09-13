"""迁移归属登记，不生成或执行尚未实现的业务代码。"""

import re

BASELINE = "6585f4075f5714ab522aa582993860c09af912c1"
# 会跨层命名的函数显式登记：PressWorldMapTargetArea 是业务动作，不能因 Press
# 一词就归入底层输入；截图获取与图像识别也必须分别拥有职责。
SCRIPT_EXACT = {
    "CaptureScreen": ("native/devices", "GuardedAdbController", "M3-CAPTURE"),
    "ScreenShot": ("native/devices", "GuardedAdbController", "M3-CAPTURE"),
    "ResetScreenshotBackend": ("native/devices", "GuardedAdbController", "M3-CAPTURE"),
    "NormalizeScreenshotImage": ("native/devices", "ViewportProfile", "M3-CAPTURE"),
    "PressWorldMapTargetArea": (
        "native/games/wvd/navigation",
        "WvdWorldMapTravel",
        "M4-NAVIGATION",
    ),
    "PressMapAutoMove": (
        "native/games/wvd/navigation",
        "WvdNavigation",
        "M4-NAVIGATION",
    ),
    "PressDisarmSafely": ("native/games/wvd/chest", "WvdChestState", "M4-CHEST"),
    "TryPressRetry": ("native/games/wvd/recovery", "WvdRecovery", "M4-RECOVERY"),
    "TryResumePauseOverlay": (
        "native/games/wvd/recovery",
        "WvdRecovery",
        "M4-RECOVERY",
    ),
    "CheckRolePortraitMatch": (
        "native/games/wvd/vision",
        "WvdBattleRecognizer",
        "M3-VISION",
    ),
    "CheckCombatTargetByTemplate": (
        "native/games/wvd/vision",
        "WvdTargetRecognizer",
        "M3-VISION",
    ),
    "CheckCombatNextTarget": (
        "native/games/wvd/vision",
        "WvdTargetRecognizer",
        "M3-VISION",
    ),
    "CheckCombatNextTargetLowConfidence": (
        "native/games/wvd/vision",
        "WvdTargetRecognizer",
        "M3-VISION",
    ),
    "CheckCombatTargetMarker": (
        "native/games/wvd/vision",
        "WvdTargetRecognizer",
        "M3-VISION",
    ),
}
RULES = [
    (
        r"Screenshot|CaptureScreen|MatchCache|RoiCache|_check|CheckTemplate|CheckIf|CheckHow|CutRoI|NormalizeScreenshot|WrapImage|MinusImage",
        "native/games/wvd/vision",
        "WvdLegacyRecognizer",
        "M3-VISION",
    ),
    (
        r"Pause|Ocr|StateCombatCheck",
        "native/games/wvd/vision",
        "WvdPauseRecognizer",
        "M3-PAUSE",
    ),
    (
        r"Portrait|Skill|Next|Target.*Click|Combat|Strategy",
        "native/games/wvd/combat",
        "WvdCombatState",
        "M4-COMBAT",
    ),
    (
        r"Clash|Vpn|Android|ADB|Adb|Emulator|Device|CMDLine|MumuIpc",
        "native/devices",
        "DeviceLifecycle",
        "M3-DEVICE",
    ),
    (
        r"restartGame|BootReady|Restart|Frozen|BlockingScreen|Resume|RiseAgain",
        "native/games/wvd/recovery",
        "WvdRecovery",
        "M4-RECOVERY",
    ),
    (r"Press|Sleep", "native/devices", "InputGate", "M2-INPUT"),
    (
        r"Debug|Diagnostics|ImportantInfo|Counter",
        "native/storage",
        "EventStore",
        "M4-DIAGNOSTICS",
    ),
    (
        r"Inn|Rest|reunionParty|BagClear",
        "native/games/wvd/supply",
        "WvdSupply",
        "M4-SUPPLY",
    ),
    (r"Chest", "native/games/wvd/chest", "WvdChestState", "M4-CHEST"),
    (
        r"Fishing|Bobber|Fish|Bait",
        "native/games/wvd/fishing",
        "WvdFishing",
        "M4-FISHING",
    ),
    (
        r"Map|Moving|Teleport|World|CursedWheel|StateEoT|StateAccept",
        "native/games/wvd/navigation",
        "WvdNavigation",
        "M4-NAVIGATION",
    ),
]


def owner(file, symbol):
    if file.endswith("gui.py"):
        if symbol in (
            "LoadSettingFromDict",
            "LoadConfig",
            "ConfigPanelApp.save_config",
        ):
            return "native/storage", "LegacyConfigImporter", "M4-CONFIG"
        if symbol.startswith(("ScrollableFrame", "CollapsibleSection", "BLOCK_WHEEL")):
            return "web/src/components", "PanelPresentation", "M5-GUI"
        return "web/src/pages", "LegacyPanelMigration", "M5-GUI"
    if file.endswith("main.py"):
        if symbol in ("parse_args", "main", "HeadlessActive", "<module>"):
            return "native/app", "ApplicationEntry", "M5-CLI"
        return "native/runtime", "RunCoordinator", "M2-LIFECYCLE"
    if file.endswith("auto_updater.py"):
        if symbol.startswith("Progressbar"):
            return "web/src/components", "UpdateProgress", "M6-UPDATE"
        return "native/app", "NewReleaseManager", "M6-UPDATE"
    if file.endswith("utils.py"):
        if re.search(
            r"Config|Json|Quest|Resource|Template|reflectImage|LoadImage",
            symbol,
            re.IGNORECASE,
        ):
            return "native/storage", "LegacyResourceImporter", "M4-IMPORT"
        if "Fishing" in symbol:
            return "native/games/wvd/fishing", "WvdFishing", "M4-FISHING"
        if re.search(r"Tooltip|ChangesLog|ScrolledTextHandler", symbol):
            return "web/src/components", "LegacyPresentation", "M5-GUI"
        return "native/storage", "EventStore", "M4-DIAGNOSTICS"
    if file.endswith("script.py"):
        if symbol.split(".")[-1] in SCRIPT_EXACT:
            return SCRIPT_EXACT[symbol.split(".")[-1]]
        # 先按类的职责定归属，再处理闭包内的细粒度动作，避免 capture 等同名方法误分类。
        if symbol.split(".")[0] in (
            "AdbScreenshotBackend",
            "MumuIpcScreenshotBackend",
            "ScreenshotBackendManager",
        ):
            return "native/devices", "GuardedAdbController", "M3-CAPTURE"
        if symbol.split(".")[0] == "RuntimeContext":
            return "native/games/wvd", "WvdRunContext", "M4-STATE"
        if symbol.split(".")[0] == "TargetInfo":
            return "native/games/wvd/navigation", "WvdRouteTarget", "M4-NAVIGATION"
        if symbol.split(".")[0] in ("FarmConfig", "FarmQuest") or symbol == "LoadQuest":
            return "native/games/wvd", "WvdProfile", "M4-CONFIG"
        for pattern, path, unit, test in RULES:
            if re.search(pattern, symbol):
                return path, unit, test
        return "native/games/wvd/tasks", "WvdTaskActions", "M4-TASKS"
    raise ValueError("未登记源码职责: " + file)


def record(
    identifier, kind, semantics, source, file=None, symbol=None, destination=None
):
    path, unit, acceptance = destination or owner(file, symbol)
    # 资源/任务记录不是 C++ 方法声明。保留其真实 ID 作为查表参数，不能把 .png
    # 扩展名错误地截成一个叫 png 的未来方法。
    operation = {
        "asset": "resolve_resource",
        "task": "resolve_task",
        "quest_field": "field_definition",
        "config": "import_field",
        "operation": "validate_release_contract",
    }.get(kind, (symbol or identifier).split(".")[-1])
    return {
        "id": identifier,
        "kind": kind,
        "legacy_symbol": symbol or identifier,
        "original_semantics": semantics,
        "source_ref": source,
        "new_owner": path,
        "new_entry": unit + "::" + operation,
        "data_mapping": "保留原值及既有优先级；字段映射见本条结构化详情；尚未执行导入",
        "acceptance_ids": [acceptance],
        "status": "MAPPED_NOT_IMPLEMENTED",
    }
