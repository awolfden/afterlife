# Original ghost rendering core

Portable C++17 rendering helpers used by the pet application. This directory is not a flashable application and does not own device drivers, storage, transport or notification authority.

## Original assets

`../art/ghost.generated.h` contains the project's original browser artwork: nine states × normal/blink, with 18 references to 16 unique images. Images are 280 × 280 with origin (140,130), positioned by the application on its 466 × 466 logical display.

- Row-bounded `{uint16_t tag, uint16_t RGB565}` runs; tag bit 15 means transparent and the other bits count pixels.
- Transparent pixels issue no draw calls. Partial transparency is preblended against RGB(12,10,16); changing the background requires regeneration.
- RGB565 quantizes shading. Source-frame equality is not a claim of pixel-perfect physical color or browser-equivalent gamut.
- `draw_sprite()` validates the run stream before drawing, clips to the display and sends bounded horizontal spans to a caller-supplied sink. The caller owns canvas lifecycle, clearing and presentation timing.
- Solo presentation retains the original large purple ghost. Multi-session rendering layers bounded hue, silhouette and state-aware pose variants; original-purple pixels bypass hue conversion. Motion belongs to presentation, never notification authority.

## Validate

From the project root:

```sh
npm test
node --test tests/ghost-assets.test.mjs tests/ghost-pages.test.mjs
```

The generated header and original source/frame digests are pinned in `../art/ghost.manifest.json`. Do not update pins merely to hide a failed consistency check.

Optional browser regeneration uses `node preview/export-ghost.mjs`. Its existing Chrome harness targets macOS Chrome, creates an isolated signed-out temporary profile and uses an ephemeral loopback server; it does not use the person's browser session or install dependencies. Regeneration is not needed to build/test the supplied source. Exported profiles and temporary evidence are not release contents.

Hardware display/color quality, frame timing and combined-load behavior require a separately supervised acceptance window. See [developer setup](../../docs/developer.md) and [preservation](../../docs/preservation.md). No firmware installation is implied by renderer tests.
