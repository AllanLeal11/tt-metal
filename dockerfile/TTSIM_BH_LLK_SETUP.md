# Running LLK Tests on ttsim (Blackhole)

This guide explains how to run LLK tests on the ttsim functional simulator for Blackhole.

## Prerequisites

- BH silicon machine with tt-metal repo checked out and built (`./build_metal.sh --release`)
- ttsim source at `/home/developer/ttsim-private`
- tt-exalens source: `git clone https://github.com/tenstorrent/tt-exalens.git`

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

## Step 3: LLK code location

LLK code is now in-tree at `tt_metal/tt-llk/` (no longer a submodule).
The ttsim support changes (conftest, device boot mode) are included in the
`pjosipovic/bh-fast-tilize` branch of tt-metal.

### tt-exalens (ttsim .so support + BH DMA fix)
```bash
cd /path/to/tt-exalens
git fetch origin
git checkout pjosipovic/ttsim-bh-llk-support
```

### UMD (fixes TTSimTTDevice for BH)
The UMD branch `pjosipovic/ttsim-bh-llk-support` may still be needed
if the pip-installed `tt-umd` lacks `create_simulation_tt_device()`.

## Step 4: Install tt-exalens from source

The patched tt-exalens must replace the pip-installed version:

```bash
cd /path/to/tt-exalens
pip install -e . --no-deps
```

If the editable install doesn't take effect (check with `python3 -c "import ttexalens; print(ttexalens.__file__)"`), manually sync:

```bash
cp ttexalens/umd_api.py $(python3 -c "import ttexalens; import os; print(os.path.dirname(ttexalens.__file__))")/umd_api.py
cp ttexalens/context.py $(python3 -c "import ttexalens; import os; print(os.path.dirname(ttexalens.__file__))")/context.py
```

## Step 5: Rebuild UMD Python binding (if needed)

Check if `create_simulation_tt_device` exists:

```bash
python3 -c "import tt_umd; print(hasattr(tt_umd, 'create_simulation_tt_device'))"
```

If `False`, rebuild from the patched UMD source:

```bash
cd tt_metal/third_party/umd
git checkout pjosipovic/ttsim-bh-llk-support
CMAKE_ARGS="-DTT_UMD_BUILD_SIMULATION=ON -DTT_UMD_BUILD_STATIC=ON" \
  pip wheel . --no-deps -w /tmp/umd_wheel

pip install --force-reinstall --upgrade \
  --target=$(python3 -c "import site; print(site.getsitepackages()[0])") \
  /tmp/umd_wheel/tt_umd-*.whl
```

## Step 6: Verify

### ttsim
```bash
cd tt_metal/tt-llk/tests/python_tests

TT_UMD_SIMULATOR_PATH=~/sim/libttsim.so \
python3 -m pytest \
  "test_risc_compute.py::test_risc_compute" \
  "test_eltwise_unary_datacopy.py::test_unary_datacopy[formats:Float16_b->Float16_b-dest_acc:No-num_faces:4-tilize:No-input_dimensions:[64, 64]]" \
  "test_zzz_bcast.py::test_unpack_bcast[tile_dimensions:[32, 32]-formats:Float16_b->Float16_b-broadcast_type:None_-dest_acc:No]" \
  --run-simulator -v --timeout=120
```

Expected: 3 passed in <1s.

### Silicon (verify nothing broke)
```bash
python3 -m pytest \
  "test_risc_compute.py::test_risc_compute" \
  "test_eltwise_unary_datacopy.py::test_unary_datacopy[formats:Float16_b->Float16_b-dest_acc:No-num_faces:4-tilize:No-input_dimensions:[64, 64]]" \
  "test_zzz_bcast.py::test_unpack_bcast[tile_dimensions:[32, 32]-formats:Float16_b->Float16_b-broadcast_type:None_-dest_acc:No]" \
  -v --timeout=120
```

Expected: 3 passed in <1s.

## What the patches do

| Repo | File | Change |
|------|------|--------|
| **UMD** | `tt_sim_tt_device.cpp` | Set `arch` from soc_descriptor; `get_noc_translation_enabled()` returns false |
| **UMD** | `tt_device.cpp` | Null-check `firmware_info_provider` in `get_chip_info()` |
| **UMD** | `py_api_soc_descriptor.cpp` | Default BH eth_harvesting_mask=0x120 in Python SocDescriptor binding |
| **LLK** | `conftest.py` | Early ttsim init before module imports; skip ExalensServer for .so |
| **LLK** | `device.py` | `BootMode.TRISC` when `TT_UMD_SIMULATOR_PATH` ends in `.so` |
| **LLK** | `test_config.py` | Deassert all 3 TRISCs for ttsim (no BRISC bootstrap) |
| **tt-exalens** | `umd_api.py` | `create_simulation_tt_device()` for .so; skip core reset for ttsim |
| **tt-exalens** | `context.py` | Disable DMA for BH (pre-existing UMD limitation) |

## Troubleshooting

- **"D2H DMA transfer is not supported on Blackhole"**: The `context.py` DMA fix isn't applied. Re-copy from tt-exalens source.
- **"sim is already running"**: The ttsim .so was initialized twice. Make sure the early init in conftest.py runs and the `pytest_configure` skips re-init.
- **TRISC timeout / kernel never completes**: Check that all 3 TRISCs are deasserted (the `test_config.py` patch). Only TRISC0 deasserted = deadlock.
- **"SOFT_RESET_0=0x6f" error in ttsim output**: Informational warning, not fatal. ttsim doesn't simulate the soft reset register but execution continues.
