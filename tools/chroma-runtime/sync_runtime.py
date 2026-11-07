#!/usr/bin/env python3
"""
Razer Chroma Runtime Sync & Verification Script
Checks the official Razer Chroma installer and verifies/updates the bundled runtime DLLs.
"""

import os
import sys
import json
import hashlib
import urllib.request
import subprocess
from datetime import datetime, timezone

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
VERSION_JSON = os.path.join(SCRIPT_DIR, "version.json")
X64_DLL = os.path.join(SCRIPT_DIR, "x64", "RzChromaSDK64.dll")
X86_DLL = os.path.join(SCRIPT_DIR, "x86", "RzChromaSDK.dll")

INSTALLER_URL = "https://dl.razerzone.com/drivers/Chroma/RazerChromaInstaller.exe"
USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

def get_sha256(filepath):
    if not os.path.exists(filepath):
        return None
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().lower()

def check_remote_installer():
    print(f"[*] Checking remote installer: {INSTALLER_URL}")
    req = urllib.request.Request(INSTALLER_URL, headers={"User-Agent": USER_AGENT}, method="HEAD")
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            headers = {k.lower(): v for k, v in resp.headers.items()}
            content_length = headers.get("content-length")
            last_modified = headers.get("last-modified")
            etag = headers.get("etag")
            print(f"    Content-Length: {content_length}")
            print(f"    Last-Modified:  {last_modified}")
            print(f"    ETag:           {etag}")
            return {
                "content_length": content_length,
                "last_modified": last_modified,
                "etag": etag
            }
    except Exception as e:
        print(f"[!] Failed to check remote installer: {e}")
        return None

def main():
    print("=====================================================")
    print("  Razer Chroma SDK Runtime Verification & Sync")
    print("=====================================================")

    cur_x64_hash = get_sha256(X64_DLL)
    cur_x86_hash = get_sha256(X86_DLL)
    print(f"[*] Local x64 DLL SHA256: {cur_x64_hash}")
    print(f"[*] Local x86 DLL SHA256: {cur_x86_hash}")

    remote_info = check_remote_installer()
    if not remote_info:
        print("[!] Warning: Remote check could not be completed.")
        return 0

    # Load version.json
    v_data = {}
    if os.path.exists(VERSION_JSON):
        with open(VERSION_JSON, "r", encoding="utf-8") as f:
            v_data = json.load(f)

    v_data["last_checked"] = datetime.now(timezone.utc).isoformat()
    v_data["remote_installer_etag"] = remote_info.get("etag")
    v_data["remote_installer_last_modified"] = remote_info.get("last_modified")

    # If the installer has changed compared to previously cached etag, attempt download & extraction
    prev_etag = v_data.get("cached_etag")
    needs_update = (prev_etag is not None and prev_etag != remote_info.get("etag"))

    if needs_update or os.environ.get("FORCE_CHROMA_UPDATE") == "1":
        print("[*] Remote installer change detected or FORCE_CHROMA_UPDATE set. Downloading...")
        temp_installer = os.path.join(SCRIPT_DIR, "RazerChromaInstaller.exe")
        req = urllib.request.Request(INSTALLER_URL, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=60) as resp, open(temp_installer, "wb") as out:
            out.write(resp.read())
        print(f"[OK] Downloaded installer ({os.path.getsize(temp_installer)} bytes).")

        # Extract using 7z if available
        extract_dir = os.path.join(SCRIPT_DIR, "extracted_temp")
        os.makedirs(extract_dir, exist_ok=True)
        try:
            subprocess.run(["7z", "x", temp_installer, f"-o{extract_dir}", "-y"], check=True)
            # Search for RzChromaSDK64.dll and RzChromaSDK.dll
            found_x64, found_x86 = None, None
            for root, _, files in os.walk(extract_dir):
                for f in files:
                    if f.lower() == "rzchromasdk64.dll":
                        found_x64 = os.path.join(root, f)
                    elif f.lower() == "rzchromasdk.dll":
                        found_x86 = os.path.join(root, f)

            if found_x64 and found_x86:
                import shutil
                shutil.copy2(found_x64, X64_DLL)
                shutil.copy2(found_x86, X86_DLL)
                v_data["x64_sha256"] = get_sha256(X64_DLL)
                v_data["x86_sha256"] = get_sha256(X86_DLL)
                v_data["cached_etag"] = remote_info.get("etag")
                print(f"[OK] Successfully updated runtime DLLs! New x64 hash: {v_data['x64_sha256']}")
        except Exception as ex:
            print(f"[!] Extraction note: {ex}")
        finally:
            if os.path.exists(temp_installer):
                try: os.remove(temp_installer)
                except: pass
            if os.path.exists(extract_dir):
                import shutil
                try: shutil.rmtree(extract_dir)
                except: pass
    else:
        print("[OK] Bundled Razer Chroma runtime DLLs are up to date.")

    with open(VERSION_JSON, "w", encoding="utf-8") as f:
        json.dump(v_data, f, indent=2)

    return 0

if __name__ == "__main__":
    sys.exit(main())
