# Audit del codice — 2026-10-31

Revisione completa di `spectrum-analyzer` (2.243 righe originali) con correzione dei
difetti trovati. Questo documento registra **cosa era rotto, cosa è stato cambiato e
cosa resta da decidere**, così l'informazione non vive solo dentro una sessione.

Verifica: `make` (0 warning con `-Wall -Wextra`), `make test` (104 assert, 0 fallimenti),
`make asan` + esecuzione di 8 s sotto ASan/UBSan (nessun report), avvio reale con
finestra, barre, cattura PulseAudio/PipeWire e OSC.

## P0 — difetti funzionali, corretti

| # | Problema | Effetto | Correzione |
|---|---|---|---|
| 1 | `Glib::RefPtr<Gtk::StatusIcon>` era una variabile **locale** in `on_startup()` | Il riferimento cadeva all'uscita della funzione: l'icona nella tray veniva distrutta subito dopo la creazione | Il `RefPtr` è ora una globale `g_status_icon`, viva quanto il processo |
| 2 | `exit(1)` se il device audio non si apre | Se PipeWire non era ancora pronto (login, USB appena inserita) il programma **non partiva affatto** | La UI parte sempre; l'apertura viene ritentata ogni 2 s; lo stato è visibile nella status bar |
| 3 | Dopo un errore di lettura il thread di cattura faceva `usleep(1000)` **per sempre** | 1000 wakeup/s a vuoto e barre congelate fino al riavvio dell'app dopo un restart di PipeWire o un unplug | Distinzione `READ_RETRY` (overrun/suspend: si recupera) da `READ_FATAL` (stream morto: si riapre con backoff 100 ms → 2 s, DSP riconfigurato, contatore `ReconnectCount()`) |
| 4 | ALSA: `ret != frame_size` trattato come errore | Le letture corte (normali) buttavano via il blocco: barre a scatti/silenzio alternato | Ciclo di riempimento fino a `frame_size`, così il DSP riceve sempre il blocco pieno |
| 5 | `new Gtk::Label(...)` passato a `pack_start(ref)`; `m_filters` e `m_quit_dialog` mai distrutti | Memory leak a ogni finestra | `Gtk::manage` per i widget, `std::unique_ptr` per filtri e dialog |
| 6 | `last_x`/`last_y` erano `static` dentro un handler membro | Rientrando nel canvas il delta includeva tutto lo spostamento esterno: il box "saltava" | Membri dell'oggetto, azzerati su press e su `leave-notify` |

## P1 — robustezza e prestazioni, corrette

| # | Problema | Effetto | Correzione |
|---|---|---|---|
| 7 | Blend additive impostato dai box e mai ripristinato | Il colore delle barre dipendeva dall'ordine di disegno del frame precedente | `glc::blend_alpha()` / `glc::blend_additive()`: ogni primitiva dichiara lo stato che le serve |
| 8 | `glc::init()` protetto da un `static bool done` | Dopo un rebuild del contesto GL (unrealize/realize) program e VAO restavano id morti → **canvas nero** | `glIsProgram()` verifica il contesto corrente; `glc::shutdown()` chiamato in `on_unrealize()` |
| 9 | `push_matrix`/`pop_matrix` operavano sempre su `modelview` | Ignoravano `matrix_mode()`: uno sbilanciamento corrompeva la matrice sbagliata | Lo stack ricorda quale matrice ha salvato |
| 10 | Ridisegno a 60 fps anche con finestra nascosta o iconificata | Consumo inutile di CPU/GPU (era ~18% con finestra visibile) | `should_render()`: si ridisegna solo se mappata e non iconificata. **L'OSC continua a funzionare**, quindi il controllo remoto non si interrompe |
| 11 | `set_destination()` chiamato a **ogni tasto** nella entry dell'host | liblo risolve il nome al primo invio: battendo un hostname il thread grafico si bloccava | Commit su `Enter`/`focus-out`, validazione host+porta, TTL 1, errori di invio limitati a 3 log |
| 12 | OSC spedito quando `m_activation != m_previous_activation` (confronto float) | Il valore smoothed differisce sempre: **240 pacchetti/s** costanti (4 box × 60 Hz) | Deadband `0.002` sull'ultimo valore inviato: si trasmette solo un cambiamento visibile |
| 13 | `send_float_packet()` dereferenziava `g_message_bus` senza controllo | Possibile crash in fase di shutdown | Null check |
| 14 | `resource_path()` usava `readlink("/proc/self/exe")` | Codice Linux-only: impediva qualunque pacchettizzazione e qualunque port | Percorso per Linux/macOS/Windows + fallback su `SPECTRUM_ANALYZER_DATADIR` (layout `PREFIX/bin` + `PREFIX/share/spectrum-analyzer`) |
| 15 | `#include <alsa/asoundlib.h>` e `<pulse/simple.h>` **nell'header pubblico** | L'interfaccia non compilava fuori da Linux nemmeno per sbaglio | Backend chiuso in `AudioSampler::Impl` (pimpl): l'header non contiene più tipi di piattaforma |
| 16 | `spectrum-analyzer.h` **senza include guard**; parametri DSP come `#define` | Ridefinizioni silenziose; impossibile usare i valori in un template o in un test | Include guard; configurazione in `sa::AnalysisConfig` e costanti `sa::kNumBars` / `sa::kFftSize` |
| 17 | `Filter` conservava `char*` verso string literal (`(char*)"Red"`) | Scrittura accidentale su memoria di sola lettura = UB | `std::string` |
| 18 | `clamp` / `overlap_2D` dentro `utils.cc` (che includeva `<gtkmm.h>`) | Logica pura non testabile senza GUI | `math-utils.h`, header senza dipendenze |
| 19 | Il DSP era mescolato all'I/O dentro `AudioSampler` | Niente test possibili: ogni verifica richiedeva un device audio e uno schermo | `spectrum-dsp.{h,cc}`: puro calcolo, nessun thread, nessun header di sistema. 104 assert in `tests/dsp-test.cc` |

## P2 — osservazioni aperte, da decidere

### 21. Licenza: FFTW è GPL — **RISOLTO: GPL-3.0-or-later**

`fftw3` è distribuito con **GPL-2+** (o licenza commerciale MIT). Il repository **non ha
un file di licenza**. Conseguenza: se pubblichi un binario (AppImage, `.exe`, `.app`),
quel binario deve essere GPL e la licenza deve essere distribuita con esso.

Due strade:

- **accettare la GPL**: aggiungere `LICENSE` (GPL-2.0-or-later) e dichiararlo nel README;
- **togliere FFTW**: `spectrum-dsp.cc` è l'unico file che lo usa. PocketFFT (BSD-3,
  header-only) lo sostituisce in ~30 righe e toglie una dipendenza dal port e dalla
  distribuzione. È la scelta che consiglio se vuoi mantenere il codice permissivo.

**Decisione presa**: si rilascia sotto **GPL-3.0-or-later** (vedi `LICENSE`). FFTW è
GPL-2-or-later, quindi compatibile. Conseguenza: i binari distribuiti (AppImage,
`.exe`, `.app`) devono essere accompagnati dal testo della licenza e dal sorgente
corrispondente. Non serve cambiare FFTW.

### 22. Livello di riferimento: le barre non arrivano in cima

Misurato su questa macchina (riproduzione di una sinusoide 440 Hz a piena scala su WAV,
volume del sink 85%, cattura dal monitor con `parec`):

```
peak = 0.125  (-18,1 dBFS)     rms = 0.072  (-22,8 dBFS)
barra a 440 Hz ≈ 0,68 del fondo scala
```

I box di default stanno a `y = 0.835..0.985`, quindi **non si attivano mai** con
materiale normale: l'OSC resta a zero finché l'utente non trascina i box più in basso
(i box sono mobili, quindi non è un blocco, ma è un default poco utile).

Attenzione: la catena di guadagno reale (volume del sink, tap del monitor, amplificatore
hardware) non è verificabile da qui. Va misurata di nuovo su un altro sistema prima di
toccare la calibrazione. Opzioni, in ordine di costo:

1. spostare i box di default dove stanno davvero le barre (cambio di una riga);
2. esporre un offset di calibrazione (`--gain-db`, salvato) così ogni impianto si tara;
3. normalizzare la potenza per la larghezza della banda (vedi punto 23).

### 23. Risposta in frequenza non piatta

`Analyze()` **somma** la potenza dei bin della banda. È la scelta giusta per il rumore
rosa, ma con rumore bianco le bande larghe (alte frequenze: decine di bin) risultano
molto più alte delle strette (20–35 Hz: 1 bin). Se serve una risposta piatta, va
normalizzato per il numero di bin (o per la larghezza in Hz) — cambia però l'aspetto.

### 24. Altro

- `NUM_BARS` è fisso a 12 e la griglia a 4×8: nessuna configurazione.
- Nessuna CI: né build né test automatici. `.github/workflows/` non esiste (e servirà
  comunque il prima possibile, per il port).
- Nessun test su UI e OSC: il core ora è coperto, il resto richiede un harness.
- `GdkEvent*` e `Gtk::GLArea` sono API GTK3: sono il vincolo principale del port su
  macOS (vedi `docs/CROSS-PLATFORM.md`).
