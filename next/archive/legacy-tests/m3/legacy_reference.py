"""沿用 M0 的 AST 隔离方式，补齐真实 CutRoI 依赖；不导入生产模块。"""

import ast
import logging
import subprocess

import cv2
import numpy as np


def extract(repo):
    source = subprocess.check_output(
        ["git", "show", "6585f4075f5714ab522aa582993860c09af912c1:src/script.py"],
        cwd=repo,
    ).decode("utf-8")
    names = {
        "CutRoI",
        "_check",
        "_check_bright_mask",
        "CheckPauseTextLayout",
        "WrapImage",
        "MinusImage",
    }
    namespace = {
        "cv2": cv2,
        "np": np,
        "logger": logging.getLogger("m3-isolated-reference"),
        "_": lambda value: value,
        "brightMaskCache": {},
        "SaveImage": lambda *args: None,
    }
    found = set()
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.FunctionDef) and node.name in names:
            exec(
                compile(
                    ast.Module(body=[node], type_ignores=[]),
                    "fixed-git/script.py",
                    "exec",
                ),
                namespace,
            )
            found.add(node.name)
    if found != names:
        raise AssertionError("旧算法依赖不完整")
    return namespace


def extract_bobber(repo, template):
    source = subprocess.check_output(
        ["git", "show", "6585f4075f5714ab522aa582993860c09af912c1:src/utils.py"],
        cwd=repo,
    ).decode("utf-8")
    node = next(
        node
        for node in ast.walk(ast.parse(source))
        if isinstance(node, ast.FunctionDef) and node.name == "Fishing_DetectBobber"
    )
    namespace = {
        "cv2": cv2,
        "np": np,
        "BOBBER": cv2.cvtColor(template, cv2.COLOR_BGR2GRAY),
    }
    exec(
        compile(ast.Module(body=[node], type_ignores=[]), "fixed-git/utils.py", "exec"),
        namespace,
    )
    return namespace["Fishing_DetectBobber"]
