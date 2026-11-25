"""Package an existing Release/WebGPU SDK and compile its small TypeScript loaders."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
FX = HERE.parents[3]
RUNTIME_FILES = (
    "Radient.mjs", "Radient.d.mts", "RadientWasm.mjs", "RadientWasm.wasm",
    "runtime.mjs", "byte_access.mjs", "RadientBrowser.mjs", "RadientBrowser.d.mts",
)
PACKAGE_FILES = ("package.json", "package-lock.json", "README.md", "copy-assets.mjs",
                 "index.mts", "browser.mts", "runtime-loader.mts", "tsconfig.json")


def read_cache(build):
    values = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        match = re.match(r"([^/#][^:]*):[^=]+=(.*)", line)
        if match:
            values[match[1]] = match[2]
    if values.get("CMAKE_BUILD_TYPE") != "Release":
        raise ValueError("The npm SDK requires a Release build")
    if values.get("DILIGENT_NO_WEBGPU", "").upper() not in ("OFF", "FALSE", "0"):
        raise ValueError("The npm SDK requires WebGPU")
    if values.get("EMSCRIPTEN", "").upper() not in ("ON", "TRUE", "1"):
        raise ValueError("The npm SDK requires an Emscripten build")
    return values


def revision(path):
    def git(*args):
        return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()
    return {"commit": git("rev-parse", "HEAD"),
            "dirty": bool(git("status", "--porcelain", "--untracked-files=normal"))}


def copy_licenses(source, destination):
    # Preserve original paths and texts, including notices alongside license files.
    # Skip build/cache metadata when walking nested third-party source checkouts.
    count = 0
    for directory, dirs, files in os.walk(source):
        dirs[:] = [d for d in dirs if d not in (".git", "node_modules", "__pycache__", "CMakeFiles")]
        for name in files:
            if not re.search(r"(^|[._-])(LICENSE|LICENCE|COPYING|COPYRIGHT|NOTICE)([._-]|$)", name, re.I):
                continue
            # A filename match alone also selects check_copyright.py and
            # license-checker.cfg. Copy notice documents, not maintenance tools.
            if Path(name).suffix.lower() not in ("", ".txt", ".md", ".rst", ".html",
                                                  ".h", ".license", ".mit", ".info-zip"):
                continue
            path = Path(directory) / name
            target = destination / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            count += 1
    return count


def write_build_info(cache, output, sources, runtime):
    info = {"configuration": "Release", "sourceCheckouts": {name: revision(path) for name, path in sources.items()},
            "runtimeSHA256": {name: hashlib.sha256((output / "dist" / name).read_bytes()).hexdigest() for name in runtime}}
    engine = Path(cache["CMAKE_HOME_DIRECTORY"])
    if (engine / ".gitmodules").is_file():
        info["engine"] = revision(engine)
    (output / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")


def collect_licenses(build, cache, output, sources):
    for name, path in sources.items():
        license_dir = output / "licenses" / name
        license_dir.mkdir(parents=True)
        shutil.copyfile(path / "License.txt", license_dir / "License.txt")
        copy_licenses(path / "ThirdParty", license_dir / "ThirdParty")
    copy_licenses(build / "_deps", output / "licenses/build-dependencies")
    # FetchContent may reuse sources outside this build's _deps directory.
    for key in ("Dawn_SOURCE_DIR", "EnTT_SOURCE_DIR"):
        if key in cache:
            copy_licenses(Path(cache[key]), output / "licenses" / key.removesuffix("_SOURCE_DIR"))
    # IJG's redistribution terms are in README rather than a LICENSE file.
    jpeg_notice = sources["DiligentTools"] / "ThirdParty/libjpeg-9e/README"
    jpeg_destination = output / "licenses/DiligentTools/ThirdParty/libjpeg-9e/README"
    jpeg_destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(jpeg_notice, jpeg_destination)
    emscripten = Path(cache["CMAKE_TOOLCHAIN_FILE"]).parents[3]
    copy_licenses(emscripten / "system/lib", output / "licenses/emscripten/system/lib")
    for name in ("LICENSE", "system/lib/libc/musl/COPYRIGHT"):
        path = emscripten / name
        if not path.is_file():
            raise ValueError(f"Missing Emscripten license: {path}")
        destination = output / "licenses/emscripten" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)


def compile_loaders(output):
    # Install the pinned compiler in the staging directory, never in the source
    # checkout. npm's files allowlist excludes these build inputs and node_modules.
    npm = shutil.which("npm.cmd") or shutil.which("npm")
    if not npm:
        raise RuntimeError("npm is required to compile the package loaders")
    subprocess.run([npm, "ci", "--include=dev", "--ignore-scripts", "--no-audit", "--no-fund"],
                   cwd=output, check=True)
    subprocess.run(["node", "node_modules/typescript/bin/tsc", "--project", "tsconfig.json"],
                   cwd=output, check=True)


def assemble(build, output, version):
    cache = read_cache(build)
    if output.exists():
        raise ValueError(f"Package output must not already exist: {output}")
    generated = build / "DiligentFX/Radient/bindings/javascript/generated"
    for name in RUNTIME_FILES:
        if not (generated / name).is_file():
            raise ValueError(f"Missing {name}; build RadientWasm before assembling")
    # Require a fresh directory: stale files must not silently enter an SDK release.
    output.mkdir(parents=True, exist_ok=False)
    dist = output / "dist"
    dist.mkdir()
    for name in PACKAGE_FILES:
        shutil.copyfile(HERE / name, output / name)
    runtime = list(RUNTIME_FILES) + [p.name for p in generated.glob("RadientWasm.worker.*")]
    for name in runtime:
        shutil.copyfile(generated / name, dist / name)
    compile_loaders(output)
    manifest = json.loads((output / "package.json").read_text())
    if version:
        manifest["version"] = version
    (output / "package.json").write_text(json.dumps(manifest, indent=2) + "\n")
    shutil.copyfile(FX / "License.txt", output / "LICENSE")
    sources = {name: FX.parent / name for name in ("DiligentCore", "DiligentTools", "DiligentFX")}
    write_build_info(cache, output, sources, runtime)
    collect_licenses(build, cache, output, sources)
    print(f"Assembled {manifest['name']}@{manifest['version']} in {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True, help="Top-level CMake build directory")
    parser.add_argument("--output", type=Path, required=True, help="New package directory")
    parser.add_argument("--version", help="Override the development version for a release")
    args = parser.parse_args()
    assemble(args.build_dir.resolve(), args.output.resolve(), args.version)


if __name__ == "__main__":
    main()
