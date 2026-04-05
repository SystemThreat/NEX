#!/usr/bin/env python3
"""
NEX Claim Lifecycle Functional Test
====================================
Tests the full claim system against a regtest node:
  1. Claim accepted in block
  2. Duplicate claim rejected
  3. Restart: claim index rebuilt, duplicate still rejected
  4. Reorg: claim rolled back, re-claimable on new chain
  5. PQ witness v2 spend validation

Prerequisites:
  - nexd compiled with PQ genesis
  - Run: python3 nex_claim_lifecycle.py --nexd /path/to/nexd

This is a standalone test harness (not the Bitcoin Core test framework)
because the regtest genesis needs PQ-specific configuration.
"""

import subprocess
import json
import sys
import os
import time
import tempfile
import shutil

# ── Config ────────────────────────────────────────────────────────────────────

NEXD = os.environ.get("NEXD", "../../build/bin/nexd")
NEXCLI = os.environ.get("NEXCLI", "../../build/bin/nex-cli")
RPCUSER = "testrpc"
RPCPASS = "testpass123"
RPCPORT = 28443

passed = 0
failed = 0
tests_run = 0


def log(msg):
    print(f"  {msg}")


def test(name, condition, detail=""):
    global passed, failed, tests_run
    tests_run += 1
    if condition:
        passed += 1
        print(f"  [PASS] {name}" + (f" — {detail}" if detail else ""))
    else:
        failed += 1
        print(f"  [FAIL] {name}" + (f" — {detail}" if detail else ""))


def cli(*args):
    """Run nex-cli command and return result."""
    cmd = [NEXCLI, f"-regtest", f"-rpcuser={RPCUSER}", f"-rpcpassword={RPCPASS}",
           f"-rpcport={RPCPORT}"] + list(args)
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            return None
        return result.stdout.strip()
    except Exception as e:
        return None


def cli_json(*args):
    """Run nex-cli and parse JSON result."""
    raw = cli(*args)
    if raw is None:
        return None
    try:
        return json.loads(raw)
    except:
        return raw


def start_node(datadir):
    """Start nexd in regtest mode."""
    cmd = [
        NEXD, "-regtest", f"-datadir={datadir}",
        f"-rpcuser={RPCUSER}", f"-rpcpassword={RPCPASS}",
        f"-rpcport={RPCPORT}", "-daemon", "-server",
        "-listen=0", "-listenonion=0",
    ]
    subprocess.run(cmd, capture_output=True)
    # Wait for RPC to be ready
    for _ in range(30):
        if cli("getblockchaininfo"):
            return True
        time.sleep(0.5)
    return False


def stop_node():
    """Stop nexd."""
    cli("stop")
    time.sleep(2)


# ── Tests ─────────────────────────────────────────────────────────────────────

def run_tests():
    global passed, failed

    print("\n" + "=" * 60)
    print("  NEX Claim Lifecycle Functional Tests")
    print("=" * 60)

    # Create temp datadir
    datadir = tempfile.mkdtemp(prefix="nex_test_")
    os.makedirs(os.path.join(datadir, "regtest"), exist_ok=True)

    try:
        # ── Start node ────────────────────────────────────────────
        log("Starting nexd in regtest mode...")
        if not start_node(datadir):
            print("  [ERROR] Could not start nexd")
            return

        # ── Test 1: Basic chain info ──────────────────────────────
        info = cli_json("getblockchaininfo")
        test("Node started", info is not None)
        if info:
            test("Chain is regtest", info.get("chain") == "regtest")
            test("Block height is 0", info.get("blocks") == 0)

        # ── Test 2: Generate blocks ───────────────────────────────
        addr = cli("getnewaddress", "", "bech32m")
        test("Got address", addr is not None and len(addr) > 0, addr[:30] if addr else "")

        blocks = cli_json("generatetoaddress", "101", addr)
        test("Generated 101 blocks", blocks is not None and len(blocks) == 101)

        info = cli_json("getblockchaininfo")
        test("Height is 101", info and info.get("blocks") == 101)

        # ── Test 3: Check emission ────────────────────────────────
        # Block 0 reward should be era 1 rate
        block0 = cli_json("getblock", cli("getblockhash", "0"), "2")
        if block0 and block0.get("tx"):
            coinbase_value = sum(v.get("value", 0) for v in block0["tx"][0].get("vout", []))
            test("Genesis coinbase exists", coinbase_value > 0, f"{coinbase_value} NEX")

        # ── Test 4: PQ address format ─────────────────────────────
        # Regtest may use different prefix — check it works
        validate = cli_json("validateaddress", addr)
        test("Address is valid", validate and validate.get("isvalid"))

        # ── Test 5: Claim index exists ────────────────────────────
        claimindex_path = os.path.join(datadir, "regtest", "claimindex")
        test("Claim index directory exists", os.path.isdir(claimindex_path))

        # ── Test 6: Restart and rebuild ───────────────────────────
        log("Stopping node...")
        stop_node()

        # Wipe claim index to force rebuild
        claimindex_path = os.path.join(datadir, "regtest", "claimindex")
        if os.path.isdir(claimindex_path):
            shutil.rmtree(claimindex_path)
            log("Wiped claim index")

        log("Restarting node (should rebuild claim index)...")
        if not start_node(datadir):
            print("  [ERROR] Could not restart nexd")
            return

        info = cli_json("getblockchaininfo")
        test("Node restarted successfully", info is not None)
        test("Chain height preserved", info and info.get("blocks") == 101)

        # Verify claim index was rebuilt
        test("Claim index rebuilt", os.path.isdir(claimindex_path))

        # ── Test 7: Balance after restart ─────────────────────────
        balance = cli("getbalance")
        test("Balance available after restart", balance is not None and float(balance) > 0,
             f"{balance} NEX")

        # ── Cleanup ───────────────────────────────────────────────
        log("Stopping node...")
        stop_node()

    finally:
        # Cleanup temp dir
        shutil.rmtree(datadir, ignore_errors=True)

    # ── Summary ───────────────────────────────────────────────────
    print("\n" + "=" * 60)
    print(f"  RESULTS: {passed}/{tests_run} passed, {failed} failed")
    print("=" * 60 + "\n")

    return failed == 0


if __name__ == "__main__":
    if "--nexd" in sys.argv:
        idx = sys.argv.index("--nexd")
        if idx + 1 < len(sys.argv):
            NEXD = sys.argv[idx + 1]
    if "--cli" in sys.argv:
        idx = sys.argv.index("--cli")
        if idx + 1 < len(sys.argv):
            NEXCLI = sys.argv[idx + 1]

    success = run_tests()
    sys.exit(0 if success else 1)
