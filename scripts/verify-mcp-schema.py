#!/usr/bin/env python3
"""Validate synthetic MCP test messages against a pinned upstream schema.

Requires jsonschema only for verification, never for the modeler runtime.
Usage: verify-mcp-schema.py PATH_TO_SCHEMA PATH_TO_SYNTHETIC_TRANSCRIPT
"""
import collections
import hashlib
import importlib.metadata
import json
from pathlib import Path
import sys

from jsonschema import Draft202012Validator, FormatChecker

SOURCE = "https://raw.githubusercontent.com/modelcontextprotocol/modelcontextprotocol/271ecc9accafdd9b83a3c869fa67c22953b2af80/schema/2026-07-28/schema.json"
DIGEST = "ef70b61f99b6d2e5e3b46863822eab08dff6a45bedc7a08914e0e5b133f40203"


def definition(message):
    if "error" in message:
        return (
            "UnsupportedProtocolVersionError"
            if message["error"]["code"] == -32022
            else "JSONRPCErrorResponse"
        )
    if "method" in message:
        return {
            "notifications/subscriptions/acknowledged": "SubscriptionsAcknowledgedNotification",
            "notifications/resources/updated": "ResourceUpdatedNotification",
        }[message["method"]]
    for field, name in (
        ("supportedVersions", "DiscoverResultResponse"),
        ("tools", "ListToolsResultResponse"),
        ("content", "CallToolResultResponse"),
        ("resources", "ListResourcesResultResponse"),
        ("resourceTemplates", "ListResourceTemplatesResultResponse"),
        ("contents", "ReadResourceResultResponse"),
    ):
        if field in message["result"]:
            return name
    return "SubscriptionsListenResultResponse"


def main():
    source = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(source).hexdigest() != DIGEST:
        raise ValueError("MCP schema does not match the reviewed upstream snapshot")
    schema = json.loads(source)
    validators = {}
    counts = collections.Counter()
    maximum = 0
    for line in Path(sys.argv[2]).read_text().splitlines():
        message = json.loads(line)
        name = definition(message)
        if name not in validators:
            validators[name] = Draft202012Validator(
                {
                    "$schema": schema["$schema"],
                    "$defs": schema["$defs"],
                    "$ref": "#/$defs/" + name,
                },
                format_checker=FormatChecker(),
            )
        validators[name].validate(message)
        counts[name] += 1
        maximum = max(maximum, len(line.encode()))
    required = {
        "DiscoverResultResponse", "ListToolsResultResponse", "CallToolResultResponse",
        "ListResourcesResultResponse", "ReadResourceResultResponse",
        "SubscriptionsAcknowledgedNotification", "ResourceUpdatedNotification",
        "SubscriptionsListenResultResponse", "JSONRPCErrorResponse",
        "UnsupportedProtocolVersionError",
    }
    if not required.issubset(counts):
        raise ValueError("Synthetic transcript is missing required protocol paths")
    print(json.dumps({
        "protocolVersion": "2026-07-28", "source": SOURCE, "schemaSha256": DIGEST,
        "validator": "jsonschema", "validatorVersion": importlib.metadata.version("jsonschema"),
        "messages": sum(counts.values()), "bySchema": dict(counts),
        "maximumMessageBytes": maximum, "passed": True,
    }, indent=2))


if __name__ == "__main__":
    main()
