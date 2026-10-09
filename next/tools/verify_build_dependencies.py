"""Read-only archive-derived verification at every native build and packaging."""

import hashlib
import json
import dependencies
import prepare_native


def verify():
    consumed = {"headers": dependencies.prepare(readonly=True),
                "native": prepare_native.prepare(readonly=True)}
    return {name: hashlib.sha256(json.dumps(value, sort_keys=True,
             separators=(",", ":")).encode("utf-8")).hexdigest()
            for name, value in consumed.items()}


if __name__ == "__main__":
    print(json.dumps(verify(), sort_keys=True))
