# CR-27 — consegna tecnica locale

Data: 18 ottobre 2026. Nessun commit, push, aggiornamento del pin AUP-SMS o
passaggio consumer a DELIVERED/VERIFIED è stato eseguito.

Implementazione approvata dall'Architect indipendente. Qualifica funzionale
Windows e Ubuntu completata. La qualifica di rilascio complessiva resta bloccata
dai controlli generali descritti sotto; gli hash identificano candidati testati,
non una release approvata. Le decisioni formali sono in [ARCHITECT.md](ARCHITECT.md).

## Identità e artefatti

| Elemento | Valore |
|---|---|
| HEAD iniziale effettivo, invariato | `acdd9c3bf2d8ae3af6be45c8a6a96285c82d65f8` |
| Baseline storica citata nella CR | `ec6427c38176efcd37b28682e82f2491afda94be` |
| Versione binari | `0.12.0-asms.cr27.1` |
| SHA-256 contratto | `2a221d063207fd4642ff73a90c2b67c430dffaeb13604980c26c39490303303b` |
| SHA-256 manifest sorgenti | `af697be0e4a1bd9ad0134a27d45ac4944c4a0754b2a8c8bfa9825a7e0100ea3e` |
| SHA-256 modulo C approvato | `c790586e53e9715512ab53f91afed82c2297094c5a9ca3acd199ac8c2135c82b` |
| SHA-256 binario Windows | `41bd0423adb512c77b1ad18c8aef2f1f6cab1e8d21974d7794f9461c4cd268b1` |
| SHA-256 binario Ubuntu | `75c228c82a104f5657175a39242af9e0999543ef41254139baab4b3d0beef4c8` |

Binari locali, dalla radice del repository:

- `build/cr27-native/codebase-memory-mcp-v2.exe` — Windows x64, GCC 16.2.
- `build/cr27-linux-codebase-memory-mcp-v2` — Ubuntu 24.04 x64, GCC 13.

[SOURCE-MANIFEST.sha256](SOURCE-MANIFEST.sha256) elenca i file di codice e test
con hash individuali. [CONTRACT.md](CONTRACT.md) definisce il protocollo;
[README.md](README.md) contiene configurazione e adozione. La copia completa
della CR e degli addenda è in
`integrations/aup-sms/change-requests/CR27-from-aupsms-project-mcp-http.md`,
SHA-256 `57fb2fa71aad8a923338e0ba4d11769cbaffb41a423bb874599761f3f6d0c57d`.

## Risultato implementato

Modulo HTTP nativo C opt-in, un processo/porta per tenant e progetto, loopback,
Streamable HTTP stateless MCP 2025-06-18. Riusa catalogo, schemi e dispatcher
CBM: `get_graph_schema`, `search_graph`, `query_graph`, `trace_path`,
`get_file_outline`, `get_architecture` con aspetti sincroni.

Ogni chiamata richiede `source_id`; la mappatura firmata READY seleziona un
artefatto fisico esatto, progetto nativo, generazione e checksum. Il control
plane separato applica HMAC, CAS, idempotenza persistente, scadenza e tombstone
di revoca. Credenziali distinte per piano/progetto e configurazione riletta
tramite handle privato validato a ogni richiesta. Worker isolati applicano
limiti, deadline e contenimento dell'albero di processi. Audit tecnico interno
limitato e redatto; errori nativi e cause SQLite/subprocesso restano osservabili.

Stdio conserva il catalogo nativo. Nessun uso della porta Lens 9749, accesso al
DB ASMS, endpoint UX/statistiche o dipendenza di produzione aggiunta.

## File modificati

- Nuovo modulo e documenti: `integrations/aup-sms/mcp-http/`; copia CR sopra.
- Ingresso e dispatch esatto: `src/main.c`, `src/mcp/mcp.c`, `src/mcp/mcp.h`.
- Diagnostica SQLite: `src/store/store.c`, `src/store/store.h`.
- Header macchina e framing: `src/ui/httpd.c`, `src/ui/httpd.h`.
- Lettura privata tramite handle: `src/daemon/ipc.c`, `src/daemon/ipc.h`.
- Assegnazione atomica al Job Windows e seam esclusivamente di test:
  `src/foundation/subprocess.c`, `src/foundation/subprocess.h`.
- Ricorsione JSON limitata: `src/foundation/recursion_whitelist.h` e
  `scripts/check-nolint-whitelist.sh`.
- Build, lint e audit: `Makefile.cbm`, `scripts/security-audit.sh`,
  `scripts/security-allowlist.txt`.
- Regressione binding/context/free: `tests/test_mcp.c`.

Le modifiche core sono additive. Le response file MinGW risolvono il limite
Windows di lunghezza della riga di comando senza spostare il quoting dei flag.

## Verifiche e log riproducibili

I log completi sono sotto `build/`, ignorato da Git ma conservato localmente.

| Verifica | Risultato | Log |
|---|---|---|
| HTTP nativo Windows, client SDK incluso | **32/32 PASS**, candidato finale dopo I08, 410.106 s | `cr27-windows-qualified-http.log` |
| HTTP nativo Ubuntu, client SDK incluso | **32/32 PASS**, ripetuto dopo la modifica condivisa | `cr27-linux-atomic-http.log` |
| Windows start/status/restart/stop e health | **PASS** | `cr27-windows-atomic-service.log` |
| Morte del padre prima di ResumeThread, appartenenza al Job e arresto del figlio | **PASS** deterministico | `cr27-windows-atomic-test.log` |
| Assenza seam nel binario produttivo, presenza nella fixture | **PASS** | `cr27-atomic-symbols.log` |
| C nativo ASan + UBSan, suite MCP/mutation guard/httpd | **503 PASS, 0 FAIL**, 5 soli test Windows esclusi per piattaforma | `cr27-linux-native-final.log` |
| clang-tidy nuovo modulo | **PASS**, zero errori del modulo | `cr27-tidy-module-cleanup.log` |
| cppcheck nuovo modulo | **PASS**, informazione sul limite standard delle configurazioni | `cr27-cppcheck-module.log` |
| Whitelist ricorsione e divieto soppressioni generiche | **PASS** | `cr27-recursion-policy-final.log` |
| Audit statico, stringhe binario, UI, install, rete, fuzz | **PASS**; fuzz **32/32**, rete senza connessioni uscenti | `cr27-security-*-final.log` |
| `git diff --check` | **PASS** | output della sessione |

Comandi applicabili da checkout completo:

```text
scripts/build.sh --version 0.12.0-asms.cr27.1
python integrations/aup-sms/mcp-http/tests/test_transport.py PATH_TO_BINARY
scripts/test.sh --suites mcp,mcp_mutation_guard,httpd
scripts/lint.sh
make -f Makefile.cbm security
git diff --check
```

Su Windows impostare `CBM_HTTP_TEST_ROOT` su una directory privata, per esempio
`$env:LOCALAPPDATA\CBM-CR27-tests`; `C:\laragon` ha un antenato condiviso e viene
correttamente rifiutato per lo stato privato. Il test lifecycle è
`integrations/aup-sms/mcp-http/tests/test_service.ps1 -Binary PATH_TO_BINARY`.
Il solo test SDK usa `@modelcontextprotocol/sdk@1.20.2` installato sotto
`build/cr27-sdk`, senza dipendenza produttiva.

Entrambe le build canoniche sono state eseguite. Per il candidato ripulito dal
lint, `build/cr27-parallel.mk` riusa esattamente liste, ordine, flag e oggetti
vendored del Makefile canonico, compilando separatamente le unità C in parallelo:

```text
make -j4 -o Makefile.cbm -f build/cr27-parallel.mk cr27-prod BUILD_DIR=build/cr27-native CC=gcc CXX=g++ CFLAGS_EXTRA=VERSION_DEFINE
make -j4 -o Makefile.cbm -f build/cr27-parallel.mk cr27-test BUILD_DIR=build/cr27-tests CC=gcc CXX=g++
build/cr27-tests/test-runner-v2 mcp mcp_mutation_guard httpd
```

I comandi esatti, incluso il quoting di `VERSION_DEFINE`, sono conservati in
`build/cr27-windows-v2.sh` e `build/cr27-linux-v2.sh`. Quest'ultimo ha inizialmente
passato flag `--suite` non supportati al runner: tutti i 503 test passarono,
ma i tre flag furono contati come errori; il comando corretto sopra è stato
rieseguito e ha prodotto il log finale senza fallimenti.

## Blocchi generali al rilascio

1. **Integrità vendored:** il manifest committato contiene 1083 caratteri CR.
   Il verificatore rifiuta le righe come percorsi invalidi, verifica zero file e
   fallisce. Riprodotto anche con `git archive HEAD` in una directory Ubuntu
   separata: `cr27-baseline-vendored.log`. Esito candidato:
   `cr27-security-vendored-proper-final.log`. Il manifest e i vendored sono
   invariati; nessun checksum è stato aggiornato o accettato automaticamente.
2. **Contratti generali:** anche la baseline originale fallisce perché
   `scripts/setup-windows.ps1` non è puro ASCII, richiesto dal controllo PS 5.1:
   `cr27-baseline-contracts.log`.
3. **Lint globale:** il checkout contiene errori preesistenti di formato e
   clang-tidy; su Ubuntu LLVM 18 compaiono anche flag GCC non riconosciuti.
   `cr27-lint-frozen.log` e `cr27-format-frozen.log` conservano gli errori.
   Anche LLVM 23 segnala formato su righe upstream invariate; il confronto con
   le righe modificate conta **zero** violazioni. Il nuovo modulo passa i due
   analizzatori separatamente. Questo non equivale a un PASS del lint globale.
   Il cppcheck globale, ancora in esecuzione dopo questi fallimenti bloccanti,
   è stato interrotto senza attribuirgli un PASS:
   `cr27-global-cppcheck-status.log`. Anche il file CLI invariato della baseline
   produce errori nel controllo isolato: `cr27-baseline-tidy-proof.log`.

## Tentativi precedenti e cause conservate

I log iniziali restano disponibili: limite di command line MinGW; rifiuto ACL
di `C:\laragon`; prime prove Linux con limite outline e causa SQLite corretti;
perdita diagnostica durante `sqlite3_step` corretta con regressioni reali;
timestamp JSON PS 7 corretto usando tick UTC decimali; crash injection Windows
anticipata al processo ancora sospeso e scadenza di un secondo troppo stretta,
corretti nel test. Un precedente test ASan incontrò il temporaneo include
`<cstdio>` suggerito impropriamente dal fix automatico LLVM 18: rimosso, build e
suite finali ripetute. I log precedenti non sono risultati qualificanti.

Durante la pulizia è stato inoltre osservato un vecchio worker sospeso con
padre già terminato. La revisione ha confermato la finestra tra CreateProcess
e l'assegnazione successiva al Job; I08 è stato corretto assegnando il Job
atomicamente alla creazione, senza fallback. La prova deterministica arresta
il padre prima di ResumeThread e verifica dall'esterno la morte del figlio.
La causa precisa di quel singolo processo osservato non viene dedotta dai soli
PID. Windows richiede ora Windows 10 / Server 2016 o successivo, come descritto
nel contratto aggiornato e approvato.

L'osservazione del crash Windows è stata resa riproducibile usando un artefatto
256 MiB, una finestra di 10 secondi e il controllo che il processo sia ancora
attivo. Restano obbligatorie le asserzioni WORKER_FAILED, CRASH e correlazione.
Il caso isolato passa in cr27-windows-atomic-crash-final.log; il tentativo
precedente non aveva osservato il worker e resta conservato nel log originale.

## Adozione AUP-SMS

Seguire il [contratto](CONTRACT.md) e la sequenza nel [README](README.md): directory
e configurazione private; artefatto immutabile registrato nel manifest fidato;
PUT READY firmato con revisione/idempotenza; health; Agent OS `route_mode=direct`
con bearer dati del progetto. Ogni tool riceve `source_id`. Rinnovo e revoca
restano espliciti, senza ricerca implicita dell'ultimo indice o rollback
automatico. Il pin consumer potrà essere aggiornato solo dopo la chiusura del
Release Gate e la verifica separata di AUP-SMS.
