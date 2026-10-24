# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a CycloneDX 1.6 document against the specification's own JSON Schema.

    python -I tests/package/validate_sbom.py <sbom.cdx.json> <schema-dir>

<schema-dir> holds bom-1.6.schema.json, spdx.schema.json and jsf-0.82.schema.json
from https://github.com/CycloneDX/specification (the 1.6 tag); the BOM schema
refers to the other two. Needs jsonschema and referencing
(.github/requirements/sbom.txt). Exit 0 = valid, 1 = invalid, 2 = bad invocation.
"""

import json
import sys
from pathlib import Path

from jsonschema import Draft7Validator
from referencing import Registry, Resource


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    document = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
    schema_dir = Path(argv[2])

    schemas = {
        name: json.loads((schema_dir / name).read_text(encoding="utf-8"))
        for name in ("bom-1.6.schema.json", "spdx.schema.json", "jsf-0.82.schema.json")
    }
    registry = Registry()
    for name, schema in schemas.items():
        resource = Resource.from_contents(schema)
        registry = registry.with_resource(schema["$id"], resource)
        registry = registry.with_resource(name, resource)

    validator = Draft7Validator(schemas["bom-1.6.schema.json"], registry=registry)
    errors = sorted(validator.iter_errors(document), key=lambda error: list(error.path))
    for error in errors[:25]:
        location = "/".join(str(part) for part in error.path) or "(document)"
        print(f"{location}: {error.message[:300]}", file=sys.stderr)
    if errors:
        print(f"{len(errors)} schema error(s) in {argv[1]}", file=sys.stderr)
        return 1
    print(f"{argv[1]}: valid CycloneDX {document.get('specVersion')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
