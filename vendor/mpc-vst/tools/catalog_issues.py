#!/usr/bin/env python3
"""Open one issue per failing release listed in problems.json (from tools/catalog_build.py), on this catalog repo.

  tools/catalog_issues.py [--problems catalog/dist/problems.json] [--repo owner/name] [--dry-run]

A failing version is already left out of catalog.json, so the previous good version stays the latest. This only makes
the failure visible. Issues are keyed by title, so a problem that persists is reported once. Needs the `gh` CLI
(GITHUB_TOKEN with issues: write in Actions). Standard library only.
"""
import argparse
import json
import subprocess
import sys


def title(p):
    return "Catalog: %s %s failed validation" % (p["id"], p["tag"]) if p.get("tag") else "Catalog: %s cannot be read" % p["id"]


def plan(problems, existing_titles):
    """-> [(title, body)] for problems that have no open issue yet (one per title; errors merged)."""
    out = {}
    for p in problems:
        t = title(p)
        if t in existing_titles:
            continue
        out.setdefault(t, []).append(p["error"])
    return [(t, "The nightly catalog build excluded this release.\n\n" + "\n".join("- " + e for e in errs) +
             "\n\nFix the release (or the registry entry); this issue can be closed once the build passes.")
            for t, errs in out.items()]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--problems", default="catalog/dist/problems.json")
    ap.add_argument("--repo", help="owner/name (default: the current repo)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    problems = json.load(open(a.problems))
    repo = ["--repo", a.repo] if a.repo else []
    listing = subprocess.run(["gh", "issue", "list", *repo, "--state", "open", "--search", "Catalog: in:title", "--json", "title", "--limit", "200"],
                             capture_output=True, text=True, check=True).stdout
    existing = {i["title"] for i in json.loads(listing)}
    for t, body in plan(problems, existing):
        print("issue:", t)
        if not a.dry_run:
            subprocess.run(["gh", "issue", "create", *repo, "--title", t, "--body", body], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
