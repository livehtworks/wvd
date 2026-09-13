"""精确大小写检查与显式动态引用登记；不把运行时表达式伪造为已验证路径。"""

from collections import defaultdict


def scan(paths, calls, tasks):
    images = [
        p
        for p in paths
        if p.startswith("resources/images/") and p.lower().endswith(".png")
    ]
    stems = {p[len("resources/images/") : -4]: p for p in images}
    folded = defaultdict(list)
    for key, value in stems.items():
        folded[key.casefold()].append(value)
    refs = []

    def check(value, source, context):
        key = value.replace("\\", "/")
        exact = stems.get(key)
        refs.append(
            {
                "reference": value,
                "source_ref": source,
                "context": context,
                "status": (
                    "EXACT"
                    if exact
                    else (
                        "CASE_MISMATCH"
                        if folded.get(key.casefold())
                        else "MISSING_OR_MOD"
                    )
                ),
                "matches": [exact] if exact else folded.get(key.casefold(), []),
                "migration_owner": "native/games/wvd/vision/WvdAssetResolver",
            }
        )

    for call in calls:
        if call["literal"] is None:
            refs.append(
                {
                    "reference": call["expression"],
                    "source_ref": call["source_ref"],
                    "context": "动态模板表达式",
                    "status": "DYNAMIC_REVIEW",
                    "matches": [],
                    "migration_owner": "native/games/wvd/vision/WvdAssetResolver",
                }
            )
        else:
            check(call["literal"], call["source_ref"], "源码模板调用")

    def walk(value, pointer):
        if isinstance(value, dict):
            for key, item in value.items():
                yield from walk(item, pointer + "/" + key)
        elif isinstance(value, list):
            for i, item in enumerate(value):
                yield from walk(item, pointer + "/" + str(i))
        elif isinstance(value, str):
            yield pointer, value

    # 已能按资产名解析的任务字符串全部登记；未解析的字符串保留在完整任务字段表中。
    for task, value in tasks.items():
        for pointer, text in walk(value, "/" + task):
            if text in stems or text.casefold() in folded:
                check(text, "resources/quest/quest.json#" + pointer, "任务数据")
    return {
        "images": images,
        "references": refs,
        "case_collisions": [v for v in folded.values() if len(v) > 1],
        "limitations": [
            "动态名称、条件/循环拼接与 mod 提供的模板仍需 M3 按真实上下文回归；未读取用户 mod。",
            "任务原始字段完整保存，不能把非模板字符串一概判缺图片。",
        ],
    }
