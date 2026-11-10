"""Exercise the actual Git/CMake build identity and package reader in isolation."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--pwsh", default="pwsh")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = repo / "build/script_tests/build_version" / uuid.uuid4().hex
    source = root / "repo"
    build = source / "out"
    (source / "cmake").mkdir(parents=True)
    build.mkdir()
    env = dict(os.environ, GITHUB_ACTIONS="false")
    step = 0
    passed = 0

    def run(command, *, cwd=root, expected=0):
        nonlocal step
        step += 1
        result = subprocess.run(
            [str(arg) for arg in command], cwd=cwd, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60,
        )
        (root / f"step_{step}.log").write_bytes(result.stdout)
        (root / f"step_{step}.json").write_text(
            json.dumps({"command": [str(arg) for arg in command], "cwd": str(cwd),
                        "exit_code": result.returncode}), encoding="utf-8",
        )
        output = result.stdout.decode("utf-8", errors="replace")
        if (expected == 0 and result.returncode != 0) or (expected != 0 and result.returncode == 0):
            raise AssertionError(f"Unexpected exit {result.returncode}: {command}\n{output}")
        return output

    def git(*arguments):
        return run(["git", "-C", source, *arguments]).strip()

    def require(condition, message):
        if not condition:
            raise AssertionError(message)

    def passed_case(name):
        nonlocal passed
        passed += 1
        print(f"PASS: {name}", flush=True)

    def read_identity(directory=build):
        path = directory / "quickclash_build_info.json"
        # Validate the same metadata reader used by the real packagers.
        run([args.pwsh, "-NoProfile", "-File", repo / "scripts/read_build_info.ps1",
             "-Path", path])
        return json.loads(path.read_text(encoding="utf-8"))

    def build_identity():
        run([args.cmake, "--build", build, "--target", "quickclash_build_info"])
        return read_identity()

    def write_identity(source_path, directory, *extra):
        directory.mkdir(parents=True, exist_ok=True)
        run([args.cmake, f"-DQUICKCLASH_SOURCE_DIR={source_path}",
             f"-DQUICKCLASH_BINARY_DIR={directory}", "-DQUICKCLASH_BASE_VERSION=0.1.1",
             *extra, "-P", repo / "cmake/write_build_info.cmake"])
        return read_identity(directory)

    def reject_read(info, message, expected_commit=""):
        path = root / "invalid.json"
        path.write_text(json.dumps(info), encoding="utf-8")
        output = run([args.pwsh, "-NoProfile", "-File", repo / "scripts/read_build_info.ps1",
                      "-Path", path, "-ExpectedCommit", expected_commit], expected=1)
        require(message in output, f"Expected '{message}': {output}")

    for name in ("build_version.cmake", "write_build_info.cmake"):
        shutil.copyfile(repo / "cmake" / name, source / "cmake" / name)
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.24)\n"
        "project(quickclash VERSION 0.1.1 LANGUAGES NONE)\n"
        "include(cmake/build_version.cmake)\n", encoding="utf-8",
    )
    (source / ".gitignore").write_text("/out/\n", encoding="utf-8")
    git("init", "--quiet", "--initial-branch=main")
    git("config", "user.name", "Build Version Fixture")
    git("config", "user.email", "fixture@example.invalid")
    git("config", "core.autocrlf", "false")
    excludes = root / "empty_git_ignore"
    excludes.write_text("", encoding="utf-8")
    git("config", "core.excludesFile", str(excludes))
    git("add", ".")
    git("-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "initial version fixture")
    commit = git("rev-parse", "HEAD")
    ninja = shutil.which(args.ninja)
    require(ninja is not None, "Ninja is required for incremental build checks.")
    run([args.cmake, "-S", source, "-B", build, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}"])
    info = build_identity()
    require(info["application_version"] == info["base_version"] == "0.1.1" and
            info["source_state"] == "clean" and info["commit"] == commit,
            "Clean checkout identity differs.")
    header = build / "generated/quickclash_build_info.h"
    require(f'#define QUICKCLASH_VERSION "{info["application_version"]}"' in
            header.read_text(encoding="utf-8"), "Header and JSON identity differ.")
    passed_case("clean checkout keeps a plain application version and separate full source commit")

    json_path = build / "quickclash_build_info.json"
    times = (header.stat().st_mtime_ns, json_path.stat().st_mtime_ns)
    build_identity()
    require(times == (header.stat().st_mtime_ns, json_path.stat().st_mtime_ns),
            "Unchanged identity rewrote build outputs.")
    passed_case("unchanged incremental build preserves header and JSON modification times")

    with (source / "CMakeLists.txt").open("a", encoding="utf-8") as output:
        output.write("# local change\n")
    info = build_identity()
    require(info["source_state"] == "dirty" and info["application_version"] == "0.1.1",
            "Unstaged changes were reported clean or altered the application version.")
    git("add", "CMakeLists.txt")
    require(build_identity()["source_state"] == "dirty", "Staged changes were reported clean.")
    passed_case("unstaged and staged changes refresh dirty state without manual configuration")

    git("-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "next version fixture")
    next_commit = git("rev-parse", "HEAD")
    info = build_identity()
    require(next_commit != commit and info["commit"] == next_commit and
            info["source_state"] == "clean" and info["application_version"] == "0.1.1",
            "New commit did not replace the old identity or altered the application version.")
    passed_case("new commit refreshes identity and clears dirty state in the same build directory")

    (source / "new_source.txt").write_text("new source\n", encoding="utf-8")
    require(build_identity()["source_state"] == "dirty", "Untracked source was reported clean.")
    (source / "new_source.txt").unlink()
    (build / "ignored.txt").write_text("ignored output\n", encoding="utf-8")
    require(build_identity()["source_state"] == "clean", "Ignored output dirtied source identity.")
    passed_case("untracked source is dirty while ignored build output is excluded")

    git("-c", "tag.gpgsign=false", "tag", "-a", "v0.1.1", "-m", "version fixture")
    git("checkout", "--quiet", "--detach", "v0.1.1")
    tagged = build_identity()
    require(all(tagged[key] == info[key] for key in
                ("application_version", "commit", "source_state")),
            "Branch and tag at the same commit have different build identities.")
    passed_case("branch and annotated tag at the same commit share the same build identity")

    worktree = root / "worktree"
    git("worktree", "add", "--quiet", "--detach", str(worktree), "HEAD")
    require(write_identity(worktree, worktree / "out")["commit"] == next_commit,
            "Worktree .git indirection was not resolved.")
    passed_case("Git worktree indirection resolves to the correct commit")

    archive = build / "archive"
    archive.mkdir()
    unknown = write_identity(archive, archive / "out")
    require(unknown["application_version"] == "0.1.1" and
            unknown["commit"] == "" and unknown["source_state"] == "unknown",
            "Source archive inherited its parent checkout commit.")
    unknown = write_identity(source, build / "without_git", "-DCMAKE_DISABLE_FIND_PACKAGE_Git=TRUE")
    require(unknown["source_state"] == "unknown" and unknown["application_version"] == "0.1.1",
            "Unavailable Git claimed a known identity or altered the application version.")
    passed_case("source archive and unavailable Git keep the version and mark source identity unknown")

    env.update(GITHUB_ACTIONS="true", GITHUB_RUN_ID="12345", GITHUB_RUN_ATTEMPT="1")
    info = build_identity()
    require(info["ci_run_id"] == "12345" and info["ci_run_attempt"] == "1",
            "CI run identity was not recorded.")
    header_time = header.stat().st_mtime_ns
    env["GITHUB_RUN_ATTEMPT"] = "2"
    info = build_identity()
    require(info["ci_run_attempt"] == "2" and header.stat().st_mtime_ns == header_time,
            "CI retry changed the compiled source version or lost its attempt.")
    passed_case("CI run and retry update metadata without changing the application version")

    reject_read(info, "Package source commit differs", "f" * 40)
    reject_read(dict(info, application_version="0.1.2"), "Application version differs")
    reject_read(dict(info, application_version=f"0.1.1+g{next_commit[:12]}"),
                "Application version differs")
    reject_read(dict(unknown, application_version="0.1.1+unknown"), "Application version differs")
    reject_read(dict(unknown, commit=next_commit), "Unknown build identity must not claim")
    reject_read(dict(info, commit="bad"), "full source commit")
    reject_read(dict(info, ci_run_attempt=""), "invalid CI run ID")
    passed_case("packaging rejects commit, version, full-SHA and CI-identity mismatches")

    env["GITHUB_RUN_ATTEMPT"] = "invalid"
    output = run([args.cmake, "--build", build, "--target", "quickclash_build_info"], expected=1)
    require("numeric run ID and attempt" in output, "Malformed CI identity was not rejected.")
    passed_case("malformed CI run metadata fails the build")
    print(f"PASS: {passed} build identity scenarios; evidence at {root}", flush=True)


if __name__ == "__main__":
    main()
