"""Auditable JSON Schema Draft 2020-12 subset used by the Phase 1 artifacts.

No external validator is available in the offline build environment.  This
module therefore deliberately accepts only the keywords used by the pinned
schemas, rejects every unknown keyword, resolves local/file/URN references,
and validates both schema structure and instances.  It is not presented as a
general-purpose JSON Schema implementation.
"""

from __future__ import annotations

from collections.abc import Mapping
from math import isfinite
from pathlib import Path
from typing import Any

DIALECT = "https://json-schema.org/draft/2020-12/schema"
ANNOTATIONS = {"$schema", "$id", "$defs", "title", "description", "default"}
ASSERTIONS = {
    "$ref", "type", "const", "enum", "minimum", "maximum", "required",
    "properties", "additionalProperties", "items", "minItems",
    "anyOf", "not", "contains",
}
ALLOWED = ANNOTATIONS | ASSERTIONS
TYPES = {"object", "array", "string", "integer", "number", "boolean", "null"}


def _fail(location: str, message: str) -> None:
    raise ValueError(f"{location}: {message}")


def validate_schema(schema: Any, location: str = "$") -> None:
    if not isinstance(schema, dict):
        _fail(location, "schema must be an object")
    unknown = set(schema) - ALLOWED
    if unknown:
        _fail(location, f"unsupported/unvalidated keywords {sorted(unknown)}")
    if "$schema" in schema and schema["$schema"] != DIALECT:
        _fail(location, "unsupported dialect")
    for keyword in ("$schema", "$id", "title", "description", "$ref"):
        if keyword in schema and not isinstance(schema[keyword], str):
            _fail(location, f"{keyword} must be a string")
    if "type" in schema and schema["type"] not in TYPES:
        _fail(location, "type is unsupported")
    if "enum" in schema and (not isinstance(schema["enum"], list) or not schema["enum"]):
        _fail(location, "enum must be a nonempty array")
    for keyword in ("minimum", "maximum"):
        value = schema.get(keyword)
        if value is not None and (isinstance(value, bool) or not isinstance(value, (int, float)) or not isfinite(value)):
            _fail(location, f"{keyword} must be a finite number")
    if "minimum" in schema and "maximum" in schema and schema["minimum"] > schema["maximum"]:
        _fail(location, "minimum exceeds maximum")
    if "required" in schema:
        required = schema["required"]
        if (not isinstance(required, list) or not all(isinstance(item, str) for item in required)
                or len(required) != len(set(required))):
            _fail(location, "required must contain unique strings")
    properties = schema.get("properties")
    if properties is not None:
        if not isinstance(properties, dict) or not all(isinstance(key, str) for key in properties):
            _fail(location, "properties must be an object")
        for key, child in properties.items():
            validate_schema(child, f"{location}.properties[{key!r}]")
    definitions = schema.get("$defs")
    if definitions is not None:
        if not isinstance(definitions, dict):
            _fail(location, "$defs must be an object")
        for key, child in definitions.items():
            validate_schema(child, f"{location}.$defs[{key!r}]")
    additional = schema.get("additionalProperties")
    if additional is not None and not isinstance(additional, (bool, dict)):
        _fail(location, "additionalProperties must be boolean or schema")
    if isinstance(additional, dict):
        validate_schema(additional, f"{location}.additionalProperties")
    if "items" in schema:
        validate_schema(schema["items"], f"{location}.items")
    if "anyOf" in schema:
        alternatives = schema["anyOf"]
        if not isinstance(alternatives, list) or not alternatives:
            _fail(location, "anyOf must be a nonempty array")
        for index, child in enumerate(alternatives):
            validate_schema(child, f"{location}.anyOf[{index}]")
    if "not" in schema:
        validate_schema(schema["not"], f"{location}.not")
    if "contains" in schema:
        validate_schema(schema["contains"], f"{location}.contains")
    if "minItems" in schema and (isinstance(schema["minItems"], bool)
                                  or not isinstance(schema["minItems"], int)
                                  or schema["minItems"] < 0):
        _fail(location, "minItems must be a nonnegative integer")


def validate_instance(value: Any, schema: Mapping[str, Any], *, registry: Mapping[str, Mapping[str, Any]] | None = None,
                      location: str = "$") -> None:
    if "$ref" in schema:
        if registry is None or schema["$ref"] not in registry:
            _fail(location, f"unresolved reference {schema['$ref']}")
        validate_instance(value, registry[schema["$ref"]], registry=registry, location=location)
    if "anyOf" in schema:
        matches = 0
        for child in schema["anyOf"]:
            try:
                validate_instance(value, child, registry=registry, location=location)
                matches += 1
            except ValueError:
                pass
        if matches == 0:
            _fail(location, "does not match any anyOf alternative")
    if "not" in schema:
        try:
            validate_instance(value, schema["not"], registry=registry, location=location)
        except ValueError:
            pass
        else:
            _fail(location, "matches forbidden not schema")
    expected = schema.get("type")
    matches = {
        "object": lambda item: isinstance(item, dict),
        "array": lambda item: isinstance(item, list),
        "string": lambda item: isinstance(item, str),
        "integer": lambda item: isinstance(item, int) and not isinstance(item, bool),
        "number": lambda item: isinstance(item, (int, float)) and not isinstance(item, bool) and isfinite(item),
        "boolean": lambda item: isinstance(item, bool),
        "null": lambda item: item is None,
    }
    if expected is not None and not matches[expected](value):
        _fail(location, f"expected {expected}")
    if "const" in schema and value != schema["const"]:
        _fail(location, "does not equal const")
    if "enum" in schema and value not in schema["enum"]:
        _fail(location, "is not in enum")
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            _fail(location, "is below minimum")
        if "maximum" in schema and value > schema["maximum"]:
            _fail(location, "is above maximum")
    if isinstance(value, dict):
        properties = schema.get("properties", {})
        missing = set(schema.get("required", [])) - set(value)
        if missing:
            _fail(location, f"missing required properties {sorted(missing)}")
        for key, child in properties.items():
            if key in value:
                validate_instance(value[key], child, registry=registry, location=f"{location}.{key}")
        extras = set(value) - set(properties)
        additional = schema.get("additionalProperties", True)
        if extras and additional is False:
            _fail(location, f"unexpected properties {sorted(extras)}")
        if isinstance(additional, dict):
            for key in extras:
                validate_instance(value[key], additional, registry=registry, location=f"{location}.{key}")
    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            _fail(location, "has too few items")
        if "items" in schema:
            for index, item in enumerate(value):
                validate_instance(item, schema["items"], registry=registry, location=f"{location}[{index}]")
        if "contains" in schema:
            if not any(_instance_matches(item, schema["contains"], registry) for item in value):
                _fail(location, "does not contain a required matching item")


def _instance_matches(value: Any, schema: Mapping[str, Any], registry: Mapping[str, Mapping[str, Any]] | None) -> bool:
    try:
        validate_instance(value, schema, registry=registry)
        return True
    except ValueError:
        return False


def load_registry(paths: list[Path]) -> dict[str, Mapping[str, Any]]:
    import json
    registry: dict[str, Mapping[str, Any]] = {}
    for path in paths:
        schema = json.loads(path.read_text(encoding="utf-8"))
        validate_schema(schema, str(path))
        schema_id = schema.get("$id")
        if schema_id:
            if schema_id in registry:
                _fail(str(path), f"duplicate $id {schema_id}")
            registry[schema_id] = schema
            for name, definition in schema.get("$defs", {}).items():
                registry[f"{schema_id}#/$defs/{name}"] = definition
        registry[path.name] = schema
        registry[f"schemas/{path.name}"] = schema
    return registry
