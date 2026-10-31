# Multipiattaforma: fattibilità e percorso

Risposta breve: **sì, è fattibile, ma non ricompilando**. Due dei quattro sottosistemi
non hanno oggi alcuna possibilità di girare su macOS e Windows così come sono scritti,
e uno di questi blocca proprio il "un .exe e via" che ti interessa su Windows.

La buona notizia: la UI è minuscola (una toolbar, un canvas OpenGL, una barra OSC).
È questo che rende il port economico.

## Cosa blocca il port oggi (verificato, non ipotizzato)

| # | Blocco | Dettaglio | Costo di rimozione |
|---|---|---|---|
| 1 ✅ | **Audio** | Risolto: la cattura passa da miniaudio (PulseAudio/ALSA/JACK, WASAPI, CoreAudio) | — |
| 2 | **`Gtk::GLArea` su macOS** | Il backend Quartz di GTK3 **non è funzionante** per GLArea. GTK4 ce l'ha (`GdkMacosGLContext`) ma con regressioni recenti (schermo nero in 4.16.7) | Alto se resti su GTK: significa passare a GTK4 *e* tenere due toolkit |
| 3 | **`Gtk::StatusIcon`** | Richiede libappindicator su Wayland, **non esiste** su macOS | Basso: si toglie o si sostituisce |
| 4 | **FFTW** | Compila ovunque, ma è **GPL-2+**: se distribuiscici binari, l'app deve essere GPL | Basso: PocketFFT (BSD-3) in un solo file |
| 5 | **liblo** | Compila ovunque (MSYS2, vcpkg, brew), ma per mandare 4 float in UDP bastano ~80 righe di socket | Basso, opzionale |
| 6 | **Nessuna CI** | Multipiattaforma = 3 toolchain. Senza CI il port si rompe in una settimana | Medio, ma indispensabile |

Già rimosso in questo lavoro: `resource_path()` non usa più `/proc/self/exe`, gli header
non includono più `<alsa/…>`/`<pulse/…>`, e il DSP è un modulo puro senza dipendenze di
piattaforma.

## Le due strade

### Strada A — SDL3 + miniaudio + PocketFFT (consigliata)

Un solo toolkit per le tre piattaforme: `SDL3` fa finestra, contesto GL, eventi e
renderer 2D; `miniaudio` (header singolo, pubblico dominio) fa la cattura con backend
**WASAPI** su Windows, **CoreAudio** su macOS, **ALSA/PulseAudio/JACK** su Linux.

| Aspetto | Conseguenza |
|---|---|
| Dipendenze | SDL3 (zlib), miniaudio (pubblico dominio), PocketFFT (BSD-3), ImGui (MIT, opzionale) → tutto permissivo, zero GPL |
| Grafica | La scena è 12 rettangoli + 14 linee + 4 box: `SDL_RenderFillRect` basta. Nessun shader, nessun profilo core, nessun problema di driver |
| Controlli | Spinbox, combo, entry, bottone: ~200 righe di widget immediate-mode, oppure Dear ImGui come scorciatoia |
| Test | Il `SpectrumDsp` già estratto e i 104 assert restano validi e identici |

**Packaging, che è la parte che ti interessa davvero:**

- **Windows**: SDL3 si linka **staticamente** → un singolo `.exe` di ~3-5 MB, nessuna
  DLL, nessuna cartella. È letteralmente "un .exe e via". Build con MSVC o MinGW-w64 in
  CI; installer opzionale con Inno Setup.
- **macOS**: bundle `.app` (SDL3 statico, universale arm64+x86_64). Per distribuirlo ad
  altri serve firma + notarizzazione (Apple Developer, 99 $/anno); senza, Gatekeeper
  richiede "Apri con → tasto destro". DMG con `create-dmg`.
- **Linux**: AppImage con `linuxdeploy` (~10 MB, senza GTK dentro) oppure binario
  singolo; Flatpak se vuoi integrazione di sistema e accesso garantito a PipeWire.

Costo stimato: 2-4 giorni per la parità su Linux, poi 1-2 giorni per OS aggiuntivo.

### Strada B — restare su GTK

- **Linux**: funziona oggi. AppImage con `linuxdeploy --plugin gtk` (impacchetta GTK,
  gli schemi GLib e i loader gdk-pixbuf): ~80 MB di AppImage. È la via corretta se
  vuoi pubblicare *solo* Linux.
- **Windows**: MSYS2 UCRT64 ha `mingw-w64-ucrt-x86_64-gtkmm3` 3.24.11, `liblo`, `fftw`.
  Funziona, ma **non è un .exe singolo**: GTK3 vuole ~40 MB di DLL, più
  `share/glib-2.0/schemas`, più i temi icone. Si distribuisce come cartella/ZIP portable
  o con un installer.
- **macOS**: è qui che la strada si chiude. GTK3 GLArea non funziona; dovresti passare a
  GTK4 + gtkmm4 (GLArea ok ma fragile), quindi riscrivere il codice dei widget, e
  impacchettare tutto GTK nel `.app`. Stesso costo della Strada A con un risultato
  peggiore.

**Verdetto**: la Strada B va bene per Linux oggi e per Windows con riserva. Se macOS è
un requisito, la Strada B non è una scorciatoia, è un vicolo cieco.

## Percorso ibrido (quello che farei)

| Passo | Lavoro | Esito |
|---|---|---|
| 0 ✅ | Audit + DSP estratto + audio con riconnessione (fatto) | Il "motore" è già separato dall'I/O |
| 0b ✅ | `Filter` reso puro (niente GL) + test headless (fatto) | La geometria dei box è verificabile e riusabile da qualsiasi toolkit |
| 1 ✅ | `make appimage` (linuxdeploy, front-end SDL) + CI | AppImage da **8,1 MB** verificata: si avvia e cattura audio con le sole librerie incluse. Con GTK sarebbe ~80 MB |
| 2 | FFTW → PocketFFT (~30 righe in `spectrum-dsp.cc`) | Niente GPL, una dipendenza in meno |
| 3 ✅ | `audio-sampler` → miniaudio (fatto) | Cattura identica su Linux/Windows/macOS; nessuna dipendenza a link-time da libpulse/libasound (dlopen a runtime). Verificato su Linux a 48 kHz nativi |
| 4 | Front-end SDL3 al posto di GTK — **avviato** | `app-sdl.cc` compilato e funzionante su Linux: stessa scena, stessi box trascinabili, stesso OSC, nessun OpenGL. Manca il packaging e la CI |
| 5 🟡 | CI: `debian:trixie` in container (verde al primo run), `windows-latest` MSYS2 UCRT64, `macos-latest` in un workflow separato | Linux: build, test e AppImage **verdi al primo run**. Windows: sistema di pacchetti corretto dopo il primo run. macOS: `macos-latest` funziona (le label `macos-14`/`macos-13` non esistono più); il workflow è separato e si allega da solo alla release — verificato su v1.1.0 |

Dopo il passo 3 il codice è già multipiattaforma **tranne la finestra**, il che rende il
passo 4 un lavoro isolato e non un rifacimento.

## Risposte dirette alle tue domande

- **Linux: AppImage?** Sì, è la scelta giusta — ma con GTK serve `linuxdeploy-plugin-gtk`
  e l'AppImage pesa ~80 MB. Con la Strada A pesa ~10 MB. In entrambi i casi va verificata
  la cattura audio dentro un AppImage: il sandbox non è un problema (non c'è sandbox),
  ma il socket di PulseAudio va raggiunto tramite `XDG_RUNTIME_DIR`.
- **Windows: un .exe e via?** Dipende dal toolkit, non dal codice: **con SDL3 statico sì**
  (un file, 3-5 MB); **con GTK no** (cartella con ~40 MB di DLL e runtime).
- **macOS?** `.app` + firma + notarizzazione. È l'OS con più attrito amministrativo, non
  tecnico.

## Fonti

- GTK3 `GtkGLArea` non funzionante su macOS/Quartz — <https://github.com/nodots/gnubg-macos>
  e <https://discourse.gnome.org/t/gtkglarea-on-osx-sonoma/21204>
- GTK4 su macOS (backend GL) e regressioni recenti — <https://docs.gtk.org/gtk4/osx.html>,
  <https://discourse.gnome.org/t/gtk-4-16-7-on-macos/25629>
- miniaudio, backend per piattaforma — <https://miniaud.io/>
- SDL3 (release stabili 3.4.x, licenza zlib) — <https://github.com/libsdl-org/SDL/releases>
- linuxdeploy / plugin GTK per AppImage — <https://github.com/linuxdeploy/linuxdeploy-plugin-gtk>
- FFTW GPL-2+ e licenza commerciale — <https://www.fftw.org/doc/License-and-Copyright.html>
- PocketFFT BSD-3 — <https://github.com/mreineck/pocketfft>
- MSYS2: `mingw-w64-gtkmm3` 3.24.11, `mingw-w64-x86_64-liblo` — <https://packages.msys2.org/>
