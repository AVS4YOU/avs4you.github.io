# YouTube plugin

Content plugin that downloads a video from a URL and hands the file to the host
app (AVS Video Converter / Video Editor). It drives a bundled `yt-dlp.exe`, with
`ffmpeg.exe` next to it for muxing.

Despite the name it is not limited to YouTube - the bundled yt-dlp supports 1752
sites. See [Which services this can download from](#which-services-this-can-download-from).

## Layout

```
dllmain.cpp                        exports, plugin object, windows, sign-in flow, About
src/browser_login.h                browser lookup, own profile, per-service sign-in URL
src/yt_downloader.h                builds the yt-dlp command line
src/browser_cookies.h              the three cookie sources, in order
src/failure_reason.h               classifies a failed download from yt-dlp output
src/output_name.h                  turns a video title into a usable file name
src/external_process_with_childs.h shared child-process runner (piped stdout/stderr)
resources/x86/yt-dlp.exe           bundled downloader
resources/x86/ffmpeg.exe           bundled muxer
resources/x86/qjs.exe              bundled QuickJS, solves YouTube's JS challenge
build/x86/youtube.avsp             ZIP: Youtube.dll + yt-dlp.exe + ffmpeg.exe + qjs.exe
```

The plugin looks for `yt-dlp.exe` next to the loaded DLL, which is where the host
extracts the `.avsp`.

## Authentication

Many services want a signed-in account for part of their catalogue, and a few
serve almost nothing without one.

### Signing in inside the plugin

There is **no sign-in button**, and no "you are signed in" indicator anywhere.
Both were tried and both are wrong:

- A single indicator cannot be honest. Login state is per service, so "Signed
  in" is a lie the moment a Twitch link is pasted after a YouTube sign-in. And
  it cannot even be measured: an anonymous headless load of youtube.com stores
  six cookies, so cookie presence does not mean an account is present.
- A button asks the user to solve a problem they do not have yet. Most videos
  need no account.

So signing in is **offered by the failure**, not advertised up front:

1. The user pastes a link and presses Download.
2. If it fails and the yt-dlp output explicitly says an account is missing
   (`NSYoutube::LooksLikeSignInRequired`), the service is derived from the URL
   host and a modal dialog says *"This video needs a signed-in Twitch account"*
   with OK / Cancel.
3. OK opens the browser on that service's sign-in page. Both buttons are
   disabled and the text becomes *"Waiting for the sign-in…"*.
4. When the browser is closed - which is also when it flushes cookies to disk -
   the dialog closes itself and returns to the main window.
5. Download again, and it works. The sign-in is remembered from then on.

The dialog polls the browser process on a `WM_TIMER`, so no worker thread can
outlive the window. The nested message loop deliberately does **not** use
`PostQuitMessage`: that `WM_QUIT` would also end the main window's loop and
close the whole plugin. It wakes itself with `PostThreadMessage(WM_NULL)`
instead.

The profile lives at `%LOCALAPPDATA%\avs_plugin_youtube\signin-profile`. Because
it is an ordinary browser profile, signing in to several services accumulates all
of their cookies in one place. See `src/browser_login.h`.

Only `http` and `https` links are followed, so a pasted `file:` or `javascript:`
URL cannot steer the browser somewhere unintended.

`LooksLikeSignInRequired` is deliberately stricter than
`WorthAnotherCookieSource`: the first decides whether to *tell the user to sign
in*, and guessing wrong there sends people off signing in for a video that was
simply deleted. The second only decides whether to spend another two seconds on
a different cookie source.

Three measured facts make this work - all verified against Chrome 140 and
yt-dlp 2026.08.19:

- A profile created through `--user-data-dir` gets **no**
  `os_crypt.app_bound_encrypted_key` in its `Local State`, only the plain DPAPI
  `encrypted_key`. Its cookies are therefore `v10`, which yt-dlp decrypts.
  Confirmed for a headful browser, not just a headless one. The user's *own*
  Chrome and Edge profiles do have that key, so their cookies are `v20` and
  yt-dlp cannot read them at all.
- yt-dlp accepts an absolute profile path
  (`--cookies-from-browser "chrome:<dir>"`) and finds both the cookie database
  and the key inside it. `chrome:` works for a profile created by any Chromium
  browser, because the key comes from that directory.
- The profile is not the one the user browses with, so nothing holds it open and
  their own session is never touched.

It is a real browser rather than an embedded web view, so Google does not refuse
the sign-in with "this browser or app may not be secure".

The sign-in browser is chosen by `FindLoginBrowser()`: the default browser if it
is Chromium-based, otherwise the first of Chrome, Edge, Brave, Vivaldi, Opera,
Chromium found in `App Paths`. Edge ships with Windows, so this practically
always finds one. `App Paths` is read from HKLM before HKCU - HKCU often points
at Chrome Canary.

While the browser is open its profile is locked, but that cannot collide with a
download: the dialog is modal, so Download is unreachable until the browser has
been closed.

To sign out, delete the `signin-profile` folder.

### The About box

Lists what is actually supported and tested (YouTube, Twitch), notes that the
other ~1750 yt-dlp sites usually work but are untested, and states plainly that
DRM services cannot be downloaded. The bundled yt-dlp version comes from
`YT_DLP_BUNDLED_VERSION` in `dllmain.cpp` - update it with the binary.

### The three cookie sources

`BuildCookieStrategies()` returns an ordered list, walked until one attempt
produces a file:

1. **No cookies.** Most videos need none. Fastest, and it keeps the user's
   accounts out of the request entirely.
2. **The plugin sign-in profile**, if it holds cookies.
3. **`cookies.txt`** in `%LOCALAPPDATA%\avs_plugin_youtube\`, if present - the
   escape hatch for anything the sign-in window cannot cover.

A failed attempt only escalates when the failure plausibly involves an account
(`NSYoutube::WorthAnotherCookieSource`). A bad URL or a removed video stops
immediately instead of walking the whole list. Which source is in use never
reaches the window title - see the user interface section.

### Why there is no scan over installed browsers

There used to be one - nine browsers, lock detection, priority ordering. It was
removed, because for the browsers most people use it cannot work at all:

| Browser  | Running | Cookies extracted (measured) |
|----------|---------|------------------------------|
| Firefox  | no      | 186                          |
| Vivaldi  | no      | 3153                         |
| Opera    | no      | 123                          |
| Chromium | no      | 1                            |
| Chrome   | yes     | **fails** - database locked   |
| Edge     | yes     | **fails** - database locked   |

Two separate walls stand in front of Chrome and Edge, and both of them are the
common case:

- **While running**, they hold `Default\Network\Cookies` open with no sharing, so
  yt-dlp cannot even copy it
  ([yt-dlp #7271](https://github.com/yt-dlp/yt-dlp/issues/7271)).
- **Even closed**, their cookies are `v20` App-Bound Encrypted, which yt-dlp
  cannot decrypt.

So the scan bought a ten second stall walking sources that were never going to
work, a confusing "close your browser" message, and a reason to read cookie
stores that are none of the plugin's business. The sign-in profile replaces all
of it, and works regardless of which browser the user runs or whether it is open.

A user who is already signed in to Firefox and would rather not sign in again can
export `cookies.txt`.

## Which services this can download from

The bundled yt-dlp carries **1752 extractors**, so the plugin is only "YouTube"
by name. Nothing in the code is YouTube specific any more: the URL is passed
straight through, the format selector degrades for sites that do not publish
separate video and audio streams, and the sign-in window opens on whatever site
the link points at.

The "needs sign-in" column is what these services typically require - it is not
measured here, since checking would mean holding accounts on each one.

| Service | Public content | Typically needs sign-in for |
|---|---|---|
| YouTube | works | age-restricted, private, members-only, bot checks |
| Twitch (VOD, clips, live) | works | subscriber-only VODs |
| Vimeo | works | private / password-protected |
| Dailymotion, Rumble, Kick, Coub | works | - |
| TikTok | works | some region and age gates |
| Reddit | works | NSFW, quarantined |
| SoundCloud, Bandcamp | works | private tracks |
| VK, OK.ru, Rutube | partly | most VK video |
| Bilibili | works | anything above 480p |
| Niconico | partly | most content |
| **Instagram** | mostly blocked | almost everything |
| **Facebook** | mostly blocked | almost everything |
| **X / Twitter** | mostly blocked | almost everything |
| Generic page with a direct video or HLS stream | sometimes | - |

**Not possible at all:** Netflix, Disney+, Prime Video, Apple TV+, Spotify and
other DRM services. yt-dlp does not break DRM, and no amount of signing in
changes that. Paid course platforms are usually DRM too.

The three sites in bold are the ones where the sign-in button matters most -
they serve almost nothing to a signed-out client, so without cookies they simply
fail. That is the case the sign-in profile was built for.

`--no-playlist` is always passed, so a channel or playlist URL downloads the one
video it points at rather than everything.

## YouTube needs a JavaScript engine

YouTube signs its stream URLs with an obfuscated JavaScript function - the "n
challenge". A usable stream URL only exists after that function has been *run*,
so extraction needs a JavaScript engine. yt-dlp carries the solver script
(`yt_dlp_ejs`, bundled inside the executable) but no engine to execute it.

Without one, YouTube rejects the player response and yt-dlp reports the
famously unhelpful `The page needs to be reloaded`. Measured on
`youtube.com/shorts/EczK1QTiAEo`:

| Cookies | Result |
|---|---|
| none | `Sign in to confirm you're not a bot` |
| plugin sign-in profile | bot check gone, then `n challenge solving failed` -> `The page needs to be reloaded` |

So the sign-in works and is not the problem: the engine is. That error must
therefore never be reported as a login failure - a signed-in user hitting it
would be sent round the sign-in loop forever. `LooksLikeMissingJsRuntime` in
`src/failure_reason.h` keeps the three cases apart.

### What is shipped

`qjs.exe` - QuickJS from [quickjs-ng](https://github.com/quickjs-ng/quickjs)
v0.16.2, the `qjs-windows-x86.exe` asset, 1 897 492 bytes, MIT licensed,
sha256 `1354a90a4587e2d917e65506d7b22a8ef9f76e53ff6e6c0027b3976210e83273`
(matches the digest GitHub publishes for the asset). The same 32-bit binary
serves both packages, since it runs as a child process.

`YouTubeDownloader::bundledJsRuntime()` looks for an engine **next to
yt-dlp.exe** and passes `--no-js-runtimes --js-runtimes "<runtime>:<path>"`,
trying `qjs.exe`, `deno.exe`, `bun.exe`, `node.exe` in that order. Nothing
installed on the user's machine is relied on - a plugin cannot assume a
developer toolchain is present.

QuickJS comes first on size: 1.9 MB against ~100 MB for Deno and ~50 MB for
Node.

### Why an engine cannot be avoided

Five player clients were tried on the same URL, signed in, looking for one that
returns stream URLs without the challenge:

| `player_client` | Result |
|---|---|
| `tv` | `The page needs to be reloaded` |
| `ios` | `Skipping client "ios" since it does not support cookies` |
| `web_safari`, `mweb`, `android_vr` | `n challenge solving failed` |

What remains available when the challenge is unsolved is only storyboard
images - `sb0`-`sb3`, mhtml, up to 101x180. No video format of any kind. So the
engine is not a configurable preference; it is the price of entry.

Verified working afterwards, from the extracted package:

```
[youtube] [jsc:quickjs] Solving JS challenges using quickjs
[Merger] Merging formats into "out\....mp4"
-> single 1 136 972 byte .mp4
```

Note that yt-dlp checks the engine's *version*, not just its presence. A Node 18
install on the test machine was found and rejected:

```
[debug] JS runtimes: node-18.17.0 (unsupported)
[debug] [youtube] [jsc] JS Challenge Providers: bun (unavailable), deno (unavailable), node (unavailable), quickjs (unavailable)
```

## User interface

The window shows the URL box, **Download**, **About**, **Ok** and **Cancel**.
Nothing about cookies or sign-in state is displayed - see the reasoning above.

The title bar carries the whole status: `Downloading...`, then
`Downloading... 45%` once yt-dlp starts reporting progress (the percentage is
parsed out of its `[download]` lines), then `Complete`, or `Error`. Which cookie
source is being tried never reaches the title; it is an implementation detail.

Every user-visible string goes through `tr()` and lives in `translation.json`
(18 keys, 14 languages). `tr(key, argument)` substitutes `%1`. A key missing from
the JSON is not an error at build time - `CTranslate` silently returns the key
itself, so the string quietly stays English in every locale. `check_i18n.py`
guards against that by extracting every `tr(L"...")` literal from the source and
comparing it with the table.

## File names

yt-dlp names the download after the video title and only removes what the
filesystem rejects, so what lands on disk keeps everything the uploader typed.
`CleanFileStem` in `src/output_name.h` is applied when the finished file is moved
out of the temp directory:

- **hashtag words are dropped**, but only where a hashtag can begin - at the
  start or after whitespace. `C#`, `F#` and `Prelude in C#m` survive.
- line breaks, tabs and control characters become spaces, and runs of whitespace
  collapse to one
- characters the filesystem rejects are dropped, **including the lookalikes
  yt-dlp substitutes for them**: it turns `A/B` into `A⧸B` (U+29F8) and `SHOW:`
  into `SHOW：` (U+FF1A) rather than removing them, which is valid and reads like
  a mistake
- leading and trailing whitespace, dots, dashes and underscores go - a trailing
  dot or space makes a path Windows cannot open
- truncated to 120 characters, at a word boundary where that keeps most of the
  name, so the whole path stays well under MAX_PATH

**The result is never empty.** A title of nothing but hashtags is common
(`#shorts`), so the fallback is the video id taken from the URL - the `v`
parameter or the last path segment - and `video` if even that yields nothing.
The fallback is deliberately not translated: a file name should not change with
the UI language.

## Threading

`yt-dlp` output arrives on the process runner's reader threads. Starting the next
attempt and showing the final message box are **posted** to the window
(`WM_YT_NEXT_ATTEMPT`, `WM_YT_FAILED`) so they run on the UI thread - restarting
the downloader from inside its own callback would tear the runner down from its
own thread. The `YouTubeDownloader` object is reused across attempts; only the
cookie source changes.

## Updating yt-dlp

YouTube breaks extractors regularly, so keep the bundled binary fresh. Use the
**32-bit** build - the plugin and the other bundled tools are 32-bit.

```bash
curl -L -o yt-dlp_x86.exe https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_x86.exe
curl -L -o SHA2-256SUMS   https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS
sha256sum yt-dlp_x86.exe && grep yt-dlp_x86.exe SHA2-256SUMS
```

Verify the hash, copy over `resources/x86/yt-dlp.exe`, then rebuild and repack
`build/x86/youtube.avsp` with the DLL plus both bundled executables. The `.avsp`
is a plain ZIP.

## Publishing

The storefront entry comes from `config.json` (`pluginId` must stay
`Youtube.plugin`, the string `PluginId()` returns). After a rebuild, bump
`version`, run `python release.py youtube` to publish the packages and then
`python package.py`.
