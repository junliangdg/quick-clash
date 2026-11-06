"""Build the self-contained UI concept preview using only Python's standard library."""

import argparse
import html
import json
import re
from html.parser import HTMLParser
from pathlib import Path


class EnglishPreview(HTMLParser):
    """Translate static copy without duplicating the preview's DOM or interactions."""

    attributes = {"aria-label", "title", "placeholder", "data-demo"}

    def __init__(self, translations):
        super().__init__(convert_charrefs=False)
        self.translations = translations
        self.output = []

    def translate(self, text):
        key = text.strip()
        if key in self.translations:
            return text.replace(key, html.escape(self.translations[key], quote=False))
        if re.search(r"[\u3400-\u9fff]", key) and key != "简体中文":
            raise ValueError(f"Missing preview translation: {key}")
        return text

    def handle_starttag(self, tag, attrs):
        raw = self.get_starttag_text()
        for name, value in attrs:
            if name in self.attributes and value in self.translations:
                raw = raw.replace(
                    f'{name}="{html.escape(value, quote=True)}"',
                    f'{name}="{html.escape(self.translations[value], quote=True)}"',
                )
            elif name in self.attributes and value:
                self.translate(value)
        self.output.append(raw)

    def handle_endtag(self, tag):
        self.output.append(f"</{tag}>")

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_data(self, data):
        self.output.append(self.translate(data))

    def handle_comment(self, data):
        self.output.append(f"<!--{data}-->")

    def handle_entityref(self, name):
        self.output.append(f"&{name};")

    def handle_charref(self, name):
        self.output.append(f"&#{name};")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without writing files")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent.parent / "docs" / "reference"
    fragment = (directory / "quickclash_minimal_preview.fragment.html").read_text(
        encoding="utf-8"
    )
    translations = json.loads(
        (directory / "quickclash_preview_translations.json").read_text(encoding="utf-8")
    )
    if len(set(translations.values())) != len(translations):
        parser.exit(1, "Preview translations must have unique English values.\n")
    for source, translated in translations.items():
        if sorted(re.findall(r"\{\d+\}", source)) != sorted(
            re.findall(r"\{\d+\}", translated)
        ):
            parser.exit(1, f"Mismatched preview translation parameters: {source}\n")
    static, behavior = fragment.split("<style>", 1)
    localized = EnglishPreview(translations)
    localized.feed(static)
    # The language options keep their native names; UI strings are translated.
    fragment = "".join(localized.output) + "<style>" + behavior
    catalog = json.dumps(translations, ensure_ascii=False, indent=2).replace("<", "\\u003c")
    if fragment.count("/* preview_translations */") != 1:
        parser.exit(1, "Expected exactly one preview translation marker.\n")
    fragment = fragment.replace("/* preview_translations */", catalog)
    icons = directory.parent.parent / "resources" / "icons" / "navigation"

    def navigation_icon(match):
        svg = (icons / (match.group(1) + ".svg")).read_text(encoding="utf-8").strip()
        return svg.replace("<svg ", '<svg aria-hidden="true" ', 1).replace(
            'stroke="#000000"', 'stroke="currentColor"'
        )

    fragment = re.sub(r"<!-- navigation_icon:([a-z_]+) -->", navigation_icon, fragment)

    def asset_icon(match):
        svg = (icons.parent / (match.group(1) + ".svg")).read_text(
            encoding="utf-8"
        ).strip()
        return svg.replace("<svg", '<svg aria-hidden="true"', 1).replace(
            'stroke="#000000"', 'stroke="currentColor"'
        )

    fragment = re.sub(r"<!-- asset_icon:([a-z_]+/[a-z_]+) -->", asset_icon, fragment)
    mono = (icons.parent / "brand" / "qcat_tray_dark.svg").read_text(encoding="utf-8")
    fragment = fragment.replace(
        "<!-- brand_mono -->",
        mono.strip().replace("<svg", '<svg aria-hidden="true"', 1).replace(
            'fill="#28313e"', 'fill="currentColor"'
        ),
    )
    header = '''<!doctype html>
<html lang="en-US">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; font-src 'none'; base-uri 'none'; form-action 'none'">
<title>QuickClash · Interactive preview</title>
<style>
html {color-scheme:light dark;background:light-dark(#FAFAFB,#101114);}
body {margin:0;padding:16px;}
@media(width < 600px) {body {padding:0;}}
</style>
</head>
<body>
'''
    expected = header + fragment.rstrip() + "\n</body>\n</html>\n"
    output = directory / "quickclash_minimal_preview.html"
    if args.check:
        if not output.exists() or output.read_text(encoding="utf-8") != expected:
            parser.exit(1, "Preview is stale; run scripts/build_ui_preview.py.\n")
        print("UI preview source/export check passed.")
        return
    output.write_text(expected, encoding="utf-8", newline="\n")
    print(f"Built {output.name} ({len(expected.encode('utf-8'))} bytes).")


if __name__ == "__main__":
    main()
