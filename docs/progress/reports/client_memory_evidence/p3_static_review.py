"""Text-only P3 review; never configures, builds or executes product/C++ tests."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
FILES = [
    "app/application_host.cpp", "app/application_host.h",
    "app/deferred_ui_unload.cpp", "app/deferred_ui_unload.h",
    "app/host_probe.cpp", "app/main.cpp",
    "src/presentation/profile_model.cpp", "src/presentation/profile_model.h",
    "src/presentation/ui_state.cpp", "src/presentation/ui_state.h",
    "tests/unit/profile_model_test.cpp", "tests/unit/deferred_ui_unload_test.cpp",
    "tests/unit/storage_test.cpp", "tests/unit/localization_test.cpp",
    "tests/integration/subscription_ui_probe.cpp",
    "tests/integration/subscription_ui_probe.h",
]
AUTHORIZED_PRIOR = {
    "CMakeLists.txt", *FILES,
    "qml/components/subscription_card.qml",
    "qml/features/subscriptions/subscriptions_page.qml",
    "qml/features/home/home_page.qml", "qml/features/settings/settings_page.qml",
    "resources/i18n/quickclash_zh_cn.ts",
    "docs/baseline/windows_user_guide.md",
    "docs/design/04-界面与窗口生命周期.md", "docs/design/07-订阅与资源更新.md",
    "docs/design/13-性能预算与基准方案.md", "docs/design/QtQuick-Mihomo-完整设计文档.md",
    "docs/progress/README.md", "docs/progress/TASKS.md", "docs/progress/CHECKLISTS.md",
    "docs/progress/DEVLOG.md", "docs/progress/reports/README.md",
    "docs/progress/reports/20261026_client_memory_optimization.md",
}
TOKEN = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|R"(?P<raw>[^\s()\\]{0,16})\([\s\S]*?\)(?P=raw)"'
    r'|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\''
)


def text(name):
    return (ROOT / name).read_text(encoding="utf-8-sig")


def git(*arguments):
    return subprocess.check_output(
        ["git", "-c", "core.quotepath=false", *arguments], cwd=ROOT
    ).decode("utf-8")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    print("TEXT ONLY: delimiters, XML/translation coverage, registration, retention and digests.")
    print("Not C++ syntax/type checking, Qt 6.8 compatibility, GUI or memory evidence.")
    for name in FILES:
        raw = (ROOT / name).read_bytes()
        stripped = TOKEN.sub(lambda match: "\n" * match[0].count("\n"), raw.decode("utf-8-sig"))
        stack = []
        for value in stripped:
            if value in "({[":
                stack.append(value)
            elif value in ")}]":
                assert stack and stack.pop() == {")": "(", "}": "{", "]": "["}[value], name
        assert not stack, name
        print(f"BALANCED {name} sha256={hashlib.sha256(raw).hexdigest()}")

    cmake = text("CMakeLists.txt")
    for name in FILES:
        assert name in cmake, name
    assert "qml/components/subscription_card.qml" in cmake
    assert "QT_QML_SOURCE_TYPENAME" in cmake
    for target in ("profile_model", "deferred_ui_unload", "ui_subscriptions", "ui_subscriptions_zh"):
        assert f"add_test(NAME {target} COMMAND " in cmake, target
    assert "--subscription-ui-test" in text("app/main.cpp")
    for path in (ROOT / "qml").rglob("*.qml"):
        assert not re.search(r"\bui\.profiles\b", path.read_text(encoding="utf-8-sig")), path
    model = text("src/presentation/profile_model.cpp")
    assert "beginResetModel" not in model and "modelReset" not in model
    page = text("qml/features/subscriptions/subscriptions_page.qml")
    assert "reuseItems: true" in page and "cacheBuffer: 0" in page
    assert "Repeater" not in page and "GridLayout" not in page
    assert page.count("parent: Overlay.overlay") == 3
    assert page.count("sourceComponent: SubscriptionCard") == 2
    assert "profiles_" not in text("src/presentation/ui_state.h")
    print("TEXT CONTRACTS: registration, no production ui.profiles, no normal reset, virtual rows/overlay.")

    tree = ET.parse(ROOT / "resources/i18n/quickclash_zh_cn.ts")
    contexts = {
        context.findtext("name"): {
            message.findtext("source"): message.find("translation")
            for message in context.findall("message")
        }
        for context in tree.getroot().findall("context")
    }
    translations = 0
    for name in ("qml/components/subscription_card.qml", "qml/features/settings/settings_page.qml"):
        context = contexts[Path(name).stem]
        sources = re.findall(r'qsTr\(("(?:\\.|[^"\\])*")\)', text(name))
        for literal in sources:
            source = json.loads(literal)
            translation = context.get(source)
            assert translation is not None, (name, source)
            assert translation.get("type") not in ("unfinished", "obsolete", "vanished"), source
            value = "".join(translation.itertext()).strip()
            assert value, source
            assert sorted(re.findall(r"%L?\d+|%n", source)) == sorted(
                re.findall(r"%L?\d+|%n", value)
            ), source
            translations += 1
    print(f"XML: well-formed; {translations} card/settings source entries translated; placeholders match.")

    baseline = json.loads(text("docs/progress/reports/client_memory_evidence/p3_baseline.json"))
    assert len(baseline["entries"]) == 114
    current = git("status", "--porcelain=v1", "--untracked-files=all").splitlines()
    changed = {line[3:] for line in current}
    old = {entry["path"] for entry in baseline["entries"]}
    assert old <= changed, sorted(old - changed)
    assert all("D" not in line[:2] for line in current)
    assert git("rev-parse", "HEAD").strip() == baseline["head"]
    unchanged = 0
    for entry in baseline["entries"]:
        path = ROOT / entry["path"]
        assert path.is_file(), entry["path"]
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest == entry["sha256"]:
            unchanged += 1
        else:
            assert entry["path"] in AUTHORIZED_PRIOR, entry["path"]
            print(f"AUTHORIZED PRIOR EDIT {entry['path']}")
    print(f"RETAINED: all 114 prior paths present/changed; byte-identical={unchanged}; "
          f"authorized continuations={114 - unchanged}; deleted=0; HEAD=45229be; no commit.")
    tracked = sum(not line.startswith("?? ") for line in current)
    print(f"SNAPSHOT: tracked={tracked} untracked={len(current) - tracked} total={len(current)}")
    print("CURRENT STATUS:")
    print("\n".join(current))


if __name__ == "__main__":
    main()
