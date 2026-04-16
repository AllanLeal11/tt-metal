# Running LLK Tests on ttsim (Blackhole)

This guide explains how to run LLK tests on the ttsim functional simulator for Blackhole.

## Prerequisites

- BH silicon machine with tt-metal repo checked out and built (`./build_metal.sh --release`)
- ttsim-private repo cloned (e.g. at `~/ttsim-private`)

## Step 1: Build ttsim for Blackhole

Use the `pjosipovic/bh-fast-tilize-support` branch which has ADC instruction fixes,
BFP4 packer support, MOP zmask_hi16, and other BH fast-tilize requirements.

```bash
cd ~/ttsim-private
git checkout pjosipovic/bh-fast-tilize-support
cd src
../make.py _out/release_bh/libttsim.so
```

This only needs g++ (no RISC-V toolchain). Takes ~30 seconds.

## Step 2: Set up the simulator directory

```bash
mkdir -p ~/sim
cp ~/ttsim-private/src/_out/release_bh/libttsim.so ~/sim/
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

## Step 5: Install patched tt-umd

The stock tt-umd Python bindings cannot construct a `SocDescriptor` from a ttsim device
because `TTSimTTDevice` doesn't set the base class `arch` field and throws on
`get_noc_translation_enabled()` and `get_chip_info()`. The patched branch fixes this.

```bash
cd $TT_METAL_HOME/tt_metal/third_party/umd
git checkout pjosipovic/ttsim-bh-llk-support
```

Then rebuild the nanobind Python module and install it:

```bash
pip install . --no-deps --no-build-isolation
```

This installs to user site-packages. If the venv's tt_umd takes priority, copy the
rebuilt `.so` into the venv:

```bash
cp ~/.local/lib/python3.10/site-packages/tt_umd/tt_umd.cpython-310-x86_64-linux-gnu.so \
   $(python3 -c "import tt_umd; import os; print(os.path.dirname(tt_umd.__file__))")/tt_umd.cpython-310-x86_64-linux-gnu.so
```

Verify:

```bash
python3 -c "import tt_umd; print(hasattr(tt_umd, 'create_simulation_tt_device'))"
# Should print: True

python3 -c "
import tt_umd
dev = tt_umd.create_simulation_tt_device('$HOME/sim/libttsim.so')
print('arch:', dev.get_arch())
soc = tt_umd.SocDescriptor(dev)
print('TENSIX cores:', len(soc.get_cores(tt_umd.CoreType.TENSIX)))
"
# Should print: arch: blackhole / TENSIX cores: 140
```

Note: `scikit-build-core` is required to build the nanobind module (`pip install scikit-build-core`).

## Step 6: Run tests

### ttsim
```bash
cd tt_metal/tt-llk/tests/python_tests

TT_UMD_SIMULATOR_PATH=~/sim/libttsim.so \
python3 -m pytest test_fast_tilize_full.py -v --run-simulator --timeout=120
```

Expected: 154 passed, 130 skipped (BFP/Float32 dimension subsets skipped by default).
All 284 pass if the skip guards are removed.

### Silicon
```bash
cd tt_metal/tt-llk/tests/python_tests

python3 -m pytest test_fast_tilize_full.py -v --timeout=600
```

Expected: 154 passed, 130 skipped.

## What the patches do

| Repo | Branch | File | Change |
|------|--------|------|--------|
| **tt-metal (LLK)** | `pjosipovic/bh-fast-tilize` | `conftest.py` | Early ttsim init before module imports; skip ExalensServer for .so |
| **tt-metal (LLK)** | `pjosipovic/bh-fast-tilize` | `device.py` | `BootMode.TRISC` when `TT_UMD_SIMULATOR_PATH` ends in `.so` |
| **tt-metal (LLK)** | `pjosipovic/bh-fast-tilize` | `test_config.py` | Deassert all 3 TRISCs in TRISC boot mode |
| **tt-umd** | `pjosipovic/ttsim-bh-llk-support` | `tt_sim_tt_device.cpp` | Set `arch` field, return false from `get_noc_translation_enabled()`, override `get_chip_info()` |
| **tt-umd** | `pjosipovic/ttsim-bh-llk-support` | `tt_sim_tt_device.hpp` | Declare `get_chip_info()` override |
| **tt-exalens** | `pjosipovic/ttsim-bh-llk-support` | `umd_api.py` | `create_simulation_tt_device()` for .so; skip core reset for ttsim |
| **tt-exalens** | `pjosipovic/ttsim-bh-llk-support` | `context.py` | Disable DMA for BH (UMD limitation) |
| **ttsim-private** | `pjosipovic/bh-fast-tilize-support` | `tensix_isa.json` | Fix ADC instruction bit field widths (1-bit -> 3/6-bit) |
| **ttsim-private** | `pjosipovic/bh-fast-tilize-support` | `tensix.cpp` | BFP4 packer modes, MOP zmask_hi16, UNPACR row wrapping, full ADDRCRXY/ADDRCRZW |
| **ttsim-private** | `pjosipovic/bh-fast-tilize-support` | `sim.h` | `mop_zmask_hi16` field, `debug_mailbox[4]` |
| **ttsim-private** | `pjosipovic/bh-fast-tilize-support` | `tile.cpp` | Debug mailbox read/write for kernel completion polling |

## Troubleshooting

- **"Getting NOC translation status is not supported in TTSim simulation device"**: The tt-umd patch isn't applied. Check Step 5.
- **"Invalid architecture for creating SocDescriptorInfo"**: Same cause — `arch` field not set in TTSimTTDevice. Check Step 5.
- **"D2H DMA transfer is not supported on Blackhole"**: The `context.py` DMA fix isn't applied. Re-install tt-exalens from patched branch.
- **"sim is already running"**: The ttsim .so was initialized twice. Make sure the early init in conftest.py runs and `pytest_configure` skips re-init.
- **TRISC timeout / kernel never completes**: Check that all 3 TRISCs are deasserted (the `test_config.py` patch). Only TRISC0 deasserted = deadlock.
- **"SOFT_RESET_0=0x6f" error in ttsim output**: Informational warning, not fatal. ttsim doesn't simulate the soft reset register but execution continues.
- **"riscv-tt-elf-g++: not found"**: SFPI compiler not set up. Run Step 3.
- **UnimplementedFunctionality: tensix_decode_and_execute_incadcxy**: ttsim ISA JSON has wrong ADC field widths. Use `pjosipovic/bh-fast-tilize-support` branch.
- **UnimplementedFunctionality: pack_fmt_conv_mode=0x57/0x107/0x157**: ttsim missing BFP4 packer support. Use `pjosipovic/bh-fast-tilize-support` branch.
