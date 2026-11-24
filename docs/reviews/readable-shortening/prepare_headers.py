#!/usr/bin/env python3
"""Reuse the immutable GCC 13 package from the earlier analysis, locally."""
from abbreviate import baseline  # also establishes the earlier script import path
import importlib.util
from pathlib import Path


def main():
    path = Path(baseline.__file__).with_name('prepare_headers.py')
    spec = importlib.util.spec_from_file_location('minify_prepare_headers', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.WORK = Path('measurements/readable-shortening')
    module.main()


if __name__ == '__main__':
    main()
