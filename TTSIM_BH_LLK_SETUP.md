# Running LLK Tests on ttsim (Blackhole)

This guide explains how to run LLK tests on the ttsim functional simulator for Blackhole.

## Prerequisites

- BH silicon machine with tt-metal repo checked out and built (`./build_metal.sh --release`)
- ttsim source at `/home/developer/ttsim-private`

## Step 1: Build ttsim for Blackhole

```bash
cd /home/developer/ttsim-private/src
../make.py _out/release_bh/libttsim.so
```

This only needs g++ (no RISC-V toolchain). Takes ~30 seconds.

## Step 2: Set up the simulator directory

```bash
mkdir -p ~/sim
cp /home/developer/ttsim-private/src/_out/release_bh/libttsim.so ~/sim/
cp $TT_METAL_HOME/tt_metal/soc_descriptors/blackhole_140_arch.yaml ~/sim/soc_descriptor.yaml
```

## Step 3: Set up SFPI compiler for LLK tests

LLK tests compile kernels using the SFPI RISC-V cross-compiler.
After building tt-metal, copy it into the LLK test tree:

```bash
mkdir -p tt_metal/tt-llk/tests/sfpi
cp -a runtime/sfpi/compiler tt_metal/tt-llk/tests/sfpi/compiler
cp -a runtime/sfpi/include tt_metal/tt-llk/tests/sfpi/include
```

Verify: `tt_metal/tt-llk/tests/sfpi/compiler/bin/riscv-tt-elf-g++ --version`

## Step 4: Install patched tt-exalens

ttsim requires two patches in tt-exalens (not yet in main):
- `umd_api.py`: Use `create_simulation_tt_device()` for `.so` ttsim paths
- `context.py`: Disable DMA for BH (UMD limitation)

```bash
git clone https://github.com/tenstorrent/tt-exalens.git  # if not already cloned
cd tt-exalens
git checkout pjosipovic/ttsim-bh-llk-support
pip install -e . --no-deps
```

If the editable install doesn't take effect (check with `python3 -c "import ttexalens; print(ttexalens.__file__)"`), manually sync:

```bash
cp ttexalens/umd_api.py $(python3 -c "import ttexalens; import os; print(os.path.dirname(ttexalens.__file__))")/umd_api.py
cp ttexalens/context.py $(python3 -c "import ttexalens; import os; print(os.path.dirname(ttexalens.__file__))")/context.py
```

## Step 5: Verify UMD has ttsim support

```bash
python3 -c "import tt_umd; print(hasattr(tt_umd, 'create_simulation_tt_device'))"
```

Should print `True`. If `False`, the installed `tt-umd` package is too old — update it from the tt-metal build or pip.

## Step 6: Run tests

### ttsim
```bash
cd tt_metal/tt-llk/tests/python_tests

TT_UMD_SIMULATOR_PATH=~/sim/libttsim.so \
python3 -m pytest test_fast_tilize_full.py -v --run-simulator --timeout=120 -k "not Bfp4"
```

Note: Bfp4_b format conversion is not implemented in ttsim, exclude with `-k "not Bfp4"`.

### Silicon
```bash
cd tt_metal/tt-llk/tests/python_tests

python3 -m pytest test_fast_tilize_full.py -v --timeout=600
```

Expected: 154 passed, 130 skipped.

## What the patches do

| Repo | File | Change |
|------|------|--------|
| **tt-metal (LLK)** | `conftest.py` | Early ttsim init before module imports; skip ExalensServer for .so |
| **tt-metal (LLK)** | `device.py` | `BootMode.TRISC` when `TT_UMD_SIMULATOR_PATH` ends in `.so` |
| **tt-metal (LLK)** | `test_config.py` | Deassert all 3 TRISCs in TRISC boot mode |
| **tt-exalens** | `umd_api.py` | `create_simulation_tt_device()` for .so; skip core reset for ttsim |
| **tt-exalens** | `context.py` | Disable DMA for BH (UMD limitation) |

## Troubleshooting

- **"D2H DMA transfer is not supported on Blackhole"**: The `context.py` DMA fix isn't applied. Re-install tt-exalens from patched branch.
- **"sim is already running"**: The ttsim .so was initialized twice. Make sure the early init in conftest.py runs and `pytest_configure` skips re-init.
- **TRISC timeout / kernel never completes**: Check that all 3 TRISCs are deasserted (the `test_config.py` patch). Only TRISC0 deasserted = deadlock.
- **"SOFT_RESET_0=0x6f" error in ttsim output**: Informational warning, not fatal. ttsim doesn't simulate the soft reset register but execution continues.
- **"riscv-tt-elf-g++: not found"**: SFPI compiler not set up. Run Step 3.
- **Bfp4_b crash in ttsim**: Known ttsim limitation. Exclude with `-k "not Bfp4"`.
