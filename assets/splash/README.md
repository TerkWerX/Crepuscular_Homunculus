# Startup artwork

The source TERKINSTEIN artwork was supplied by Terkinstein for this project. The 1,254-pixel square master preserves its white thorned wordmark on black. `terkinstein-splash-240.png` is the 240 × 240 grayscale display source, and `firmware/gen/terkinstein_splash.h` contains its packed pixels. `tools/make_splash.py` reproduces the generated header.

The splash remains visible until a physical key or button press. USB MIDI input and encoder turns do not dismiss it. See [LICENSING.md](../../LICENSING.md) for rights and credits.
