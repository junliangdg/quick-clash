"""Build the self-contained UI concept preview using only Python's standard library."""

import argparse
import base64
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without writing files")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent.parent / "docs" / "reference"
    fragment = (directory / "quickclash_minimal_preview.fragment.html").read_text(
        encoding="utf-8"
    )
    icons = directory.parent.parent / "resources" / "icons" / "navigation"

    def navigation_icon(match):
        svg = (icons / (match.group(1) + ".svg")).read_text(encoding="utf-8").strip()
        return svg.replace("<svg ", '<svg aria-hidden="true" ', 1).replace(
            'stroke="#000000"', 'stroke="currentColor"'
        )

    fragment = re.sub(r"<!-- navigation_icon:([a-z_]+) -->", navigation_icon, fragment)
    brand = (icons.parent / "brand" / "qcat.svg").read_bytes()
    brand_uri = "data:image/svg+xml;base64," + base64.b64encode(brand).decode("ascii")
    fragment = fragment.replace("<!-- brand_icon -->", f'<img src="{brand_uri}" alt="" width="24" height="24">')
    header = '''<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; font-src 'none'; base-uri 'none'; form-action 'none'">
<title>QuickClash · 方案 C 概念预览</title>
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
