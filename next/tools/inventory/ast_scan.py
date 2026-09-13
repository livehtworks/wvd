"""只解析 AST，绝不 import 旧模块，避免日志清理、GUI 和设备副作用。"""

import ast
import hashlib
from mapping import record


def expr(node):
    return ast.unparse(node) if node is not None else ""


def owned_nodes(node):
    for child in ast.iter_child_nodes(node):
        if isinstance(
            child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)
        ):
            continue
        yield child
        yield from owned_nodes(child)


class Scanner(ast.NodeVisitor):
    def __init__(self, file, text):
        self.file, self.text, self.scope = file, text, []
        self.data_fields = []
        (
            self.functions,
            self.entries,
            self.branches,
            self.calls,
            self.runtime_fields,
            self.template_calls,
        ) = ([], [], [], [], [], [])

    def ref(self, node):
        return f"{self.file}:{node.lineno}"

    def visit_ClassDef(self, node):
        self.scope.append(node.name)
        self.generic_visit(node)
        self.scope.pop()

    def visit_FunctionDef(self, node):
        self.scope.append(node.name)
        symbol = ".".join(self.scope)
        own = list(owned_nodes(node))
        calls = [n for n in own if isinstance(n, ast.Call)]
        doc = ast.get_docstring(node)
        row = record(
            f"function:{self.file}:{symbol}:{node.lineno}",
            "function",
            doc
            or "声明 "
            + node.name
            + "("
            + expr(node.args)
            + ")；直接调用："
            + ", ".join(dict.fromkeys(expr(c.func) for c in calls))[:600],
            self.ref(node),
            self.file,
            symbol,
        )
        row.update(
            signature=expr(node.args),
            end_line=node.end_lineno,
            body_sha256=hashlib.sha256(
                ast.get_source_segment(self.text, node).encode()
            ).hexdigest(),
            decorators=[expr(d) for d in node.decorator_list],
        )
        self.functions.append(row)
        self.generic_visit(node)
        self.scope.pop()

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_If(self, node):
        symbol = ".".join(self.scope) or "<module>"
        row = record(
            f"branch:{self.file}:{node.lineno}",
            "branch",
            "条件分支，保留判断顺序与真假出口",
            self.ref(node),
            self.file,
            symbol,
        )
        row.update(
            condition=expr(node.test),
            true_lines=[n.lineno for n in node.body],
            false_lines=[n.lineno for n in node.orelse],
            task_dispatch=any(
                x in expr(node.test)
                for x in ("FARM_TARGET", "_TYPE", "quest.", "targetInfo")
            ),
        )
        self.branches.append(row)
        self.generic_visit(node)

    def visit_Match(self, node):
        # QuestFarm 和消息循环都用 match/case；只扫 if 会漏掉真正的任务分发入口。
        for index, case in enumerate(node.cases):
            subject, pattern, guard = (
                expr(node.subject),
                expr(case.pattern),
                expr(case.guard),
            )
            row = record(
                f"branch:{self.file}:{case.pattern.lineno}:case",
                "branch",
                "模式分发；保留 subject、case 顺序、guard 与业务出口",
                self.ref(case.pattern),
                self.file,
                ".".join(self.scope) or "<module>",
            )
            row.update(
                condition=f"match {subject}: case {pattern}"
                + (f" if {guard}" if guard else ""),
                branch_type="match_case",
                subject=subject,
                pattern=pattern,
                guard=guard,
                true_lines=[n.lineno for n in case.body],
                false_lines=(
                    [node.cases[index + 1].pattern.lineno]
                    if index + 1 < len(node.cases)
                    else []
                ),
                task_dispatch="FARM_TARGET" in subject,
            )
            self.branches.append(row)
        self.generic_visit(node)

    def visit_Call(self, node):
        name = expr(node.func)
        symbol = ".".join(self.scope) or "<module>"
        self.calls.append(
            {
                "caller": symbol,
                "file": self.file,
                "line": node.lineno,
                "callee_expression": name,
                "arguments": [expr(a) for a in node.args],
                "resolution": "PENDING_STATIC_RESOLUTION",
            }
        )
        commands = [
            kw
            for kw in node.keywords
            if kw.arg
            in (
                "command",
                "validatecommand",
                "postcommand",
                "xscrollcommand",
                "yscrollcommand",
            )
        ]
        binding = isinstance(node.func, ast.Attribute) and node.func.attr in (
            "bind",
            "bind_all",
            "bind_class",
            "tag_bind",
            "protocol",
            "trace_add",
            "add_command",
        )
        cli = name.endswith("add_argument")
        widget = name.split(".")[-1] in {
            "Button",
            "Checkbutton",
            "Radiobutton",
            "Entry",
            "Combobox",
            "Spinbox",
            "Scale",
            "Listbox",
            "Menu",
            "Scrollbar",
            "ScrolledText",
            "Text",
        }
        if commands or binding or cli or widget:
            row = record(
                f"entry:{self.file}:{node.lineno}:{node.col_offset}",
                "entry",
                "GUI/启动事件注册，保持回调和参数",
                self.ref(node),
                self.file,
                symbol,
            )
            row.update(
                registration=expr(node),
                callbacks=[expr(k.value) for k in commands],
                binding=binding,
                cli=cli,
                widget=widget,
                bound_variables={
                    k.arg: expr(k.value)
                    for k in node.keywords
                    if k.arg in ("variable", "textvariable", "values", "state")
                },
            )
            self.entries.append(row)
        short = name.split(".")[-1]
        index = (
            0
            if short == "LoadTemplateImage"
            else (
                1
                if short
                in ("CheckIf", "CheckHow", "CheckIf_MultiRect", "CheckTemplateInRoi")
                else None
            )
        )
        if index is not None and len(node.args) > index:
            arg = node.args[index]
            self.template_calls.append(
                {
                    "source_ref": self.ref(node),
                    "expression": expr(arg),
                    "literal": (
                        arg.value
                        if isinstance(arg, ast.Constant) and isinstance(arg.value, str)
                        else None
                    ),
                }
            )
        self.generic_visit(node)

        if (
            isinstance(node.func, ast.Attribute)
            and node.func.attr in ("get", "setdefault", "pop")
            and node.args
        ):
            key = node.args[0]
            if isinstance(key, ast.Constant) and isinstance(key.value, str):
                self.add_data_field(node, key.value, node.func.attr, expr(node))

    def add_data_field(self, node, key, access, expression):
        row = record(
            f"data-field:{self.file}:{node.lineno}:{node.col_offset}:{key}",
            "data_field",
            "源码字典字段的构造/读取/写入；保留默认表达式，不能全当作持久化配置",
            self.ref(node),
            self.file,
            ".".join(self.scope) or "<module>",
        )
        row.update(key=key, access=access, expression=expression)
        self.data_fields.append(row)

    def visit_Dict(self, node):
        for key, value in zip(node.keys, node.values):
            if isinstance(key, ast.Constant) and isinstance(key.value, str):
                self.add_data_field(key, key.value, "construct", expr(value))
        self.generic_visit(node)

    def visit_Subscript(self, node):
        if isinstance(node.slice, ast.Constant) and isinstance(node.slice.value, str):
            self.add_data_field(
                node, node.slice.value, type(node.ctx).__name__, expr(node)
            )
        self.generic_visit(node)

    def visit_Attribute(self, node):
        if (
            isinstance(node.ctx, ast.Store)
            and isinstance(node.value, ast.Name)
            and node.value.id in ("self", "setting", "runtimeContext", "quest")
        ):
            self.runtime_fields.append(
                {
                    "source_ref": self.ref(node),
                    "scope": ".".join(self.scope),
                    "receiver": node.value.id,
                    "field": node.attr,
                }
            )
        self.generic_visit(node)


def config_rows(tree, file):
    rows = []
    for node in tree.body:
        if not isinstance(node, ast.Assign) or not any(
            isinstance(t, ast.Name) and t.id == "CONFIG_VAR_LIST" for t in node.targets
        ):
            continue
        for entry in node.value.elts:
            category, name, kind, default = entry.elts
            key = ast.literal_eval(name)
            # 所有导入只有 storage 一个写入权威，设备字段的消费者不等于导入所有者。
            path = "native/storage"
            row = record(
                "config:" + key,
                "config",
                "配置表定义，保留默认值、分类与旧字段拼写",
                f"{file}:{entry.lineno}",
                destination=(path, "LegacyConfigImporter", "M4-CONFIG-" + key),
            )
            try:
                value = ast.literal_eval(default)
            except (ValueError, TypeError):
                value = {
                    "expression": expr(default),
                    "translation": "保留 gettext msgid，不执行翻译",
                }
            row.update(
                name=key,
                category=ast.literal_eval(category),
                type=expr(kind),
                default=value,
                default_source=expr(default),
            )
            rows.append(row)
    return rows


def resolve_calls(calls, functions):
    symbols = {
        (f["source_ref"].split(":")[0], f["legacy_symbol"]): f["id"] for f in functions
    }
    for call in calls:
        name = call["callee_expression"]
        parts = call["caller"].split(".")
        found = []
        if name.startswith("self."):
            # 方法访问：在当前限定类作用域逐层解析，不把动态对象调用猜成唯一函数。
            for i in range(len(parts) - 1, 0, -1):
                key = (call["file"], ".".join(parts[:i]) + "." + name[5:])
                if key in symbols:
                    found = [symbols[key]]
                    break
        elif name.isidentifier():
            for i in range(len(parts), -1, -1):
                key = (call["file"], ".".join(parts[:i] + [name]))
                if key in symbols:
                    found = [symbols[key]]
                    break
            # 跨模块同名不等于实际绑定；没有证明 import/别名关系的保留动态项。
        call["targets"] = found
        call["resolution"] = "STATIC" if found else "EXTERNAL_OR_DYNAMIC"
