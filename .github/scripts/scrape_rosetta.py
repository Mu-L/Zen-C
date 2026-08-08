#!/usr/bin/env python3
"""Rosetta Code Zen C auto-scraper.

Scrapes Zen C solutions from Rosetta Code and writes one .zc file per *valid*
solution. Robust handling:

  * A task's "Zen C" section often contains several <lang>/<syntaxhighlight>
    blocks. They may be (a) separate complete solutions, (b) parts of one
    program, or (c) a module plus a main that imports it.
  * We split blocks, classify each as a "program" (has a top-level `fn main`)
    or a "module" (no main). Every program is kept -- but only if it passes
    validation (zc transpile + build when zc is available, otherwise a
    structural check). Separate solutions therefore become separate files
    ({title}.zc, {title}_2.zc, ...) instead of being merged into one broken
    file.
  * A program that does `import "NAME.zc"` where a module block declares
    `/* NAME.zc */` gets that module inlined (the import line is replaced by
    the module body), turning a multi-file solution into a single valid file.
  * Blocks are NOT filtered by their lang= attribute: contributors commonly
    mislabel Zen C blocks (e.g. lang="rust"), but the section is already
    scoped to the Zen C header.
"""

import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.request as urllib_request
from urllib.parse import quote as urllib_quote

API_URL = "https://rosettacode.org/w/api.php"
CATEGORY = "Category:Zen_C"
UA = "Zen-C-AutoScraper/1.0"

ZC_BINARY = os.environ.get("ZC_BINARY", "./zc")
ZC_ROOT = os.environ.get("ZC_ROOT", os.getcwd())


def fetch_json(url):
    req = urllib_request.Request(url, headers={"User-Agent": UA})
    with urllib_request.urlopen(req) as response:
        return json.loads(response.read().decode())


def wiki_to_markdown(wiki_text, page_url):
    # Convert <lang> or <syntaxhighlight> blocks
    def repl_code(match):
        return "\n```zc\n%s\n```\n" % match.group(1).strip()

    md = re.sub(
        r"(?:<lang[^>]*>|<syntaxhighlight[^>]*>|<highlight[^>]*>)(.*?)(?:</lang>|</syntaxhighlight>|</highlight>)",
        repl_code,
        wiki_text,
        flags=re.DOTALL | re.IGNORECASE,
    )

    md = re.sub(r"\{\{out\}\}", r"\n**Output:**\n", md, flags=re.IGNORECASE)

    def repl_pre(match):
        return "\n```\n%s\n```\n" % match.group(1).strip()

    md = re.sub(r"<pre[^>]*>(.*?)</pre>", repl_pre, md, flags=re.DOTALL | re.IGNORECASE)

    def repl_header(match):
        level = len(match.group(1))
        content = match.group(2).strip()
        return "\n%s %s\n" % ("#" * level, content)

    md = re.sub(r"^(=+)\s*(.*?)\s*\1\s*$", repl_header, md, flags=re.MULTILINE)

    md = re.sub(r"\[\[([^\]|]+)\|([^\]]+)\]\]", r"[\2](https://rosettacode.org/wiki/\1)", md)
    md = re.sub(r"\[\[([^\]]+)\]\]", r"[\1](https://rosettacode.org/wiki/\1)", md)
    md = re.sub(r"'''(.*?)'''", r"**\1**", md)
    md = re.sub(r"''(.*?)''", r"*\1*", md)
    md = re.sub(r"\n{3,}", "\n\n", md)
    return md.strip()


# --- Block classification ---------------------------------------------------

BLOCK_RE = re.compile(
    r'<(?:lang|syntaxhighlight|highlight)\s+lang=["\']?([^"\'>]*)["\']?[^>]*>(.*?)'
    r"</(?:lang|syntaxhighlight|highlight)>",
    re.DOTALL | re.IGNORECASE,
)
MAIN_RE = re.compile(r"(?m)^\s*fn\s+main\s*\(")
MODULE_HEADER_RE = re.compile(r"^\s*/\*\s*([\w.-]+\.zc)\s*\*/")
IMPORT_RE = re.compile(r'^\s*import\s+"([^"]+)"\s*;?\s*$', re.MULTILINE)


def split_blocks(section):
    """Return a list of (lang, code) blocks from the Zen C section."""
    blocks = []
    for m in BLOCK_RE.finditer(section):
        lang = m.group(1) or ""
        code = m.group(2).strip()
        if code:
            blocks.append((lang, code))
    return blocks


def is_program(code):
    return MAIN_RE.search(code) is not None


def module_name(code):
    m = MODULE_HEADER_RE.match(code)
    return m.group(1) if m else None


def inline_modules(program, modules):
    """Replace `import "NAME.zc"` in program with the body of module NAME."""
    by_name = {}
    for mod in modules:
        name = module_name(mod)
        if name and name not in by_name:
            by_name[name] = mod

    def repl(match):
        name = match.group(1)
        body = by_name.get(name)
        if body is None:
            return match.group(0)  # not a module we have; leave the import alone
        return body.strip()

    return IMPORT_RE.sub(repl, program)


def structural_check(code):
    """Cheap check used when zc is unavailable. Returns (ok, reason)."""
    if not code.strip():
        return False, "empty code block"
    mains = MAIN_RE.findall(code)
    if len(mains) != 1:
        return False, "expected exactly one top-level fn main (got %d)" % len(mains)
    for m in IMPORT_RE.finditer(code):
        name = m.group(1)
        if name.endswith(".zc") and "/" not in name:
            return False, "dangling module import: %s" % name
    return True, ""


def validate_code(code):
    """Validate a candidate program. Returns (ok, reason).

    When zc is available we run `zc transpile` and `zc build`; otherwise we
    fall back to a structural check so the scraper still works without the
    compiler binary.
    """
    if not os.path.exists(ZC_BINARY):
        return structural_check(code)

    with tempfile.TemporaryDirectory(prefix="zc_rosetta_") as td:
        src = os.path.join(td, "prog.zc")
        out_c = os.path.join(td, "prog.c")
        out_bin = os.path.join(td, "prog")
        with open(src, "w", encoding="utf-8") as f:
            f.write(code)

        env = dict(os.environ)
        if ZC_ROOT:
            env["ZC_ROOT"] = ZC_ROOT

        try:
            r1 = subprocess.run(
                [ZC_BINARY, "transpile", src, "-o", out_c],
                capture_output=True, text=True, env=env, timeout=120,
            )
            if r1.returncode != 0:
                return False, _first_error(r1.stderr)
            r2 = subprocess.run(
                [ZC_BINARY, "build", src, "-o", out_bin],
                capture_output=True, text=True, env=env, timeout=300,
            )
            if r2.returncode != 0:
                return False, _first_error(r2.stderr)
        except (OSError, subprocess.TimeoutExpired) as exc:
            return False, "validation error: %s" % exc

    return True, ""


def _first_error(stderr):
    for line in stderr.splitlines():
        line = line.strip()
        if line.startswith("error:"):
            return line
    return (stderr or "validation failed").strip().splitlines()[-1][:200]


def main():
    print("-> Fetching tasks from Rosetta Code...")

    pages = []
    cm_continue = {}
    while True:
        url = (
            API_URL + "?action=query&list=categorymembers&cmtitle=" + CATEGORY
            + "&cmlimit=500&format=json"
        )
        for key, val in cm_continue.items():
            url += "&%s=%s" % (key, urllib_quote(val))
        data = fetch_json(url)
        pages.extend(data["query"]["categorymembers"])
        cm_continue = data.get("continue", {})
        if not cm_continue:
            break

    print("-> %d tasks found" % len(pages))

    os.makedirs("examples/examples/rosetta", exist_ok=True)
    os.makedirs("website_out", exist_ok=True)

    scraped = 0
    kept = 0
    skipped = 0

    for page in pages:
        title = page["title"]
        pageid = page["pageid"]

        content_url = (
            API_URL + "?action=query&prop=revisions&rvprop=content&rvslots=main"
            + "&pageids=%d&format=json" % pageid
        )
        content_data = fetch_json(content_url)
        text = content_data["query"]["pages"][str(pageid)]["revisions"][0]["slots"]["main"]["*"]

        parts = re.split(r"==\{\{header\|Zen[ _-]?C\}\}==", text, flags=re.IGNORECASE)

        if len(parts) <= 1:
            print("-> Could not find Zen C header in: %s" % title)
            continue

        zen_c_section = parts[1].split("=={{header|")[0].strip()
        blocks = split_blocks(zen_c_section)

        if not blocks:
            print("-> Found header, but NO code block in: %s" % title)
            continue

        scraped += 1
        safe_title = title.replace("/", "_").replace(" ", "_")
        page_url = "https://rosettacode.org/wiki/" + title.replace(" ", "_")
        history_url = page_url + "?action=history"

        programs = [code for _, code in blocks if is_program(code)]
        modules = [code for _, code in blocks if not is_program(code)]

        if not programs:
            print("-> No complete program in Zen C section of: %s" % title)
            continue

        written = 0
        for idx, program in enumerate(programs):
            candidate = inline_modules(program, modules)
            ok, reason = validate_code(candidate)
            if not ok:
                print("  ! %s: block %d rejected (%s)" % (title, idx + 1, reason))
                continue

            suffix = "" if written == 0 else "_%d" % (written + 1)
            zc_filename = "examples/examples/rosetta/%s%s.zc" % (safe_title, suffix)
            with open(zc_filename, "w", encoding="utf-8") as f:
                f.write(candidate + "\n")
            written += 1
            kept += 1

        if written == 0:
            skipped += 1
            print("-> No valid program for: %s" % title)
            continue

        # Documentation (markdown) mirrors the whole Zen C section.
        md_filename = "website_out/%s.md" % safe_title
        content_md = wiki_to_markdown(zen_c_section, page_url)
        with open(md_filename, "w", encoding="utf-8") as f:
            f.write("+++\n")
            f.write('title = "%s"\n' % title)
            f.write("+++\n\n")
            f.write("# %s\n\n" % title)
            f.write(content_md + "\n\n")
            f.write("---\n")
            f.write(
                "**Attribution:** This is a community solution for the Rosetta Code task "
                "[**%s**](%s) in Zen C.\n\n" % (title, page_url)
            )
            f.write(
                "*This article uses material from the Rosetta Code article **%s**, which is "
                "released under the [GNU Free Documentation License "
                "1.3](https://www.gnu.org/licenses/fdl-1.3.html). A list of the original "
                "authors can be found in the [page history](%s).*\n"
                % (title, history_url)
            )

        print("-> Scraped: %s (%d blocks, %d valid program(s))" % (title, len(blocks), written))

    print("--------------------------------------------------")
    print("Tasks with a Zen C section : %d" % scraped)
    print("Programs kept              : %d" % kept)
    print("Tasks with no valid program: %d" % skipped)


if __name__ == "__main__":
    main()
