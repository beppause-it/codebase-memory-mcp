# CR-36 - Attestazione con shadow di copertura

Data: 2026-10-22. Baseline owner: `d0e5838f` (`mcp http`).
Riferimento consumer: `C:/laragon/www/aup-sms/docs/43-cross-team-change-request.md`,
CR-36, e rapporto `docs/114-0780-attivazione-sorgenti-cbm.md`.

## Correzione owner

Il verificatore del trasporto MCP rifiutava ogni SQLite con due righe in
`projects`, compreso il caso legittimo progetto principale + `::missed`.
Ora accetta soltanto il progetto nativo configurato, presente esattamente una
volta, con il suo eventuale shadow esatto. Riusa
`cbm_store_coverage_shadow_project`; non interpreta genericamente ogni suffisso
`::` come attendibile. Un secondo progetto reale, uno shadow estraneo o uno
shadow senza il principale continuano a essere rifiutati.

La modifica produttiva e' confinata a
`integrations/aup-sms/mcp-http/src/mcp_http.c`: nessuna nuova API, tabella,
configurazione o dipendenza. Nessun artefatto reale viene modificato o ripulito.
Checksum, generazione nativa, integrita', autenticazione, firma, tenant,
revisioni e idempotenza restano verificati.

## Protocollo degli errori tra sistemi

Il contratto resta `asms-cbm-http/v1`. Non cambiano codice/messaggio esterni
`ATTESTATION_UNVERIFIABLE`, causa `ARTIFACT_IDENTITY_OR_INTEGRITY_FAILED`,
campi OS/SQLite, `request_id`, `correlation_id` e catena `causes`.
Il diagnostico nativo aggiunge soltanto `verification_stage`, con valori
`project_identity`, `native_generation` o `integrity`. Un mismatch semantico
puo' quindi essere distinto dall'ultimo stato SQLite `101 / no more rows
available`, che viene comunque conservato. Nessun catch/fallback di successo
e nessuna eccezione originale vengono introdotti o nascosti.

## N sorgenti nello stesso MCP

Il server MCP di un tenant/progetto supporta piu' binding. Nel caso consumer,
`7 -> 19` e `8 -> 20` convivono nello stesso endpoint `t1-p3`; ogni chiamata
seleziona esplicitamente `source_id`. Ogni artefatto contiene un solo progetto
reale ed eventualmente il proprio shadow. I tool non uniscono automaticamente
gli indici e non leggono lo shadow al posto della sorgente.

Un test nuovo attiva contemporaneamente due sorgenti con shadow e verifica
risultati separati e revoca indipendente. Non esiste un limite logico di una
o due sorgenti; rimangono i limiti operativi di configurazione, memoria,
deadline e quota gia' previsti dal contratto.

## Generazione: intervento consumer ancora distinto

La generazione nativa non e' `lens_generation`. CBM continua correttamente a
rifiutare l'hash Lens anche quando checksum e shadow sono validi. Non si
sovrascrive l'identita' Lens e non si adotta il valore atteso come fallback.

I due punti consumer verificati sono:

- `packages/aup-modules/asms-catalog/src/Infrastructure/ProjectMcp/ProjectCbmRuntimeConfig.php`;
- `packages/aup-modules/asms-catalog/src/Application/ProjectMcp/SyncProjectSourceBindingToCbm.php`.

Entrambi assegnano attualmente `generation` da `$index->lens_generation`.
Devono riusare la distinta identita' nativa dell'artefatto sigillato, secondo
`cbm_store_generation`: `u<db_uid>g<mutation_gen>` per gli artefatti attuali.
Il valore del rapporto operativo e' una prova di quel file, non una costante
da inserire nel codice. Configurazione e attestazione devono usare lo stesso
valore letto/verificato dall'artefatto corretto, mantenendo gli errori originali
nel protocollo tra sistemi.

Questa correzione owner non modifica il repository ASMS, la sua configurazione
privata o i binding operativi. Il binding reale resta da verificare dopo il
distinto adeguamento della generazione nel consumer e l'adozione del binario.

## Prove

Regressione riprodotta sul precedente `build/asms/codebase-memory-mcp.exe`:
il test con progetto + shadow fallisce con HTTP 409 e gli stessi codici e
diagnostici SQLite del rapporto ASMS. Log: `build/cr36-baseline-shadow.log`.

Build ufficiale in directory isolata, senza sostituire il consumer:

```bash
scripts/build.sh --with-ui --version 0.12.0-asms.cr36.1 \
  BUILD_DIR=build/cr36-native CC=clang CXX=clang++
```

La build include Lens e i controlli UI esistenti per start/stop. Il primo
tentativo MSYS2 non trovava npm: PATH corretto e build ripetuta; nessun
successo attribuito al tentativo fallito. Log: `build/cr36-build.log`.

Test HTTP reali con database sintetici, mai gli artefatti source7/index19:

```text
python integrations/aup-sms/mcp-http/tests/test_transport.py PATH_TO_NEW_BINARY
```

I test aggiunti coprono shadow legittimo e tool dopo attestazione, due
sorgenti con shadow, progetti/shadow estranei, shadow senza principale,
generazione nativa diversa e hash Lens, checksum errato, integrita' invalida e
conservazione del protocollo diagnostico. La suite esistente copre anche gli
artefatti senza shadow e i controlli autenticativi/tenant.

Non vengono dichiarati commit, push, installazione, attivazione dei binding o
verifica consumer. Gli esiti finali di build e collaudo vanno registrati prima
dell'adozione e del pin ASMS; le modifiche restano non committate.

## Build e collaudo owner conclusi - 2026-10-28

La correzione e' ora nel commit sorgente
`a7211dbb33dfc5d8fca77d03507d1135581ae8e1` (`fix multi sources`), trovato gia'
committato alla ripresa. Questa sessione non ha creato commit o push. La build
e' stata eseguita da un working tree pulito; questa nota documentale e'
successiva alla compilazione.

Build C ufficiale **con UI**, eseguita da zero e conclusa con exit code 0:

```bash
scripts/build.sh --with-ui --version 0.12.0-asms.cr36.1 \
  BUILD_DIR=build/cr36-final CC=clang CXX=clang++
```

Su questo host e' stato usato MSYS2 CLANG64, con Node/npm, il make locale gia'
disponibile e le utility Git nel PATH. Il target con UI usa il response file
gia' presente nel commit, evitando il limite della riga di comando Windows.
Non si tratta della sola build JavaScript: gli asset Lens sono incorporati
nell'eseguibile C e verificati tramite HTTP.

- Binario: `build/cr36-final/codebase-memory-mcp.exe`.
- Versione verificata con `--version`: `0.12.0-asms.cr36.1`.
- Dimensione: 335353856 byte.
- SHA-256 del binario:
  `f8924842dec0189c79671916ec666f7d0e243e14d1d0a1142ac6a4a212e154cf`.
- Evidenza macchina: `build/cr36-final-evidence.json`.
- Log build: `build/cr36-final-build.log`.

| Verifica sul nuovo binario | Esito | Log |
|---|---|---|
| Intera suite HTTP nativa | 39 test passati, 524.377 secondi, exit 0 | `build/cr36-final-http-tests.log` |
| Lifecycle Windows e health | start/status/restart/stop passati, exit 0 | `build/cr36-final-service.log` |
| UI nativa | HTML Lens e 2 asset principali serviti; processo e cleanup conclusi con exit 0 | `build/cr36-final-ui-smoke.log` |
| Formatting C | `clang-format --dry-run --Werror` passato | output terminale |
| Sintassi test | `python -m py_compile` passato | output terminale |
| Diff | `git diff --check` passato | output terminale |

La suite comprende il client MCP SDK standard, compatibilita' stdio, due
sorgenti su artefatti distinti, due sorgenti con shadow e revoca indipendente,
rifiuto di generazioni/checksum/integrita' errati e di progetti/shadow estranei,
conservazione di errori/cause/correlazione e controlli autenticativi/tenant.
I ResourceWarning Python su connessioni SQLite delle fixture restano visibili
nel log originale; non sono stati soppressi e non hanno causato test falliti.
Lo smoke UI prova il servizio di HTML/asset, non un collaudo visuale o GPU.

Il task owner di compilazione C con UI e collaudo e' concluso. Non sono stati
sostituiti `build/codebase-memory.exe`, il binario consumer, i suoi pin o i
binding operativi. L'eseguibile qualificato e' nella directory indicata sopra.

La verifica consumer resta distinta: al 2026-10-28 i due assegnamenti ASMS
indicati sopra usano ancora `$index->lens_generation`. Questa build non
aggira quel mismatch: ASMS deve inviare la distinta generazione nativa e poi
verificare il binding reale sul binario adottato. Non viene dichiarato
`VERIFIED` il percorso operativo ASMS.
