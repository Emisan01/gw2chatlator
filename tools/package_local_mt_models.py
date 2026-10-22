#!/usr/bin/env python3
"""Convert and package the OPUS-MT models listed by src/core/local_mt_protocol.cpp.

Examples:
  python tools/package_local_mt_models.py --pairs de-en
  python tools/package_local_mt_models.py --convert --pairs de-en en-de

Converted models are kept under local/translation-spike/models (ignored by Git). The resulting
GW2Keyboard-OPUS-MT-<pair>.zip files go to build/dist for upload as assets of the matching release.
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOG_CPP = ROOT / "src" / "core" / "local_mt_protocol.cpp"
DEFAULT_MODELS = ROOT / "local" / "translation-spike" / "models"
DEFAULT_OUTPUT = ROOT / "build" / "dist"
ROW = re.compile(r'\{"([^\"]+)",\s*"([^\"]+)",\s*"([^\"]*)",\s*"([^\"]+)"(?:,\s*([0-9]+))?\}')


def catalog():
    text = CATALOG_CPP.read_text(encoding="utf-8")
    models = {
        pair: {
            "pair": pair,
            "model": model,
            "source_prefix": prefix,
            "license": license_name,
            "size_bytes": int(size_str) if size_str else 0,
        }
        for pair, model, prefix, license_name, size_str in ROW.findall(text)
    }
    if not models:
        raise RuntimeError(f"Could not read model catalog from {CATALOG_CPP}")
    return models


def convert(model, target):
    converter = shutil.which("ct2-transformers-converter")
    if not converter:
        raise RuntimeError("ct2-transformers-converter was not found; install ctranslate2 and transformers first")
    target.mkdir(parents=True, exist_ok=True)
    if not (target / "model.bin").is_file():
        command = [
            converter,
            "--model",
            model["model"],
            "--output_dir",
            str(target),
            "--quantization",
            "int8",
            "--force",
        ]
        print("[*] Converting", model["pair"], "from", model["model"], flush=True)
        subprocess.run(command, check=True)
    # CTranslate2 converts the weights and vocabulary, but our helper tokenizes with SentencePiece itself.
    # Keep the exact upstream tokenizer files beside each converted model, including on partial retries.
    from huggingface_hub import snapshot_download
    snapshot = Path(snapshot_download(repo_id=model["model"], allow_patterns=["source.spm", "target.spm"]))
    for name in ("source.spm", "target.spm"):
        source = snapshot / name
        if not source.is_file():
            raise RuntimeError(f"{model['pair']} checkpoint is missing {name}")
        if not (target / name).is_file():
            shutil.copy2(source, target / name)


def notice(model):
    license_id = model["license"]
    license_url = (
        "https://creativecommons.org/licenses/by/4.0/" if license_id == "CC-BY-4.0"
        else "https://www.apache.org/licenses/LICENSE-2.0"
    )
    return (
        f"OPUS-MT model package: {model['pair']}\n"
        "Attribution: Language Technology Research Group, University of Helsinki (Helsinki-NLP).\n"
        "OPUS-MT reference: Jörg Tiedemann and Santhosh Thottingal (2020), "
        "OPUS-MT — Building open translation services for the World, EAMT 2020. "
        "https://aclanthology.org/2020.eamt-1.61/\n"
        f"Upstream model: https://huggingface.co/{model['model']}\n"
        f"License declared by this model card: {license_id}\n"
        f"License text: {license_url}\n"
        "Changes: converted from the upstream Transformers checkpoint to CTranslate2 INT8 format;\n"
        "SentencePiece files and language-pair metadata are included for local inference.\n"
    )


def package(model, model_dir, output_dir):
    required = ("model.bin", "source.spm", "target.spm", "config.json")
    missing = [name for name in required if not (model_dir / name).is_file()]
    if missing:
        raise RuntimeError(f"{model['pair']} is missing: {', '.join(missing)}")

    metadata = model_dir / "pair.json"
    metadata.write_text(json.dumps(model, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    output_dir.mkdir(parents=True, exist_ok=True)
    archive = output_dir / f"GW2Keyboard-OPUS-MT-{model['pair']}.zip"
    files = [*required, "shared_vocabulary.json", "pair.json"]
    with zipfile.ZipFile(archive, "w") as z:
        for name in files:
            source = model_dir / name
            if source.is_file():
                compress = zipfile.ZIP_STORED if name == "model.bin" else zipfile.ZIP_DEFLATED
                z.write(source, f"translate/{model['pair']}/{name}", compress_type=compress)
        z.writestr(f"translate/{model['pair']}/MODEL_NOTICE.txt", notice(model))
        if model["license"] == "Apache-2.0":
            apache = ROOT / "third_party" / "licenses" / "Apache-2.0.txt"
            if not apache.is_file():
                raise RuntimeError(f"Apache-2.0 text is missing: {apache}")
            z.write(apache, f"translate/{model['pair']}/LICENSE-Apache-2.0.txt")
    print(f"[PASS] {archive} ({archive.stat().st_size:,} bytes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--models-dir", type=Path, default=DEFAULT_MODELS)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--pairs", nargs="+", help="pair IDs to process; default: converted models present in models-dir")
    parser.add_argument("--convert", action="store_true", help="download and convert missing models from Hugging Face")
    args = parser.parse_args()

    models = catalog()
    pairs = args.pairs or sorted(pair for pair in models if (args.models_dir / pair / "model.bin").is_file())
    if not pairs:
        raise RuntimeError("No converted model found; pass --pairs and optionally --convert")
    for pair in pairs:
        if pair not in models:
            raise RuntimeError(f"Unknown model pair: {pair}")
        model_dir = args.models_dir / pair
        if args.convert:
            convert(models[pair], model_dir)
        package(models[pair], model_dir, args.output_dir)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"[FAIL] {error}", file=sys.stderr)
        sys.exit(1)
