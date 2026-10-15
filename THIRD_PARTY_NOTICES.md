# Third-party notices

The ghost artwork and watcher logic are original project work. Existing init() firmware, conference artwork, profiles and device storage are not included in this distribution or licensed for redistribution by this document. Public conference source was consulted for factual comparison only.

## M5GFX

Pinned M5GFX 0.2.26 commit `729297d6e3d657ddc1ec5189bac2f2ea68828085`. The explicit StopWatch setup follows its published driver. M5GFX includes LovyanGFX-origin files with their own retained FreeBSD license headers; preserve those headers and consult the pinned source before redistribution.

MIT License

Copyright (c) 2021 M5Stack

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Touch protocol reference

The original release-only touch sampler and gesture recognizer consult the CST820/CST816S register and coordinate configuration in the pinned M5GFX source. `src/lgfx/v1/touch/Touch_CSTxxx.cpp` retains its LovyanGFX-origin FreeBSD license header, crediting lovyan03 and contributors ciniml, mongonta0716 and tobozo; original source: https://github.com/lovyan03/LovyanGFX/ and license reference: https://github.com/lovyan03/LovyanGFX/blob/master/license.txt. That upstream implementation is not vendored here. The application uses an independently written sampler that distinguishes I2C failures from release and does not copy the stock touch driver. M5GFX/LovyanGFX remain external build dependencies with their own notices; no conference touch implementation or calibration is redistributed.

## M5Unified audio reference

The optional StopWatch audio seam follows the speaker pin mapping, ES8311 register setup and power-enable sequence in M5Unified tag `0.2.19`, commit `4fb444784c85791e0b0207701392b42be234b2e7`, `src/M5Unified.cpp`. M5IOE1 pin numbering was cross-checked against `src/utility/M5IOE1_Class.hpp`. Its MIT license is Copyright (c) 2021 M5Stack, with the same permission, inclusion condition and disclaimer reproduced in the M5GFX MIT notice above. M5Unified itself is not linked or initialized.

## Espressif codec driver

Factory-feedback builds vendor an unmodified ES8311/core/interface subset of `espressif/esp_codec_dev` **1.5.4**, source commit `fcce23e3d5ecf7ba75ce9f4e3761489dc9e0299e`, under `firmware/pet/components/watcher_codec/upstream/`. Copyright (c) 2023 Espressif Systems (Shanghai) CO LTD; Apache-2.0. The complete license and source headers are retained there. `provenance.json` records the registry URL, archive SHA-256 and every copied file hash; CMake checks those hashes. Default I2C/I2S backends and the upstream build/setup scripts are not used. The package's stale runtime version string is not a provenance or hardware-revision gate.

The factory application is used for configuration/lifecycle reference, not imported wholesale. Its NVS, board discovery, storage and boot paths are not incorporated. Historical M5Unified-derived probe code remains separately identified above; the normal completion build no longer selects it.

## Adafruit classic GLCD font (Font0)

The UI explicitly uses Font0. The pinned display image's symbol table contains Font0 and its data. Notice retained from M5GFX `src/lgfx/Fonts/glcdfont.h`:

Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

## Bosch BMI270

Optional auto-rotation vendors six unmodified files from Bosch Sensortec BMI270_SensorAPI commit `41129fcfe39c583ee5462d79195741945d51c1fe` (v2.113.0), pinned by the factory's BMI270_BMM150_Sensor 0.1.2 wrapper commit `ae8bc480bca638d3458e65d5e9a4fff4270c1430`. Copyright (c) 2023 Bosch Sensortec GmbH, BSD-3-Clause. Full terms and disclaimer are reproduced in `firmware/pet/components/watcher_imu/upstream/LICENSE` and must accompany binary redistribution. File hashes and provenance are checked by CMake. Only the Bosch driver is compiled: no wrapper I2C driver, auxiliary magnetometer setup, factory HAL or storage code is transplanted.

## SDK and host dependencies

ESP-IDF 5.5.4 and its components retain upstream licenses in the separately provisioned toolchain (including Apache-2.0 SDK code and separately licensed components). M5Unified and M5PM1/M5IOE1 driver checkouts were references, not wholesale application imports. pyserial 3.5, Node.js, Pi and any separately installed child-observation provider retain their respective upstream licenses. No package installation or public binary publication is implied by this file.

This records the directly consulted driver/font notices; it is not an exhaustive linked-firmware redistribution audit or permission to publish the private preservation images.
