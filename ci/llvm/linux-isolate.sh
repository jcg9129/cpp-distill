#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
python3 ci/llvm/isolate.py
chown -R "$HOST_UID:$HOST_GID" isolated results spike-work
