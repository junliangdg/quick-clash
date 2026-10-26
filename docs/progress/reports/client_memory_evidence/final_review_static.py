"""Cross-batch text/tool checks only; never configure, build or run product code."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
EVIDENCE = ROOT / "docs/progress/reports/client_memory_evidence"
HEAD = "45229be4960d00d86d10f2b6c280105f99b3ac7f"
CONTINUATIONS = {
    "docs/progress/README.md", "docs/progress/TASKS.md",
    "docs/progress/CHECKLISTS.md", "docs/progress/DEVLOG.md",
    "docs/progress/reports/README.md",
    "docs/progress/reports/20261026_client_memory_optimization.md",
}
TOKEN = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|R"(?P<raw>[^\s()\\]{0,16})\([\s\S]*?\)(?P=raw)"'
    r'|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\''
)


def text(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def git(*args):
    return subprocess.check_output(
        ["git", "-c", "core.quotepath=false", *args], cwd=ROOT
    ).decode("utf-8")


def digest(path):
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def run(label, command, discard_output=False):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (EVIDENCE / f"final_review_{label}.log").write_bytes(
        (b"" if discard_output and result.returncode == 0 else result.stdout)
        + result.stderr + f"\nEXIT_CODE={result.returncode}\n".encode()
    )
    print(f"{label}: exit={result.returncode}; command={command!r}")
    if result.returncode:
        print(result.stdout.decode("utf-8", errors="replace"))
        print(result.stderr.decode("utf-8", errors="replace"))
    return result.returncode


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    print("TEXT ONLY: formatting, QML parsing, script parsing, registration and retention.")
    print("NOT C++ compilation/type checking, MOC/QML registration execution or product acceptance.")
    status = git("status", "--porcelain=v1", "--untracked-files=all").splitlines()
    paths = [line[3:] for line in status]
    assert git("rev-parse", "HEAD").strip() == HEAD
    assert not any("D" in line[:2] for line in status)
    baseline = json.loads(text(EVIDENCE.relative_to(ROOT) / "final_review_baseline_manifest.json"))
    assert len(baseline) == 140
    original = {entry["path"] for entry in baseline}
    assert original <= set(paths)
    continued = []
    for entry in baseline:
        if digest(entry["path"]) != entry["sha256"].lower():
            assert entry["path"] in CONTINUATIONS, entry["path"]
            continued.append(entry["path"])
    missing = [path for path in paths if not path.isascii()]
    assert len(missing) == 9 and all(path.startswith("docs/design/") for path in missing)
    supplement = EVIDENCE / "final_review_utf8_supplement.json"
    if not supplement.exists():
        supplement.write_text(json.dumps({
            "note": "Post-review UTF-8 supplement, NOT initial SHA evidence for these 9 paths.",
            "entries": [{"path": path, "sha256": digest(path)} for path in missing],
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for entry in json.loads(supplement.read_text(encoding="utf-8"))["entries"]:
        assert digest(entry["path"]) == entry["sha256"], entry["path"]
    assert original | set(missing) <= set(paths)
    print(f"RETENTION: 149 prior paths present; initial SHA coverage=140; "
          f"byte-identical={140 - len(continued)}; documented continuations={continued!r}; "
          "9 non-ASCII design paths only have post-review SHA; deleted=0; HEAD unchanged.")

    cpp = sorted(path for path in paths if Path(path).suffix in (".cpp", ".h"))
    qml = sorted(path for path in paths if path.endswith(".qml"))
    ps = sorted(path for path in paths if path.endswith(".ps1"))
    cmake = text("CMakeLists.txt")
    header_only = {"src/infra/io/bounded_read.h", "src/infra/io/device_stream.h"}
    for path in cpp:
        assert path in cmake or path in header_only, path
        source = text(path)
        stripped = TOKEN.sub(lambda match: "\n" * match[0].count("\n"), source)
        stack = []
        for character in stripped:
            if character in "({[":
                stack.append(character)
            elif character in ")}]":
                assert stack and stack.pop() == {")": "(", "}": "{", "]": "["}[character], path
        assert not stack, path
        for include in re.findall(r'^#include "([^"]+)"', source, flags=re.M):
            if include.endswith(".moc"):
                assert "Q_OBJECT" in source, path
                continue
            assert any(candidate.is_file() for candidate in (
                ROOT / Path(path).parent / include, ROOT / "src" / include,
                ROOT / "app" / include, ROOT / "tests/integration" / include,
            )), (path, include)
    for name in ("bounded_read", "geo_resource_writer", "provider_stream", "latest_mailbox",
                 "runtime_publication", "runtime_list_model", "profile_model",
                 "deferred_ui_unload", "ui_runtime_lists", "ui_subscriptions", "ui_subscriptions_zh"):
        assert f"add_test(NAME {name} COMMAND " in cmake, name
    assert "QT_QML_SOURCE_TYPENAME" in cmake
    for path in (ROOT / "qml").rglob("*.qml"):
        assert not re.search(r"\bui\.profiles\b", path.read_text(encoding="utf-8-sig")), path
    for path in paths:
        if path.endswith(".json"):
            json.loads(text(path))
        elif path.endswith(".ts"):
            ET.parse(ROOT / path)
    catalog = ET.parse(ROOT / "resources/i18n/quickclash_zh_cn.ts")
    contexts = {
        context.findtext("name"): {
            message.findtext("source"): message.find("translation")
            for message in context.findall("message")
        }
        for context in catalog.getroot().findall("context")
    }
    translations = 0
    for path in qml:
        for literal in re.findall(r'qsTr\(("(?:\\.|[^"\\])*")\)', text(path)):
            source = json.loads(literal)
            translation = contexts[Path(path).stem].get(source)
            assert translation is not None, (path, source)
            assert translation.get("type") not in ("unfinished", "obsolete", "vanished")
            value = "".join(translation.itertext()).strip()
            assert value, (path, source)
            assert sorted(re.findall(r"%L?\d+|%n", source)) == sorted(
                re.findall(r"%L?\d+|%n", value)
            ), (path, source)
            translations += 1
    print(f"TRANSLATIONS: {translations} changed-QML source uses have entries/placeholders; text only.")
    print(f"TEXT: {len(cpp)} C++ delimiters/local includes/registration; JSON/XML parsed; "
          "no production ui.profiles consumer. These checks cannot prove types or linking.")
    codes = {}
    codes["format"] = run("format", [
        "C:/Qt/Tools/QtCreator/bin/clang/bin/clang-format.exe", "--dry-run", "--Werror", *cpp
    ])
    for index, path in enumerate(qml):
        codes[f"qml_{index}"] = run(f"qml_{index}", [
            "C:/Qt/6.9.0/msvc2022_64/bin/qmlformat.exe", path
        ], discard_output=True)
    parser = (
        "$ErrorActionPreference='Stop'; $files=" +
        ",".join("'" + path.replace("'", "''") + "'" for path in ps) +
        "; foreach($file in $files) { $tokens=$null; $errors=$null; "
        "[void][System.Management.Automation.Language.Parser]::ParseFile("
        "(Join-Path (Get-Location) $file),[ref]$tokens,[ref]$errors); "
        "if($errors.Count) { $errors | Out-String | Write-Output; exit 1 }; "
        "Write-Output ('PARSED '+$file) }; exit 0"
    )
    codes["script_parse"] = run("script_parse", ["pwsh", "-NoProfile", "-Command", parser])
    for label, script, args in (
        ("design_check", "scripts/build_design_docs.ps1", ["-Check"]),
        ("layout", "scripts/check_source_layout.ps1", []),
        ("progress", "scripts/check_progress.ps1", []),
    ):
        codes[label] = run(label, ["pwsh", "-NoProfile", "-File", script, *args])
    codes["diff"] = run("diff", ["git", "diff", "--check"])
    print(f"CHECK_COUNTS: C++={len(cpp)} QML={len(qml)} PowerShell={len(ps)}")
    print("RESULT_CODES=" + json.dumps(codes, sort_keys=True))
    assert all(code == 0 for name, code in codes.items() if name != "progress")
    assert codes["progress"] == 1, "Reassess progress output; never hide additional failures."
    # Native PowerShell diagnostics can contain mixed terminal encodings; match ASCII bytes.
    progress = (EVIDENCE / "final_review_progress.log").read_bytes()
    assert b"proxy_memory_fixes" in progress and b"quickclash.exe" in progress
    assert progress.count(b"ERROR: ") == 1
    print("Progress exits 1; inspect its raw log to confirm the sole known historical package gap.")
    print("PRODUCT/C++/GUI/IPC/ACL/memory savings/100 MB/leak conclusions: NotRun.")


if __name__ == "__main__":
    main()
