# Soie

A lightweight HTML/CSS rendering engine in C99 with a small
reference browser. No toolkit, no JavaScript in the core, built to
render modern pages on old machines. See `doc/DECISIONS.md` for what
Soie is and is not, and how extensions plug in.

Status: version 0.1.3 (November 2026). Extensions
(`lib/soie/ext.h`) bring cookies, content blocking, WebP, JavaScript
(QuickJS) and audio/video (FFmpeg, ALSA, Media Source Extensions: YouTube plays). `browser/xsoie` browses the
web (HTTPS, forms, history, cookies, scripts, video). The HTML parser follows the WHATWG algorithm and passes the
html5lib tree-construction tests; the CSS parser, selectors, cascade and computed values pass css-parsing-tests,
including all of CSS Color 4; block and inline layout work with a
FreeType/HarfBuzz text (phase 4) and tables; flexbox and grid.

## Source

The repository is https://hg.slitaz.org/soie (Mercurial). A read-only
git mirror is at https://github.com/pankso/soie: pull requests there
are welcome, they are read and imported by hand into Mercurial
(`tools/pr-import.sh`). Bugs and ideas: https://maille.slitaz.org/tags/soie

## Build

    make                # libsoie, tools/soie-render, available browsers/extensions
    make XSOIE=         # explicitly skip browser/xsoie, even if dependencies present
    make WSOIE=         # same for browser/wsoie
    make check          # unit tests and the local corpus, no network
    make check-html5lib # fetches the html5lib corpus, runs it
    make check-css      # fetches css-parsing-tests, runs it (Python 3)
    make asan           # the tests under AddressSanitizer (gcc-83) in asan-build/
    make asan-sites     # the same, and sites rendered whole (tools/asan.sh -m first)
    make install PREFIX=/usr DESTDIR=...

The default build always includes the engine and `tools/soie-render`.
Browser frontends and extensions are selected according to installed
dependencies. `browser/xsoie` is optional: missing X11 or shell libraries
skip it in both `make` and `make install`, including its desktop entry.
To skip it explicitly even when its dependencies are present, use
`make XSOIE=` (and `make install XSOIE=` when installing).

### Dependencies

Needs a C99 compiler, GNU make and pkg-config, plus development headers
and libraries for the components below. Minimum versions are inferred
from the APIs used by the current source, not a tested compatibility
matrix; the Makefile does not enforce these version bounds. A maximum
of "Not established" means no upper API limit has been identified, not
that all newer releases have been tested. Transitive dependencies must
also satisfy the selected libraries' own requirements.

| Library | Required for | Minimum version / API | Maximum compatible version / known limit |
| --- | --- | --- | --- |
| FreeType (`freetype2`) | Core font rendering | 2.3.8 (`FT_Get_Advance`) | Not established |
| libpng | Core PNG decoding and PNG output | 1.0.6 (`png_jmpbuf`) | Not established |
| libjpeg or libjpeg-turbo | Core JPEG decoding | libjpeg 8, or libjpeg-turbo 1.3 with memory source support (`jpeg_mem_src`) | Not established |
| HarfBuzz | Optional text shaping; enabled by default, disable with `make HARFBUZZ=0` | 0.9.38 (`hb_ft_face_create_referenced`) | Not established |
| GnuTLS | Browser shell HTTPS | 3.4.6 (`gnutls_session_set_verify_cert`) | Not established |
| zlib | Browser shell HTTP decompression; also a libpng dependency | 1.2.0.4 for automatic gzip/zlib detection | Not established |
| Xlib (`x11`) | Optional `browser/xsoie` | X11 API with XIM/UTF-8 support; earliest library release not established | Not established |
| libXext (`xext`) | Optional `browser/xsoie` shared memory rendering | MIT-SHM API; earliest library release not established | Not established |
| Wayland client (`wayland-client`) | Optional `browser/wsoie` | 1.22 for all fields in the current pointer listener initializer; rendering uses compositor version 4 | Not established |
| Wayland cursor (`wayland-cursor`) | Optional `browser/wsoie` cursors | 1.0 cursor theme API | Not established |
| libxkbcommon (`xkbcommon`) | Optional `browser/wsoie` keyboard input | 0.5.0 (Compose API) | Not established |
| GTK (`gtk+-3.0`, including GDK) | Optional SoieView widget and `gtk/gsoie` | 3.22 (`gtk_menu_popup_at_pointer`) | 3.x; GTK 4 is incompatible |
| GLib / GObject / GIO | GTK frontend, supplied through GTK's pkg-config dependencies | 2.36 (`g_unix_fd_add`); GTK may require newer | Not established |
| Cairo | GTK frontend drawing, supplied through GTK's pkg-config dependencies | 1.14 (`cairo_surface_set_device_scale`) | Not established |
| libwebp | Optional `ext/webp.so` | 0.5.0 (`WebPFree`) | Not established |
| FFmpeg (`libavformat`, `libavcodec`, `libswscale`, `libswresample`, `libavutil`) | Optional `ext/media.so` | 3.4 API floor (`avio_context_free`, libavformat 57.80.100) | 6.1.x release series; FFmpeg 7.0+ removes the legacy channel layout/resampler APIs used here |
| ALSA (`alsa`) | Optional `ext/media.so`, including builds played silently | 1.0.11 (`snd_pcm_set_params`, `snd_pcm_recover`) | Not established |
| QuickJS | Optional `ext/script.so` | Target release: 2026-06-04 | Only 2026-06-04 is targeted; compatibility with other releases is not established |
| System C/POSIX libraries (`libc`, `libm`, `libdl`, pthreads) | Core and browser/extension support | C99 and POSIX.1-2008 interfaces; no implementation-specific minimum established | Not established |
| libatomic | QuickJS extension on platforms needing out-of-line atomics | Compiler-compatible library, detected by a link probe | Must match the compiler/runtime ABI; no numeric maximum established |

`browser/xsoie` is built and installed only when pkg-config finds
`x11`, `xext`, `gnutls`, `zlib` and `libpng`.

The optional Wayland frontend also needs `wayland-scanner` and
`wayland-protocols` at build time. The latter needs version 1.26 for
all fields in the current xdg-toplevel listener initializer (stable
xdg-shell and unstable xdg-decoration protocol XML files). Use a scanner
matching the Wayland development libraries; no upper version limit is
established for either build dependency. Older Wayland/protocol headers
may produce excess-initializer warnings even where the runtime APIs
exist.

Version evidence: upstream [FreeType changes](https://github.com/freetype/freetype/blob/master/docs/CHANGES),
[libpng 1.0.6 headers](https://github.com/pnggroup/libpng/blob/v1.0.6/pngconf.h),
[libjpeg-turbo compatibility notes](https://libjpeg-turbo.org/Documentation/OfficialBinaries),
[HarfBuzz API](https://harfbuzz.github.io/harfbuzz-hb-ft.html),
[GnuTLS API](https://gnutls.org/reference/gnutls-gnutls.html),
[GTK API](https://docs.gtk.org/gtk3/method.Menu.popup_at_pointer.html),
[GLib API](https://docs.gtk.org/glib-unix/func.fd_add.html),
[Cairo API](https://www.cairographics.org/manual/cairo-cairo-surface-t.html#cairo-surface-set-device-scale),
[xkbcommon API](https://xkbcommon.org/doc/current/group__compose.html),
[libwebp release notes](https://chromium.googlesource.com/webm/libwebp/+/refs/heads/0.5.0/NEWS),
[ALSA changes](https://www.alsa-project.org/wiki/Changes_v1.0.10_v1.0.11_detailed1)
and [FFmpeg API history](https://github.com/FFmpeg/FFmpeg/blob/master/doc/APIchanges).

### Extensions

Extensions are modules in `ext/`, installed in `$(PREFIX)/lib/soie/ext`:
`cookies.so` and `block.so` need nothing, `webp.so` is built when
pkg-config finds libwebp, `media.so` when it finds FFmpeg and ALSA,
`script.so` (JavaScript) when quickjs-dev is installed (built with
-fPIC) or from the QuickJS sources:
`make QUICKJS_SRC=/path/to/quickjs-2026-06-04`.
`make EXTS="ext/cookies.so"` picks them. `make check-js` tests the
script extension.

## Try it

    tools/soie-render page.html          # dump the DOM tree
    tools/soie-render -f tr cells.html   # parse as a fragment of <tr>
    tools/soie-render -c latin1 old.html # charset from the transport
    tools/soie-render -p display,color,margin-top page.html
                                         # computed styles, after the cascade
    tools/soie-render -l -w 800 page.html # box tree and geometry
    tools/soie-render -o page.png -a page.html # render the whole page

When built, the X11 browser runs as `browser/xsoie [URL]` or
`browser/xsoie -o out.png URL` to render a page without a display.
It is selected automatically when pkg-config finds Xlib, libXext,
GnuTLS, zlib and libpng. Its shell (`shell/`) has no toolkit; what its fronts
share (bars, keys, repainting only what changed) is `browser/front.c`,
xsoie is the Xlib part (MIT-SHM), `browser/wsoie` the Wayland one
(built when wayland-client, wayland-cursor and xkbcommon are there). Ctrl+L address (or words to search for,
`$SOIE_SEARCH`), Ctrl+F find, a drag (a double, triple click) selects
text, Ctrl+A all of it, Ctrl+C/Ctrl+V clipboard, Ctrl+N or the
middle button on a link a new window, Alt+arrows history, F5 reload,
Ctrl+plus, Ctrl+minus, Ctrl+0 zoom. Files that are not pages (archives,
ISO images) are downloaded to `~/Downloads` (or `$XDG_DOWNLOAD_DIR`).
It loads the extensions of `$SOIE_EXT_DIR`, else those of the source
tree when run from it, else the installed ones.
Cookies live in `~/.local/share/soie/cookies.txt`; a hosts-style
block list in `~/.config/soie/block.txt` turns content blocking on.
The ⚙ button (or `about:settings`) sets the start page, the search
engine, JavaScript, colours, zoom, cookies, blocking and the downloads
folder, kept in `~/.config/soie/settings`.
`soie-render -x ext/webp.so page.html` loads an extension;
`soie-render -x ext/script.so -j page.html` runs the page's scripts
(console output on stderr). `browser/xsoie -s` turns scripts off,
`SOIE_DEBUG=1` prints loads, timings and script errors (2: the input too), F12 saves the page for a bug report, a crash writes `~/.cache/soie/crash-PID.txt`. A click plays a video;
`SOIE_AUDIO=none` keeps players silent.

With GTK 3, `gtk/libsoie-gtk.so` is SoieView, a widget for a browser
to show its pages with Soie (`gtk/soie-view.h`), and `gtk/gsoie [URL...]`
a little tabbed browser on it.

## Layout

    lib/         libsoie (public headers in lib/soie/)
    shell/       the shell, no toolkit: pages, history, forms, find, HTTP
                 and TLS, fetch threads, debugging
    browser/     the browser's fronts: their common part, xsoie (Xlib),
                 wsoie (Wayland)
    gtk/         SoieView, a GTK 3 widget (libsoie-gtk) for browsers like
                 Toile, and gsoie, a little tabbed browser on it
    ext/         extensions (cookies, block, webp, script, media)
    tools/       soie-render, generators
    tests/       unit tests, html5lib test runner, local corpus
    doc/         DECISIONS.md, CHANGELOG.md, TODO.md

## Known limits

- Google search and YouTube may stop answering after a while: their
  bot checks (BotGuard) do not trust a JavaScript engine that is not
  SpiderMonkey or V8.
- No JavaScript in iframes (no realm of their own yet).
- No HSTS, and an https page may be redirected to http.
- HTTP logins are Basic only (about:auth), no Digest or Kerberos.
- No `:hover`, `:focus` and other user states in style sheets.
- No tabs in xsoie and wsoie, windows (Ctrl+N): Toile has tabs, on
  SoieView.

## Licence

BSD (2 clauses), as most SliTaz tools: see LICENSE. Authors and
assistants: see AUTHORS.
