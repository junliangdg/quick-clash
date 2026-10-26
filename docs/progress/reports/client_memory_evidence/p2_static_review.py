"""Text-only review helper. Does not compile, link or execute product/C++ tests."""

import ast
import hashlib
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
FILES = [
    "src/infra/io/device_stream.h",
    "src/infra/config/config_metadata.h",
    "src/infra/config/config_metadata.cpp",
    "src/infra/config/config_preflight.h",
    "src/infra/config/config_preflight.cpp",
    "src/infra/config/config_provider_stream.cpp",
    "src/infra/storage/json_store.h",
    "src/infra/storage/json_store.cpp",
    "src/infra/storage/provider_stage.h",
    "src/infra/storage/provider_stage.cpp",
    "src/application/desktop_providers.cpp",
    "src/application/desktop_runtime.h",
    "src/application/desktop_runtime.cpp",
    "tests/unit/provider_stream_test.cpp",
    "tests/integration/runtime_test.cpp",
]
TOKEN = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|R"(?P<raw>[^\s()\\]{0,16})\([\s\S]*?\)(?P=raw)"'
    r'|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\''
)


def git(*arguments):
    return subprocess.check_output(
        ["git", "-c", "core.quotepath=false", *arguments], cwd=ROOT
    ).decode("utf-8")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    print("TEXT ONLY: delimiters, registration, path retention and source digests.")
    print("Not C++ syntax/type checking or functional/security/performance evidence.")
    for name in FILES:
        raw = (ROOT / name).read_bytes()
        text = raw.decode("utf-8-sig")
        stripped = TOKEN.sub(lambda match: "\n" * match[0].count("\n"), text)
        stack = []
        for value in stripped:
            if value in "({[":
                stack.append(value)
            elif value in ")}]":
                assert stack and stack.pop() == {")": "(", "}": "{", "]": "["}[value], name
        assert not stack, name
        print(f"BALANCED {name} sha256={hashlib.sha256(raw).hexdigest()}")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    for name in ("src/infra/config/config_provider_stream.cpp",
                 "src/infra/storage/provider_stage.cpp", "tests/unit/provider_stream_test.cpp"):
        assert name in cmake, name
    assert "add_test(NAME provider_stream COMMAND provider_stream_test)" in cmake
    providers = (ROOT / "src/application/desktop_providers.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/application/desktop_runtime.h").read_text(encoding="utf-8")
    runtime = (ROOT / "src/application/desktop_runtime.cpp").read_text(encoding="utf-8")
    assert "QHash<QString, QByteArray>" not in providers
    assert "expandProviderFiles(" in providers and "expandProviders(" not in providers
    assert "validatedSemantic" not in header + runtime
    control = runtime.split("QJsonObject DesktopRuntime::controlState() const", 1)[1].split(
        "QJsonObject DesktopRuntime::snapshot() const", 1
    )[0]
    assert "configLifecycle" not in control
    publication = runtime.split("void DesktopRuntime::publish()", 1)[1].split(
        "void DesktopRuntime::flushData()", 1
    )[0]
    assert "snapshot()" not in publication
    print("TEXT CONTRACTS: provider file API, private trust, diagnostic-only counters, registration.")

    prior = (ROOT / "docs/progress/reports/client_memory_evidence/p1b_final_worktree.log").read_text(
        encoding="utf-8-sig"
    ).splitlines()
    tracked = []
    for line in prior:
        if line.startswith(" M ") and len(tracked) < 41:
            name = line[3:]
            if name.startswith('"'):
                name = ast.literal_eval(name).encode("latin-1").decode("utf-8")
            tracked.append(name)
    untracked = {line[3:] for line in prior if line.startswith("?? ") and not line.endswith("/")}
    baseline = set(tracked) | untracked
    assert len(tracked) == 41 and len(untracked) == 49 and len(baseline) == 90
    current = git("status", "--porcelain=v1", "--untracked-files=all").splitlines()
    changed = {line[3:] for line in current}
    assert baseline <= changed, sorted(baseline - changed)
    assert all((ROOT / name).is_file() for name in baseline)
    assert all("D" not in line[:2] for line in current)
    assert git("rev-parse", "--short", "HEAD").strip() == "45229be"
    print("RETAINED: all 90 prior changed paths remain changed/present; deleted=0; HEAD=45229be.")
    print("This path check is not a byte-for-byte comparison with an unavailable initial snapshot.")
    tracked_count = sum(not line.startswith("?? ") for line in current)
    print(f"Tracked={tracked_count} Untracked={len(current) - tracked_count} Total={len(current)}")
    print("CURRENT STATUS:")
    print("\n".join(current))


if __name__ == "__main__":
    main()
