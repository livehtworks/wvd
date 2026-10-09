"""Shared byte/record/output bounds for read-only diagnostic analysis."""

import hashlib
import json
from pathlib import Path


class InputBudget:
    def __init__(self, maximum_bytes=128*1024*1024, maximum_records=100000, maximum_line=1024*1024):
        self.maximum_bytes = maximum_bytes
        self.maximum_records = maximum_records
        self.maximum_line = maximum_line
        self.bytes = self.records = 0

    def charge(self, count):
        self.bytes += count
        if self.bytes > self.maximum_bytes:
            raise ValueError("DIAGNOSTIC_TOTAL_INPUT_BUDGET")

    def lines(self, path, provenance=None):
        digest = hashlib.sha256()
        count = 0
        with Path(path).open("rb") as stream:
            while True:
                raw = stream.readline(self.maximum_line + 1)
                if not raw:
                    break
                if len(raw) > self.maximum_line:
                    raise ValueError("DIAGNOSTIC_LINE_BUDGET:" + str(path))
                self.charge(len(raw))
                self.records += 1
                if self.records > self.maximum_records:
                    raise ValueError("DIAGNOSTIC_RECORD_BUDGET")
                if not raw.endswith(b"\n"):
                    raise ValueError("DIAGNOSTIC_TRUNCATED_LINE:" + str(path))
                count += len(raw)
                digest.update(raw)
                yield raw.decode("utf-8-sig" if count == len(raw) else "utf-8")
        if provenance is not None:
            provenance.update(bytes=count, sha256=digest.hexdigest())

    def json(self, path):
        path = Path(path)
        if path.stat().st_size > 32*1024*1024:
            raise ValueError("DIAGNOSTIC_JSON_BUDGET:" + str(path))
        with path.open("rb") as source:
            data = source.read(32*1024*1024+1)
        if len(data) > 32*1024*1024:
            raise ValueError("DIAGNOSTIC_JSON_BUDGET:" + str(path))
        self.charge(len(data))
        return json.loads(data.decode("utf-8-sig")), {"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()}


def write_report(path, report, maximum=32*1024*1024):
    path = Path(path)
    if path.exists():
        raise ValueError("OUTPUT_ALREADY_EXISTS")
    partial = path.with_suffix(path.suffix+".partial")
    encoder = json.JSONEncoder(ensure_ascii=False,indent=2)
    total = 0
    with partial.open("xb") as stream:
        for chunk in encoder.iterencode(report):
            content = chunk.encode("utf-8")
            total += len(content)
            if total > maximum:
                raise ValueError("DIAGNOSTIC_OUTPUT_BUDGET")
            stream.write(content)
        if total + 1 > maximum:
            raise ValueError("DIAGNOSTIC_OUTPUT_BUDGET")
        stream.write(b"\n")
    partial.rename(path)
