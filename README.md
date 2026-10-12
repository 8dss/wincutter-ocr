# WinCutter

A screen text translator for Windows: select an on-screen area and the app
recognizes the text (OCR), translates it to Russian, and copies the result to
the clipboard.

## Features

- **Hotkey + region selection.** A global hotkey is bound to the first available
  combination from the list (`Win+Shift+A` → `Win+Shift+D` → `Win+Shift+X` →
  `Ctrl+Shift+A` → `Ctrl+Alt+A`). The exact combination is shown in the tray
  tooltip. Press it and a selection overlay appears on screen.
- **OCR without external services** — uses the built-in Windows.Media.Ocr
  (C++/WinRT). The recognition engine is chosen automatically: the app tries all
  installed OCR languages and keeps the best result, so both Russian and Latin
  text are read reliably.
- **Translation** — the source language is auto-detected (`sl=auto`) and the
  text is translated to Russian.
- **Clipboard** and a **result window** showing the text with copy buttons
  (source / translation / both).
- **Single-word translation**: click a word in the result window and a short
  translation pops up next to it (English field → Russian, Russian field →
  English).
- Tray icon, single instance, and per-monitor high-DPI support.

## Requirements

- Windows 10/11 (x64).
- Visual Studio with the **Desktop development with C++** workload (the WinRT
  headers ship with the Windows SDK).
- Installed OCR language packs: at minimum **Russian** (otherwise Cyrillic is
  not recognized). Manage them in *Settings → Time & language → Language & region*.

## Build

Open `wincutter-ocr.slnx` in Visual Studio and run **Build → Build Solution**
(`Ctrl+Shift+B`). The binary is produced at:

```
wincutter-ocr\x64\Release\WinCutter.exe
```

Configurations: `Debug`/`Release` × `x64`. For regular use, pick **Release**.

## Usage

```
WinCutter                 run in the background and wait for the hotkey
WinCutter --capture       capture the full screen → OCR → translate → clipboard
WinCutter --translate X   translate the string X
WinCutter --probe         list free key combinations
WinCutter --langs         list available OCR languages
WinCutter --demo          show the result window (for testing)
WinCutter --help          show help
```

With no arguments no console window appears: the app lives in the tray and waits
for the hotkey. Double-click the tray icon to translate a region, right-click for
a menu with "Exit".

## Project layout

```
wincutter-ocr.slnx              Visual Studio solution
wincutter-ocr/
  wincutter-ocr.vcxproj         project (GUI, C++20, WinRT)
  src/
    main.cpp                    entry point, hotkey, orchestration
    capture.cpp                 screen capture (GDI BitBlt)
    overlay.cpp                 full-screen region selection window
    ocr.cpp                     recognition (WinRT Windows.Media.Ocr)
    translate.cpp               translation (WinHTTP)
    result_window.cpp           result window
    clipboard.cpp               clipboard
    tray.cpp                    tray icon
    common.h                    shared utilities
```

## How it works

1. Hotkey → `overlay.cpp` draws a translucent window; the user selects a region.
2. `capture.cpp` grabs the region into a DIB via GDI `BitBlt`.
3. `ocr.cpp` recognizes the text, automatically picking the best engine language.
4. `translate.cpp` calls the free Google endpoint
   (`translate.googleapis.com`, no API key) and parses the JSON.
5. The result is copied to the clipboard and shown in the result window.

## Notes

- Translation goes through an unofficial public Google endpoint (the same one
  used by translate.google.com): no API key is required, but rate limits are
  possible. All translation is isolated in a single `Translate(...)` function, so
  swapping in another service is easy.
- The `Win+Shift+A` hotkey may be taken by the system or another app — in that
  case the app picks the next available combination (see `--probe`).
