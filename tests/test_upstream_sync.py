"""上游同步回归：使用真实资源和隔离配置，不连接模拟器。"""
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))


def find_closure_function(entry, name):
    """Factory 的实际闭包函数；不复制识别/恢复算法到测试中。"""
    pending, visited = [entry], set()
    while pending:
        fn = pending.pop()
        if id(fn) in visited:
            continue
        visited.add(id(fn))
        if fn.__name__ == name:
            return fn
        for cell in fn.__closure__ or ():
            try:
                value = cell.cell_contents
            except ValueError:
                continue
            if isinstance(value, types.FunctionType):
                pending.append(value)
    raise AssertionError(name)


def replace_dependency(fn, name, value):
    cells = dict(zip(fn.__code__.co_freevars, fn.__closure__ or ()))
    cells[name].cell_contents = value


class UpstreamSyncTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_cwd = Path.cwd()
        cls.original_streams = (sys.stdout, sys.stderr)
        cls.temp = tempfile.TemporaryDirectory(prefix="wvd-sync-test-")
        os.chdir(cls.temp.name)
        # utils/GUI 都可能写配置或清理日志，因此必须先切换到独立目录再导入。
        import gui
        import script
        import utils
        cls.gui, cls.script, cls.utils = gui, script, utils

    @classmethod
    def tearDownClass(cls):
        cls.utils.LOG_LISTENER_MGR.stop()
        sys.stdout, sys.stderr = cls.original_streams
        for handler in cls.utils.logger.handlers[:]:
            cls.utils.logger.removeHandler(handler)
            handler.close()
        os.chdir(cls.original_cwd)
        cls.temp.cleanup()

    def test_new_quests_load_and_r6_is_correct(self):
        for key in ("fishing", "fishing2", "sandman", "FFXI-5F-2Elite-left"):
            self.assertIsNotNone(self.script.LoadQuest(key))
        data = self.utils.QUEST_DATA
        r6 = [q for q in data.values() if "R6" in q.get("questName", "")]
        self.assertTrue(r6)
        for quest in r6:
            self.assertIn("DH-R6", json.dumps(quest.get("_EOT", [])))
        self.assertEqual(next(iter(self.script.DUNGEON_TARGETS)), "最新任务")

    def test_old_configuration_round_trip(self):
        config = {
            "GENERAL": {"RELOAD_STRATEGY_WHEN": "每场战斗前", "TASK_SPECIFIC_CONFIG": False,
                        "STRATEGY": [{"group_name": "旧策略", "skill_settings": []}]},
            "DEFAULT": {"SKIP_COMBAT_RECOVER": True, "SKIP_CHEST_RECOVER": False},
        }
        self.utils.SaveConfigToFile(config)
        loaded = self.gui.LoadConfig()
        self.assertTrue(loaded["SKIP_COMBAT_RECOVER"])
        self.assertFalse(loaded["SKIP_CHEST_RECOVER"])
        self.assertEqual(loaded["RELOAD_STRATEGY_WHEN"], "每场战斗前")
        self.assertEqual(self.utils.LoadRawConfigFromFile(), config)

    def test_gui_preserves_old_values_and_exposes_new_option(self):
        import queue
        config = {
            "GENERAL": {"LAST_VERSION": "2.8.7", "RELOAD_STRATEGY_WHEN": "每场战斗前",
                        "FARM_TARGET": "FFXI-Org", "FARM_TARGET_TEXT": "[矿石]FFXI挖矿",
                        "STRATEGY": [{"group_name": "旧策略", "skill_settings": []}]},
            "DEFAULT": {"SKIP_COMBAT_RECOVER": True, "SKIP_CHEST_RECOVER": False},
        }
        self.utils.SaveConfigToFile(config)
        root = self.gui.tk.Tk()
        root.withdraw()
        panel = None
        try:
            panel = self.gui.ConfigPanelApp(root, "2.8.7", queue.Queue())
            panel.withdraw()
            self.assertTrue(panel.SKIP_COMBAT_RECOVER.get())
            self.assertFalse(panel.SKIP_CHEST_RECOVER.get())
            self.assertFalse(panel.skip_recover_check.instate(["selected"]))
            self.assertTrue(panel.skip_chest_recover_check.instate(["selected"]))
            self.assertEqual(panel.farm_target_category_combo.get(), "FFXI联动")
            skill = self.gui.SkillConfigPanel(root, init_config={
                "group_name": "测试策略", "skill_settings": [], "complete_one_as_all": True})
            self.assertTrue(skill.get_config_list()["complete_one_as_all"])
            skill.destroy()
            panel.skip_recover_check.invoke()
            self.assertFalse(self.utils.LoadRawConfigFromFile()["DEFAULT"]["SKIP_COMBAT_RECOVER"])
            root.after(250, root.quit)
            root.mainloop()
        finally:
            if panel is not None:
                for handler in (panel.scrolled_text_handler, panel.summary_text_handler):
                    self.utils.logger.removeHandler(handler)
                    handler.close()
                panel.destroy()
            root.destroy()
            sys.stdout, sys.stderr = self.original_streams

    def test_all_resource_images_decode(self):
        images = list((ROOT / "resources/images").rglob("*.png"))
        self.assertGreater(len(images), 100)
        for path in images:
            with self.subTest(resource=path.name):
                self.assertIsNotNone(self.utils.LoadImage(str(path)))

    def test_legacy_character_names_remain_available(self):
        for name in ("N 无名人类女僧侣", "N 无名兽人女盗贼", "N 无名妖精女法师", "N 人类忍者"):
            self.assertIn(name, self.utils.CHAR_LIST)

    def test_matching_debug_does_not_modify_frame(self):
        np = self.script.np
        matcher = find_closure_function(self.script.Factory(), "_check")
        template = self.utils.LoadTemplateImage("next")
        screen = np.zeros((1600, 900, 3), dtype=np.uint8)
        h, w = template.shape[:2]
        screen[350:350+h, 450:450+w] = template
        original = screen.copy()
        for roi in (None, [[80, 220, 819, 680]]):
            matcher(screen, template, roi, outputMatchResult=True)
            self.assertTrue(np.array_equal(original, screen))

    def test_fishing_detector_handles_blank_and_template(self):
        np = self.script.np
        screen = np.zeros((600, 400, 3), dtype=np.uint8)
        detections, marked = self.utils.Fishing_DetectBobber(screen)
        self.assertEqual(detections, [])
        self.assertEqual(marked.shape, (600, 400))
        template = self.utils.LoadTemplateImage("fishing/bobber")
        h, w = template.shape[:2]
        screen[220:220+h, 180:180+w] = template
        original = screen.copy()
        detections, _ = self.utils.Fishing_DetectBobber(screen)
        self.assertTrue(detections)
        self.assertTrue(np.array_equal(screen, original))

    def test_map_search_rejects_missing_map_and_wrong_floor(self):
        finder = find_closure_function(self.script.Factory(), "StateMapSearch")
        quest = self.script.FarmQuest()
        quest._FloorCheck = "stair_fortress3f"
        replace_dependency(finder, "quest", quest)
        screen = self.script.np.zeros((1600, 900, 3), dtype=self.script.np.uint8)
        replace_dependency(finder, "ScreenShot", lambda: screen)
        self.assertEqual(finder(self.script.TargetInfo("position")), (None, "FAIL"))
        template = self.utils.LoadTemplateImage("mapFlag")
        h, w = template.shape[:2]
        screen = screen.copy()
        screen[100:100+h, 720:720+w] = template
        self.assertEqual(finder(self.script.TargetInfo("position")), (None, "WRONGFLOOR"))

    def test_restart_sequence_retries_and_honors_stop(self):
        runner = find_closure_function(self.script.Factory(), "RestartableSequenceExecution")
        cells = dict(zip(runner.__code__.co_freevars, runner.__closure__))
        restart = cells["RestartSignal"].cell_contents
        stop = threading.Event()
        replace_dependency(runner, "setting", types.SimpleNamespace(_FORCESTOPING=stop))
        calls = []

        def operation():
            calls.append(1)
            if len(calls) == 1:
                raise restart()

        runner(operation)
        self.assertEqual(len(calls), 2)
        stop.set()
        runner(operation)
        self.assertEqual(len(calls), 2)

    def test_stopped_fishing_and_sandman_do_not_send_input(self):
        for target in ("fishing", "fishing2", "sandman"):
            quest_farm = find_closure_function(self.script.Factory(), "QuestFarm")
            config = self.script.FarmConfig()
            config.FARM_TARGET = target
            config._FORCESTOPING = threading.Event()
            config._FORCESTOPING.set()
            finished = []
            config._FINISHINGCALLBACK = lambda: finished.append(True)
            replace_dependency(quest_farm, "setting", config)
            replace_dependency(quest_farm, "quest", self.script.LoadQuest(target))
            quest_farm()
            self.assertEqual(finished, [True])

    def test_log_cleanup_preserves_other_files(self):
        import time
        folder = Path("logs")
        folder.mkdir(exist_ok=True)
        names = ("old.png", "log_old.txt", "config.json", "notes.txt")
        for name in names:
            path = folder / name
            path.write_text("test", encoding="utf-8")
            os.utime(path, (time.time() - 5 * 86400,) * 2)
        (folder / "directory.png").mkdir(exist_ok=True)
        self.utils.CleanupOldLogFiles()
        self.assertFalse((folder / "old.png").exists())
        self.assertFalse((folder / "log_old.txt").exists())
        self.assertTrue((folder / "config.json").exists())
        self.assertTrue((folder / "notes.txt").exists())
        self.assertTrue((folder / "directory.png").is_dir())


if __name__ == "__main__":
    unittest.main(verbosity=2)
