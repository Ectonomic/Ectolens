# Geist and Geist Mono

Ectolens ships these fonts inside its binary (src/CMakeLists.txt, `fonts` resource) and loads them at start (`src/Main.cpp`), so the Ecto look draws the same on every computer whether or not the fonts are installed.

- Source: https://github.com/vercel/geist-font, release v1.9.0 (2026-10-30), `geist-font-v1.9.0.zip`, sha256 1f2dd9ea90e68d0358a1d234a5f156807b2c3e0e295d78d87980a6dbad1fa58c.
- Files: the static TTFs from `Geist/ttf` and `GeistMono/ttf` (Regular, Medium, SemiBold; Mono Regular, Medium), unchanged.
- License: SIL Open Font License 1.1, in `OFL.txt` beside them (copied from the release). The license allows bundling with software as long as the license travels with the fonts and the fonts aren't sold on their own.
- Approved as a dependency on 2026-11-02 (design proposal 2).
