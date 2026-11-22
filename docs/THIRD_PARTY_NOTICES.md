# Third-party notices

TeamSync includes third-party components that retain their own licenses. These terms apply to those components, not to TeamSync's original code or artwork.

## Qt 6

The Windows GUI dynamically links Qt 6.8.3 modules Qt Core, Gui, Widgets, Network, and Svg, and includes corresponding Qt platform/style/image plugins. Qt documents these modules as available under the LGPLv3 (or alternative licenses depending on the edition). The local SDK's license edition could not be confirmed from its installed metadata, so the v1.1.0 Windows binary is being held until this is verified.

Qt licensing overview: <https://doc.qt.io/qt-6.8/licensing.html>

Official corresponding source archives for the exact Qt version used by this build:

- [qtbase 6.8.3 source archive](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz) — SHA-256 `56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80`
- [qtsvg 6.8.3 source archive](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtsvg-everywhere-src-6.8.3.tar.xz) — SHA-256 `35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438`

Qt's LGPL obligations include providing complete corresponding library source (or a valid written offer) and preserving users' ability to replace dynamically linked libraries. The Windows release will include the LGPL text, relevant notices, and corresponding Qt source archives once the SDK edition is verified.

## Lucide icons

The SVG interface icons are derived from Lucide Icons and Contributors; some files retain Feather-derived artwork by Cole Bemis. The ISC and applicable MIT notices are in [resources/LICENSE-Lucide.txt](../resources/LICENSE-Lucide.txt). Icon mappings and provenance are in [resources/ICON_ATTRIBUTION.md](../resources/ICON_ATTRIBUTION.md).

## MinGW runtime

The Windows GUI contains GCC/MinGW runtime libraries (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, and `libwinpthread-1.dll`). The release archive includes the toolchain's GCC Runtime Library Exception, GCC, and MinGW-w64 license notices in `THIRD_PARTY_NOTICES/`. GCC 13.1.0 source is available from <https://ftp.gnu.org/gnu/gcc/gcc-13.1.0/>; MinGW-w64 sources are available from <https://sourceforge.net/projects/mingw-w64/files/mingw-w64/>.

Refer to each bundled component's own notice for its complete terms. The TeamSync Limited Use License does not replace or narrow third-party license rights.
