# TWISTFM per M-VAVE FM-1

Firmware non ufficiale per l'M-VAVE FM-1, ispirato al suono OPL3 della Twisted Electrons TWISTfm:
- 8 voci, ognuna con un layer FM a 4 operatori (10 algoritmi) e un layer a 2 operatori;
- le 8 forme d'onda OPL3;
- un filtro a 4 poli per voce, con 15 modi, LIQUID/MS20 e HEAT;
- una catena di 4 effetti: distorsione, phaser, tape echo e reverb.

Costruito su [fm1-base](https://github.com/charlesvestal/fm1-base) (Charles Vestal) e
[felucca-bsp](https://github.com/hugelton/felucca-bsp) (Leo Kuroshita, Hügelton Instruments), MPL-2.0.

> Versione di sviluppo. Si installa a proprio rischio. L'installer web può rimettere il firmware originale M-VAVE.
> TWISTfm è un prodotto di Twisted Electrons; M-VAVE e FM-1 sono marchi dei rispettivi proprietari.

## Come si compila

Non serve installare niente sul PC: compila GitHub.

1. A ogni modifica del progetto GitHub avvia la compilazione da solo. Si può avviare anche a mano: scheda **Actions**,
   poi **build**, poi **Run workflow**.
2. Dopo qualche minuto, nella pagina dell'esecuzione trovi sotto **Artifacts** il pacchetto **twistfm-firmware**:
   contiene il file `.fwsc` e il registro della compilazione (`build-log.txt`).
3. Se hai attivato le Pages (Settings → Pages → Source: **GitHub Actions**), il sito con l'installer web viene
   pubblicato su https://8blend.github.io/twistfm-fm1/

## Come si installa sull'FM-1

1. Fai il backup dei tuoi progetti e suoni con il Workbench M-VAVE.
2. Apri https://8blend.github.io/twistfm-fm1/install/ in Chrome o Edge, con l'FM-1 collegato via USB.
3. Segui l'installer. Lo stesso installer permette di tornare al firmware originale.

## Struttura

| Percorso | Cosa contiene |
| --- | --- |
| `app/app.c`, `app/app.h` | il collegamento con la piattaforma: audio, tasti, encoder, schermo, MIDI |
| `app/src/tfm_engine.*` | il motore: operatori OPL3, algoritmi, filtri, modi voce (MIT) |
| `app/src/tfm_fx.*` | gli effetti (MIT) |
| `app/src/tfm_ui.*` | l'interfaccia sullo schermo 240x240 (GPL-3.0, font 5x7 di FM1 Open di M-VAVE) |
| `docs/simulatore.html` | il simulatore nel browser, con lo stesso codice |
| `.github/workflows/build.yml` | le istruzioni di compilazione per GitHub |

## Licenza

GPL-3.0-only per il firmware nel suo insieme (vedi `LICENSE`). Motore ed effetti sono MIT. fm1-base e
felucca-bsp sono MPL-2.0 e vengono scaricati durante la compilazione.
