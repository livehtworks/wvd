"""Private child-process paths shared by the build and diagnostic entry points."""
import json
import os
from pathlib import Path
import stat

PATHS = json.loads(Path(__file__).with_name("tool-paths.json").read_text(encoding="utf-8"))


def checked_directory(root, relative, *, create=True):
    root = Path(root).absolute()
    relative = Path(relative)
    if root == Path(root.anchor) or relative.is_absolute() or ".." in relative.parts:
        raise RuntimeError("TOOL_STATE_PATH_INVALID")
    path = root / relative
    if not path.is_relative_to(root / ".local") or path == root / ".local":
        raise RuntimeError("TOOL_STATE_PATH_OUTSIDE_PRIVATE_ROOT")
    for ancestor in [path, *path.parents]:
        if ancestor.exists() or ancestor.is_symlink():
            info = ancestor.lstat()
            if ancestor.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
                raise RuntimeError("TOOL_STATE_REPARSE_FORBIDDEN:" + str(ancestor))
            if not stat.S_ISDIR(info.st_mode):
                raise RuntimeError("TOOL_STATE_DIRECTORY_REQUIRED:" + str(ancestor))
    if create:
        path.mkdir(parents=True, exist_ok=True)
    return path


def tool_environment(root, inherited=None):
    environment = dict(os.environ if inherited is None else inherited)
    for key, relative in PATHS.items():
        environment[key] = str(checked_directory(root, relative))
    return environment
